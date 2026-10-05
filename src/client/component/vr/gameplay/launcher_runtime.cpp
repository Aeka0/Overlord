#include <std_include.hpp>
#include "launcher_runtime.hpp"
#include "launcher_targeting.hpp"
#include "javelin_screen.hpp"
#include "hand_interaction/runtime.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapon_interaction.hpp"
#include "native_scripted_control.hpp"
#include "native_shot_history.hpp"
#include "weapon_feedback.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include <mutex>
#include <utils/hook.hpp>
#include "loader/component_loader.hpp"

namespace vr::gameplay::weapons::launcher
{
	namespace
	{
		using namespace hands;using namespace hands::pose_math;
		std::mutex mutex;
		struct instance {presentation view{};scene_frame scene{};};
		std::array<instance,15> instances{};
		std::atomic_uint boundaries{};
		native_shot_history shots;
		const void* player{};std::uint64_t timeline{};int game_time{};
		const char* reason="waiting for launcher scene";
		instance* find(weapon_identity id)noexcept{for(auto& i:instances)if(i.scene.owner.id()==id)return &i;return nullptr;}
		const launcher_profile* admitted(const void* ps,std::uint32_t token)noexcept
		{
			if(!ready() || native_ammunition::local_role(ps)<0 || !token || token>=512 || !game::weapon_defs[token])return nullptr;
			const auto id=carry::native_identity(token);const auto owner=carry::held(id);
			const launcher_profile* p{};
			{const std::lock_guard lock(mutex);if(const auto* i=find(id);i && owns_feed(owner,i->scene))p=i->scene.definition;}
			const auto* name=game::weapon_defs[token]->szInternalName;
			if(!p || !name || !p->matches({name,strnlen_s(name,64)}) || game::weapon_defs[token]->clipSize!=1 ||
				utils::hook::invoke<int>(0x1406A5440,token,false)!=3)return nullptr;
			return p;
		}
		bool settle_instance(instance& i,bool insert)
		{
			const auto ammo=native_ammunition::observe_carried(player,i.scene.owner.id());
			if(!ammo.valid)return !valid_hand(i.view.rocket.actor);
			return settle(i.view.rocket,{ammo.loaded,ammo.reserve},insert,[&](ammunition::projection next){return native_ammunition::commit_carried(ammo,next.loaded,next.reserve);});
		}
		void lifecycle(bool suspended)
		{
			if(!scheduler::is_executing(scheduler::pipeline::server))return;
			const auto* ps=game::CL_IsCgameInitialized()?game::g_entities[0].client:nullptr;
			const auto epoch=native_ammunition::timeline();const int now=ps?game::CG_GetGameTime(0):0;
			const auto owned=carry::held_instances();
			const std::lock_guard lock(mutex);
			if(ps!=player || epoch!=timeline || now<game_time){instances={};shots={};player=ps;timeline=epoch;}
			game_time=now;if(!ps)return;
			for(auto& i:instances)
			{
				if(!i.scene.owner.id())continue;
				const auto ammo=native_ammunition::observe_carried(ps,i.scene.owner.id());
				if(!ammo.valid){i={};continue;}
				const auto token=i.scene.owner.weapon;const auto* def=token<512?game::weapon_defs[token]:nullptr;
				if(!def || !def->szInternalName || !i.scene.definition->matches({def->szInternalName,strnlen_s(def->szInternalName,64)}) ||
					def->clipSize!=1 || utils::hook::invoke<int>(0x1406A5440,token,false)!=3){i.view.fault=true;continue;}
				const auto live=std::find_if(owned.begin(),owned.end(),[&](const auto& x){return x.owner.id()==i.scene.owner.id();});
				if(suspended || !ready() || !scripted_control::allowed(ps) || live==owned.end() || live->owner.rear!=i.view.owner.rear || live->owner.rear_revision!=i.view.owner.rear_revision)
				{
					i.view.fire_armed=false;
					if(!settle_instance(i,false)){reason="rocket refund compare rejected; escrow retained";continue;}
				}
				if(i.scene.definition->loading==launcher_loading::disposable && (i.view.spent || ammo.loaded==0))
				{
					i.view.spent=true;
					// Ammo grants cannot rearm this physical disposable. Keep the grant
					// budget in reserve; a genuinely new inventory instance starts fresh.
					if(ammo.loaded && !native_ammunition::commit_carried(ammo,0,ammo.reserve+ammo.loaded)){i.view.fault=true;continue;}
				}
				i.view.loaded=i.view.spent?0:ammo.loaded;i.view.active=ready();
			}
		}
		bool sample(const instance& i,const hand_interaction::frame& f,vec& delta,float& alignment,float& waist)
		{
			const auto* live=f.find(i.scene.owner.id());const auto& s=i.scene;
			if(!live || !s.definition || !s.binding.valid || live->assembly!=s.assembly || !valid_hand(live->owner.rear) || f.body.units_per_meter<=0)return false;
			const int off=1-int(live->owner.rear);if((f.valid_hands&3)!=3)return false;
			const anchor wrist{f.wrists[off].position,normalize(multiply(f.wrists[off].rotation,s.binding.wrist))};
			const auto grasp=off?hands::pose_mirror::object_in_wrist({},s.definition->rocket_in_wrist,s.binding.mirror):s.definition->rocket_in_wrist;
			const auto rocket=compose(compose(inverse(live->gun),wrist),grasp);
			delta=tail_contact(*s.definition,rocket,f.body.units_per_meter);
			alignment=dot(rotate(rocket.rotation,{1,0,0}),rotate(s.definition->rocket_rest.rotation,{1,0,0}));
			waist=f.waist(hand(off),{},.18f);return true;
		}
	}
	bool ready()noexcept{return boundaries.load()==7 && carry::active();}
	void set_boundary_ready(unsigned bit)noexcept{boundaries.fetch_or(bit);}
	presentation current(weapon_identity id)noexcept{const std::lock_guard lock(mutex);const auto* i=find(id);return i?i->view:presentation{};}
	void publish_scene(const scene_frame& s)noexcept
	{
		if(!s.owner.id() || !s.definition || !s.assembly || !s.binding.valid)return;
		const std::lock_guard lock(mutex);auto* i=find(s.owner.id());
		if(!i)for(auto& x:instances)if(!x.scene.owner.id()){i=&x;break;}
		if(i){i->scene=s;if(!i->view.active){i->view.owner=s.owner;i->view.definition=s.definition;}}
	}
	void update_lifecycle(bool suspended){lifecycle(suspended);}
	void support_feedback(std::span<const carry::instance> before,std::span<const carry::instance> after,
		const controller_input::frame& input,const std::array<hands::anchor,2>& wrists,unsigned valid_hands,unsigned resumed)
	{
		if(!scheduler::is_executing(scheduler::pipeline::server) || !ready() || !input.focused || !input.sequence)return;
		for(const auto& current:after)
		{
			if(current.at!=carry::location::held || !current.owner.can_fire())continue;
			const auto old=std::find_if(before.begin(),before.end(),[&](const auto& v){return v.id==current.id && v.at==carry::location::held;});
			if(old==before.end())continue;
			const auto transition=support_changed(old->owner,current.owner);if(!valid_hand(transition.actor))continue;
			const unsigned bit=1u<<unsigned(transition.actor);if(!(valid_hands&bit) || (resumed&bit))continue;
			const launcher_profile* profile{};
			{const std::lock_guard lock(mutex);const auto* i=find(current.id);
				if(i && i->view.active && !i->view.fault && owns_feed(current.owner,i->scene))profile=i->scene.definition;}
			if(!profile)continue;const auto sound=support_sound(*profile,transition.change);if(!sound.name)continue;
			feedback::event event{mechanics::effect::none,current.owner,input.reference_generation,input.sampled_at,wrists[unsigned(transition.actor)].position};
			event.mechanical_instance=current.id.generation;event.launcher_definition=profile;event.explicit_sound=sound;
			feedback::publish(event); // Native sound remains on the existing main-thread queue.
		}
	}
	void collect_interactions(const hand_interaction::frame& f)noexcept
	{
		using namespace hand_interaction;
		lifecycle(false);if(!ready())return;
		const std::lock_guard lock(mutex);
		for(auto& i:instances)
		{
			const auto* live=f.find(i.scene.owner.id());if(!live || !valid_hand(live->owner.rear) || !i.view.active || i.view.fault)continue;
			const auto actor=hand(1-int(live->owner.rear));const auto target=object(domain::launcher,live->owner.id());
			if(valid_hand(i.view.rocket.actor))continue;
			if(!i.scene.definition->manual_loading() || i.view.loaded)continue;
			vec delta{};float alignment{},waist{};if(!sample(i,f,delta,alignment,waist) || waist>.18f)continue;
			const auto e=input(actor,button::trigger);if(e.press && e.down)offer({actor,{target,role::supply,button::trigger,recipe::single,{}},e.event,30u,waist/.18f,1,true,true});
		}
	}
	void update_interactions()
	{
		using namespace hand_interaction;const auto* f=simulation();if(!f || !ready() || !player)return;
		const std::lock_guard lock(mutex);
		for(auto& i:instances)
		{
			const auto* live=f->find(i.scene.owner.id());if(!live || !i.view.active || !valid_hand(live->owner.rear))continue;
			i.view.owner=live->owner;i.view.reference=f->input.reference_generation;i.view.sequence=f->input.sequence;i.view.at=f->input.sampled_at;
			if(!f->input.trigger[int(live->owner.rear)].down)i.view.fire_armed=true;
			if(!i.scene.definition->manual_loading())continue;
			const auto actor=hand(1-int(live->owner.rear));const auto e=input(actor,button::trigger);
			vec delta{};float alignment{},waist{};const bool contact=sample(i,*f,delta,alignment,waist);
			if(valid_hand(i.view.rocket.actor))
			{
				if(!contact || e.release || !e.down){if(!settle_instance(i,false))reason="rocket refund pending";continue;}
				if(seated(i.view.rocket,delta,alignment) && settle_instance(i,true))
				{
					i.view.loaded=1;i.view.fire_armed=false;reason="rocket seated";
					feedback::event event{mechanics::effect::magazine_in,live->owner,f->input.reference_generation,f->input.sampled_at,
						compose(live->gun,{i.scene.definition->load_mouth,{0,0,0,1}}).position};
					event.mechanical_instance=live->owner.instance_generation;event.launcher_definition=i.scene.definition;
					event.explicit_sound=i.scene.definition->load_sound;feedback::publish(event);
				}
			}
			else if(contact && waist<=.18f && e.press && granted(actor,domain::launcher,live->owner.id(),button::trigger,role::supply))
			{
				const auto ammo=native_ammunition::observe_carried(player,live->owner.id());if(!ammo.valid)continue;
				if(draw(*i.scene.definition,i.view.rocket,actor,{ammo.loaded,ammo.reserve},[&](ammunition::projection next){return native_ammunition::commit_carried(ammo,next.loaded,next.reserve);}))reason="rocket drawn";
			}
		}
	}
	void report_interactions()noexcept
	{
		using namespace hand_interaction;const std::lock_guard lock(mutex);
		for(const auto& i:instances)if(valid_hand(i.view.rocket.actor))observed(i.view.rocket.actor,{object(domain::launcher,i.scene.owner.id()),role::supply,button::trigger,recipe::single,{}});
	}
	bool prepare_transfer(weapon_identity id)noexcept{const std::lock_guard lock(mutex);auto* i=find(id);return !i || settle_instance(*i,false);}
	bool restore_transfer(const presentation& saved)noexcept
	{
		if(!saved.active)return true;
		if(!scheduler::is_executing(scheduler::pipeline::server) || !saved.definition || valid_hand(saved.rocket.actor))return false;
		const auto ammo=native_ammunition::observe_carried(game::g_entities[0].client,saved.owner.id());
		if(!ammo.valid || ammo.loaded!=saved.loaded || (saved.spent && ammo.loaded))return false;
		const std::lock_guard lock(mutex);auto* i=find(saved.owner.id());
		if(!i)for(auto& v:instances)if(!v.scene.owner.id()){i=&v;break;}
		if(!i)return false;*i={};i->view=saved;i->view.fire_armed=false;i->view.reference=i->view.sequence=0;i->view.at={};
		i->scene.owner=saved.owner;i->scene.definition=saved.definition;return true;
	}
	bool blocks_reload(const void* ps,int side)noexcept
	{const auto* p=side?nullptr:admitted(ps,native_ammunition::observe(ps).weapon);return p && p->blocks_native_reload();}
	bool allow_fire(const void* ps,int command,int side)noexcept
	{
		if(side)return true;const auto ammo=native_ammunition::observe(ps);if(!admitted(ps,ammo.weapon))return true;
		const std::lock_guard lock(mutex);auto* i=find(ammo.id());if(!i)return false;const auto& v=i->view;
		const auto now=controller_input::clock::now();
		const bool allowed=v.active && v.owner.can_fire() && !v.fault && !v.spent && v.fire_armed && !valid_hand(v.rocket.actor) &&
			ammo.loaded==1 && v.loaded==1 && now>=v.at && now-v.at<=150ms && javelin_screen::allows_fire(v.owner);
		return shots.allow(ammo.weapon,ammo.instance_generation?ammo.instance_generation:1,command,ammo.loaded,native_ammunition::local_role(ps)==0,allowed);
	}
	void consumed(const void* ps,int command,std::uint32_t token,bool alternate,int amount,int side,const native_ammunition::snapshot& before,const native_ammunition::snapshot& after,bool sustained)noexcept
	{
		if(native_ammunition::local_role(ps)!=0 || side || alternate || amount!=1 || !before.valid || !after.valid || before.id()!=after.id() || before.weapon!=token || before.loaded!=1 || after.loaded!=int(sustained) || before.reserve!=after.reserve)return;
		const std::lock_guard lock(mutex);if(auto* i=find(before.id())){i->view.loaded=int(sustained);i->view.spent=!sustained && i->scene.definition->loading==launcher_loading::disposable;i->view.fire_armed=false;shots.spent(token,before.instance_generation?before.instance_generation:1,command);}
	}
	bool aim_supported(const hold& owner)noexcept
	{
		const auto scene=carry::firing_scene(owner.id());const auto v=current(owner.id());
		return ready() && scene.authored && scene.authored->launcher && v.active && !v.fault && !v.spent && v.loaded>0;
	}
	muzzle_frame ads_muzzle(const hold& owner,muzzle_frame muzzle)noexcept
	{
		const auto v=current(owner.id());
		return v.active && !v.fault && v.definition && muzzle.owner.id()==owner.id()?sighting_frame(*v.definition,muzzle):muzzle;
	}
	bool physical_fire_mode(const void* ps,std::uint32_t token,bool alternate)noexcept
	{
		return !alternate && admitted(ps,token) && scripted_control::allowed(ps) &&
			native_ammunition::observe(ps).weapon==token;
	}
	bool allows_trigger(const hold& owner,std::uint64_t reference,controller_input::clock::time_point now)noexcept
	{
		const auto v=current(owner.id());
		return !v.active || (!v.fault && !v.spent && v.fire_armed && !valid_hand(v.rocket.actor) &&
			v.owner.rear_revision==owner.rear_revision && v.reference==reference && now>=v.at && now-v.at<=150ms);
	}
	bool retain_empty(const void* ps,std::uint32_t token)noexcept
	{
		if(!ready() || native_ammunition::local_role(ps)<0 || !scripted_control::allowed(ps))return false;
		const auto id=carry::native_identity(token);const auto v=current(id);
		if(!v.active || !v.definition || !v.definition->retain_empty())return false;
		const auto ammo=native_ammunition::observe_native_boundary(ps,token,false);return ammo.valid && ammo.loaded==0;
	}
	class component final:public component_interface
	{
		void post_unpack()override{command::add("vr_launcher_status",[]{const auto native=native_statistics();const std::lock_guard lock(mutex);console::info("[VR launcher] boundaries=%u reason=%s targets=%llu retained_selection=%llu retained_ownership=%llu\n",boundaries.load(),reason,native[0],native[1],native[2]);for(const auto& i:instances)if(i.view.active)console::info("%s token=%u loaded=%d spent=%d rocket_hand=%d\n",i.view.definition->id.data(),i.view.owner.weapon,i.view.loaded,i.view.spent,int(i.view.rocket.actor));});}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::launcher::component)
