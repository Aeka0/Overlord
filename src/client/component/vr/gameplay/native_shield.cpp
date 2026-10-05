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
  constexpr std::uintptr_t trace_address=0x1404ac4e0,damage_address=0x1404abc50;
  utils::hook::detour trace_hook,damage_hook;
  game::dvar_t* setting{};std::atomic_bool installed{},alive{true};
  std::atomic_uint64_t queries{},blocks{},events{},filtered{},stale{},fire_blocks{},arm_blocks{};
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
  bool trace(bullet_parameters* parameters,unsigned weapon,bool alternate,game::gentity_s* attacker,bullet_result* result,int surface)
  {
   stamps.invalidate(result);
   const bool managed=parameters && ballistic_damage(parameters->means) && manages_player();
   const bool previous=filtering;filtering=managed;
   const auto restore=gsl::finally([&]{filtering=previous;});
   const bool original=trace_hook.invoke<bool>(parameters,weapon,alternate,attacker,result,surface);
   if(!managed || !parameters || !result)return original;
   const bool outgoing=attacker==&game::g_entities[0] || parameters->shooter==0;
   ++queries;
   const auto input=controller_input::latest();const auto now=controller_input::clock::now();
   const auto tracking=head_pose_bridge::get_status();
   if(!tracking.enabled || !tracking.pose_available || tracking.recenter_pending || tracking.recenter_count!=input.reference_generation ||
    !input.focused || input.orientation_settling || !input.sequence || now<input.sampled_at || now-input.sampled_at>150ms)return original;
   contact nearest;nearest.fraction=result->fraction;hold owner;
   for(const auto& item:carry::held_instances())
   {
    if(!item.id || !valid_hand(item.owner.holding_hand()))continue;
    const auto scene=carry::firing_scene(item.id);
    if(!scene.authored || !scene.authored->defense)continue;
    if(!pose_usable(scene.owner,item.owner,scene.sequence,scene.reference,scene.at,input,now)){++stale;continue;}
    // A gun behind the shield is allowed to fire. Native outgoing rays/pellets
    // stop on either face, after the normal shot and ammo transaction.
    const auto hit=intersect(*scene.authored->defense,scene.gun,parameters->start,parameters->end,nearest.fraction,outgoing?faces::both:faces::front);
    if(hit.valid){nearest=hit;owner=item.owner;}
   }
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
   ++blocks;
   // Same native EV_BULLET_HIT_CLIENT_SHIELD emitter as 0x1404ab5d7, with the
   // actual world contact. Do not invoke stock ricochet or durability writes:
   // the selected native weapon can be the pistol held beside this shield.
   if(make_event)
   {
    utils::hook::invoke<void>(0x1404ac950,result->position.data(),result->normal.data(),parameters->start.data(),
     weapon,alternate,int(parameters->shooter),&game::g_entities[0],19,0x47);++events;
   }
   feedback::carry_confirmation(owner.holding_hand(),controller_input::latest());
   return false; // Native pellet/penetration caller stops this segment here.
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
  const auto input=controller_input::latest();const auto now=controller_input::clock::now();
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
   constexpr std::uint8_t trace_entry[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
   constexpr std::uint8_t damage_entry[]{0x48,0x89,0x5c,0x24,0x10,0x44,0x89,0x4c,0x24,0x20};
   constexpr std::uint8_t query_priority[]{0x4c,0x8b,0x4e,0x78};
   constexpr std::uint8_t priority_skip[]{0x41,0x3b,0xde,0x0f,0x82};
   constexpr std::uint8_t emitter[]{0x48,0x89,0x74,0x24,0x10,0x44,0x89,0x4c,0x24,0x20};
   const bool ready=verify(trace_address,trace_entry) && verify(damage_address,damage_entry) &&
    verify(0x1406b7727,query_priority) && verify(0x140655afa,priority_skip) && verify(0x1404ac950,emitter) &&
    call_matches(0x140599938,0x1406b73e0) && call_matches(0x1404abd94,0x1404a9b00) && call_matches(0x1404ab5f7,0x1404a9b00) &&
    call_matches(0x1404aa3f6,damage_address) && call_matches(0x1404ab5d7,0x1404ac950);
   if(ready)
   {
    trace_hook.create(trace_address,trace);damage_hook.create(damage_address,damage);
    utils::hook::call(0x140599938,entity_query);utils::hook::call(0x1404abd94,angle_query);utils::hook::call(0x1404ab5f7,angle_query);
    installed=true;
   }
   else console::error("[VR shield] Native contracts rejected; shield adapter unavailable\n");
   command::add("vr_shield_status",[] {
    const auto text=std::format("installed={} enabled={} queries={} blocked={} events={} filtered_native_parts={} stale_poses={} fire_blocks={} arm_blocks={}\n",
     installed.load(),enabled(),queries.load(),blocks.load(),events.load(),filtered.load(),stale.load(),fire_blocks.load(),arm_blocks.load());
    console::info("%s",text.c_str());utils::io::write_file("minidumps/h2-mod-vr-shield.txt",text);
   });
  }
  void pre_destroy() override {alive=false;}
 };
}
REGISTER_COMPONENT(vr::gameplay::weapons::shield::component)
