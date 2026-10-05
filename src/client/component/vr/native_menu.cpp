#include <std_include.hpp>
#include "native_menu.hpp"
#include "movie_presentation.hpp"
#include "native_menu_hook.hpp"
#include "native_menu_stack.hpp"
#include "menu_acceptance.hpp"
#include "menu_input_trace.hpp"
#include "debug_options.hpp"
#include <utils/native_memory.hpp>
#include "native_hud_capture.hpp"
#include "native_hud_quad.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/input.hpp"
#include "component/gui/gui.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::native_menu
{
	namespace
	{
		using object=game::hks::HksObject;
		using utils::native_memory::read_bytes;
		std::mutex mutex;
		state published;
		std::shared_ptr<const images> output;
		pointer input_pointer;
		std::atomic_bool requested{},installed{},capture_needed{};
		std::atomic_uint64_t tagged{},rejected{},publications{};
		std::atomic_uint64_t cached_draws{};
		utils::hook::detour cached_render_hook;
		std::atomic_uint32_t rejected_command{};
		std::uintptr_t vm{};
		std::uint64_t session{1},revision{1};
		using record=commands::emission;
		struct arena_frame {std::uintptr_t arena{};state owner{};std::vector<record> records;bool failed{};};
		std::array<arena_frame,3> arenas;
		std::mutex trace_mutex;
		input_trace input_history;
		void write_input_trace()
		{
			// Disk IO belongs to the async pipeline; the LUI owner only copies a
			// bounded row. Two overwrite-only files preserve startup and wake data.
			for(unsigned recovery=0;recovery<2;++recovery)
			{
				std::vector<input_sample> rows;
				{
					const std::lock_guard lock(trace_mutex);auto& window=recovery?input_history.recovery:input_history.startup;
					if(!window.dirty)continue;
					rows.assign(window.rows.begin(),window.rows.begin()+window.count);window.dirty=false;
				}
				std::ostringstream out;
				out<<"tick,session,revision,sequence,continuity,physical,left_generation,right_generation,left_presses,right_presses,input_age_ms,pointer_age_ms,flags,menus,hand,overlay_error,stage,mouse_polls,mouse_moves,mouse_suppressed,key_events\n";
				for(const auto& r:rows)out<<r.tick<<','<<r.session<<','<<r.revision<<','<<r.sequence<<','<<r.continuity<<','<<r.physical<<','
					<<r.trigger_generation[0]<<','<<r.trigger_generation[1]<<','<<r.trigger_presses[0]<<','<<r.trigger_presses[1]<<','
					<<r.input_age<<','<<r.pointer_age<<','<<r.flags<<','<<r.count<<','<<r.hand<<','<<r.overlay_error<<','<<int(r.stage)<<','
					<<r.mouse_polls<<','<<r.mouse_moves<<','<<r.mouse_suppressed<<','<<r.key_events<<'\n';
				utils::io::write_file(recovery?"minidumps/vr-menu-input-recovery.csv":"minidumps/vr-menu-input-startup.csv",out.str(),false);
			}
		}
		template<class T> T read(std::uintptr_t address) noexcept
		{T out{};utils::native_memory::read_bytes(&out,reinterpret_cast<void*>(address),sizeof(out));return out;}
		std::string_view copied_string(object value,std::array<char,256>& s) noexcept
		{
			if(value.t!=game::hks::TSTRING)return {};
			if(!utils::native_memory::read_bytes(s.data(),reinterpret_cast<char*>(value.v.ptr)+20,s.size()))return {};
			const auto n=strnlen(s.data(),s.size());return n<s.size()?std::string_view(s.data(),n):std::string_view{};
		}
		std::string string(object value)
		{std::array<char,256> storage{};return std::string(copied_string(value,storage));}
		object table(std::uintptr_t address) noexcept {object v{};v.t=game::hks::TTABLE;v.v.ptr=reinterpret_cast<void*>(address);return v;}
		// Batch sibling properties on the LUI owner. No native table/value survives
		// this observation, so Lua mutation, VM replacement and GC need no cache.
		template<std::size_t N>
		std::array<object,N> get_properties(object source,const std::array<std::string_view,N>& names,unsigned depth=0)
		{
			if(depth>6)return {};
			if(source.t==game::hks::TUSERDATA)
				return get_properties(get_properties<1>(table(read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(source.v.ptr)+24)),{"__index"},depth+1)[0],names,depth+1);
			if(source.t!=game::hks::TTABLE)return {};
			std::array<object,N> result{};std::array<bool,N> found{};std::size_t remaining=N;
			const auto h=read<game::hks::HashTable>(reinterpret_cast<std::uintptr_t>(source.v.ptr));
			if(h.m_mask!=UINT32_MAX)
			{
				if(h.m_mask>=32768||!h.m_hashPart)return {};
				for(unsigned i=0;i<=h.m_mask;++i)
				{
					const auto node=read<game::hks::Node>(reinterpret_cast<std::uintptr_t>(h.m_hashPart+i));
					if(node.m_value.t==game::hks::TNIL||node.m_value.t==game::hks::TNONE)continue;
					std::array<char,256> storage{};const auto key=copied_string(node.m_key,storage);
					for(std::size_t n{};n<N;++n)if(!found[n]&&key==names[n])
					{result[n]=node.m_value;found[n]=true;--remaining;}
					if(!remaining)return result;
				}
			}
			if(h.m_meta)
			{
				const auto inherited=get_properties(get_properties<1>(table(reinterpret_cast<std::uintptr_t>(h.m_meta)),{"__index"},depth+1)[0],names,depth+1);
				for(std::size_t n{};n<N;++n)if(!found[n])result[n]=inherited[n];
			}
			return result;
		}
		object get(object source,std::string_view name)
		{return get_properties<1>(source,{name})[0];}
		std::uintptr_t element(object value) noexcept
		{
			if(value.t!=game::hks::TUSERDATA)return 0;
			const auto handle=read<std::array<std::uint16_t,2>>(reinterpret_cast<std::uintptr_t>(value.v.ptr)+32);
			const auto base=read<std::uintptr_t>(0x141a783a0),generations=read<std::uintptr_t>(0x141a783a8);
			const unsigned first=read<std::uint16_t>(0x141a783b4),capacity=read<std::uint16_t>(0x141a783b6);
			if(!base||!generations||!capacity||capacity>16384||handle[0]<first||handle[0]-first>=capacity||
				read<std::uint16_t>(generations+(handle[0]-first)*2)!=handle[1])return 0;
			return base+(handle[0]-first)*0x188;
		}
		bool truth(object v) noexcept {return v.t==game::hks::TBOOLEAN&&v.v.boolean;}
		bool video_playing() noexcept
		{
			return menu_surface::native_video_playing(read<std::uint8_t>(0x14d36add5)!=0,
				read<std::uint8_t>(0x14d36abc0)!=0,read<std::uint8_t>(0x14d36add4)!=0,
				read<unsigned>(0x14d36addc),read<unsigned>(0x14d36ade0));
		}
		state observe()
		{
			state s;s.timestamp=GetTickCount64();
			const auto* enable=game::Dvar_FindVar("vr_enable");
			s.enabled=installed&&enable&&enable->current.enabled;
			const auto current_vm=reinterpret_cast<std::uintptr_t>(*game::hks::lua_state);
			if(!s.enabled||!current_vm){capture_needed=false;const std::lock_guard lock(mutex);published=s;output.reset();input_pointer={};return s;}
			s.frontend=read<std::uint8_t>(0x140bebccc)!=0;
			const auto* paused=game::Dvar_FindVar("cl_paused");
			const auto* pause_menu=game::Dvar_FindVar("lui_pausemenu");
			if(!s.frontend&&paused&&paused->current.integer&&pause_menu&&pause_menu->current.enabled)s.scene_dim=140.f/255.f;
			s.video=video_playing();
			const auto globals=read<object>(current_vm+0x70);
			const auto roots=get(get(globals,"LUI"),"roots");
			const auto root=get(roots,s.frontend?"UIRootFull":"UIRoot0");
			const auto root_fields=get_properties<5>(root,{"m_blockButtonInput","m_blockMouseMove","persistentBackground","childRecord","flowManager"});
			s.button_blocked=truth(root_fields[0]);s.mouse_blocked=truth(root_fields[1]);
			s.blocked=s.button_blocked||s.mouse_blocked||(*game::keyCatchers&1)!=0||gui::captures_input();
			s.background=element(root_fields[2]);
			s.cursor=element(get(root_fields[3],"mouse_cursor"));
			const auto menu_stack=get(root_fields[4],"menuInfoStack");
			if(menu_stack.t==game::hks::TTABLE)
			{
				const auto h=read<game::hks::HashTable>(reinterpret_cast<std::uintptr_t>(menu_stack.v.ptr));
				if(h.m_arraySize>128)return {};
				std::vector<entry> entries;
				std::vector<object> menus;
				std::vector<stack::role> roles;
				entries.reserve(h.m_arraySize);menus.reserve(h.m_arraySize);roles.reserve(h.m_arraySize);
				for(unsigned i=0;i<h.m_arraySize;++i)
				{
					const auto info=read<object>(reinterpret_cast<std::uintptr_t>(h.m_arrayPart+i));
					if(info.t!=game::hks::TTABLE)continue;
					const auto fields=get_properties<4>(info,{"menu","name","isModal","isPopup"});
					const auto menu=fields[0];
					const auto name=string(fields[1]);s.briefing=name=="LuiBriefingMenu";
					s.accept_screen=name=="main_lockout"||s.briefing;
					menus.push_back(menu);roles.push_back(stack::classify(truth(fields[3])));
					entries.push_back({reinterpret_cast<std::uintptr_t>(info.v.ptr),element(menu),truth(fields[2]),
						menu.t==game::hks::TUSERDATA?read<std::uint32_t>(reinterpret_cast<std::uintptr_t>(menu.v.ptr)+32):0});
				}
				const auto first=stack::first_visible(roles,menu_surface::maximum_menus);
				for(auto i=first;i<entries.size();++i)s.menus[s.count++]=entries[i];
				for(unsigned i=0;i<s.count;++i)
				{
					// Verified generic popup backdrop is a separate native subtree;
					// retain only menu ink when an ancestor becomes noninteractive.
					const auto menu=menus[first+i];
					const auto menu_children=get_properties<2>(get(menu,"childRecord"),{"generic_selectionList_intermediate","sp_pause_menu_container"});
					const auto intermediate=menu_children[0];
					s.backdrops[i]=element(get(get(intermediate,"childRecord"),"generic_popup_screen_overlay_blur"));
					const auto pause=menu_children[1];
					const auto children=get(pause,"childRecord");
					const auto backgrounds=get_properties<2>(children,{"sp_pause_menu_blur","sp_pause_menu_darken"});
					s.pause_backgrounds[i]={element(backgrounds[0]),element(backgrounds[1])};
				}
			}
			if(s.frontend||s.count)controller_input::set_gameplay_active(false);
			capture_needed=s.frontend||s.count;
			const std::lock_guard lock(mutex);
			if(vm!=current_vm||s.frontend!=published.frontend||(!s.frontend&&s.count&&!published.count))
			{vm=current_vm;++session;output.reset();}
			bool changed=s.count!=published.count||s.blocked!=published.blocked||s.background!=published.background||
				s.accept_screen!=published.accept_screen||s.briefing!=published.briefing||s.video!=published.video;
			for(unsigned i=0;i<s.count;++i)changed|=s.menus[i].id!=published.menus[i].id||s.menus[i].element!=published.menus[i].element||s.menus[i].generation!=published.menus[i].generation;
			if(changed)++revision;
			s.session=session;s.revision=revision;published=s;return s;
		}
		struct cursor {std::uintptr_t base{},used{};};
		cursor command_cursor() noexcept
		{
			const auto p=read<std::uintptr_t>(0x150f911a8);const auto c=read<cursor>(p);
			return c.base>=0x10000&&c.used<=0x400000?c:cursor{};
		}
		std::uint64_t fingerprint(std::uintptr_t begin,std::uintptr_t end,bool growing_lines=false) noexcept
		{
			if(end<=begin||end-begin>65536)return 0;
			const auto origin=begin;
			std::array<std::byte,1024> bytes{};std::uint64_t hash=14695981039346656037ull;
			while(begin<end)
			{
				const auto n=std::min<std::uintptr_t>(end-begin,bytes.size());
				if(!utils::native_memory::read_bytes(bytes.data(),reinterpret_cast<void*>(begin),n))return 0;
				for(std::size_t i=0;i<n;++i)commands::hash_byte(hash,std::to_integer<unsigned char>(bytes[i]),begin-origin+i,growing_lines);
				begin+=n;
			}
			return hash;
		}
		void deliver()
		{
			static menu_surface::navigation navigation;
			static menu_surface::menu_button pause_recenter;
			static menu_surface::acceptance acceptance;
			static bool accept_held{};
			static std::uint64_t previous_revision{},previous_session{},previous_sequence{},physical{},toggle_generation{},toggle_presses{};
			static bool toggle_armed{},drag{},stick_mode{};
			static unsigned previous_hand{2};
			static float last_u{},last_v{};
			state owner;pointer p;std::shared_ptr<const images> images;
			{const std::lock_guard lock(mutex);owner=published;p=input_pointer;images=output;}
			const auto frame=controller_input::latest();const auto now=controller_input::clock::now();const auto ticks=GetTickCount64();
			const bool fresh=frame.focused&&now>=frame.sampled_at&&now-frame.sampled_at<=std::chrono::milliseconds(150);
			input_stage stage=input_stage::unavailable;
			const auto record=[&]{
				input_sample row;row.tick=ticks;row.session=owner.session;row.revision=owner.revision;row.sequence=frame.sequence;
				row.continuity=frame.continuity_generation;row.physical=::input::physical_activity();row.count=owner.count;row.hand=p.hand;row.overlay_error=p.overlay_error;row.stage=stage;
				row.input_age=now>=frame.sampled_at?static_cast<std::uint32_t>(std::min<std::int64_t>(UINT32_MAX,std::chrono::duration_cast<std::chrono::milliseconds>(now-frame.sampled_at).count())):UINT32_MAX;
				row.pointer_age=ticks>=p.stamp?static_cast<std::uint32_t>(std::min<std::uint64_t>(UINT32_MAX,ticks-p.stamp)):UINT32_MAX;
				// Bits: focus, fresh, button block, mouse block, pointer ready/hit,
				// left/right pose, frontend, video, entry screen, left/right active,
				// left/right down, native console capture, GUI capture, stick mode.
				row.flags=unsigned(frame.focused)|(fresh<<1)|(owner.button_blocked<<2)|(owner.mouse_blocked<<3)|(p.ready<<4)|(p.hit<<5)|
					(frame.runtime_aim[0].valid<<6)|(frame.runtime_aim[1].valid<<7)|(owner.frontend<<8)|(owner.video<<9)|(owner.accept_screen<<10)|
					(frame.trigger[0].active<<11)|(frame.trigger[1].active<<12)|(frame.trigger[0].down<<13)|(frame.trigger[1].down<<14)|
					(bool(*game::keyCatchers&1)<<15)|(gui::captures_input()<<16)|(stick_mode<<17);
				for(unsigned h=0;h<2;++h){row.trigger_generation[h]=frame.trigger[h].generation;row.trigger_presses[h]=frame.trigger[h].presses;}
				const auto desktop=::input::ui_activity_counters();row.mouse_polls=desktop.mouse_polls;row.mouse_moves=desktop.mouse_moves;
				row.mouse_suppressed=desktop.mouse_suppressed;row.key_events=desktop.key_events;
				const std::lock_guard lock(trace_mutex);input_history.record(row,requested&&owner.enabled,fresh);
			};
			// Do not construct the recording guard or sample when the startup
			// diagnostic is unloaded. Input delivery itself remains unconditional.
			const auto record_input=debug_options::enabled(debug_options::probe::menu_input)
				? std::make_optional(gsl::finally(record)) : std::nullopt;
			const bool unavailable=!requested||!owner.enabled||!fresh||gui::captures_input()||(*game::keyCatchers&1);
			if(unavailable)
			{::input::release_vr_ui_input();navigation.reset();pause_recenter.reset();acceptance.reset();accept_held=false;drag=false;toggle_armed=false;previous_sequence=0;return;}
			const auto tap=[](int key){if(::input::vr_ui_key(key,true))::input::vr_ui_key(key,false);};
			const auto gesture=pause_recenter.consume(frame,owner.revision,true,now);
			if(gesture!=menu_surface::menu_action::none)
			{
				stage=input_stage::gesture;acceptance.reset();accept_held=false;
				::input::release_vr_ui_input();navigation.reset();drag=false;
				if(gesture==menu_surface::menu_action::recenter)command::execute("vr_recenter");
				else tap(game::K_ESCAPE);
				return;
			}
			const auto& toggle=frame.menu_toggle;
			if(toggle_generation!=toggle.generation){toggle_generation=toggle.generation;toggle_armed=false;toggle_presses=toggle.presses;}
			if(!toggle_armed){toggle_armed=toggle.active&&!toggle.down;toggle_presses=toggle.presses;}
			else if(toggle.active&&toggle.presses>toggle_presses)
			{stage=input_stage::toggle;toggle_presses=toggle.presses;::input::release_vr_ui_input();navigation.reset();acceptance.reset();accept_held=false;drag=false;tap(game::K_ESCAPE);return;}
			if(previous_revision!=owner.revision||previous_session!=owner.session)
			{
				previous_revision=owner.revision;previous_session=owner.session;previous_sequence=0;
				physical=::input::physical_activity();last_u=p.u;last_v=p.v;
				::input::release_vr_ui_input();navigation.reset();drag=false;stick_mode=false;
				acceptance.reset();accept_held=false;
			}
			// Briefing owns its own hold timer/skip permission. Deliver native
			// Enter edges on the LUI owner, independent of overlay/ray readiness.
			if(menu_surface::accepts_without_pointer(owner.accept_screen,owner.video,owner.count)&&!owner.button_blocked)
			{
				stage=input_stage::accept;
				const bool held=acceptance.held(frame,owner.revision,true,now);
				if(held!=accept_held){::input::vr_ui_key(game::K_ENTER,held);accept_held=held;}
				navigation.reset();return;
			}
			acceptance.reset();if(accept_held){::input::vr_ui_key(game::K_ENTER,false);accept_held=false;}
			if(!owner.interactive()||!p.ready||p.session!=owner.session||p.target!=owner.revision||
				ticks<p.stamp||ticks-p.stamp>150||!images||images->owner.revision!=owner.revision)
			{stage=input_stage::waiting_surface;::input::release_vr_ui_input();navigation.reset();drag=false;return;}
			if(previous_sequence==frame.sequence){stage=input_stage::duplicate;return;}previous_sequence=frame.sequence;
			if(previous_hand!=p.hand)
			{
				previous_hand=p.hand;::input::release_vr_ui_input();navigation.reset();drag=false;stick_mode=false;
			}
			const auto activity=::input::physical_activity();
			stage=input_stage::delivered;
			const bool deliberate=p.hit&&(std::abs(p.u-last_u)+std::abs(p.v-last_v)>.025f);
			const auto buttons=navigation.consume(frame,owner.revision,true,p.hand,p.hit||stick_mode,now);
			const bool controller_action=buttons.click||buttons.confirm||buttons.back||buttons.horizontal||buttons.vertical;
			if(activity!=physical)
			{
				physical=activity;
				// Do not starve deliberate controller input behind desktop events.
				// Once desktop navigation owns input, further events need no re-arm.
				if(!deliberate&&!controller_action)
				{
					stage=input_stage::physical;
					if(!stick_mode){navigation.reset();::input::release_vr_ui_input();drag=false;last_u=p.u;last_v=p.v;}
					stick_mode=true;
				}
			}
			if(deliberate){stick_mode=false;last_u=p.u;last_v=p.v;}
			if(buttons.horizontal||buttons.vertical)
			{stick_mode=true;last_u=p.u;last_v=p.v;tap(buttons.vertical>0?game::K_UPARROW:buttons.vertical<0?game::K_DOWNARROW:buttons.horizontal>0?game::K_RIGHTARROW:game::K_LEFTARROW);}
			if(buttons.back){::input::release_vr_ui_input();drag=false;tap(game::K_ESCAPE);return;}
			const auto& texture=images->layers[owner.count-1];
			if(!stick_mode&&p.hit&&texture)
			{
				const auto width=texture->canvas.valid()?texture->canvas.source_width:texture->width;
				const auto height=texture->canvas.valid()?texture->canvas.source_height:texture->height;
				const auto x=int(std::lround(std::clamp(p.u,0.f,1.f)*float(width-1)));
				const auto y=int(std::lround(std::clamp(p.v,0.f,1.f)*float(height-1)));
				::input::vr_ui_pointer(x,y);
			}
			if(buttons.confirm||(buttons.click&&stick_mode)){tap(game::K_ENTER);return;}
			if(buttons.click&&!stick_mode&&p.hit){drag=::input::vr_ui_key(game::K_MOUSE1,true);}
			if(drag&&(!buttons.click_held||!p.hit||stick_mode)){::input::vr_ui_key(game::K_MOUSE1,false);drag=false;}
		}
		template<class Draw> void render_owned(void* native,const Draw& draw)
		{
			const auto address=reinterpret_cast<std::uintptr_t>(native);
			if(!requested||!capture_needed){draw();return;}
			const auto before=command_cursor();if(!before.base){draw();return;}
			state owner;
			bool needs_observe{};
			{
				const std::lock_guard lock(mutex);
				auto it=std::find_if(arenas.begin(),arenas.end(),[&](const auto& a){return a.arena==before.base;});
				needs_observe=it==arenas.end();if(!needs_observe)owner=it->owner;
			}
			if(needs_observe)
			{
				owner=observe();
				const std::lock_guard lock(mutex);
				auto it=std::min_element(arenas.begin(),arenas.end(),[](const auto& a,const auto& b){return a.owner.timestamp<b.owner.timestamp;});
				it->arena=before.base;it->owner=owner;it->failed=false;it->records.clear();
				if(it->records.capacity()<512)it->records.reserve(512);
			}
			unsigned surface=menu_surface::surface_count;
			bool inactive_backdrop{};
			for(auto p=address;owner.enabled&&p&&surface==menu_surface::surface_count;)
			{
				unsigned depth{};
				for(;p&&depth<64;++depth,p=read<std::uintptr_t>(p+0xb8))
				{
					if(!owner.frontend)for(unsigned i=0;i<owner.count;++i)
						for(const auto background:owner.pause_backgrounds[i])if(p==background)inactive_backdrop=true;
					if(inactive_backdrop)break;
					for(unsigned i=0;i<owner.count;++i)if(p==owner.backdrops[i])
					{if(owner.frontend&&i+1==owner.count)surface=menu_surface::backdrop_slot;else inactive_backdrop=true;break;}
					if(inactive_backdrop||surface!=menu_surface::surface_count)break;
					if(p==owner.cursor){surface=menu_surface::cursor_slot;break;}
					if(p==owner.background){surface=menu_surface::background_slot;break;}
					for(unsigned i=0;i<owner.count;++i)if(p==owner.menus[i].element){surface=i;break;}
					if(surface!=menu_surface::surface_count)break;
				}
				break;
			}
			draw();
			if(inactive_backdrop)surface=commands::excluded_surface;
			else if(surface==menu_surface::surface_count)return;
			const auto after=command_cursor();if(before.base!=after.base||after.used<before.used){++rejected;return;}
			if(after.used==before.used)return;
			const auto begin=before.base+before.used,end=after.base+after.used;
			const auto header=read<std::array<std::uint8_t,8>>(begin);
			const bool growing_lines=commands::lines_2d(header.data(),end-begin);
			const auto hash=fingerprint(begin,end,growing_lines);
			const std::lock_guard lock(mutex);
			auto it=std::find_if(arenas.begin(),arenas.end(),[&](const auto& a){return a.arena==before.base;});
			if(it==arenas.end())
			{
				it=std::min_element(arenas.begin(),arenas.end(),[](const auto& a,const auto& b){return a.owner.timestamp<b.owner.timestamp;});
				it->arena=before.base;it->owner=owner;it->failed=false;it->records.clear();
			}
			if(!hash||it->records.size()>=2048){it->failed=true;++rejected;return;}
			it->records.push_back({{begin,end,surface},hash,growing_lines});++tagged;
		}
		void render_primitive(int client,void* native,void* root,float alpha,int flags,void* lua)
		{
			bool drawn{};
			const auto draw=[&]{drawn=true;utils::hook::invoke<void>(read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(native)+0xf0),client,native,root,alpha,flags,lua);};
			try{render_owned(native,draw);}
			catch(const std::bad_alloc&)
			{
				++rejected;
				if(!drawn)draw();
			}
		}
		bool render_cached(void* native,void* lua)
		{
			bool drawn{},hit{};
			const auto draw=[&]{drawn=true;hit=cached_render_hook.invoke<bool>(native,lua);if(hit)++cached_draws;};
			try{render_owned(native,draw);}
			catch(const std::bad_alloc&){++rejected;if(!drawn)draw();}
			return hit;
		}
		template<std::size_t N> bool verify(std::uintptr_t at,const std::uint8_t(&bytes)[N])
		{std::array<std::uint8_t,N> mask;mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),{bytes,mask.data(),N}));}
	}
	state current() noexcept {const std::lock_guard lock(mutex);return published;}
	presentation current_presentation() noexcept
	{
		if(!installed||!requested)return {};
		const auto* enabled=game::Dvar_FindVar("vr_enable");
		if(!enabled||!enabled->current.enabled)return {};
		// Native CG's cinematic draw at 0x140367F84 combines active playback
		// with this dvar. It is an ownership gate, never playback evidence alone.
		const auto* fullscreen=game::Dvar_FindVar("cg_cinematicFullscreen");
		return {true,read<std::uint8_t>(0x140bebccc)!=0,game::CL_IsCgameInitialized(),
			video_playing(),fullscreen && fullscreen->current.enabled};
	}
	void set_requested(bool value) noexcept {requested=value;if(!value){const std::lock_guard lock(mutex);output.reset();input_pointer={};}}
	void reset_vm() noexcept {capture_needed=false;const std::lock_guard lock(mutex);vm=0;++session;++revision;published={};output.reset();input_pointer={};arenas={};}
	void invalidate_images() noexcept {const std::lock_guard lock(mutex);published.revision=++revision;output.reset();input_pointer={};arenas={};}
	void recenter() noexcept
	{
		// A paused scene need not produce a new stereo pose immediately. Reset
		// the menu anchor session using the present owner's current UI pose too.
		const std::lock_guard lock(mutex);published.session=++session;published.revision=++revision;
		output.reset();input_pointer={};arenas={};
	}
	void arena_reset(std::uintptr_t arena) noexcept {const std::lock_guard lock(mutex);for(auto& a:arenas)if(!arena||a.arena==arena){a.arena=0;a.owner={};a.failed=false;a.records.clear();}}
	std::shared_ptr<const capture_plan> for_stream(std::uintptr_t begin) noexcept
	{
		if(!requested)return {};
		try
		{
			arena_frame saved;std::uintptr_t limit{};
			{
				const std::lock_guard lock(mutex);
				for(const auto& a:arenas)
				{
					std::uintptr_t end{};for(const auto& r:a.records)end=std::max(end,r.bytes.end);
					if(!commands::contains(a.arena,end,begin))continue;
					if(saved.arena){++rejected;return {};}
					saved=a;limit=end;
				}
			}
			if(saved.failed||!saved.arena||saved.records.empty()||GetTickCount64()-saved.owner.timestamp>250)return {};
			std::sort(saved.records.begin(),saved.records.end(),[](const auto& a,const auto& b){return a.bytes.begin<b.bytes.begin;});
			auto selected=commands::select(begin,limit,saved.records,[](auto at,void* to,std::size_t size)
				{return utils::native_memory::read_bytes(to,reinterpret_cast<const void*>(at),size);});
			if(!selected.valid){rejected_command=selected.rejected_command;++rejected;return {};}
			if(selected.ranges.empty())return {};
			for(const auto& r:saved.records)
			{
				if(r.bytes.end<=begin||r.bytes.begin>=selected.end)continue;
				if(r.bytes.begin<begin||r.bytes.end>selected.end||fingerprint(r.bytes.begin,r.bytes.end,r.growing_lines)!=r.fingerprint){++rejected;return {};}
			}
			for(auto& range:selected.ranges)
			{
				if(range.surface>=menu_surface::maximum_menus)continue;
				std::array<std::byte,104> bytes{};native_hud_quad::quad quad{};
				if(range.end-range.begin>bytes.size()||!utils::native_memory::read_bytes(bytes.data(),reinterpret_cast<const void*>(range.begin),range.end-range.begin)||
					!native_hud_quad::decode(bytes.data(),range.end-range.begin,quad))continue;
				std::array<char,96> name{};
				if(utils::native_memory::read_bytes(name.data(),reinterpret_cast<const void*>(read<std::uintptr_t>(quad.material)),name.size())&&
					commands::vignette_material(std::string_view(name.data(),strnlen(name.data(),name.size()))))range.surface=menu_surface::backdrop_slot;
			}
			auto plan=std::make_shared<capture_plan>();plan->owner=saved.owner;plan->arena=saved.arena;plan->ranges=std::move(selected.ranges);
			return plan;
		}catch(...){++rejected;return {};}
	}
	void publish(images value) noexcept
	{
		try
		{
			const std::lock_guard lock(mutex);
			if(!requested||value.owner.session!=published.session||value.owner.revision!=published.revision)return;
			// Explicit noninteractive history survives native tree hide/destruction;
			// only matching logical ancestors can borrow their old menu ink.
			if(output&&output->owner.session==value.owner.session)
				for(unsigned i=0;i+1<value.owner.count;++i)if(!value.layers[i])
					for(unsigned j=0;j<output->owner.count;++j)if(value.owner.menus[i].id==output->owner.menus[j].id)value.layers[i]=output->layers[j];
			output=std::make_shared<images>(std::move(value));++publications;
		}catch(...){++rejected;}
	}
	std::shared_ptr<const images> latest() noexcept {const std::lock_guard lock(mutex);return output;}
	void publish_pointer(pointer value) noexcept {const std::lock_guard lock(mutex);input_pointer=value;}
	void clear_pointer() noexcept {publish_pointer({});}
	class component final:public component_interface
	{
	public:
		void post_unpack() override
		{
			constexpr std::uint8_t call[]{0xff,0xd3,0x48,0x8b,0x9c,0x24,0xd8,0,0,0};
			constexpr std::uint8_t frontend[]{0x0f,0xb6,0x05,0xf5,0x30,0x8c,0,0xc3};
			constexpr std::uint8_t cached[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
			if(!verify(0x140314c51,call)||!verify(0x140328bd0,frontend)||!verify(0x14032d2c0,cached))
			{console::error("[VR menus] native render/state signature rejected\n");return;}
			const auto thunk=utils::hook::assemble([](utils::hook::assembler& a)
			{
				emit_render_bridge(a,reinterpret_cast<std::uintptr_t>(render_primitive),0x140314c5b);
			});
			if(!install_render_bridge(0x140314c51,thunk))
			{console::error("[VR menus] render relay allocation failed; native instructions preserved\n");return;}
			// LUI tries its cached draw list before calling the element renderer.
			// A hit emits current-frame commands and skips the primitive callback;
			// tag that emission through the same ownership path, without disabling
			// native caching or retaining stale pixels for text/cursor/video.
			cached_render_hook.create(0x14032d2c0,render_cached);
			installed=true;
			scheduler::loop([]{try{(void)observe();deliver();}catch(...){++rejected;::input::release_vr_ui_input();reset_vm();}},scheduler::lui);
			if(debug_options::enabled(debug_options::probe::menu_input))
				scheduler::loop([]{try{write_input_trace();}catch(...){}},scheduler::async,1s);
			command::add("vr_menu_status",[]{const auto s=current();pointer p;std::shared_ptr<const images> image;
				{const std::lock_guard lock(mutex);p=input_pointer;image=output;}
				unsigned layers{};if(image)for(const auto& layer:image->layers)if(layer)++layers;
				console::info("[VR menus] cached_draws=%llu\n",cached_draws.load());
				console::info("[VR menus] ready=%d requested=%d frontend=%d video=%d menus=%u session=%llu revision=%llu tagged=%llu rejected=%llu rejected_command=%04x publications=%llu layers=%u input_ready=%d ray_hit=%d overlay_errors=%llu last_overlay_error=%d\n",installed.load(),requested.load(),s.frontend,s.video,s.count,s.session,s.revision,tagged.load(),rejected.load(),rejected_command.load(),publications.load(),layers,p.ready,p.hit,p.overlay_errors,p.overlay_error);});
		}
		void pre_destroy() override {set_requested(false);reset_vm();}
	};
}
REGISTER_COMPONENT(vr::native_menu::component)
