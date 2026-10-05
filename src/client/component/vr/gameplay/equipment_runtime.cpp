#include <std_include.hpp>
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "shield_geometry.hpp"
#include "hand_interaction/runtime.hpp"
#include "equipment_runtime.hpp"
#include "carry_interaction.hpp"
#include "knife_profile.hpp"
#include "official_cheats.hpp"
#include "native_weapon_read.hpp"
#include "cliffhanger_runtime.hpp"
#include "cliffhanger_pickaxe_policy.hpp"
#include "physical_reload_runtime.hpp"
#include "hand_attachment_pose.hpp"
#include "native_melee.hpp"
#include "native_melee_feedback.hpp"
#include "special_melee.hpp"
#include "npc_collision.hpp"
#include "native_carry_model.hpp"
#include "../engine_stereo_view.hpp"
#include "native_scripted_control.hpp"
#include "weapon_feedback.hpp"
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler.hpp"
#include "component/fastfiles.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/native_memory.hpp>
#include <utils/io.hpp>
#include <mutex>

namespace vr::gameplay::equipment
{
	namespace
	{
		using namespace hands::pose_math;
		using clock=controller_input::clock;
		std::atomic_bool alive{true};game::dvar_t* enabled{};
		std::mutex mutex;
		struct state
		{
			knife_state knife{};chest_slots chest{};anchor world{};
			std::array<quat,2> basis{},mirror{};
			bool hand_ready{},visible{},bayonet{};
			std::uint64_t reference{};clock::time_point at{};
		};
		state published;
		std::array<melee::motion,2> motion;
		std::array<hand_interaction::recipe,2> knife_motion_pose{};
		melee::hit_gate hits;
		grab_intent draw_intent;

		std::atomic<game::XModel*> model{};
		std::atomic_uint32_t knife_weapon{};
		std::array<std::unique_ptr<scene_models::rigid_part>,2> knife_models;
		std::array<std::atomic<game::XModel*>,2> models{};
		std::array<std::atomic_uint32_t,2> providers{};
		std::array<unsigned short,2> lighting{};
		std::atomic_uint64_t draws{},returns{},attempts{},contacts{},damaging_hits{};
		std::atomic_uint64_t submitted{},prepared{},missing_pose{};
		std::atomic_uint64_t chest_prepared{},grip_checks{};
		std::atomic_uint grab_available{},grab_pending{};
		std::array<std::atomic<float>,2> palm_distance{};
		std::atomic_int last_damage{},last_target{-1};
		struct motion_status
		{
			std::atomic<float> speed{},path{};
			std::atomic_uint kind{},armed_points{},rejected{};
			std::atomic_uint64_t sequence{},tracking_rejections{};
		};
		std::array<motion_status,2> motion_diagnostics;
		void reset_motion(unsigned h) noexcept {motion[h].reset();motion_diagnostics[h].sequence=0;}
		std::atomic<const char*> reason{"waiting for native knife resources"};
		bool running() noexcept {return alive && enabled && enabled->current.enabled && !cheats::transitioning() && weapons::carry::active();}
		bool fresh(clock::time_point at,clock::time_point now) noexcept {return now>=at && now-at<=150ms;}
		anchor in_hand(const state& s,unsigned h) noexcept
		{return knife_profile::attachment(s.knife.grip,s.mirror[h],h==1,knife_hand_pose::grip,s.bayonet);}
        void refresh()
        {
            if(!alive || !game::CL_IsCgameInitialized() || !scene_models::ready())return;
            for(unsigned variant=0;variant<2;++variant)
            {
                const auto* map=game::Dvar_FindVar("mapname");
                if(variant && map && map->current.string && cheats::pickaxe_level(map->current.string))continue;
                const auto* name=variant?knife_profile::bayonet_model_name:knife_profile::model_name;
                if(!models[variant].load())
                {
                    auto* candidate=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,name,0).model;
                    if(!candidate || !candidate->name || std::string_view(candidate->name)!=name ||
                        candidate->numBones!=(variant?3:2) || !candidate->boneNames || !candidate->baseMat)continue;
                    const auto* root=game::SL_ConvertToString(candidate->boneNames[0]);
                    if(!root || std::string_view(root)!=(variant?"j_gun":"tag_knife"))continue;
                    if(variant)
                    {const auto* sheath=game::SL_ConvertToString(candidate->boneNames[1]);if(!sheath || std::string_view(sheath)!="tag_clip")continue;}
                    auto subset=std::make_unique<scene_models::rigid_part>();
                    // Root-only geometry excludes the bayonet sheath and empty FX socket.
                    if(!subset->create(candidate,0)){reason=subset->status();continue;}
                    const std::array<scene_models::runtime_model,1> identity{{{subset->model(),candidate}}};
                    if(!scene_models::register_runtime_models(identity))continue;
                    knife_models[variant]=std::move(subset);models[variant]=knife_models[variant]->model();
                }
                if(providers[variant])continue;
                for(std::uint32_t i=1;i<512;++i)
                {
                    const auto value=weapons::native_weapon_read::get(i);if(!value)continue;
                    if(variant)
                    {if(std::string_view(value.name.data())=="h2_cheatcommandoknife"){providers[variant]=i;break;}continue;}
                    game::XModel* attached{};
                    if(!utils::native_memory::read_at(value.definition,0x488,attached))continue;
                    if(attached==knife_models[variant]->source()){providers[variant]=i;break;}
                }
            }
        }
		void retire()
		{
			model=nullptr;knife_weapon=0;
			{const std::lock_guard lock(mutex);published.knife.return_to_chest();published.visible=false;published.hand_ready=false;}
			hands::attachments::clear_after_drain();for(unsigned i=0;i<2;++i){models[i]=nullptr;providers[i]=0;knife_models[i].reset();}
		}
		void submit()
		{
				if (!running() || cheats::waist_pickaxes() || !scripted_control::predicted_allowed()) return;
			state s;{const std::lock_guard lock(mutex);s=published;}
			auto* asset=models[unsigned(s.bayonet)].load();if (!asset || !s.visible || !fresh(s.at,clock::now())) return;
			// Scene admission follows the current render body, not the server tick.
			// Final chest/held placement is refined against the actual view record.
			if (s.knife.holder==hand::none)
			{
				if(!sequences::chest_equipment_visible())return;
				head_pose_bridge::spatial_frame body;
				if (!head_pose_bridge::get_spatial_frame(body) || body.generation!=s.reference || !fresh(body.captured_at,clock::now())) return;
				const auto chest=locate_chest(body);if (!chest.valid) return;s.world=knife_profile::stowed(chest,s.bayonet);
			}
			game::GfxScaledPlacement placement{};placement.scale=1;
			std::copy(s.world.position.begin(),s.world.position.end(),placement.base.origin);
			std::copy(s.world.rotation.begin(),s.world.rotation.end(),placement.base.quat);
			float color[4]{1,1,1,1};scene_models::submit(asset,&placement,scene_models::no_cast_shadow,&lighting[unsigned(s.bayonet)],color,color,color,8.f);
			++submitted;
		}
		scene_models::placement_result prepare(const scene_models::preparation& p,const void* entry,game::GfxPlacement& current,game::GfxPlacement& previous) noexcept
		{
			using result=scene_models::placement_result;
			std::uintptr_t handle{};
			if (!utils::native_memory::read_bytes(&handle,static_cast<const std::byte*>(entry)+0x68,8))return result::unchanged;
			const int variant=handle==reinterpret_cast<std::uintptr_t>(&lighting[0])?0:handle==reinterpret_cast<std::uintptr_t>(&lighting[1])?1:-1;
			if(variant<0)return result::unchanged;
			state s;{const std::lock_guard lock(mutex);s=published;}
				if (!running() || cheats::waist_pickaxes() || variant!=int(s.bayonet) || !s.visible || !fresh(s.at,clock::now())) return result::omit;
			const bool held=vr::valid_hand(s.knife.holder);
			if(!held && !sequences::chest_equipment_visible())return result::omit;
			std::array<float,12> camera{};vec origin{};hands::attachments::solved pose;
			if (!utils::native_memory::read_bytes(camera.data(),static_cast<const std::byte*>(p.record)+engine_stereo_view::h2_view_origin_offset,sizeof(camera)) ||
				!hands::attachments::for_record(p.record,camera,pose) || pose.reference!=s.reference || pose.lease!=s.knife.revision ||
				!utils::native_memory::read_bytes(origin.data(),static_cast<const std::byte*>(p.record)+engine_stereo_view::h2_current_model_placement_origin_offset,sizeof(origin)))
			{if (held) {++missing_pose;return result::omit;} return result::retain;}
			for (float x:origin) if (!std::isfinite(x) || std::abs(x)>1e7f) return result::omit;
			anchor world;
			if (held)
			{
				const auto h=unsigned(s.knife.holder);
				world=compose({add(pose.wrists[h].position,origin),normalize(pose.wrists[h].rotation)},knife_profile::attachment(s.knife.grip,pose.mirror_basis[h],h==1,pose.knife_poses[h],s.bayonet));
			}
			else
			{
				const auto body=hands::attachments::body_frame(pose,origin);
				const auto chest=locate_chest(body);if (!chest.valid) return result::omit;world=knife_profile::stowed(chest,s.bayonet);++chest_prepared;
			}
			std::copy(world.position.begin(),world.position.end(),current.origin);std::copy(world.rotation.begin(),world.rotation.end(),current.quat);previous=current;++prepared;return result::replace;
		}
	}
	bool held_by(hand h) noexcept {return running() && hand_interaction::has(h,hand_interaction::domain::knife);}
	bool current_knife(hand h,std::uint64_t revision,std::uint64_t reference) noexcept
	{
		if (!held_by(h) || cheats::waist_pickaxes()) return false;
		const std::lock_guard lock(mutex);
		return published.visible && published.knife.holder==h && published.knife.revision==revision &&
			published.reference==reference && fresh(published.at,clock::now());
	}
	void collect_interactions(const hand_interaction::frame& f)noexcept
	{
		if(cheats::waist_pickaxes()){draw_intent.reset();return;}
		if(!sequences::chest_equipment_visible()){draw_intent.reset();return;}
		if(!running() || !knife_weapon || !model)return;
		namespace hi=hand_interaction;state s;{const std::lock_guard lock(mutex);s=published;}
		if(!s.hand_ready || s.bayonet!=cheats::chest_bayonet() || vr::valid_hand(s.knife.holder))return;
		const auto chest=locate_chest(f.body);if(!chest.valid)return;
		unsigned pressed{},released{},available{};
		for(int h=0;h<2;++h){const auto edge=hi::input(hand(h),hi::button::grip);if(edge.press)pressed|=1u<<h;if(edge.release)released|=1u<<h;if(hi::free(hand(h)))available|=1u<<h;}
		const auto pending=draw_intent.consume(f.input,available&f.valid_hands,pressed,released);
		for(int h=0;h<2;++h)if(pending&(1u<<h))
		{
			const auto palm=knife_profile::palm_contact(f.wrists[h],s.basis[h],s.mirror[h],h==1);
			const float distance=chest_grab_distance(chest,slot::knife,palm);
			if(distance<=1)hi::offer({hand(h),{hi::object(hi::domain::knife,{knife_weapon.load(),1}),hi::role::knife,hi::button::grip,hi::recipe::single,hi::capability::melee},hi::input(hand(h),hi::button::grip).event,20,distance,1,true,true});
		}
	}
	void report_interactions()noexcept
	{
		namespace hi=hand_interaction;if(!running())return;const std::lock_guard lock(mutex);
		if(!cheats::waist_pickaxes() && vr::valid_hand(published.knife.holder))hi::observed(published.knife.holder,{hi::object(hi::domain::knife,{knife_weapon.load(),1}),hi::role::knife,hi::button::grip,hi::recipe::single,hi::capability::melee});
	}
	void suspend(bool return_knife) noexcept
	{
		for (unsigned h=0;h<2;++h) reset_motion(h);hits.clear_contacts();draw_intent.reset();grab_pending=0;
		const std::lock_guard lock(mutex);published.visible=false;
		if (return_knife) {published.knife.return_to_chest();hits.reset();}
	}
	unsigned settle_interactions(const hand_interaction::frame& frame,
	                             const controller_input::frame& input,
	                             const weapons::carry::grip_edges& edges)
	{
		unsigned available{}, released = edges.released;
		const unsigned pressed = edges.pressed;
		for (int h = 0; h < 2; ++h)
			if (hand_interaction::has(hand(h), hand_interaction::domain::knife))
				available |= 1u << h;
		const auto& body = frame.body;
		const auto& wrists = frame.wrists;
		const auto valid_hands = frame.valid_hands;
		const auto owned = weapons::carry::interaction_instances();
		const std::span<const weapons::carry::scene> scenes = frame.objects;
		if (!running())
		{
			suspend(true);
			return 0;
		}
		if (!melee::native::allowed() || !input.focused || !fresh(input.sampled_at,clock::now()) || body.generation!=input.reference_generation || owned.size()!=scenes.size()) {suspend();return 0;}
		state s;{const std::lock_guard lock(mutex);s=published;}
		const bool bayonet=cheats::chest_bayonet();
        if(s.bayonet!=bayonet)
        {
            s.knife.return_to_chest();++s.knife.revision;s.bayonet=bayonet;
            for(unsigned h=0;h<2;++h)reset_motion(h);hits.clear_contacts();draw_intent.reset();
        }
        model=models[unsigned(bayonet)].load();knife_weapon=providers[unsigned(bayonet)].load();
        const bool waist_picks=cheats::waist_pickaxes();
        if(waist_picks){s.knife.return_to_chest();draw_intent.reset();}
        reason=model && knife_weapon?"native chest knife ready":"selected chest knife resources unavailable";
        s.chest=locate_chest(body);s.reference=input.reference_generation;s.at=input.sampled_at;s.visible=s.chest.valid && model && !waist_picks;
		if (!s.chest.valid) {suspend();return 0;}
		if(vr::valid_hand(s.knife.holder))
		{
			const auto h=unsigned(s.knife.holder);
			if(input.squeeze[h].active && !input.squeeze[h].down)released|=1u<<h;
		}
		grab_available=available&valid_hands;
		const auto requested=draw_intent.consume(input,available&valid_hands,pressed,released);grab_pending=requested;
		std::array<vec,2> palms{};
		for (unsigned h=0;h<2;++h)
		{
			palms[h]=knife_profile::palm_contact(wrists[h],s.basis[h],s.mirror[h],h==1);
			palm_distance[h]=length(sub(palms[h],s.chest.anchors[0].position))/body.units_per_meter;
		}
		unsigned claimed{};
		if (s.knife.release(released)) {claimed|=released;++returns;}
		if (vr::valid_hand(s.knife.holder))
		{
			const auto holder=s.knife.holder;
			for (const auto& v:owned) if (v.at==weapons::carry::location::held && (v.owner.rear==holder || v.owner.support==holder)) s.knife.return_to_chest();
		}
		if (s.visible && knife_weapon && s.hand_ready && s.knife.holder==hand::none)
		{
			unsigned best=2;float distance=1;
			for (unsigned h=0;h<2;++h) if (requested&(1u<<h))
			{
				++grip_checks;const auto d=chest_grab_distance(s.chest,slot::knife,palms[h]);if (d<=distance) {distance=d;best=h;}
			}
			if (best<2)
			{
				const auto wrist=anchor{wrists[best].position,normalize(multiply(wrists[best].rotation,s.basis[best]))};
				const auto forward=compose(wrist,knife_profile::attachment(knife_grip::forward,s.mirror[best],best==1,knife_hand_pose::grip,s.bayonet));
				const auto reverse=compose(wrist,knife_profile::attachment(knife_grip::reverse,s.mirror[best],best==1,knife_hand_pose::grip,s.bayonet));
				const auto stowed=rotate(s.chest.anchors[0].rotation,{1,0,0});
				const vec blade_axis=s.bayonet?vec{0,0,1}:vec{1,0,0};
				const auto grip=dot(rotate(forward.rotation,blade_axis),stowed)>=dot(rotate(reverse.rotation,blade_axis),stowed) ? knife_grip::forward : knife_grip::reverse;
				if (s.knife.take(static_cast<hand>(best),grip)) {draw_intent.reset();claimed|=1u<<best;++draws;weapons::feedback::carry_confirmation(static_cast<hand>(best),input);}
			}
		}
		if (s.chest.valid)
		{
			s.world=knife_profile::stowed(s.chest,s.bayonet);
			if (vr::valid_hand(s.knife.holder))
			{
				const auto h=unsigned(s.knife.holder);s.visible=s.visible && (valid_hands&(1u<<h));claimed|=1u<<h;
				s.world=compose({wrists[h].position,normalize(multiply(wrists[h].rotation,s.basis[h]))},in_hand(s,h));
			}
		}
		{const std::lock_guard lock(mutex);published.knife=s.knife;published.bayonet=s.bayonet;published.chest=s.chest;published.world=s.world;published.visible=s.visible;published.reference=s.reference;published.at=s.at;}
		const auto body_point=[&](vec p) {return body_local(body,p);};
		const auto world_point=[&](vec p) {return body_world(body,p);};
		const auto body_rotation=conjugate(from_axis(body.head_yaw_axis));
		for (unsigned h=0;h<2;++h)
		{
			if (input.orientation_settling || !(valid_hands&(1u<<h))) {reset_motion(h);hits.contact(h,0,false,input.sampled_at);continue;}
			melee::sample sample;sample.sequence=input.sequence;sample.reference=input.reference_generation;sample.at=input.sampled_at;
			sample.continuity=input.continuity_generation;
			sample.hand={body_point(wrists[h].position),normalize(multiply(body_rotation,wrists[h].rotation))};
			std::uint32_t weapon=knife_weapon.load();anchor object{};vec start{},end{};weapons::hold striking_owner{};
			const weapons::shield::profile* shield_contact{};
			special::cliffhanger::pickaxe_strike pick;
			const bool striking_pick=special::cliffhanger::melee_pickaxe(hand(h),wrists[h],input.reference_generation,pick);
			if(striking_pick)
			{
				sample.kind=melee::tool::pickaxe;sample.identity=pick.lease;weapon=pick.weapon;object=pick.root;claimed|=1u<<h;
			}
			else if (s.knife.holder==static_cast<hand>(h) && s.visible)
			{
				const auto plan=hand_interaction::pose(hand(h));
				// Co-grasp can strike, but acquiring/releasing its pose cannot bridge a swing.
				if (knife_motion_pose[h]!=plan.grasp) {reset_motion(h);knife_motion_pose[h]=plan.grasp;}
				if (!plan.melee) {reset_motion(h);hits.contact(h,0,false,input.sampled_at);continue;}
				sample.kind=melee::tool::knife;sample.identity=s.knife.revision;object=s.world;start=s.bayonet?knife_profile::bayonet_blade_base:knife_profile::blade_base;end=s.bayonet?knife_profile::bayonet_blade_tip:knife_profile::blade_tip;
			}
			else
			{
				bool held=false;
				for (std::size_t i=0;i<owned.size();++i)
				{
					const auto& v=owned[i];if (v.at!=weapons::carry::location::held || v.owner.holding_hand()!=static_cast<hand>(h)) continue;
					held=true;const auto& scene=scenes[i];
					if(scene.owner.id()==v.id && scene.authored && scene.authored->defense)
					{
						shield_contact=scene.authored->defense;object=scene.gun;striking_owner=v.owner;
						sample.kind=melee::tool::shield;weapon=v.id.weapon;sample.identity=v.id.generation;sample.count=9;break;
					}
					if(scene.owner.id()==v.id && scene.authored && scene.authored->melee)
					{
						if(!hand_interaction::allows_melee(hand(h)))break;
						object=scene.gun;striking_owner=v.owner;weapon=v.id.weapon;
						sample.kind=melee::tool::knife;sample.identity=v.owner.revision;sample.count=9;
						start=scene.authored->melee->base;end=scene.authored->melee->tip;break;
					}
					const auto geometry=weapons::native_carry::geometry(v.id.weapon);
					if (scene.owner.id()!=v.id || !scene.has_muzzle || !geometry.valid || !geometry.has_muzzle) break;
					sample.kind=melee::tool::firearm;sample.identity=v.id.generation;weapon=v.id.weapon;object=scene.gun;striking_owner=v.owner;
					// Align the native world receiver to this viewmodel's muzzle,
					// then span its complete longest axis, including the stock.
					object=compose(scene.gun,compose(scene.muzzle,inverse(geometry.muzzle)));
					start=end=scale(add(geometry.low,geometry.high),.5f);unsigned axis=0;
					for (unsigned n=1;n<3;++n) if (geometry.high[n]-geometry.low[n]>geometry.high[axis]-geometry.low[axis]) axis=n;
					start[axis]=geometry.low[axis];end[axis]=geometry.high[axis];sample.count=9;break;
				}
				if (held && !sample.count) {reset_motion(h);hits.contact(h,0,false,input.sampled_at);continue;}
				if (!held)
				{
					// Knife acquisition grants and empty-hand striking are separate
					// permissions. A free fist does not acquire an equipment session.
					if (!hand_interaction::free(hand(h)) || (claimed&(1u<<h)) || !input.squeeze[h].active || !input.squeeze[h].down) {reset_motion(h);hits.contact(h,0,false,input.sampled_at);continue;}
					sample.kind=melee::tool::fist;sample.identity=1;object={wrists[h].position,wrists[h].rotation};start={1.5f,0,0};end={3.f,0,0};
				}
			}
			sample.count=9;sample.weapon=weapon;sample.pose_revision=striking_owner.revision;
			for (unsigned n=0;n<9;++n) sample.points[n]=body_point(compose(object,{striking_pick ? special::cliffhanger::pickaxe_head(pick.side)[n] :
				shield_contact ? shield_contact->melee_points[n] : add(start,scale(sub(end,start),float(n)/8)),{0,0,0,1}}).position);
			const auto swing=motion[h].advance(sample);
			auto& diagnostic=motion_diagnostics[h];diagnostic.kind=unsigned(sample.kind);diagnostic.sequence=sample.sequence;
			diagnostic.speed=swing.speed;diagnostic.path=swing.path;
			diagnostic.armed_points=swing.armed_points;diagnostic.rejected=unsigned(swing.rejected);
			if (swing.rejected==melee::rejection::tracking) ++diagnostic.tracking_rejections;
			if (!swing.valid) continue;
			melee::native::hit hit,overlap;
			if (swing.speed>.05f && (swing.armed || hits.touching(h)))
				for (unsigned n=0;n<sample.count;++n)
				{
					if (!swing.armed_at(n) && !hits.touching(h)) continue;
					const auto candidate=melee::native::trace(world_point(swing.from.points[n]),world_point(sample.points[n]),body.head_position,body.units_per_meter,sample.kind);
					if (!candidate) continue;
					if (!overlap || candidate.fraction<overlap.fraction) overlap=candidate;
					if (swing.armed_at(n) && (!hit || candidate.fraction<hit.fraction)) hit=candidate;
				}
			if (!hit && swing.speed<=.05f) continue; // A stationary overlap never rearms a used contact.
			const auto contact=hit ? hit : overlap;
			const auto key=contact ? (contact.target.generation<<12)|std::uint64_t(contact.target.entity) : 0;
			if (contact) ++contacts;
			if (hits.contact(h,key,bool(hit),input.sampled_at))
			{
				++attempts;const int damage=melee::native::damage(hit,weapon,sample.kind,knife_weapon.load());last_target=hit.target.entity;last_damage=damage;
				if (damage>0)
				{
					++damaging_hits;weapons::feedback::carry_confirmation(static_cast<hand>(h),input);
					if(striking_pick && hit.flesh)melee::feedback::pickaxe(object,hand(h),pick.side,pick.lease,input.reference_generation);
					if (sample.kind==melee::tool::knife && hit.flesh)
						melee::feedback::knife(object,static_cast<hand>(h),striking_owner.weapon ? striking_owner.revision : s.knife.revision,
							input.reference_generation,striking_owner.id());
					if (sample.kind==melee::tool::firearm) weapons::feedback::melee_confirmation(striking_owner,input,hit.point);
				}
			}
		}
		return claimed;
	}
	knife_hand_snapshot prepare_hand_pose(const hands::interaction_rig& parts,const hands::rig& r) noexcept
	{
		if(!parts.valid)return {};
		const bool active=running();
		const std::lock_guard lock(mutex);published.hand_ready=true;published.basis=parts.basis;
		for(unsigned h=0;h<2;++h)published.mirror[h]=parts.library.mirror_basis[r.arms[h].wrist];
		return {published.knife,active};
	}
	void present(const knife_hand_snapshot& s,const hands::interaction_rig& parts,const hands::rig& r,
		const std::array<anchor,2>& targets,const std::array<hand_interaction::pose_plan,2>& plans,
		std::span<hands::bone> solved,unsigned occupied_hands,unsigned visible_hands) noexcept
	{
		if(!parts.valid || solved.size()<std::size_t(r.count))return;
		for (unsigned h=0;h<2;++h)
		{
			if(!(visible_hands&(1u<<h)))continue;
			const auto actor=static_cast<hand>(h);const auto& plan=plans[h];
			const bool knife=s.active && s.knife.holder==actor;
			const bool occupied=(occupied_hands&(1u<<h))!=0;
			if(!occupied && (!plan.driver || plan.driver.provider==hand_interaction::domain::knife || plan.driver.provider==hand_interaction::domain::world) &&
				(knife || plan.driver.provider==hand_interaction::domain::world))
			{
				if(knife){const auto wrist=r.arms[h].wrist;move_part(r,wrist,{solved[wrist].position,normalize(multiply(targets[h].rotation,parts.basis[h]))},solved);}
				hands::pose_mirror::fingers(r,parts.library,hands::native_hand_schema::definition,knife_profile::fingers,h,solved,h==1);
			}
		}
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			enabled=dvars::register_bool("vr_physicalMelee",true,game::DVAR_FLAG_SAVED,"Chest knife and tracked hand/weapon melee");
			if (!melee::native::initialize()) {reason="native melee contract rejected";return;}
			if (!melee::feedback::initialize()) console::warn("[VR melee] native knife FX contract rejected\n");
			scene_models::on_submit(submit);scene_models::on_prepare_placement(prepare);fastfiles::on_pre_unload(retire);
			scheduler::loop(refresh,scheduler::pipeline::main,250ms);
			command::add("vr_melee_status",[] {
				state s;{const std::lock_guard lock(mutex);s=published;}
				const auto feedback=melee::native::feedback_status();
				auto report=std::format("[VR melee] enabled={} model={} weapon={} hand={} grip={} revision={} visible={} hands_ready={} draws={} returns={} contacts={} attempts={} damaging_hits={} last_target={} last_damage={} submissions={} prepared={} missing_pose={} cooldown_ms=400 reason={}\n",
					running(),model.load()!=nullptr,knife_weapon.load(),int(s.knife.holder),unsigned(s.knife.grip),s.knife.revision,s.visible,s.hand_ready,
					draws.load(),returns.load(),contacts.load(),attempts.load(),damaging_hits.load(),last_target.load(),last_damage.load(),submitted.load(),prepared.load(),missing_pose.load(),reason.load())+
					std::format("model_name={} chest_prepared={} grip_checks={} available={} pending={} palm_distance_m={:.3f}/{:.3f} world_depth=1\n",s.bayonet?knife_profile::bayonet_model_name:knife_profile::model_name,
						chest_prepared.load(),grip_checks.load(),grab_available.load(),grab_pending.load(),palm_distance[0].load(),palm_distance[1].load())+
					std::format("native_knife_hit_events={} native_knife_blood_events={} native_shield_hit_events={} native_shield_blood_events={}\n",
						feedback.hits,feedback.blood,feedback.shield_hits,feedback.shield_blood)+melee::feedback::status()+npc_collision::status();
				for (unsigned h=0;h<2;++h)
				{
					const auto& d=motion_diagnostics[h];
					report+=std::format("motion hand={} kind={} sequence={} speed_mps={:.3f} travel_m={:.3f} armed_points={} rejected={} tracking_rejections={}\n",
						h,d.kind.load(),d.sequence.load(),d.speed.load(),d.path.load(),d.armed_points.load(),d.rejected.load(),d.tracking_rejections.load());
				}
				console::print_text(console::con_type_info,report);scheduler::once([report]{utils::io::write_file("minidumps/h2-mod-vr-melee.txt",report);},scheduler::pipeline::async);
			});
		}
		void pre_destroy() override {alive=false;model=nullptr;}
	};
}
REGISTER_COMPONENT(vr::gameplay::equipment::component)
