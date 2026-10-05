#include <std_include.hpp>
#include "hand_interaction/runtime.hpp"
#include "world_interaction.hpp"
#include "weapon_carry_runtime.hpp"
#include "interaction_debug.hpp"
#include "native_use.hpp"
#include "native_scripted_control.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>
#include <sstream>

namespace vr::gameplay::interaction
{
	namespace
	{
		game::dvar_t* enabled{},*reach{},*cone{};
		std::atomic_bool ready{},alive{true};
		std::atomic_uint lease_hands{};
		use_lease lease; // Server hand arbiter only.
		query_batch queries;
		std::mutex mutex;
		struct publication
		{
			presentation hover{};
			target use{};
			int hand{-1};
			bool held{},native_button{};
			std::uint64_t serial{};
		} state;
		notification_gate notifications; // Existing command owner only.
		bool pending_release{};
		std::uint64_t gesture_serial{};
		std::atomic_uint64_t press_notifies{},release_notifies{};
		std::atomic_uint64_t updates{},picks{},uses{},cancels{};
		bool notification_ready()
		{
			constexpr std::uint8_t expected[]{0x40,0x53,0x55,0x57,0x48,0x81,0xec,0x30,0x04,0,0};
			std::array<std::uint8_t,sizeof(expected)> mask{};mask.fill(0xff);
			// Binding identities are verified in addition to the existing jump
			// adapter's validated reliable-queue entry/callsite.
			const auto* table=reinterpret_cast<const char* const*>(0x140BF84E0);
			return table[73] && table[74] && std::string_view(table[73])=="+activate" &&
				std::string_view(table[74])=="-activate" &&
				bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403D3A90),{expected,mask.data(),sizeof(expected)}));
		}
		bool can_notify()
		{
			// Match the native reliable queue's admission checks. In particular it
			// discards fresh bindings while paused, so defer the matching release
			// until the queue can accept it rather than losing a script notification.
			const auto* paused=game::Dvar_FindVar("cl_paused");
			return ready && utils::hook::invoke<bool>(0x1406B3860) && paused && !paused->current.integer &&
				!utils::hook::invoke<bool>(0x1406B0120) && !utils::hook::invoke<int>(0x1404C8700);
		}
		target query_target(int hand,const ray& aim,target_key held,const controller_input::frame& input)
		{
			debug::world_sample diagnostic;const bool diagnose=debug::world_enabled();
			const auto chosen=native::query(aim,held,diagnose?&diagnostic:nullptr);
			if(diagnose){diagnostic.reference=input.reference_generation;diagnostic.at=input.sampled_at;debug::publish_world(hand,diagnostic);}
			return chosen;
		}
	}
	void suspend() noexcept
	{
		lease.cancel();lease_hands=0;queries.begin();
		const std::lock_guard lock(mutex);state.hover={};state.held=false;state.native_button=false;
	}
	bool hand_leased(int hand) noexcept {return hand>=0 && hand<2 && (lease_hands.load()&(1u<<hand));}
	bool scripted_frame(hand_interaction::frame& f)noexcept
	{
		if(!ready || !alive || !enabled->current.enabled || !weapons::carry::active() || !game::CL_IsCgameInitialized() ||
			!sequences::for_player(game::g_entities[0].client).allow_world_use || scripted_control::allowed(game::g_entities[0].client))return false;
		f.input=controller_input::latest();const auto now=controller_input::clock::now();const auto head=head_pose_bridge::get_status();
		const auto* paused=game::Dvar_FindVar("cl_paused");
		if(!f.input.focused || f.input.orientation_settling || !f.input.sequence || !head.enabled || !head.pose_available || head.recenter_pending ||
			*game::keyCatchers || !paused || paused->current.integer || now<f.input.sampled_at || now-f.input.sampled_at>150ms ||
			!head_pose_bridge::get_spatial_frame(f.body) || f.body.generation!=f.input.reference_generation ||
			now<f.body.captured_at || now-f.body.captured_at>150ms)return false;
		for(unsigned h=0;h<2;++h)
		{
			head_pose_bridge::world_pose grip,aim;
			if(!f.input.grip[h].valid || !f.input.aim[h].valid || !head_pose_bridge::tracking_to_world(f.body,f.input.grip[h].tracking,grip) ||
				!head_pose_bridge::tracking_to_world(f.body,f.input.aim[h].tracking,aim))continue;
			f.valid_hands|=1u<<h;f.wrists[h]={grip.position,hands::from_axis(grip.axis)};
		}
		return f.valid_hands!=0;
	}
	void collect_interactions(const hand_interaction::frame& f)noexcept
	{
		queries.begin(f.input.sequence,f.input.reference_generation);
		namespace hi=hand_interaction;if(!ready || !alive || !enabled->current.enabled)return;
		for(int h=0;h<2;++h)
		{
			const auto edge=hi::input(vr::hand(h),hi::button::grip);if(!edge.press || (!edge.down && !edge.release) || !(f.valid_hands&(1u<<h)))continue;
			head_pose_bridge::world_pose aim;if(!head_pose_bridge::tracking_to_world(f.body,f.input.aim[h].tracking,aim))continue;
			const ray r{aim.position,aim.axis[0],f.body.head_position,f.body.units_per_meter,reach->current.value,cone->current.value};
			const auto chosen=query_target(h,r,lease.active() && lease.hand()==h?lease.value().key:target_key{},f.input);
			queries.record(unsigned(h),r,chosen);
			if(!chosen || (chosen.weapon && !edge.down))continue;
			hi::offer({vr::hand(h),{hi::world_object(std::uint32_t(chosen.key.entity),chosen.key.generation),hi::role::world,hi::button::grip,hi::recipe::single,{}},edge.event,chosen.contact?20u:50u,chosen.distance/(r.units*r.distance_meters),chosen.cosine,true,true});
		}
	}
	void report_interactions()noexcept
	{
		namespace hi=hand_interaction;if(lease.owns_hand())hi::observed(vr::hand(lease.hand()),{hi::world_object(std::uint32_t(lease.value().key.entity),lease.value().key.generation),hi::role::world,hi::button::grip,hi::recipe::single,{}});
	}
	void update(const controller_input::frame& input,const head_pose_bridge::spatial_frame& body,
		unsigned available,unsigned pressed,unsigned released,const pickup_callback& pickup)
	{
		const auto now=controller_input::clock::now();
		if (!ready || !alive || !enabled->current.enabled || !input.focused || !input.sequence ||
			now<input.sampled_at || now-input.sampled_at>150ms || body.generation!=input.reference_generation)
		{suspend();return;}
		++updates;
		publication next;{const std::lock_guard lock(mutex);next=state;}
		next.hover={};next.hover.at=input.sampled_at;next.hover.units=body.units_per_meter;next.hover.reference=body.generation;
		unsigned held{};std::array<ray,2> rays{};
		for (int h=0;h<2;++h)
		{
			if (input.squeeze[h].active && input.squeeze[h].down) held|=1u<<h;
			head_pose_bridge::world_pose aim;
			if (!(available&(1u<<h)) || !input.grip[h].valid || !input.aim[h].valid ||
				!head_pose_bridge::tracking_to_world(body,input.aim[h].tracking,aim)) {available&=~(1u<<h);continue;}
			rays[h]={aim.position,aim.axis[0],body.head_position,body.units_per_meter,reach->current.value,cone->current.value};
			if(const auto chosen=queries.find(unsigned(h),input.sequence,input.reference_generation,rays[h]))
				next.hover.targets[h]=*chosen && !native::live(*chosen)?target{}:*chosen;
			else next.hover.targets[h]=query_target(h,rays[h],lease.active() && lease.hand()==h?lease.value().key:target_key{},input);
		}
		const bool had=lease.active();
		bool retained=!lease.value() || native::live(lease.value());
		if (had && lease.value())
		{
			// The locked target must remain admitted, in range, in the hand cone and
			// visible. Another candidate becoming more centered cannot steal a hold.
			retained=retained && next.hover.targets[lease.hand()].key==lease.value().key;
		}
		lease.retain(available,held&~released,body.generation,retained);
		if (had && !lease.active()) ++cancels;
		// Resolve simultaneous world-use presses by the same angular priority as
		// hover. Native F has one active hold; neither hand can steal that lease.
		std::array<int,2> order{0,1};
		if (better(next.hover.targets[1],next.hover.targets[0])) std::swap(order[0],order[1]);
		for (const int h:order)
		{
			if (!(pressed&(1u<<h)) || !(available&(1u<<h)) || lease.active()) continue;
			const auto chosen=next.hover.targets[h];
			if(!chosen && hand_interaction::has(vr::hand(h),hand_interaction::domain::world))continue;
			if(chosen && !hand_interaction::granted(vr::hand(h),hand_interaction::domain::world,{std::uint32_t(chosen.key.entity),chosen.key.generation}))continue;
			if (chosen.weapon)
			{
				if (!(released&(1u<<h)) && pickup(chosen,h))
				{
					++picks;next.use=chosen;next.hand=h;next.serial=++gesture_serial;
					next.native_button=false;next.hover.targets[h]={};
				}
				continue;
			}
			if (lease.begin(h,chosen,body.generation))
			{
				++uses;next.use=chosen;next.hand=h;next.serial=++gesture_serial;next.native_button=true;
				if (released&(1u<<h))
				{
					lease.cancel(); // A tap still sends one native press.
					if(chosen)hand_interaction::completed(vr::hand(h),hand_interaction::world_object(std::uint32_t(chosen.key.entity),chosen.key.generation));
				}
			}
		}
		next.held=lease.active();
		lease_hands=lease.owns_hand() ? 1u<<lease.hand() : 0;
		{const std::lock_guard lock(mutex);state=next;}
	}
	int command(const controller_input::frame& input,bool gameplay,controller_input::clock::time_point now)
	{
		publication current;{const std::lock_guard lock(mutex);current=state;}
		const bool valid=alive && ready && gameplay && enabled->current.enabled && input.focused &&
			current.hover.at.time_since_epoch().count() && now>=current.hover.at && now-current.hover.at<=150ms &&
			now>=input.sampled_at && now-input.sampled_at<=150ms && input.reference_generation==current.hover.reference;
		const bool down=valid && current.held && current.hand>=0 && current.hand<2 &&
			input.squeeze[current.hand].active && input.squeeze[current.hand].down;
		const bool queue_ready=can_notify();
		const auto edge=notifications.consume(current.serial,down,valid && queue_ready);
		const auto release=[&] {
			if (queue_ready) {utils::hook::invoke<void>(0x1403D3A90,0,74,0);++release_notifies;pending_release=false;}
			else pending_release=true;
		};
		if (pending_release && queue_ready) release();
		if (edge.release_before) release();
		if (edge.press) {utils::hook::invoke<void>(0x1403D3A90,0,73,0);++press_notifies;}
		if (edge.release_after) release();
		const bool use=valid && current.native_button && (edge.down || edge.press);
		native::set_command_lease(current.use,use,current.hover.reference);
		return use ? 0x8 : 0; // +activate; deliberately distinct from use/reload 0x20.
	}
	presentation latest() noexcept {const std::lock_guard lock(mutex);return state.hover;}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			enabled=dvars::register_bool("vr_worldInteraction",true,game::DVAR_FLAG_SAVED,"Empty-hand grip uses native world interactions");
			reach=dvars::register_float("vr_interactionReach",2.2f,.5f,3.f,game::DVAR_FLAG_SAVED,"Maximum world interaction reach from hand and head in meters");
			cone=dvars::register_float("vr_interactionCone",12.f,1.f,25.f,game::DVAR_FLAG_SAVED,"Hand-ray interaction half angle in degrees");
			if (!notification_ready() || !native::initialize()) {console::error("[VR use] Native interaction contracts rejected\n");return;}
			ready=true;
			command::add("vr_interaction_status",[] {
				scheduler::once([] {
					publication current;{const std::lock_guard lock(mutex);current=state;}
					std::ostringstream out;out<<"ready="<<ready<<" updates="<<updates<<" pickups="<<picks<<" uses="<<uses<<" cancelled="<<cancels
						<<" press_notifies="<<press_notifies<<" release_notifies="<<release_notifies
						<<" native="<<native::status()<<"\nreach_m="<<reach->current.value<<" cone_degrees="<<cone->current.value
						<<" lease_hand="<<current.hand<<" held="<<current.held<<" target="<<current.use.key.entity<<'\n';
					for (int h=0;h<2;++h) {const auto& t=current.hover.targets[h];out<<"hand="<<h<<" entity="<<t.key.entity<<" generation="<<t.key.generation
						<<" weapon="<<t.weapon<<" cosine="<<t.cosine<<" distance="<<t.distance<<'\n';}
					out<<prompt_status();
					const auto text=out.str();console::info("%s",text.c_str());
					scheduler::once([text]{utils::io::write_file_atomic("minidumps/h2-mod-vr-interaction.txt",text);},scheduler::pipeline::async);
				},scheduler::pipeline::server);
			});
		}
		void pre_destroy() override {alive=false;}
	};
}
REGISTER_COMPONENT(vr::gameplay::interaction::component)
