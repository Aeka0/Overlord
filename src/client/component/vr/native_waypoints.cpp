#include <std_include.hpp>
#include "native_waypoints.hpp"
#include "native_menu.hpp"
#include "native_script_image_bridge.hpp"
#include "game/game.hpp"
#include <utils/native_memory.hpp>
#include "engine_stereo_bridge.hpp"
#include "narrative_ui.hpp"
#include "hud_prompts.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::native_waypoints
{
	namespace
	{
		using utils::native_memory::read_bytes;
		utils::hook::detour waypoint_hook, projection_hook, clamp_hook, reset_hook;
		utils::hook::detour script_hud_hook;
		std::mutex mutex;
		std::array<group,128> groups{};
		std::atomic_bool ready{};
		bool script_text_keys_ready{};
		std::atomic_uint64_t calls{}, recorded{}, matched{}, rejected{}, resets{};
		std::atomic_uint64_t image_layouts{},image_unscoped{};
		struct observation { group value{}; unsigned projections{}; bool projected{}, failed{}; };
		thread_local observation* current{};
		struct script_group
		{
			bool text{},progress{},script_ink{};
			std::optional<narrative_ui::backdrop> hint_backdrop;
		};
		thread_local script_group* script_current{};
		struct cursor { std::uintptr_t base{}, used{}; };
		cursor commands() noexcept
		{
			const void* allocator{}; cursor out{};
			if (utils::native_memory::read_bytes(&allocator,reinterpret_cast<void*>(0x150F911A8),sizeof(allocator)))
				utils::native_memory::read_bytes(&out,allocator,sizeof(out));
			if (out.base<0x10000 || out.used>0x400000) return {};
			return out;
		}
		std::uint64_t fingerprint(std::uintptr_t begin, std::uintptr_t end) noexcept
		{
			if (end<=begin || end-begin>8192) return 0;
			std::array<std::byte,8192> bytes{};
			if (!utils::native_memory::read_bytes(bytes.data(),reinterpret_cast<void*>(begin),end-begin)) return 0;
			std::uint64_t hash=14695981039346656037ull;
			for (std::size_t i=0;i<end-begin;++i) { hash^=std::to_integer<unsigned char>(bytes[i]); hash*=1099511628211ull; }
			return hash;
		}
		void reset_stub()
		{
			reset_hook.invoke<void>();
			// R_InitNextFrame waits for native ownership, flips the frontend and
			// assigns its command allocator at 0x14076EBDC. Invalidate ONLY that
			// reused arena; the other frontend may still be rendering on the GPU.
			const auto c=commands();
			native_menu::arena_reset(c.base);
			++resets;
			const std::lock_guard lock(mutex);
			for (auto& g:groups) if (!c.base || g.arena==c.base) g={};
		}
		bool projection_stub(int client, const void* placement, const float* world, float* point)
		{
			const bool result=projection_hook.invoke<bool>(client,placement,world,point);
			if (current)
			{
				auto& s=*current; auto& m=s.value.marker;
				std::array<float,2> xy{}; directional_ui::vec3 position{};
				if (!utils::native_memory::read_bytes(xy.data(),point,sizeof(xy)) || !utils::native_memory::read_bytes(position.data(),world,sizeof(position)) ||
					!utils::native_memory::read_bytes(m.viewport.data(),static_cast<const std::byte*>(placement)+0x20,sizeof(m.viewport)) ||
					!utils::native_memory::read_bytes(m.tangent.data(),reinterpret_cast<void*>(0x141BC2510),sizeof(m.tangent)))
				{ s.failed=true; return result; }
				// The alternate height-scaled waypoint projects its bottom and top;
				// its visual center is their midpoint, not either individual endpoint.
				if (!s.projections) { m.world=position; m.anchor=xy; }
				else if (s.projections==1)
				{
					for (unsigned i=0;i<3;++i) m.world[i]=(m.world[i]+position[i])*.5f;
					for (unsigned i=0;i<2;++i) m.anchor[i]=(m.anchor[i]+xy[i])*.5f;
				}
				else s.failed=true;
				++s.projections; s.projected=true; m.clamped=m.clamped || !result;
			}
			return result;
		}
		bool clamp_stub(int client, float* point, float left, float right, float top, float bottom,
			float radius, float* normal, float* distance)
		{
			const bool result=clamp_hook.invoke<bool>(client,point,left,right,top,bottom,radius,normal,distance);
			if (current && result) current->value.marker.clamped=true;
			return result;
		}
		bool record(group g,const cursor& before,const cursor& after)
		{
			if (!before.base || before.base!=after.base || after.used<=before.used) {++rejected;return false;}
			g.arena=before.base;g.begin=before.base+before.used;g.end=after.base+after.used;
			g.hash=fingerprint(g.begin,g.end);g.timestamp=GetTickCount64();
			if (!g.hash) {++rejected;return false;}
			const std::lock_guard lock(mutex);
			group* available{};
			for (auto& entry:groups)
			{
				if (entry.begin && ((entry.arena==g.arena && entry.begin<g.end && g.begin<entry.end) || g.timestamp-entry.timestamp>250)) entry={};
				if (!entry.begin && !available) available=&entry;
			}
			if (available) {*available=g;++recorded;return true;}
			++rejected;return false;
		}
		void draw_script_text(int client,const char* text,const void* element,const void* parameters,std::size_t key_offset)
		{
			std::array<char,128> key{};
			if (script_current && script_text_keys_ready && element)
			{
				int index{};
				// CG_DrawHudElem resolves both its label (+0x50) and text (+0x94)
				// through the native localized config-string range, starting at 0xf5.
				if (utils::native_memory::read_bytes(&index,static_cast<const std::byte*>(element)+key_offset,sizeof(index)) && index>0 && index<1024)
				{
					const auto* value=utils::hook::invoke<const char*>(0x1403C9D60,index+0xf5);
					if (!value || !utils::native_memory::read_bytes(key.data(),value,key.size()) ||
						std::find(key.begin(),key.end(),'\0')==key.end()) key={};
				}
			}
			const auto replacement=script_current && text?hud_prompts::replace(hud_prompts::source::script_hud,key.data()):std::nullopt;
			if(replacement)text=replacement->c_str();
			if(script_current)
			{
				script_current->text=true;
				const auto labels=narrative_ui::progress_labels();
				if(text && labels)script_current->progress=script_current->progress || narrative_ui::progress_text(text,*labels);
			}
			utils::hook::invoke<void>(0x14037AE70,client,text,element,parameters);
		}
		void script_label_stub(int client,const char* text,const void* element,const void* parameters)
		{draw_script_text(client,text,element,parameters,0x50);}
		void script_text_stub(int client,const char* text,const void* element,const void* parameters)
		{draw_script_text(client,text,element,parameters,0x94);}
		void dead_quote_stub(const void* cg,void* rect,void* font,float scale,float* color,int style,float x,float y)
		{
			// The native owner emits both the localized reason/quote and its
			// optional hint, with ordinary text flags. Tag its exact allocation,
			// never all unflagged text or a translated string match.
			bool drawn{};
			const auto draw=[&]{drawn=true;utils::hook::invoke<void>(0x1403874C0,cg,rect,font,scale,color,style,x,y);};
			draw_narrative_text(draw);if(!drawn)draw();
		}
		void cursor_hint_stub(int client,void* rect,void* font,float scale,int style)
		{
			// Read-only native witness: ownerdraw CALL 38AF9C -> 3869F0,
			// RCX client, RDX rect, R8 font, XMM3 scale, stack[0x20] style.
			// The stock producer resolves its own locale, binding variants, names
			// and parameters. On a missing VR translation, admit its exact text
			// allocation to the existing narrative canvas without rebuilding it.
			bool drawn{};
			const auto draw=[&]{drawn=true;utils::hook::invoke<void>(0x1403869F0,client,rect,font,scale,style);};
			if(hud_prompts::native_cursor_required())draw_narrative_text(draw);
			if(!drawn)draw();
		}
		void observe_script_image_layout(const image_layout* image) noexcept
		{
			++image_layouts;if(!script_current){++image_unscoped;return;}
			if(image&&image->material)
			{
				const char* name{};std::array<char,96> storage{};
				if (utils::native_memory::read_bytes(&name,reinterpret_cast<const void*>(image->material),sizeof(name)) && utils::native_memory::read_bytes(storage.data(),name,storage.size()) &&
					std::find(storage.begin(),storage.end(),'\0')!=storage.end())
				{
					if (narrative_ui::hint_blur_material(storage.data()))
					{
						const narrative_ui::backdrop value{{image->x,image->y,image->x+image->width,image->y+image->height},image->alpha};
						if(narrative_ui::valid_backdrop(value))script_current->hint_backdrop=value;
					}
					else if (narrative_ui::script_ink_material(storage.data())) script_current->script_ink=true;
				}
			}
		}
		void script_hud_stub(int client,void* element,int time)
		{
			if(!ready || !engine_stereo_bridge::is_active() || script_current)
			{script_hud_hook.invoke<void>(client,element,time);return;}
			const auto before=commands();script_group scope;script_current=&scope;
			const auto leave=gsl::finally([]{script_current=nullptr;});
			script_hud_hook.invoke<void>(client,element,time);
			// Tag the entire element: native labels and dynamic numeric values
			// are emitted in separate calls but must share one stereo depth.
			if(scope.text || scope.hint_backdrop || scope.script_ink)
			{
				group value;value.narrative=true;value.progress=scope.progress;
				value.hint_backdrop=scope.hint_backdrop;value.script_ink=scope.script_ink;
				const auto* map=game::Dvar_FindVar("mapname");
				value.panel=map && map->current.string && narrative_ui::script_panel(map->current.string);
				record(value,before,commands());
			}
		}
		bool text_call(std::uintptr_t site)
		{
			std::array<std::uint8_t,5> bytes{0xe8},mask{};mask.fill(0xff);
			const auto delta=std::int32_t(0x14037AE70-site-5);std::memcpy(bytes.data()+1,&delta,4);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(site),{bytes.data(),mask.data(),bytes.size()}));
		}
		void waypoint_stub(int client, void* parameters)
		{
			++calls;
			if (!ready.load() || current || !engine_stereo_bridge::is_active())
			{ waypoint_hook.invoke<void>(client,parameters); return; }
			observation s{}; const auto before=commands();
			current=&s;
			const auto leave=gsl::finally([] { current=nullptr; });
			waypoint_hook.invoke<void>(client,parameters);
			const auto after=commands();
			if (s.failed || !s.projected)
			{ ++rejected; return; }
			record(s.value,before,after);
		}
		template<std::size_t N> bool verify(std::uintptr_t address,const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{}; mask.fill(0xff);
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));
		}
	}
	std::vector<group> for_stream(std::uintptr_t begin,std::uintptr_t end)
	{
		std::vector<group> result; result.reserve(groups.size());
		const auto now=GetTickCount64();
		{
			const std::lock_guard lock(mutex);
			for (const auto& g:groups)
				if (g.begin>=begin && g.end<=end && g.end>g.begin && now>=g.timestamp && now-g.timestamp<=250 &&
					result.size()<groups.size()) result.push_back(g);
		}
		std::erase_if(result,[](const group& g) { return fingerprint(g.begin,g.end)!=g.hash; });
		std::sort(result.begin(),result.end(),[](const group& a,const group& b) { return a.begin<b.begin; });
		matched.fetch_add(result.size());
		return result;
	}
	bool draw_world_marker(const directional_ui::waypoint& marker,const std::function<void()>& draw,
		std::optional<directional_ui::rectangle> measured_bounds)
	{
		if (!ready || current || !draw || !engine_stereo_bridge::is_active()) return false;
		directional_ui::rectangle crop{};
		if (measured_bounds && !directional_ui::marker_crop(*measured_bounds,crop)) return false;
		const auto before=commands();if (!before.base) return false;
		draw();group value;value.marker=marker;value.measured_bounds=measured_bounds;
		return record(value,before,commands());
	}
	bool draw_narrative_text(const std::function<void()>& draw)
	{
		if (!ready || current || !draw || !engine_stereo_bridge::is_active()) return false;
		const auto before=commands();if (!before.base) return false;
		draw();group value;value.narrative=true;
		return record(value,before,commands());
	}
	counters get_counters() noexcept { return {calls.load(),recorded.load(),matched.load(),rejected.load(),resets.load()}; }
	class component final: public component_interface
	{
	public:
		void post_unpack() override
		{
			constexpr std::uint8_t waypoint[]{0x40,0x55,0x53,0x57,0x48,0x8d,0xac,0x24,0x60,0xff,0xff,0xff};
			constexpr std::uint8_t projection[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18};
			constexpr std::uint8_t clamp[]{0x48,0x8b,0xc4,0x48,0x89,0x58,0x20,0x57};
			constexpr std::uint8_t reset[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57};
			constexpr std::uint8_t allocator[]{0x48,0x89,0x05,0xc5,0x25,0x82,0x10};
			constexpr std::uint8_t fov[]{0xf3,0x0f,0x5e,0x05,0x1b,0x33,0x85,0x01};
			if (!verify(0x140378CD0,waypoint) || !verify(0x14036F310,projection) || !verify(0x14037A1A0,clamp) ||
				!verify(0x14076E810,reset) || !verify(0x14076EBDC,allocator) || !verify(0x14036F1ED,fov))
			{ console::error("[VR indicators] native waypoint signatures rejected\n"); return; }
			reset_hook.create(0x14076E810,reset_stub);
			projection_hook.create(0x14036F310,projection_stub);
			clamp_hook.create(0x14037A1A0,clamp_stub);
			waypoint_hook.create(0x140378CD0,waypoint_stub);
			ready.store(true);
			constexpr std::uint8_t dead_quote_call[]{0xe8,0xd8,0xc5,0xff,0xff};
			constexpr std::uint8_t dead_quote_entry[]{0x40,0x55,0x56,0x41,0x55,0x41,0x57,0x48,0x8d,0xac,0x24,0xc8,0xfe,0xff,0xff};
			if(verify(0x14038AEE3,dead_quote_call) && verify(0x1403874C0,dead_quote_entry))
				utils::hook::call(0x14038AEE3,dead_quote_stub);
			else console::error("[VR narrative] native death quote boundary rejected\n");
			constexpr std::uint8_t cursor_call[]{0xe8,0x4f,0xba,0xff,0xff};
			constexpr std::uint8_t cursor_entry[]{0x4c,0x8b,0xdc,0x55,0x56,0x41,0x54,0x48,0x81,0xec,0x10,0x0a,0,0};
			constexpr std::uint8_t cursor_args[]{0x8b,0x85,0xcf,0,0,0,0x48,0x8d,0x55,0x97,0xf3,0x0f,0x10,0x9d,0xb7,0,0,0,
				0x4c,0x8b,0xc3,0x8b,0xce,0x89,0x44,0x24,0x20};
			if(verify(0x14038AF9C,cursor_call) && verify(0x1403869F0,cursor_entry) && verify(0x14038AF81,cursor_args))
				utils::hook::call(0x14038AF9C,cursor_hint_stub);
			else console::error("[VR prompts] native cursor hint fallback boundary rejected\n");
			// CG_DrawHudElem and its label/value text calls. These boundaries
			// retain native timing, localization, binding glyphs and font patches.
			constexpr std::uint8_t script_hud[]{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x8d,0xac,0x24,0x60,0xfd,0xff,0xff};
			constexpr std::uint8_t script_text[]{0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10};
			constexpr std::uint8_t label_key[]{0x8b,0x4e,0x50,0x85,0xc9};
			constexpr std::uint8_t value_key[]{0x8b,0x96,0x94,0,0,0,0x85,0xd2};
			constexpr std::uint8_t key_lookup[]{0x48,0x63,0xc1,0x48,0x8d,0x0d,0x3a,0xfc,0xc6,0x01,0x8b,0x0c,0x81};
			script_text_keys_ready=verify(0x14037B401,label_key) && verify(0x14037B4A2,value_key) && verify(0x1403C9D60,key_lookup);
			if(verify(0x14037B080,script_hud) && verify(0x14037AE70,script_text) && text_call(0x14037B202) && text_call(0x14037B24B))
			{
				script_hud_hook.create(0x14037B080,script_hud_stub);
				utils::hook::call(0x14037B202,script_label_stub);utils::hook::call(0x14037B24B,script_text_stub);
				// Common image layout is at 37AB52, not the separate 37A800
				// producer's scalar call. It covers all real HUD image branches.
				constexpr std::uint8_t image_layout[]{0xf7,0x86,0xb8,0,0,0,0,0x40,0,0};
				if(verify(0x14037AB52,image_layout))
				{
					const auto marker=utils::hook::assemble([](auto& a){emit_image_layout_marker(a,reinterpret_cast<std::uintptr_t>(observe_script_image_layout),0x14037AB5C);});
					if(const auto relay=utils::hook::create_preserving_near_jump(0x14037AB52,marker))
					{utils::hook::jump(0x14037AB52,relay);utils::hook::nop(0x14037AB57,5);}
					else console::error("[VR narrative] native image layout relay unavailable\n");
				}
				else console::error("[VR narrative] native hint backdrop layout boundary rejected\n");
			}
			else console::error("[VR narrative] native script HUD ownership signatures rejected\n");
		}
		void pre_destroy() override { ready.store(false); }
	};
}
REGISTER_COMPONENT(vr::native_waypoints::component)
