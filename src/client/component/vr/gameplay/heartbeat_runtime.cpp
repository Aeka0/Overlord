#include <std_include.hpp>
#include "hand_interaction/runtime.hpp"
#include "heartbeat_runtime.hpp"
#include "heartbeat_native_mode.hpp"
#include "weapon_instance_cache.hpp"
#include "part_hand_constraint.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "weapon_feedback.hpp"
#include "part_return_transition.hpp"
#include <utils/native_memory.hpp>
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <mutex>

namespace vr::gameplay::weapons::heartbeat
{
	namespace
	{
		using namespace hands::pose_math;
		using clock=controller_input::clock;
		struct record
		{
			carry::identity id{};
			hinged_attachment::gesture motion;
			physical_reload::part_return_transition fold_return,tilt_return;
			hand actor{hand::none};
			std::array<hinged_attachment::path,2> paths{};
			std::array<vec,4> corners{};
			clock::time_point rendered{},updated{};
			controller_input::consumer_continuity continuity;
			std::uint64_t input_continuity{};
			std::uint64_t reference{},render_reference{},revision{},assembly{},button_generation{},button_releases{};
			float units{};
			bool held{};
		};
		std::mutex mutex;
		instance_cache<record,carry::inventory::capacity> records;
		std::atomic_bool installed{};
		utils::hook::detour draw_hook;
		thread_local const std::array<vec,4>* drawing{};
		std::atomic_uint64_t grabs{},changes{},draws{},geometry_rejected{},updates{},previews{};
		template<class T> bool read(const void* p,size_t offset,T& value)noexcept
		{return p && utils::native_memory::read_bytes(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));}
		bool fresh(clock::time_point t,clock::time_point now)noexcept{return now>=t && now-t<=150ms;}
		bool native_open(std::uint32_t weapon)noexcept
		{
			const auto* ps=reinterpret_cast<const void*>(0x141BB3C30);
			std::uint32_t selected{},flags{};
			return read(ps,0x3bc,selected) && selected==weapon && read(ps,0x3c0,flags) && !(flags&0x4000);
		}
		bool any_open()noexcept
		{
			const auto now=clock::now();const std::lock_guard lock(mutex);
			for(const auto& entry:records.entries())if(const auto& r=entry.value;entry.id)if(r.held && r.motion.open() && r.reference==r.render_reference && fresh(r.updated,now) && fresh(r.rendered,now))return true;
			return false;
		}
		bool tracker_query(std::uint32_t weapon,bool alternate)
		{
			// Only five witnessed tracker call sites are redirected. Weapon-mode,
			// script and underbarrel property queries retain their original semantics.
			if(enabled())return any_open();
			return utils::hook::invoke<bool>(0x1406A55D0,weapon,alternate);
		}
		int geometry(int client,vec* center,float* width,float* height,vec* axis)
		{
			if(!drawing)return utils::hook::invoke<int>(0x140385230,client,center,width,height,axis);
			if(!center || !width || !height || !axis)return 0;
			// Preserve the original three-tag winding instead of assuming how its
			// corner names map to the renderer's width/height axes.
			std::array<vec,3> points{};
			for(int i=0;i<3;++i)
			{
				std::uint32_t tag{};
				if(!read(reinterpret_cast<void*>(0x14641D9CC),i*4,tag) || !tag){++geometry_rejected;return 0;}
				const auto* name=game::SL_ConvertToString(static_cast<game::scr_string_t>(tag));
				int index=-1;
				for(int j=0;j<4;++j)if(name && bone_names[5+j]==name)index=j;
				if(index<0){++geometry_rejected;return 0;}points[i]=(*drawing)[index];
			}
			const auto vertical=sub(points[0],points[1]),horizontal=sub(points[1],points[2]);
			*height=length(vertical);*width=length(horizontal);
			if(!std::isfinite(*height) || !std::isfinite(*width) || *height<.01f || *width<.01f || *width>20 || *height>20)return 0;
			axis[2]=scale(vertical,1 / *height);axis[1]=scale(horizontal,1 / *width);
			axis[0]=cross(axis[2],axis[1]);*center=scale(add(points[0],points[2]),.5f);return 1;
		}
		void draw(int client)
		{
			if(!enabled()){draw_hook.invoke<void>(client);return;}
			std::array<std::array<vec,4>,2> screens{};size_t count{};const auto now=clock::now();
			{
				const std::lock_guard lock(mutex);
				for(const auto& entry:records.entries())if(const auto& r=entry.value;entry.id)if(r.held && r.motion.open() && r.reference==r.render_reference && fresh(r.updated,now) && fresh(r.rendered,now) && count<screens.size())screens[count++]=r.corners;
			}
			// Native scan targets, sweep phase and materials remain native-owned.
			// Each held screen supplies its own solved world geometry to that pass.
			const auto previous=drawing;const auto restore=gsl::finally([&]{drawing=previous;});
			for(size_t i=0;i<count;++i){drawing=&screens[i];draw_hook.invoke<void>(client);++draws;}
		}
		bool call_matches(std::uintptr_t address,std::uintptr_t destination)noexcept
		{
			std::array<std::byte,5> bytes{};std::int32_t displacement{};
			if(!utils::native_memory::read_bytes(bytes.data(),reinterpret_cast<void*>(address),bytes.size()) || bytes[0]!=std::byte{0xe8})return false;
			std::memcpy(&displacement,bytes.data()+1,4);return address+5+displacement==destination;
		}
	}
	bool enabled()noexcept{return installed.load() && carry::active();}
	part_rig bind_native(std::span<const game::XModel* const> native,std::span<const model_definition> models,
		const rig& layout,std::span<const bone_definition> bones)noexcept
	{
		if(native.size()!=models.size())return {};
		for(size_t i=0;i<models.size();++i)if(models[i].name=="attach_h2_heartbeat_vm" || models[i].name=="attach_h2_heartbeat_vm_arctic")
		{
			if(models[i].count!=9)return {};
			const game::XBoneInfo* bounds{};game::XBoneInfo body{};
			if(!read(native[i],offsetof(game::XModel,boneInfo),bounds) || !read(bounds,2*sizeof(game::XBoneInfo),body))return {};
			housing_bounds volume;
			for(int axis=0;axis<3;++axis){volume.low[axis]=body.bounds.midPoint[axis]-body.bounds.halfSize[axis];
				volume.high[axis]=body.bounds.midPoint[axis]+body.bounds.halfSize[axis];}
			return bind(models,layout,bones,volume);
		}
		return {};
	}
	bool equivalent_mode(std::uint32_t weapon)noexcept
	{
		if(!installed || !weapon || (weapon&~511u))return false;
		const void* definition{};const char* name{};std::array<char,128> text{};
		if(!read(reinterpret_cast<void*>(0x14CE01580),weapon*8,definition) || !read(definition,0,name) ||
			!utils::native_memory::read_bytes(text.data(),name,text.size()))return false;
		const auto size=strnlen_s(text.data(),text.size());if(!size || size==text.size())return false;
		const std::string_view token(text.data(),size);
		std::uint32_t linked_weapon{};
		if(!read(definition,0x5c0,linked_weapon) || !admits_native_mode(token,linked_weapon,
			utils::hook::invoke<bool>(0x1406A55D0,weapon,false),utils::hook::invoke<bool>(0x1406A55D0,weapon,true)))return false;
		// Both the ACR toggle and fixed M240 sensor must preserve the same feed.
		// Never admit an underbarrel's separate ammunition as a folded accessory.
		const auto key=[&](std::uintptr_t address,bool alternate){return utils::hook::invoke<std::uint64_t>(address,weapon,alternate);};
		return (key(0x14069D250,false)&0xffffffffffull)==(key(0x14069D250,true)&0xffffffffffull) &&
			(key(0x14069D0B0,false)&0xffffffffffffull)==(key(0x14069D0B0,true)&0xffffffffffffull);
	}
	void suspend()noexcept
	{
		const std::lock_guard lock(mutex);
		for(auto& entry:records.entries()){auto& r=entry.value;r.motion.cancel();r.actor=hand::none;r.held=false;}
	}
	void collect_interactions(const hand_interaction::frame& f)noexcept
	{
		namespace hi=hand_interaction;if(!enabled())return;const std::lock_guard lock(mutex);
		for(const auto& e:records.entries())if(e.id){const auto* s=f.find(e.id);if(!s)continue;const auto& r=e.value;if(r.assembly!=s->assembly || r.units!=f.body.units_per_meter || r.render_reference!=f.input.reference_generation)continue;
			for(int h=0;h<2;++h){const auto edge=hi::input(hand(h),hi::button::trigger);if(!edge.press || !edge.down || s->owner.holding_hand()==hand(h))continue;
				auto local=compose(inverse(s->gun),f.wrists[h]);local.position=scale(local.position,1/f.body.units_per_meter);auto trial=r.motion;
				if(trial.acquire(r.paths[h],local))hi::offer({hand(h),{hi::object(hi::domain::sensor,e.id,0,e.value.assembly),hi::role::sensor,hi::button::trigger,hi::recipe::single,hi::capability::action},edge.event,20,hinged_attachment::acquisition_distance(r.paths[h],local),1,true,true});}}
	}
	void report_interactions()noexcept
	{
		namespace hi=hand_interaction;const std::lock_guard lock(mutex);
		for(const auto& e:records.entries())if(e.id && e.value.held && e.value.motion.held() && valid_hand(e.value.actor))
			hi::observed(e.value.actor,{hi::object(hi::domain::sensor,e.id,0,e.value.assembly),hi::role::sensor,hi::button::trigger,hi::recipe::single,hi::capability::action});
	}
	void update(const controller_input::frame& input,std::span<const carry::instance> owned,std::span<const carry::scene> scenes,
		const std::array<anchor,2>& wrists,float units,unsigned available)noexcept
	{
		if(!enabled() || !std::isfinite(units) || units<=0 || owned.size()!=scenes.size()){suspend();return;}
		namespace hi=hand_interaction;unsigned pulse{};const auto now=input.sampled_at;++updates;
		{
			const std::lock_guard lock(mutex);
			records.retain([](weapon_identity id){return carry::contains(id);});
			for(auto& entry:records.entries())entry.value.held=false;
			for(size_t i=0;i<owned.size();++i)
			{
				const auto& v=owned[i];auto* cached=records.find(v.id);if(!cached)continue;
				auto& r=*cached;const auto& scene=scenes[i];
				if(r.id!=v.id || v.at!=carry::location::held || !valid_hand(v.owner.holding_hand()) || scene.owner.id()!=v.id ||
					r.assembly!=scene.assembly || r.units!=units)continue;
				const bool discontinuity=r.continuity.update(input,now);
				if(discontinuity || r.reference!=input.reference_generation){r.motion.cancel();r.actor=hand::none;r.reference=input.reference_generation;}
				r.input_continuity=input.continuity_generation;
				r.held=true;const float dt=std::chrono::duration<float>(now-r.updated).count();r.updated=now;
				// A physical holding-hand change ends the grasp like a release.
				// Support-only revisions do not erase a sensor's fold state.
				if(r.revision!=v.owner.rear_revision){r.motion.release();r.actor=hand::none;r.revision=v.owner.rear_revision;}
				const bool before=r.motion.open();
				if(valid_hand(r.actor))
				{
					const int h=int(r.actor);
					const auto edge=hi::input(r.actor,hi::button::trigger);
					if(input.trigger[h].generation!=r.button_generation)r.motion.cancel();
					else
					{
						const bool release=edge.release || (input.trigger[h].active && !input.trigger[h].down);
						// Include the final tracked stroke before settling its endpoint.
						// A bad release-frame pose cannot erase the last valid stroke;
						// inactive input without a proven release is cancellation instead.
						if((available&(1u<<h)) && hi::permits(r.actor,hi::domain::sensor,r.id))
						{auto local=compose(inverse(scene.gun),wrists[h]);local.position=scale(local.position,1/units);auto moved=r.motion;moved.move(r.paths[h],local,dt,input.continuity_generation!=0);
							if(!release || moved.held())r.motion=moved;}
						if(release)r.motion.release();
						else if(!edge.down || !(available&(1u<<h)) || !hi::permits(r.actor,hi::domain::sensor,r.id))r.motion.cancel();
					}
					if(before!=r.motion.open()){++changes;pulse|=1u<<h;}
					if(!r.motion.held())r.actor=hand::none;
				}
				else for(int h=0;h<2;++h)
				{
					const auto edge=hi::input(hand(h),hi::button::trigger);
					if(!(available&(1u<<h)) || !edge.press || edge.release || !edge.down || v.owner.holding_hand()==hand(h) ||
						!hi::granted(hand(h),hi::domain::sensor,r.id,hi::button::trigger,hi::role::sensor))continue;
					auto local=compose(inverse(scene.gun),wrists[h]);local.position=scale(local.position,1/units);
					if(r.motion.acquire(r.paths[h],local)){r.actor=hand(h);r.button_generation=input.trigger[h].generation;r.button_releases=input.trigger[h].releases;available&=~(1u<<h);++grabs;pulse|=1u<<h;break;}
				}
			}
			for(auto& entry:records.entries())if(auto& r=entry.value;!r.held){r.motion.release();r.actor=hand::none;}
		}
		for(int h=0;h<2;++h)if(pulse&(1u<<h))feedback::carry_confirmation(static_cast<hand>(h),input);
	}
	void present(const part_rig& parts,const rig& layout,const pose_library& library,const profile& grip,
		const controller_input::frame& input,const hold& owner,std::uint64_t assembly,const std::array<anchor,2>& targets,
		const std::array<vec,2>& shoulders,const std::array<vec,3>& body_axis,vec view_offset,float units,std::span<bone> solved,hands::part_hand_frame* hand_motion)noexcept
	{
		if(!enabled() || !parts.valid || !library.valid || solved.size()<size_t(layout.count) || !std::isfinite(units) || units<=0 || !equivalent_mode(owner.weapon))return;
		const auto id=owner.id();if(!id || !carry::contains(id))return;
		const auto gun=as_anchor(solved[layout.gun]);float amount{},tilt{};hand actor{hand::none};
		{
			const std::lock_guard lock(mutex);
			records.retain([](weapon_identity key){return carry::contains(key);});
			auto* cached=records.acquire(id);if(!cached)return;auto& r=*cached;
			if(r.id!=id){r={};r.id=id;r.motion.reset(native_open(id.weapon));r.revision=owner.rear_revision;}
			auto preview=r.motion;actor=r.actor;
			if(valid_hand(actor))
			{
				const int h=int(actor);
				if(actor==owner.holding_hand() || r.revision!=owner.rear_revision || r.reference!=input.reference_generation || r.input_continuity!=input.continuity_generation || r.assembly!=assembly || r.units!=units ||
					!input.focused || !input.trigger[h].active || input.trigger[h].generation!=r.button_generation ||
					!input.grip[h].valid || !input.aim[h].valid)preview.cancel();
				else
				{
					const bool release=!input.trigger[h].down || input.trigger[h].releases!=r.button_releases;
					// Render the latest XR angle without committing scan state or a
					// hand lease from the renderer. Server cadence cannot step the lid.
					if(input.sampled_at>r.updated)
					{auto local=compose(inverse(gun),targets[h]);local.position=scale(local.position,1/units);
						auto moved=preview;moved.move(r.paths[h],local,std::chrono::duration<float>(input.sampled_at-r.updated).count(),input.continuity_generation!=0);
						if(!release || moved.held())preview=moved;++previews;}
					if(release)preview.release();
				}
				if(!preview.held())actor=hand::none;
			}
			const bool held=valid_hand(actor);const auto now=input.sampled_at;
			amount=r.fold_return.update(id.generation,input.reference_generation,held,preview.amount(),now,.09f);
			const bool flat=held || !preview.open();
			tilt=1-r.tilt_return.update(id.generation,input.reference_generation,flat,flat?1.f:0.f,now,.12f);
		}
		std::array<hinged_attachment::path,2> paths{};std::array<anchor,2> wrists{};
		const auto mechanical=pose(parts,amount,tilt);
		for(int h=0;h<2;++h)
		{
			auto local=authored::wrist_in_sensor;
			if(h==1)local=hands::pose_mirror::wrist(local,library.mirror_basis[layout.arms[1].wrist]);
			paths[h]=manipulation_path(parts,local,units,amount,tilt);wrists[h]=compose(mechanical[3],local);
		}
		for(int i=0;i<9;++i)move_part(layout,parts.bones[i],compose(gun,mechanical[i]),solved);
		if(valid_hand(actor) && actor!=owner.holding_hand())
		{
			const int h=int(actor);const auto wrist=compose(gun,wrists[h]);
			if(constrain_part_hand(layout,library,grip,targets,shoulders,body_axis,1-h,wrist,solved))
				hands::pose_mirror::fingers(layout,library,grip,authored::fingers,h,solved,h==1);
		}
		if(hand_motion && valid_hand(actor))hand_motion->apply(int(actor),part_hand_attachment::sensor);
		std::array<vec,4> corners{};for(int i=0;i<4;++i)corners[i]=add(solved[parts.bones[5+i]].position,view_offset);
		const std::lock_guard lock(mutex);auto* cached=records.find(id);if(!cached)return;auto& r=*cached;if(r.id!=id)return;
		r.paths=paths;r.corners=corners;r.rendered=input.sampled_at;r.render_reference=input.reference_generation;r.assembly=assembly;r.units=units;
	}
	class component final:public component_interface
	{
	public:
		void post_unpack()override
		{
			constexpr std::array<std::uintptr_t,5> calls{0x140383020,0x140383346,0x1403835C6,0x14038372C,0x1403837FC};
			constexpr std::uint8_t entry[]{0x40,0x55,0x53,0x41,0x54,0x48,0x8d,0xac,0x24,0x20,0xff,0xff,0xff,0x48,0x81,0xec};
			constexpr std::uint8_t property[]{0x48,0x83,0xec,0x28,0x41,0xb8,0x90,0x0e,0x00,0x00};
			std::array<std::uint8_t,sizeof(entry)> mask{};mask.fill(0xff);
			if(!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140383F50),{entry,mask.data(),sizeof(entry)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1406A55D0),{property,mask.data(),sizeof(property)}) ||
				!call_matches(0x140383FB5,0x140385230) || !std::all_of(calls.begin(),calls.end(),[](auto p){return call_matches(p,0x1406A55D0);}))
			{console::warn("[VR heartbeat] native contracts rejected; attachment remains native\n");return;}
			// Allocate the relays before mutating call sites; ASLR must not turn a
			// valid adapter into a 32-bit relative-branch startup failure.
			const auto geometry_relay=utils::hook::create_far_jump<0x140000000>(geometry);
			const auto query_relay=utils::hook::create_far_jump<0x140000000>(tracker_query);
			draw_hook.create(0x140383F50,draw);
			utils::hook::call(0x140383FB5,geometry_relay);
			for(auto p:calls)utils::hook::call(p,query_relay);
			installed=true;
			command::add("vr_heartbeat_status",[]
			{
				console::info("[VR heartbeat] enabled=%d grabs=%llu changes=%llu draws=%llu geometryReject=%llu updates=%llu renderPreviews=%llu\n",enabled(),grabs.load(),changes.load(),draws.load(),geometry_rejected.load(),updates.load(),previews.load());
				const std::lock_guard lock(mutex);
				for(const auto& entry:records.entries())if(const auto& r=entry.value;entry.id)if(r.held)console::info("  weapon=%u generation=%llu open=%d travel=%.3f hand=%d\n",r.id.weapon,r.id.generation,r.motion.open(),r.motion.amount(),int(r.actor));
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::heartbeat::component)
