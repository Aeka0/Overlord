#include <std_include.hpp>
#include "native_shield.hpp"
#include "shield_geometry.hpp"
#include "shield_trace.hpp"
#include "weapon_profile.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapon_feedback.hpp"
#include "native_scripted_control.hpp"
#include <utils/native_memory.hpp>
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::weapons::shield
{
 namespace
 {
  // H2 server BulletTrace / BulletProcess, witnessed in the saved native image.
  constexpr std::uintptr_t damage_address=0x1404abc50;
  utils::hook::detour damage_hook;
  game::dvar_t* setting{};std::atomic_bool installed{},alive{true};
  std::atomic_uint64_t queries{},blocks{},events{},filtered{},stale{},fire_blocks{},arm_blocks{},cone_blocks{};
  thread_local bool filtering{},filtering_angles{};
  // Trace results have native lifetimes and may be copied/reused. A MOD stamp
  // identifies only an unchanged result produced by this exact trace call.
  thread_local trace_stamps stamps;
  bool manages_player() noexcept
  {return enabled() && carry::active() && game::CL_IsCgameInitialized() && game::g_entities[0].client && scripted_control::allowed(game::g_entities[0].client);}
  template<class T> bool read(const void* p,std::size_t offset,T& value) noexcept
  {return p && utils::native_memory::read_bytes(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));}
  void entity_query(void* query,void* link,int entity,void* result,void* context)
  {
   // Only the local player's old shield part is excluded. NPC shields and all
   // other trace priorities remain native. No shared map or DObj is mutated.
   alignas(16) std::array<std::byte,0x80> local{};std::array<std::uint8_t,24> priorities{};
   const void* map{};
   if(filtering && entity==0 && read(query,0,local) && read(query,0x78,map) && read(map,0,priorities))
   {
    priorities=without_shield(priorities);map=priorities.data();std::memcpy(local.data()+0x78,&map,sizeof(map));
    ++filtered;utils::hook::invoke<void>(0x1406b73e0,local.data(),link,entity,result,context);return;
   }
   utils::hook::invoke<void>(0x1406b73e0,query,link,entity,result,context);
  }
  bool angle_query(game::gentity_s* target,game::gentity_s* attacker,const float* start,const float* end)
  {
   if(target==&game::g_entities[0] && filtering_angles)return false;
   return utils::hook::invoke<bool>(0x1404a9b00,target,attacker,start,end);
  }
  bool cone_angle_query(game::gentity_s* target,game::gentity_s* attacker,const float* start,const float* end)
  {
   // This caller belongs only to non-explosive spread fire. Its stock facing
   // test runs before the per-target sight trace, outside BulletProcess.
   if(target==&game::g_entities[0] && manages_player())return false;
   return utils::hook::invoke<bool>(0x1404a9b00,target,attacker,start,end);
  }
  struct interception {contact hit;hold owner;};
  interception collide(hands::vec start,hands::vec end,float limit,bool outgoing)
  {
   ++queries;
   const auto input=controller_input::latest_interaction();const auto now=controller_input::clock::now();
   const auto tracking=head_pose_bridge::get_status();
   if(!tracking.enabled || !tracking.pose_available || tracking.recenter_pending || tracking.recenter_count!=input.reference_generation ||
    !input.focused || input.orientation_settling || !input.sequence || now<input.sampled_at || now-input.sampled_at>150ms)return {};
   contact nearest;nearest.fraction=limit;hold owner;
   for(const auto& item:carry::held_instances())
   {
    if(!item.id || !valid_hand(item.owner.holding_hand()))continue;
    const auto scene=carry::firing_scene(item.id);
    if(!scene.authored || !scene.authored->defense)continue;
    if(!pose_usable(scene.owner,item.owner,scene.sequence,scene.reference,scene.at,input,now)){++stale;continue;}
    // A gun behind the shield is allowed to fire. Native outgoing rays/pellets
    // stop on either face, after the normal shot and ammo transaction.
    const auto hit=intersect(*scene.authored->defense,scene.gun,start,end,nearest.fraction,outgoing?faces::both:faces::front);
    if(hit.valid){nearest=hit;owner=item.owner;}
   }
   return {nearest,owner};
  }
  void impact(const hold& owner,hands::vec point,hands::vec normal,hands::vec start,unsigned weapon,bool alternate,int shooter,bool make_event)
  {
   ++blocks;
   // Same native EV_BULLET_HIT_CLIENT_SHIELD emitter as 0x1404ab5d7, with the
   // actual world contact. The other hand may own the selected native weapon.
   if(make_event)
   {
    utils::hook::invoke<void>(0x1404ac950,point.data(),normal.data(),start.data(),
     weapon,alternate,shooter,&game::g_entities[0],19,0x47);++events;
   }
   feedback::carry_confirmation(owner.holding_hand(),controller_input::latest_interaction());
  }
  bool trace(native_bullet_trace::next next,bullet_parameters* parameters,unsigned weapon,bool alternate,game::gentity_s* attacker,bullet_result* result,int surface)
  {
   stamps.invalidate(result);
   const bool managed=parameters && ballistic_damage(parameters->means) && manages_player();
   const bool previous=filtering;filtering=managed;
   const auto restore=gsl::finally([&]{filtering=previous;});
   const bool original=next(parameters,weapon,alternate,attacker,result,surface);
   if(!managed || !parameters || !result)return original;
   const bool outgoing=attacker==&game::g_entities[0] || parameters->shooter==0;
   const auto [nearest,owner]=collide(parameters->start,parameters->end,result->fraction,outgoing);
   if(!nearest.valid)return original;
   *result={};result->fraction=nearest.fraction;result->normal=nearest.normal;result->surface=0x1d00000;
   result->type=1;result->entity=0;result->target=&game::g_entities[0];result->location=19;
   result->position=nearest.point;result->surface_type=29;
   stamps.publish(result,parameters,owner);return true;
  }
  bool damage(void* random,bullet_parameters* parameters,bullet_result* result,unsigned weapon,bool alternate,
   int event_parameter,game::gentity_s* attacker,int flags,int time,int* output_flags,bool make_event)
  {
   const bool previous=filtering_angles;
   filtering_angles=parameters && ballistic_damage(parameters->means) && manages_player();
   const auto restore=gsl::finally([&]{filtering_angles=previous;});
   const auto owner=stamps.consume(result,parameters); // Before reentrant callbacks.
   if(!owner.id())return damage_hook.invoke<bool>(random,parameters,result,weapon,alternate,event_parameter,attacker,flags,time,output_flags,make_event);
   if(output_flags)*output_flags=0;
   impact(owner,result->position,result->normal,parameters->start,weapon,alternate,int(parameters->shooter),make_event);
   return false; // Native pellet/penetration caller stops this segment here.
  }
  void cone_damage(game::gentity_s* target,game::gentity_s* inflictor,game::gentity_s* attacker,
   const float* direction,const float* point,int amount,int flags,unsigned means,game::Weapon weapon,bool alternate,
   unsigned surface,int location,unsigned model,unsigned part)
  {
   // Non-explosive shotguns also call G_Damage directly at 0x1404aa082,
   // AFTER a successful native sight trace. Blocking their visual pellets in
   // BulletTrace cannot stop this separate cone damage. Only this call site
   // supplies direction=&params.direction and point=&params.end; witness both
   // argument instructions at startup and reject a mismatched packet here.
   if(target==&game::g_entities[0] && attacker && attacker!=target && direction &&
    ballistic_damage(means) && manages_player())
   {
    const auto* packet=reinterpret_cast<const std::byte*>(direction)-offsetof(bullet_parameters,direction);
    bullet_parameters ray{};
    if(point==reinterpret_cast<const float*>(packet+offsetof(bullet_parameters,end)) &&
     read(packet,0,ray) && ray.means==int(means))
    {
     const auto [hit,owner]=collide(ray.start,ray.end,1,false);
     if(hit.valid)
     {
      ++cone_blocks;
      impact(owner,hit.point,hit.normal,ray.start,weapon.data,alternate,int(ray.shooter),true);
      return;
     }
    }
   }
   // Preserve the common script/Lua damage owner and every native argument.
   utils::hook::invoke<void>(0x1404bd2e0,target,inflictor,attacker,direction,point,amount,flags,
    means,weapon,alternate,surface,location,model,part);
  }
  template<std::size_t N> bool verify(std::uintptr_t at,const std::uint8_t (&bytes)[N])
  {std::array<std::uint8_t,N> mask;mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),{bytes,mask.data(),N}));}
  bool call_matches(std::uintptr_t at,std::uintptr_t target)
  {std::uint8_t bytes[5]{0xe8};const auto delta=std::int32_t(target-at-5);std::memcpy(bytes+1,&delta,4);return verify(at,bytes);}
 }
 bool enabled() noexcept{return alive.load() && installed.load() && setting && setting->current.enabled;}
 bool firing_clear(weapon_identity weapon,hands::vec muzzle) noexcept
 {
  if(!enabled() || !carry::active())return true;
  const auto input=controller_input::latest_interaction();const auto now=controller_input::clock::now();
  const auto gun=carry::firing_scene(weapon);const auto owner=carry::held(weapon);
  for(const auto& item:carry::held_instances())
  {
   if(!item.id || item.id==weapon)continue;
   const auto shield=carry::firing_scene(item.id);
   const auto* definition=item.id.weapon<512 ? game::weapon_defs[item.id.weapon] : nullptr;
   if((!shield.authored || !shield.authored->defense) && (!definition || definition->weapType!=game::WEAPTYPE_RIOTSHIELD))continue;
   // A known held shield with unavailable tracking cannot grant permission to
   // shoot through it. Resume from fresh poses, never a stale cached blocker.
   if(!shield.authored || !shield.authored->defense ||
    !pose_usable(shield.owner,item.owner,shield.sequence,shield.reference,shield.at,input,now) ||
    !pose_usable(gun.owner,owner,gun.sequence,gun.reference,gun.at,input,now)){++fire_blocks;return false;}
   const auto penetration=fire_penetration(*shield.authored->defense,shield.gun,gun.firing_arm,muzzle);
   if(penetration!=clipping::none)
   {if(penetration==clipping::upper_arm || penetration==clipping::forearm)++arm_blocks;++fire_blocks;return false;}
  }
  return true;
 }
 class component final:public component_interface
 {
  void post_unpack() override
  {
   setting=dvars::register_bool("vr_physicalShield",true,game::DVAR_FLAG_SAVED,"Tracked shield front panels block bullets; native blunt physical melee");
   constexpr std::uint8_t damage_entry[]{0x48,0x89,0x5c,0x24,0x10,0x44,0x89,0x4c,0x24,0x20};
   constexpr std::uint8_t query_priority[]{0x4c,0x8b,0x4e,0x78};
   constexpr std::uint8_t priority_skip[]{0x41,0x3b,0xde,0x0f,0x82};
   constexpr std::uint8_t emitter[]{0x48,0x89,0x74,0x24,0x10,0x44,0x89,0x4c,0x24,0x20};
   constexpr std::uint8_t cone_end[]{0x49,0x8d,0x70,0x28};
   constexpr std::uint8_t cone_direction[]{0x4c,0x8d,0x4f,0x34};
   constexpr std::uint8_t cone_point[]{0x48,0x89,0x74,0x24,0x20};
   const bool ready=native_bullet_trace::initialize() && verify(damage_address,damage_entry) &&
    verify(0x1406b7727,query_priority) && verify(0x140655afa,priority_skip) && verify(0x1404ac950,emitter) &&
    call_matches(0x140599938,0x1406b73e0) && call_matches(0x1404abd94,0x1404a9b00) && call_matches(0x1404ab5f7,0x1404a9b00) &&
    call_matches(0x1404aa3f6,damage_address) && call_matches(0x1404ab5d7,0x1404ac950) &&
    call_matches(0x1404ac409,0x1404a9b00) && call_matches(0x1404aa082,0x1404bd2e0) &&
    verify(0x1404a9f42,cone_end) && verify(0x1404aa037,cone_direction) && verify(0x1404aa07d,cone_point);
   if(ready)
   {
    if(!native_bullet_trace::set_interceptor(trace))return;
    damage_hook.create(damage_address,damage);
    utils::hook::call(0x140599938,entity_query);utils::hook::call(0x1404abd94,angle_query);utils::hook::call(0x1404ab5f7,angle_query);
    utils::hook::call(0x1404ac409,cone_angle_query);utils::hook::call(0x1404aa082,cone_damage);
    installed=true;
   }
   else console::error("[VR shield] Native contracts rejected; shield adapter unavailable\n");
   command::add("vr_shield_status",[] {
    const auto text=std::format("installed={} enabled={} queries={} blocked={} cone_blocks={} events={} filtered_native_parts={} stale_poses={} fire_blocks={} arm_blocks={}\n",
     installed.load(),enabled(),queries.load(),blocks.load(),cone_blocks.load(),events.load(),filtered.load(),stale.load(),fire_blocks.load(),arm_blocks.load());
    console::info("%s",text.c_str());utils::io::write_file("minidumps/overlord-shield.txt",text);
   });
  }
  void pre_destroy() override {alive=false;}
 };
}
REGISTER_COMPONENT(vr::gameplay::weapons::shield::component)
