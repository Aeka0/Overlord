#include <std_include.hpp>
#include "hand_interaction/runtime.hpp"
#include "empty_hands_native.hpp"
#include "physical_reload_presenter.hpp"
#include "falling_item_presenter.hpp"
#include "reload_attachment_state.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_carry_runtime.hpp"
#include "interaction_debug.hpp"
#include "viewmodel_visibility.hpp"
#include "part_hand_constraint.hpp"
#include "part_return_transition.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "knife_magazine_pose.hpp"
#include "magazine_grip_selection.hpp"
#include "weapon_render_pose.hpp"
#include "reload_well_debug.hpp"
#include "hk_slap_debug.hpp"
#include "bolt_debug.hpp"
#include "cover_push_debug.hpp"
#include "belt_presentation.hpp"
#include "arm_clearance.hpp"
#include <utils/native_memory.hpp>
#include "weapon_reload_profiles.hpp"
#include "native_magazine_assets.hpp"
#include "native_partition_assets.hpp"
#include "chamber_cartridge.hpp"
#include "chambering_guide.hpp"
#include "chambering_guide_presenter.hpp"
#include "charging_handle_fold_motion.hpp"
#include "component/scene_models.hpp"
#include "component/scene_pose_match.hpp"
#include "component/scene_model_record.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <mutex>

namespace vr::gameplay::weapons::physical_reload
{
	namespace
	{
		using namespace hands;
		std::atomic_bool alive{true};
		std::atomic_uint64_t scene_generation{1};
		std::mutex mutex;
		struct held_frame
		{
			std::uint32_t weapon{}; std::uint64_t instance{};
			anchor world{}, attached{}; clock::time_point at{};
			presentation view{}; // same native skeleton epoch as held position and hide mask
			std::uintptr_t object{}, matrices{};
			std::uint32_t epoch{};
			anchor model{};
			float units{};
			const reload_profile* definition{};
			size_t slot{};
			std::array<anchor,2> partition_model{};vec partition_origin{};bool partitioned_mesh{};
			anchor inserted_model{};vec inserted_origin{};bool counted_inserted{};
			anchor chamber_model{};bool live_chamber{};
			anchor attached_model{};
		};
		std::array<held_frame,15> held_frames{};
		std::array<held_frame,128> held_poses{};
		size_t held_cursor{};
		struct return_state {part_return_transition slide,catch_motion,bridge,cover,bolt_lift;std::uint64_t cover_owner_revision{};charging_handle_fold_motion fold;hands::arm_clearance::motion support_arm;};
		instance_cache<return_state,512> returns;
		std::atomic_uint64_t attachment_matches{}, attachment_misses{};
		std::array<std::atomic_uint64_t,4> attachment_rejections{}; // record, epoch, state, origin
		struct dropped
		{
			bool active{}; presentation::event event{};
			::vr::gameplay::motion::flight motion{};
			const reload_profile* definition{};
			std::uint32_t weapon{}; std::uint64_t instance{};
			std::uint64_t reference{};
			std::uint64_t scene{};
			weapon_identity owner{};
		};
		std::array<dropped,8> drops{};
		// R_FilterXModelIntoScene copies placement but RETAINS this pointer in
		// sceneModel+0x68. Lighting handles must outlive all queued render work.
		std::array<unsigned short,15+8+30+15+15> lighting_handles{}; // Held, drops, partition, inserted, live chamber.
		struct event_cursor { std::uint64_t instance{}, sequence{}; };
		instance_cache<event_cursor,512> event_cursors;
		template<class T> bool read(const void* source, size_t offset, T& output) noexcept
		{
			return source && utils::native_memory::read_bytes(&output,
				static_cast<const std::byte*>(source)+offset,sizeof(output));
		}
		scene_models::placement_result prepare_held(const scene_models::preparation& preparing, const void* entry,
			game::GfxPlacement& placed, game::GfxPlacement& previous) noexcept
		{
			using result = scene_models::placement_result;
			std::uintptr_t lighting{};
			const auto base=reinterpret_cast<std::uintptr_t>(lighting_handles.data());
			if (!alive.load() || !scene_models::native_entry::lighting(entry,lighting) || lighting<base || lighting>=base+sizeof(lighting_handles) || (lighting-base)%sizeof(unsigned short)) return result::unchanged;
			const auto index=(lighting-base)/sizeof(unsigned short);
			if(index>=15 && index<23)return result::unchanged;
			const bool chamber=index>=68;
			const bool inserted=index>=53 && !chamber;
			const bool partitioning=index>=23 && index<53;const auto piece=partitioning?(index-23)%2:0;
			const auto slot=chamber?index-68:inserted?index-53:partitioning?(index-23)/2:index;
			weapon_identity id;
			{const std::lock_guard lock(mutex);id=held_frames[slot].view.owner.id();}
			game::XModel* model{};
			if (!scene_models::native_entry::model(entry,model) || !model) return result::omit;
			const auto reject = [](size_t reason) { ++attachment_misses; ++attachment_rejections[reason]; return result::omit; };
			std::array<float,12> camera{};
			weapon_render_pose::snapshot skinned;
			if (!read(preparing.record,engine_stereo_view::h2_view_origin_offset,camera) ||
				!weapon_render_pose::for_record(reinterpret_cast<std::uintptr_t>(preparing.record),camera,skinned,id))
				return reject(0);
			held_frame selected;
			bool found = false;
			{
				const std::lock_guard lock(mutex);
				found=scene_models::latest_skeleton_pose(held_poses,held_cursor,skinned,
					[&](const held_frame& value)noexcept{return value.view.owner.id()==id;},selected);
			}
			if (!found) return reject(1);
			game::XModel* expected{};anchor adjustment;
			if(partitioning)
			{
				const auto asset=partition_assets::get(selected.definition);if(!selected.partitioned_mesh || !asset)return reject(2);
				expected=asset.models[piece];adjustment=asset.in_part[piece];
			}
			else {const auto asset=chamber?magazine_assets::cartridge(selected.definition):magazine_assets::get(selected.definition,inserted?selected.view.ammo.magazine_rounds:selected.view.ammo.held_rounds);expected=asset.model;adjustment=asset.in_magazine;}
			if (!selected.definition || !expected || model != expected) return reject(2);
			vec origin{};
			// Skinning can publish during this preparation job. Check freshness at
			// consumption, not its start, or a newly skinned hand would be omitted.
			const auto consumed_at = clock::now();
			const auto live = current(selected.view.owner.id());
			const auto kind=chamber?attachment_kind::chamber_round:inserted?attachment_kind::inserted_magazine:
				partitioning?attachment_kind::action_partition:attachment_kind::held_magazine;
			if(selected.instance!=selected.view.ammo.instance_generation || selected.definition!=selected.view.definition ||
				!attachment_state_matches(kind,selected.view,live,selected.at,consumed_at) ||
				(inserted && !selected.counted_inserted) || (chamber && !selected.live_chamber))return reject(2);
			if (!read(preparing.record,engine_stereo_view::h2_current_model_placement_origin_offset,origin) || !finite(origin))
				return reject(3);
			// Same native skinned epoch and CURRENT scene placement as the arm.
			// No solve-time world origin, latest pose or previous-frame substitution.
			auto world=chamber?selected.chamber_model:inserted?selected.inserted_model:partitioning?selected.partition_model[piece]:selected.model;
			world.position=add(world.position,origin);
			world=compose_reload(world,adjustment);
			std::copy(world.position.begin(),world.position.end(),placed.origin);
			std::copy(world.rotation.begin(),world.rotation.end(),placed.quat);
			previous=placed; // preserve current no-history semantics, never inject old solve-origin motion
			++attachment_matches;
			return result::replace;
		}
		anchor compose(anchor a, anchor b) noexcept
		{ return hands::pose_math::compose(a,b); }
		anchor as_anchor(const bone& b) noexcept { return {b.position,normalize(b.rotation)}; }
		anchor inverse(anchor a) noexcept
		{ return hands::pose_math::inverse(a); }
		void move_part(const rig& r, int root, anchor target, std::span<bone> pose)
		{ hands::pose_math::move_part(r,root,target,pose); }
		void pose_fingers(const rig& r, const pose_library& library, const weapons::profile& grip,
			std::span<const joint_pose> fingers, int off, std::span<bone> pose)
		{
			hands::pose_mirror::fingers(r,library,grip,fingers,off,pose,off==1);
		}
		void submit_models()
		{
			if (!alive.load()) return;
			if (!game::CL_IsCgameInitialized()) { drops = {}; event_cursors = {}; return; }
			std::array<held_frame,15> frames;
			{ const std::lock_guard lock(mutex); frames=held_frames; }
			const auto draw = [&](anchor world, size_t index, const reload_profile* definition, float units,int rounds,bool cartridge=false,
				const ::vr::gameplay::motion::flight* falling=nullptr,std::uint64_t reference=0,weapon_identity origin={}) {
				const auto asset = cartridge ? magazine_assets::cartridge(definition) : magazine_assets::get(definition,rounds);
				if (!asset.model) return;
				if(falling)
				{
					falling_item_presentation::motion motion;motion.path=*falling;motion.local=asset.in_magazine;
					motion.origin=origin;motion.rail_profile=definition;
					falling_item_presentation::submit(asset.model,motion,reference,.25f*units);
					return;
				}
				world = compose(world,asset.in_magazine);
				game::GfxScaledPlacement placement{};
				std::copy(world.position.begin(),world.position.end(),placement.base.origin);
				std::copy(world.rotation.begin(),world.rotation.end(),placement.base.quat);
				placement.scale = 1;
				float color[4]{1,1,1,1};
				// Pad frustum admission only; never scale the mesh or shared asset.
				// The independent hands do not request the bit-0 foreground effect.
				// Held and falling magazines share their scene depth, so fingers can occlude them.
				scene_models::submit(asset.model,&placement,0,&lighting_handles[index],color,color,color,.25f*units);
			};
			const auto now=clock::now();
			for (const auto& hand_frame:frames)
			{
			if (!hand_frame.weapon) continue;
			const auto live = current(hand_frame.view.owner.id());
			const auto& frozen = hand_frame.view;
			const auto wall_now = clock::now();
			const auto eligible=[&](attachment_kind kind) {
				return hand_frame.instance==frozen.ammo.instance_generation && hand_frame.definition==frozen.definition &&
					attachment_state_matches(kind,frozen,live,hand_frame.at,wall_now);
			};
			// Same scene pose/time for both eyes. Only interruption delivery may
			// fall back to a newer state when no fresh skeleton is being produced.
			// Events already contain their commit pose. Waiting for a later hand
			// skeleton to echo them introduces a visible empty frame on release.
			const auto& state = live;
			// Cosmetic drops run at native scene cadence even if the skeleton or
			// server state was reused. Both eyes consume one packed scene payload.

			event_cursors.retain([](weapon_identity id){return carry::active() ? carry::contains(id) : !id.generation;});
			if (auto* cursor_ptr=state.active ? event_cursors.acquire(state.owner.id()) : nullptr)
			{
				auto& cursor = *cursor_ptr;
				if (cursor.instance != state.ammo.instance_generation) cursor = {state.ammo.instance_generation,0};
				auto& last_event = cursor.sequence;
				for (auto n = state.effect_sequence > state.events.size() ? state.effect_sequence-state.events.size()+1 : 1;
					n <= state.effect_sequence; ++n)
				{
					const auto& event = state.events[n % state.events.size()];
					if (n <= last_event || event.sequence != n) continue;
					if (now < event.at) break; // retry on next pose epoch; do not lose a newer simulation event
					if (!event.recoverable && (event.kind == mechanics::effect::magazine_out || event.kind == mechanics::effect::magazine_cancel || event.kind==mechanics::effect::live_eject) &&
						event.units_per_meter > 0 && now >= event.at && now-event.at < 1200ms)
					{
						auto* place = &drops[0];
						for (auto& drop : drops)
							if (!drop.active) { place = &drop; break; }
							else if (drop.event.at < place->event.at) place = &drop;
						*place = {true,event,{},state.definition,state.ammo.weapon,state.ammo.instance_generation,state.reference_generation,scene_generation.load()};
						place->motion.start=event.world;place->motion.born=event.at;place->motion.units=event.units_per_meter;
						place->owner=state.owner.id();
						if(event.kind==mechanics::effect::magazine_out)
						{place->motion.rail=magazine_exit_translation(*state.definition,event.units_per_meter);place->motion.rail_seconds=state.definition->presentation.magazine_exit_seconds;}
						else if(event.kind==mechanics::effect::live_eject)place->motion.velocity=cartridge_exit_velocity(event.world,event.units_per_meter);
					}
					last_event = n;
				}
			}

			if (eligible(attachment_kind::held_magazine))
				draw(hand_frame.world,hand_frame.slot,hand_frame.definition,hand_frame.units,frozen.ammo.held_rounds);
			if(hand_frame.counted_inserted && eligible(attachment_kind::inserted_magazine))
			{
				auto world=hand_frame.inserted_model;world.position=add(world.position,hand_frame.inserted_origin);
				draw(world,53+hand_frame.slot,hand_frame.definition,hand_frame.units,frozen.ammo.magazine_rounds);
			}
			if(hand_frame.live_chamber && eligible(attachment_kind::chamber_round))
			{
				auto world=hand_frame.chamber_model;world.position=add(world.position,hand_frame.inserted_origin);
				draw(world,68+hand_frame.slot,hand_frame.definition,hand_frame.units,0,true);
			}
			if(hand_frame.partitioned_mesh && eligible(attachment_kind::action_partition))
			{
				const auto asset=partition_assets::get(hand_frame.definition);
				if(asset)for(size_t piece=0;piece<2;++piece)
				{
					auto world=hand_frame.partition_model[piece];world.position=add(world.position,hand_frame.partition_origin);
					world=compose(world,asset.in_part[piece]);game::GfxScaledPlacement placement{};
					std::copy(world.position.begin(),world.position.end(),placement.base.origin);std::copy(world.rotation.begin(),world.rotation.end(),placement.base.quat);placement.scale=1;
					float color[4]{1,1,1,1};scene_models::submit(asset.models[piece],&placement,0,&lighting_handles[23+hand_frame.slot*2+piece],color,color,color,.25f*hand_frame.units);
				}
			}
			}
			for (size_t index = 0; index < drops.size(); ++index)
			{
				auto& drop = drops[index];
				if (!drop.active) continue;
				// The released object owns its short visual lifetime. Dropping or
				// stowing its former gun must not delete an already falling magazine.
				if (drop.reference!=controller_input::latest().reference_generation || drop.scene!=scene_generation.load())
				{ drop.active = false; continue; }
				const auto age = std::chrono::duration<float>(now-drop.event.at).count();
				if (age < 0 || age > 1.2f) { drop.active = false; continue; }
				if (drop.event.kind == mechanics::effect::magazine_out)
				{
					// Stay on this gun's rail until the whole magazine clears the mouth.
					// Only then freeze its release pose/velocity and apply world gravity.
					auto rail=drop.event.world;
					for (const auto& frame:frames) if (frame.weapon==drop.weapon && frame.instance==drop.instance && now>=frame.at && now-frame.at<=150ms) {rail=frame.attached;break;}
					drop.motion.advance(now,&rail);
				}
				draw({},index+15,drop.definition,drop.event.units_per_meter,drop.event.magazine_rounds,
					drop.event.kind==mechanics::effect::live_eject,&drop.motion,drop.reference,drop.owner);
			}
		}
	}
	bool magazine_rail_for_record(const scene_models::preparation& preparing,weapon_identity id,
		const reload_profile* definition,std::uint64_t reference,anchor& out) noexcept
	{
		if(!alive || !id.weapon || !definition)return false;
		std::array<float,12> camera{};weapon_render_pose::snapshot skinned;
		if(!read(preparing.record,engine_stereo_view::h2_view_origin_offset,camera) ||
			!weapon_render_pose::for_record(reinterpret_cast<std::uintptr_t>(preparing.record),camera,skinned,id))return false;
		held_frame selected;
		{
			const std::lock_guard lock(mutex);
			if(!scene_models::latest_skeleton_pose(held_poses,held_cursor,skinned,
				[&](const held_frame& value)noexcept{return value.view.owner.id()==id && value.definition==definition;},selected))return false;
		}
		const auto now=clock::now();vec origin{};
		if(selected.view.reference_generation!=reference || now<selected.at || now-selected.at>150ms ||
			!read(preparing.record,engine_stereo_view::h2_current_model_placement_origin_offset,origin) || !finite(origin))return false;
		out=selected.attached_model;out.position=add(out.position,origin);return true;
	}
	bool presentation_available(const reload_profile* definition, part_visibility visibility) noexcept
	{
		return alive.load() && viewmodel_visibility::ready(visibility) && scene_models::ready() &&
			(!definition || (magazine_assets::get(definition,0).model &&
			 (!partition_mesh(*definition) || bool(partition_assets::get(definition)))));
	}
	std::string presentation_status()
	{
		auto result = std::format("held_attachment=native_viewmodel_record matches={} misses={} rejected(record/epoch/state/origin)={}/{}/{}/{} slide_visual=current_pose return=per_profile\n",
			attachment_matches.load(),attachment_misses.load(),attachment_rejections[0].load(),attachment_rejections[1].load(),
			attachment_rejections[2].load(),attachment_rejections[3].load());
		for (size_t i=0;i<reload_profiles.size();++i)
		{
			result += std::format("magazine_asset({})={} [{}] ",reload_profiles[i]->id,
				magazine_assets::get(reload_profiles[i],0).model!=nullptr,magazine_assets::status(reload_profiles[i]));
			if(partition_mesh(*reload_profiles[i]))
				result+=std::format("partition_asset({})={} [{}] ",reload_profiles[i]->id,bool(partition_assets::get(reload_profiles[i])),partition_assets::status(reload_profiles[i]));
		}
		return result + chambering_guide::render_status();
	}
	presentation_result present(void* object, std::uint32_t epoch, const void* matrix_buffer,
		const part_rig& parts, const hands::rig& r, const hands::pose_library& library,
		const weapons::profile& grip_profile, const controller_input::frame& input, const hold& owner,
		const presentation& state,bool knife_held,
		std::uint64_t assembly, bool gameplay, bool manipulation, const std::array<hands::anchor, 2>& targets,
		const std::array<hands::vec, 2>& shoulders, const std::array<hands::vec, 3>& body_axis,
		hands::vec head, hands::vec view_offset, float units, std::span<hands::bone> solved,
		clock::time_point now,hands::quat ordinary_wrist,std::optional<hands::quat> paired_wrist,
		const body_pose::estimate& body_reference,hands::part_hand_frame* hand_motion) noexcept
	{
		using namespace hands;
		const auto* profile = grip_profile.reload;
		if (!parts.valid || !profile || !valid_hand(owner.holding_hand()) ||
			!std::isfinite(units) || units <= 0 || solved.size() < static_cast<size_t>(r.count)) return {};
		returns.retain([](weapon_identity id){return carry::active() ? carry::contains(id) : !id.generation;});
		auto* motion=returns.acquire(owner.id());if (!motion) return {};
		auto& slide_return=motion->slide;
		auto& handle_fold_return=motion->fold;
		auto& handle_catch_return=motion->catch_motion;
		const auto& definition = *profile;
		const auto rear=static_cast<int>(owner.holding_hand()),off=1-rear;
		const auto plan=hand_interaction::pose(hand(off));
		if(plan.driver && (plan.driver.provider!=hand_interaction::domain::magazine || plan.driver.object!=owner.id()))manipulation=false;
		const bool knife_grasp=definition.knife_magazine_in_wrist &&
			(state.magazine_leased() ? state.knife_magazine_grasp : knife_held);
		const auto basis=library.mirror_basis[r.arms[off].wrist];
		const auto& slap_hand=off ? parts.right_slap_hand : parts.slap_hand;
		const bool knife_slide=!definition.knife_slide_grips.empty() && (state.slide_held ? state.knife_slide_grasp : knife_held);
		const auto source_grips=knife_slide ? definition.knife_slide_grips : definition.slide_grips;
		std::array<part_grip_pose,max_part_grips> hand_grips{};
		if (source_grips.size()>hand_grips.size() ||
			definition.knife_slide_grips.size()!=definition.interaction.knife_slide_pose_count) return {};
		for (size_t i=0;i<source_grips.size();++i)
			hand_grips[i]=off ? hands::pose_mirror::part(source_grips[i],basis) : source_grips[i];
		const std::span<const part_grip_pose> slide_grips{hand_grips.data(),source_grips.size()};
		// No valid contact scene for unavailable rendering or magazine assets.
		// The caller still presents its independent cosmetic assembly policy.
		if (!presentation_available(profile,grip_profile.viewmodel.visibility) ||
			(state.active && state.definition != profile)) return {};
		const auto gun = as_anchor(solved[r.gun]);
		const auto attached = compose(gun,definition.magazine_rest);
		// Raw corrected controller targets drive ALL interactions; IK/snap results
		// only drive presentation. Feeding a snapped hand back would stick the
		// slide or fabricate insertion and suppress breakaway.
		const bool paired=knife_held || (state.magazine_leased() && state.knife_magazine_grasp) || (state.slide_held && state.knife_slide_grasp);
		const auto ordinary_basis=paired && paired_wrist ? *paired_wrist : ordinary_wrist;
		const anchor wrist{targets[off].position,normalize(multiply(targets[off].rotation,ordinary_basis))};
		const auto wrist_in_gun = compose(inverse(gun),wrist);
		const auto magazine_pose=select_magazine_grip(definition,wrist_in_gun.rotation,off,basis,knife_grasp,state.magazine_leased(),state.magazine_pose,
			controller_in_body(targets[off].rotation,body_reference),state.ammo.magazine_inserted && (state.magazine_seated || state.magazine_grabbed));
		const auto attached_pose=select_magazine_grip(definition,wrist_in_gun.rotation,off,basis,knife_grasp,state.magazine_leased(),state.magazine_pose,
			controller_in_body(targets[off].rotation,body_reference),true);
		const auto in_wrist=magazine_pose.in_wrist;
		const anchor magazine_wrist{wrist.position,normalize(multiply(targets[off].rotation,
			magazine_wrist_basis(definition,magazine_pose,ordinary_basis,knife_grasp)))};
		const auto raw_mag = compose(magazine_wrist,in_wrist);
		auto mag = compose(as_anchor(solved[r.arms[off].wrist]),in_wrist);
		const auto hand_local = scale(wrist_in_gun.position,1/units);
		const auto* manual=definition.interaction.manual_bolt;
		const auto held_style=state.slide_grip.pose<slide_grips.size()?state.slide_grip.pose:0;
		const auto bolt_hand=slide_grips.empty() ? hand_local :
			scale(compose(wrist_in_gun,{slide_grips[held_style].contact_in_wrist,{0,0,0,1}}).position,1/units);
		// Visual travel follows the SAME current pose as the hands, not the last
		// server tick's scalar. This never runs the mechanical controller or
		// spends/feeds ammunition. The server still owns acquisition/extraction.
		const bool slide_live = state.slide_held && !state.fault && gameplay && manipulation && input.focused &&
			state.reference_generation == input.reference_generation && input.sequence >= state.input_sequence &&
			input.trigger[off].active && input.trigger[off].down;
		auto bolt_target=manual && slide_live ? rotating_bolt::project(*manual,state.ammo.bolt,state.slide_grip.start,bolt_hand) :
			manual_bolt::target{state.ammo.bolt.lift,state.ammo.bolt.travel};
		if(manual)bolt_target.lift=motion->bolt_lift.update(state.ammo.instance_generation,input.reference_generation,
			state.slide_held || !gameplay || !input.focused || state.fault,bolt_target.lift,now,.08f);
		const float visual_travel = manual ? bolt_target.travel*manual->stroke : std::max(minimum_slide_travel(definition.interaction,definition.ammunition,state.ammo),
			slide_live ? constrained_slide_travel(definition.interaction,state.slide_grip,hand_local) : state.slide_travel);
		const float return_target = state.slide_held ? visual_travel : minimum_slide_travel(definition.interaction,definition.ammunition,state.ammo);
		float presented_travel = slide_return.update(state.ammo.instance_generation,input.reference_generation,
			state.slide_held,return_target,now,definition.presentation.slide_return_seconds);
		if (manual) presented_travel=visual_travel;
		const bool independent=hands::owned_native::owns(object);
		const bool authored_action_fire=independent || definition.authored_action_fire;
		const float shot_age=std::chrono::duration<float>(now-state.shot_at).count();
		// Tree-less viewmodels derive the brief reciprocating action from this
		// instance's committed shot, never the selected weapon's animation tree.
		const float shot_travel=authored_action_fire && !state.slide_held && !state.fault ?
			definition.interaction.slide_stroke*action_shot_fraction(shot_age) : 0.f;
		if (native_action_recoil(definition.interaction)) presented_travel=std::max(presented_travel,shot_travel);
		const float handle_unfold = handle_fold_return.update(state.ammo.instance_generation,input.reference_generation,
			state.slide_held && definition.handle_fold && !state.fault,off,now,definition.presentation.slide_return_seconds,owner.rear_revision);
		float catch_progress=state.ammo.action==mechanics::action_state::latched_open ? 1.f : 0.f;
		if (definition.interaction.manual_catch && state.slide_held)
		{
			catch_progress=slide_live ? catch_amount(*definition.interaction.manual_catch,state.handle_grip,
				sub(hand_local,state.slide_grip.start),scale(definition.interaction.slide_axis,-1),wrist_in_gun.rotation) : state.handle_amount;
			if (visual_travel<definition.interaction.full_stroke && state.ammo.action!=mechanics::action_state::latched_open) catch_progress=0;
		}
		const float handle_raise=handle_catch_return.update(state.ammo.instance_generation,input.reference_generation,
			state.slide_held,catch_progress,now,definition.presentation.slide_return_seconds);
		const auto action_pose=manual ? rotating_bolt::pose(definition.slide_rest,*manual,bolt_target,units) :
			handle_pose(definition.slide_rest,definition.handle_catch,definition.interaction.slide_axis,presented_travel*units,handle_raise);
		const auto tip_local = magazine_tip_in_well(definition,gun,raw_mag,units);
		float waist_distance = 10;
		const auto supply=body_supply_volumes(head,body_axis,units,definition.supply,definition.interaction.waist_radius);
		for (const auto& v:supply) waist_distance=std::min(waist_distance,v.distance(wrist.position)/units);
		if (interaction::debug::body_enabled() && gameplay && input.focused)
		{
			interaction::debug::supply_sample diagnostic{supply,add(wrist.position,view_offset),owner,units,off,input.reference_generation,input.sampled_at};
			for (auto& v:diagnostic.volumes) v.origin=add(v.origin,view_offset);
			interaction::debug::publish_supply(diagnostic);
		}
		auto slide_offset = scale(definition.interaction.slide_axis,
			state.active ? minimum_slide_travel(definition.interaction,definition.ammunition,state.ammo)*units : 0);
		// Acquisition follows the caught tab's pivot/arc and rearward offset.
		// The source contact is still wrist-local after carrying its whole pose.
		std::array<part_grip_pose,max_part_grips> catch_grips{};
		std::span<const part_grip_pose> candidate_grips=slide_grips;
		auto grab_low=definition.slide_grab_low,grab_high=definition.slide_grab_high;
		if ((manual || (definition.handle_catch && state.ammo.action==mechanics::action_state::latched_open)) && slide_grips.size()<=catch_grips.size())
		{
			const auto raised=manual ? rotating_bolt::pose(definition.slide_rest,*manual,
				{state.ammo.bolt.lift,state.ammo.bolt.travel},units) : handle_pose(definition.slide_rest,definition.handle_catch,{},0,1);
			if (manual) slide_offset={};
			std::copy(slide_grips.begin(),slide_grips.end(),catch_grips.begin());
			for (size_t i=0;i<slide_grips.size();++i)
				catch_grips[i].wrist=carry_with_handle(definition.slide_rest,raised,catch_grips[i].wrist);
			candidate_grips={catch_grips.data(),slide_grips.size()};
			const auto bounds=handle_bounds(definition.slide_rest,raised,grab_low,grab_high);
			grab_low=bounds[0]; grab_high=bounds[1];
		}
		const bool fixed_style=state.slide_held && state.slide_grip.pose<candidate_grips.size();
		const auto palm=rotate(compose(inverse(gun),targets[off]).rotation,controller_palm_axis(off));
		auto slide_candidate = choose_part_grip(fixed_style?candidate_grips.subspan(state.slide_grip.pose,1):candidate_grips,
			wrist_in_gun,slide_offset,grab_low,grab_high,units,definition.slide_capture,off,palm[2],fixed_style);
		if(fixed_style && slide_candidate.pose!=no_part_grip)slide_candidate.pose=state.slide_grip.pose;
		scene_frame next{input,owner,{true,owner.weapon,state.active ? state.ammo.instance_generation : 0,
			input.reference_generation,input.sequence,waist_distance,
			slide_candidate.distance_meters,
			magazine_alignment(definition,gun,raw_mag),hand_local,tip_local,
				length(sub(wrist.position,compose(attached,inverse(in_wrist)).position))/units,
				slide_candidate.pose},assembly,gameplay};
		next.contact.bolt_hand=!state.slide_held && slide_candidate.pose<slide_grips.size()?
			scale(compose(wrist_in_gun,{slide_grips[slide_candidate.pose].contact_in_wrist,{0,0,0,1}}).position,1/units):bolt_hand;
		next.binding={ordinary_wrist,basis,true,paired_wrist.value_or(ordinary_wrist),paired_wrist.has_value()};
		next.contact.knife_held=knife_held;
		next.contact.magazine_pose=magazine_pose.index;
		sample_attached_magazine(definition,next.contact,gun,wrist,targets[off].rotation,ordinary_basis,attached_pose,raw_mag,units,knife_grasp);
		if (definition.interaction.manual_magazine && definition.interaction.manual_magazine->prefer_grasp_facing &&
			slide_candidate.pose<candidate_grips.size())
			next.contact.magazine_facing=closer_grasp_facing(wrist_in_gun.rotation,
				compose(definition.magazine_rest,inverse(attached_pose.in_wrist)).rotation,candidate_grips[slide_candidate.pose].wrist.rotation);
		hk_slap_debug::sample slap_diagnostic;
		if (definition.handle_catch || definition.interaction.receiver_release)
		{
			next.contact.diagnose_slap=definition.handle_catch && hk_slap_debug::enabled();
			auto& contact=next.contact.catch_input;
			const auto centre=action_slap_centre(definition,state.ammo.action,units);
			contact.rotation=wrist_in_gun.rotation;
			contact.hand_world=scale(add(wrist.position,view_offset),1/units);
			std::array<vec,hands::hand_contact_count> points;
			contact.valid=hands::contact_points(slap_hand,solved,wrist_in_gun,points);
			if (contact.valid) for (size_t i=0;i<points.size();++i)
			{
				contact.slap_points[i]=scale(sub(points[i],centre),1/units);
				next.contact_in_wrist[i]=compose(inverse(wrist_in_gun),{points[i],{0,0,0,1}}).position;
			}
			if(contact.valid && definition.interaction.receiver_release)
			{
				next.palm_in_wrist=palm_volume(next.contact_in_wrist,units);
				// Extreme world scales may exceed the bounded palm budget. Keep
				// valid finger contacts and UMP's independent HK catch available.
				if(next.palm_in_wrist)contact.palm=palm_relative_to_target(*next.palm_in_wrist,wrist_in_gun,centre,units);
			}
			if (next.contact.diagnose_slap && state.active && !state.fault && gameplay && input.focused)
			{
				auto& diagnostic=slap_diagnostic;
				diagnostic.object=reinterpret_cast<std::uintptr_t>(object);
				diagnostic.matrices=reinterpret_cast<std::uintptr_t>(matrix_buffer); diagnostic.epoch=epoch;
				diagnostic.owner=owner; diagnostic.instance=state.ammo.instance_generation;
				diagnostic.reference=input.reference_generation; diagnostic.input_sequence=input.sequence;
				diagnostic.at=input.sampled_at; diagnostic.definition=profile; diagnostic.units=units;
				diagnostic.contact_model=compose(gun,{centre,{0,0,0,1}});
				diagnostic.raw=contact.slap_points; diagnostic.valid=contact.valid;
				if (state.reference_generation==input.reference_generation) diagnostic.trace=state.slap_diagnostics;
			}
		}
		next.attached_world = attached; next.attached_world.position = add(attached.position,view_offset);
		next.held_world = mag; next.held_world.position = add(mag.position,view_offset); next.units_per_meter = units;
		next.definition = profile; next.manipulation=manipulation;
		if (parts.ejection>=0)
		{
			next.ejection_world=compose(gun,parts.port.local);
			next.ejection_world.position=add(next.ejection_world.position,view_offset);
		}
		belt_feed::visual belt_visual;
		float shown_bridge=state.ammo.belt.bridge;
		float shown_cover_amount=state.ammo.belt.cover;
		if(const auto* belt=definition.interaction.belt)
		{
			if(motion->cover_owner_revision!=owner.rear_revision){motion->cover={};motion->cover_owner_revision=owner.rear_revision;}
			// Rendering fills the gaps between authoritative mechanical ticks.
			// This never delays latch/ammo state or affects raw contact sampling.
			shown_cover_amount=motion->cover.update(state.ammo.instance_generation,input.reference_generation,
				state.belt_grip.part==belt_feed::lease::cover || !gameplay || !input.focused || state.fault,
				state.ammo.belt.cover,now,.045f);
			if(belt->bridge && belt->bridge->release)
				shown_bridge=1.f-motion->bridge.update(state.ammo.instance_generation,input.reference_generation,
					state.belt_grip.part==belt_feed::lease::bridge || state.ammo.belt.cover>0,1.f-state.ammo.belt.bridge,now,.22f);
			belt_visual=belt_feed::present(*belt,parts,r,state.ammo,state.belt_grip,gun,wrist,mag,definition.magazine_rest,
				off,basis,units,state.active,false,solved,true,shown_bridge);
			next.contact.belt=belt_visual.query;
			if(belt->push)
			{
				std::array<vec,hands::hand_contact_count> points;
				if(hands::contact_points(slap_hand,solved,wrist_in_gun,points))
				{
					const auto point=compose(gun,{points.back(),{0,0,0,1}}).position;
					next.contact_in_wrist.back()=compose(inverse(wrist_in_gun),{points.back(),{0,0,0,1}}).position;
					next.contact.belt.push=belt_feed::sample_cover_push(*belt,gun,point,
						rotate(targets[off].rotation,controller_palm_axis(off)),add(point,view_offset),units,sub(point,wrist.position));
				}
				if(cover_debug::enabled())
				{
					cover_debug::sample s;
					s.object=reinterpret_cast<std::uintptr_t>(object);s.matrices=reinterpret_cast<std::uintptr_t>(matrix_buffer);s.epoch=epoch;
					s.owner=owner;s.instance=state.ammo.instance_generation;s.reference=input.reference_generation;s.input_sequence=input.sequence;
					s.at=input.sampled_at;s.definition=profile;s.units=units;s.hinge=compose(gun,belt->cover_rest);
					s.cover=state.ammo.belt.cover;s.shown_cover=shown_cover_amount;s.raw=next.contact.belt.push;s.examined=state.examined.belt.push;
					s.simulation_sequence=state.input_sequence;s.reason=state.belt_push_reason;s.decision=state.decision;
					s.trigger=input.trigger[off].down;s.squeeze=input.squeeze[off].down;s.squeeze_active=input.squeeze[off].active;
					s.tracking=gameplay && input.focused && input.grip[off].valid && input.aim[off].valid;
					s.available=manipulation && !knife_held && !state.slide_held && !state.magazine_leased() && owner.support!=hand(off);
					if(next.contact.belt.push.valid)
					{
						const auto point=compose(as_anchor(solved[r.arms[off].wrist]),{next.contact_in_wrist.back(),{0,0,0,1}}).position;
						s.visual=scale(compose(inverse(s.hinge),{point,{0,0,0,1}}).position,1/units);s.visual_valid=true;
					}
					cover_debug::publish(s);
				}
			}
		}

		if (manual && bolt_debug::enabled() && state.active && gameplay && input.focused)
		{
			bolt_debug::sample diagnostic;
			diagnostic.object=reinterpret_cast<std::uintptr_t>(object);diagnostic.matrices=reinterpret_cast<std::uintptr_t>(matrix_buffer);
			diagnostic.epoch=epoch;diagnostic.owner=owner;diagnostic.instance=state.ammo.instance_generation;
			diagnostic.reference=input.reference_generation;diagnostic.input_sequence=input.sequence;diagnostic.at=input.sampled_at;
			diagnostic.definition=profile;diagnostic.units=units;diagnostic.gun=gun;diagnostic.raw=bolt_hand;
			diagnostic.contact=scale(compose(slide_grips[held_style].wrist,{slide_grips[held_style].contact_in_wrist,{0,0,0,1}}).position,1/units);
			diagnostic.state=state.ammo;diagnostic.held=state.slide_held;diagnostic.eligible=manipulation && hand(off)==manual->actor;
			bolt_debug::publish(diagnostic);
		}
		if (well_debug::enabled() && state.active && !state.fault && gameplay && input.focused)
		{
			well_debug::sample diagnostic;
			diagnostic.object=reinterpret_cast<std::uintptr_t>(object);
			diagnostic.matrices=reinterpret_cast<std::uintptr_t>(matrix_buffer); diagnostic.epoch=epoch;
			diagnostic.owner=owner; diagnostic.instance=state.ammo.instance_generation;
			diagnostic.reference=input.reference_generation; diagnostic.input_sequence=input.sequence;
			diagnostic.at=input.sampled_at; diagnostic.definition=profile;
			diagnostic.well_model=compose(gun,definition.well); diagnostic.units=units;
			diagnostic.raw_tip=tip_local; diagnostic.visual_tip=magazine_tip_in_well(definition,gun,mag,units);
			diagnostic.alignment=next.contact.insertion_alignment;
			diagnostic.held=state.ammo.magazine_hand!=hand::none; diagnostic.occupied=state.ammo.magazine_inserted;
			diagnostic.examined=state.examined.valid && state.examined.instance_generation==diagnostic.instance &&
				state.examined.reference_generation==input.reference_generation;
			diagnostic.examined_tip=state.examined.magazine_top_in_well;
			diagnostic.well_contact=state.well_contact; diagnostic.requires_withdrawal=state.requires_withdrawal;
			diagnostic.decision=state.decision;
			well_debug::publish(diagnostic);
		}
		part_mask hidden{};
		if (state.active)
		{
			hidden=parts.animation_only_magazines;
			const auto* slide_pose = state.slide_grip.pose < slide_grips.size() ? &slide_grips[state.slide_grip.pose] : nullptr;
			move_part(r,parts.magazine,attached,solved);
			const auto& event = state.events[state.effect_sequence % state.events.size()];
			const auto seating_age = std::chrono::duration<float>(now-event.at).count();
			const bool seating = event.kind == mechanics::effect::magazine_in && state.ammo.magazine_inserted &&
				!state.slide_held && state.ammo.magazine_hand == hand::none && seating_age >= 0 && seating_age < .16f;
			auto presented_magazine = attached;
			if (!state.fault && state.magazine_grabbed && definition.interaction.manual_magazine)
			{
				const auto& p=*definition.interaction.manual_magazine;
				const auto travel=std::clamp(dot(sub(hand_local,state.magazine_grab_start),p.pull_axis),0.f,p.pull_travel);
				presented_magazine.position=add(attached.position,rotate(gun.rotation,scale(p.pull_axis,travel*units)));
				move_part(r,parts.magazine,presented_magazine,solved);
			}
			else if (seating)
			{
				const auto t = std::clamp(seating_age/.16f,0.0f,1.0f);
				const auto blend = t*t*(3-2*t);
				const auto start_local = compose(inverse(event.attached_world),event.world);
				// Capture can begin below the mouth; snap sideways/rotation to the
				// magazine rail and animate ONLY its permitted insertion coordinate.
				const float start_z=std::clamp(start_local.position[2],-.4f*units,0.f);
				presented_magazine=translate_local(attached,{0,0,start_z*(1-blend)});
				move_part(r,parts.magazine,presented_magazine,solved);
			}
			if (definition.receiver_parented_bullets)
				move_part(r,parts.bullets,compose(presented_magazine,parts.bullets_in_magazine),solved);
			if (definition.chamber_round)
			{
				const auto chamber=chamber_cartridge_pose(definition,state.ammo,visual_travel,0,units);
				if(chamber)move_part(r,parts.bullets,compose(gun,*chamber),solved);
				// The auto-cycle's spent case needs its own audited asset. Never
				// show the newly accounted live round riding backward as that case.
				if(!chamber || (!state.slide_held && action_shot_fraction(shot_age)>0))
					for(size_t i=0;i<hidden.size();++i)hidden[i]|=parts.bullet_mask[i];
			}
			if (manual && definition.feeding_path)
			{
				const auto& path=*definition.feeding_path;
				auto round=compose(presented_magazine,compose(inverse(definition.magazine_rest),path[0]));
				if (state.ammo.bolt.feeding)
				{
					const auto t=std::clamp(1.f-bolt_target.travel/.35f,0.f,1.f);
					auto local=path[1];local.position=add(path[1].position,scale(sub(path[2].position,path[1].position),t));
					round=compose(gun,local);
				}
				move_part(r,parts.bullets,round,solved);
			}
			if (manipulation && !state.fault && (state.magazine_seated || state.magazine_grabbed) && state.ammo.magazine_inserted)
				(void)constrain_part_hand(r,library,grip_profile,targets,shoulders,body_axis,rear,
					compose(presented_magazine,inverse(in_wrist)),solved);
			else if (manipulation && !state.fault && state.slide_held && slide_pose)
			{
				const auto handle=handle_pose(definition.slide_rest,definition.handle_catch,definition.interaction.slide_axis,
					visual_travel*units,handle_raise);
				(void)constrain_part_hand(r,library,grip_profile,targets,shoulders,body_axis,rear,
					compose(gun,carry_with_handle(definition.slide_rest,manual ? action_pose : handle,slide_pose->wrist)),solved);
			}
			if (manipulation && (state.ammo.magazine_hand != hand::none || state.magazine_seated || state.magazine_grabbed))
				pose_fingers(r,library,grip_profile,knife_grasp ? std::span<const joint_pose>{equipment::knife_magazine_pose::fingers} : magazine_pose.fingers,off,solved);
			else if (manipulation && state.slide_held && slide_pose) pose_fingers(r,library,grip_profile,slide_pose->fingers,off,solved);
			const bool native_recoil = !authored_action_fire && native_action_recoil(definition.interaction) &&
				!state.slide_held && !slide_return.active() && state.ammo.action == mechanics::action_state::closed &&
				now >= state.shot_at && now-state.shot_at < 100ms;
			if (!native_recoil)
			{
				auto local = action_pose;
				if(definition.handle_fold && definition.handle_fold->end_bone.empty() && !definition.handle_fold->mesh)
					local=folded_handle_pose(local,*definition.handle_fold,handle_unfold,handle_fold_return.hand());
				move_part(r,parts.slide,compose(gun,local),solved);
			}
			if(definition.handle_fold && parts.fold_end>=0)
				move_part(r,parts.fold_end,folded_handle_pose(as_anchor(solved[parts.slide]),*definition.handle_fold,handle_unfold,handle_fold_return.hand()),solved);
			if (definition.bolt && parts.bolt>=0)
			{
				const bool firing=!definition.handle_child_of_bolt && !independent && !state.slide_held && !slide_return.active() &&
					(state.ammo.action==mechanics::action_state::closed || state.ammo.action==mechanics::action_state::cocked_open) &&
					now>=state.shot_at && now-state.shot_at<100ms;
				if (!firing)
				{
					const bool retained=retained_internal_bolt(definition.ammunition,state.ammo);
					const auto travel=displayed_internal_bolt(definition,presented_travel,shot_travel,retained,
						state.slide_held || state.fault ? -1.f : shot_age);
					auto local=definition.bolt->rest;
					local.position=add(local.position,scale(definition.interaction.slide_axis,
						travel*units));
					move_part(r,parts.bolt,compose(gun,local),solved);
				}
				// Parent motion must not drag this independently held handle twice.
				if(definition.handle_child_of_bolt)move_part(r,parts.slide,compose(gun,action_pose),solved);
			}
			hidden=combine_part_masks(hidden,magazine_visibility(r,parts,definition,state.ammo));
			if(const auto* belt=definition.interaction.belt)
			{
				if(owner.support==hand(off) && state.ammo.magazine_inserted && definition.magazine_contacts && !state.fault && gameplay &&
					!state.slide_held && !state.magazine_grabbed && state.belt_grip.part==belt_feed::lease::none && state.ammo.magazine_hand==hand::none)
				{
					const auto contact=compose(gun,grip_profile.wrists[off]).position;
					motion->support_arm.apply(r,off,solved,gun,definition.magazine_contacts->grab_low,definition.magazine_contacts->grab_high,units,input.reference_generation,now,&contact,&body_axis);
				}
				else motion->support_arm.reset();
				const bool live=!state.fault && manipulation && input.focused && gameplay && state.reference_generation==input.reference_generation &&
					input.sequence>=state.input_sequence && input.trigger[off].active && input.trigger[off].down;
				belt_visual=belt_feed::present(*belt,parts,r,state.ammo,state.belt_grip,gun,wrist,mag,definition.magazine_rest,off,basis,units,true,live,solved,false,shown_bridge,shown_cover_amount);
				if(belt_visual.held)
				{
					(void)constrain_part_hand(r,library,grip_profile,targets,shoulders,body_axis,rear,belt_visual.wrist,solved);
					pose_fingers(r,library,grip_profile,belt_visual.fingers,off,solved);
				}
				const int rounds=state.ammo.magazine_inserted ? state.ammo.magazine_rounds : state.ammo.held_rounds;
				for(size_t i=0;i<parts.belt_link_count;++i)
				{
					const auto b=parts.belt_links[i];const auto bit=0x80000000u>>(b%32);
					if(rounds>int(i))hidden[b/32]&=~bit;else hidden[b/32]|=bit;
				}
			}
		}
		if(hand_motion && (!plan.driver || plan.driver.provider==hand_interaction::domain::magazine))
		{
			using enum hands::part_hand_attachment;
			const auto attachment=!manipulation || state.fault || !state.active ? free :
				state.slide_held ? action : state.belt_grip.part==belt_feed::lease::cover ? belt_cover :
				state.belt_grip.part==belt_feed::lease::bridge ? belt_bridge :
				state.magazine_seated || state.magazine_grabbed ? seated_magazine :
				state.ammo.magazine_hand!=hand::none ? magazine : free;
			hand_motion->apply(off,attachment);
			mag=compose(as_anchor(solved[r.arms[off].wrist]),in_wrist);
			next.held_world=mag;next.held_world.position=add(mag.position,view_offset);
		}
		publish_scene(next);
		if (slap_diagnostic.definition)
		{
			// Compare against the final rendered hand, including manipulation poses.
			std::array<vec,hands::hand_contact_count> points;
			const auto centre=compose(inverse(gun),slap_diagnostic.contact_model).position;
			slap_diagnostic.visual_valid=hands::contact_points(slap_hand,solved,compose(inverse(gun),as_anchor(solved[r.arms[off].wrist])),points);
			if (slap_diagnostic.visual_valid) for (size_t i=0;i<points.size();++i) slap_diagnostic.visual[i]=scale(sub(points[i],centre),1/units);
			hk_slap_debug::publish(slap_diagnostic);
		}
		const std::lock_guard lock(mutex);
		held_frame held = {owner.weapon,state.active ? state.ammo.instance_generation : 0,next.held_world,next.attached_world,input.sampled_at,state};
		held.object = reinterpret_cast<std::uintptr_t>(object);
		held.matrices = reinterpret_cast<std::uintptr_t>(matrix_buffer);
		held.epoch = epoch; held.model = mag;
		held.attached_model=attached;
		held.units = units;
		held.definition = profile;held.view.owner=owner;
		held.inserted_origin=view_offset;
		if(state.active && !state.fault && manual && magazine_assets::cartridge(profile).model)
		{
			if(const auto chamber=chamber_cartridge_pose(definition,state.ammo,visual_travel,bolt_target.travel,units))
			{held.live_chamber=true;held.chamber_model=compose(gun,*chamber);}
		}
		if(state.active && !state.fault && definition.magazine_fill() && state.ammo.magazine_inserted)
		{
			held.counted_inserted=true;held.inserted_model=as_anchor(solved[parts.magazine]);held.inserted_origin=view_offset;
			// Replace only this magazine subtree with the same immutable count
			// view used in a hand or after a drop; never hide another gun's rounds.
			for(int b=0;b<r.count;++b)if(descendant(b,parts.magazine,r))hidden[b/32]|=0x80000000u>>(b%32);
		}
		if(state.active && !state.fault && partition_mesh(definition) && partition_assets::get(profile))
		{
			const int root=definition.bolt_partition?parts.partition_root:parts.slide;
			held.partitioned_mesh=true;held.partition_origin=view_offset;held.partition_model[0]=as_anchor(solved[root]);
			held.partition_model[1]=definition.bolt_partition ?
				compose(gun,partition_bolt_pose(definition,presented_travel,retained_internal_bolt(definition.ammunition,state.ammo),
					state.slide_held?-1.f:shot_age,units)) :
				folded_handle_pose(held.partition_model[0],*definition.handle_fold,handle_unfold,handle_fold_return.hand());
			// Hide the rigid source group only. Accessories, rounds, the magazine
			// and unrelated skinned straps remain on their original native bones.
			hidden[root/32]|=0x80000000u>>(root%32);
		}
		if(chambering_guide::enabled())
		{
			const auto guide=chambering_guide::request(grip_profile);
			chambering_guide::sample sample;
			sample.object=held.object;sample.matrices=held.matrices;sample.epoch=held.epoch;
			sample.owner=owner;sample.instance=held.instance;sample.reference=input.reference_generation;sample.at=held.at;sample.reload=profile;
			if(state.active && !state.fault && gameplay && input.focused &&
				state.reference_generation==input.reference_generation && chambering_guide::needed(definition,state.ammo,state.slide_held))
			{
				// Overlay exact source triangles using this skeletal epoch. Keep the
				// native surface for depth/occlusion and ordinary rendering on failure.
				const std::array<int,4> roots{parts.slide,parts.fold_end,parts.guide_catch,parts.guide_detail};
				for(size_t i=0;i<roots.size();++i)
				{
					const int root=roots[i];
					if(!guide.bones[i].geometry || root<0 || root>=r.count || (hidden[root/32]&(0x80000000u>>(root%32))) ||
						(i==2 && !chambering_guide::catch_needed(definition,state.ammo)))continue;
					sample.add(guide.bones[i],as_anchor(solved[root]));
				}
				if(held.partitioned_mesh)for(size_t i=0;i<guide.partition.size();++i)sample.add(guide.partition[i],held.partition_model[i]);
			}
			chambering_guide::publish(sample);
		}
		auto* slot=&held_frames[0];
		for (auto& frame:held_frames) if (frame.view.owner.id()==owner.id()) {slot=&frame;break;} else if (!frame.weapon || frame.at<slot->at) slot=&frame;
		held.slot=size_t(slot-held_frames.data());
		*slot=held;
		held_poses[held_cursor++ % held_poses.size()] = held;
		const unsigned posed=state.active && !state.fault && manipulation && state.part_leased() ? 1u<<off : 0;
		presentation_result result{true,hidden,posed};
		if (posed) result.knife_poses[off]=knife_grasp && state.magazine_leased() ? equipment::knife_hand_pose::magazine :
			knife_slide && state.slide_held ? equipment::knife_hand_pose::slide : equipment::knife_hand_pose::grip;
		return result;
	}
	class presentation_component final : public component_interface
	{
		void post_unpack() override
		{
			scene_models::on_submit(submit_models);
			fastfiles::on_pre_unload([] {
				++scene_generation;
				{ const std::lock_guard lock(mutex);held_frames={};held_poses={};held_cursor=0;drops={}; }
				returns={};event_cursors={};
				magazine_assets::retire_after_drain();
				partition_assets::retire_after_drain();
			});
			scene_models::on_prepare_placement(prepare_held);
			scheduler::loop([] {
				if (!alive.load()) return;
				magazine_assets::refresh();
				partition_assets::refresh();
				if (presentation_available()) set_boundary_ready(4);
			},scheduler::pipeline::main,250ms);
		}
		void pre_destroy() override
		{
			alive=false;
			magazine_assets::clear();partition_assets::clear();
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::physical_reload::presentation_component)
