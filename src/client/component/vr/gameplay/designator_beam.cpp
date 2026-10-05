#include <std_include.hpp>
#include "designator_visual.hpp"
#include "weapon_interaction.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapon_render_pose.hpp"
#include "weapon_render_owner.hpp"
#include "native_scripted_control.hpp"
#include <utils/native_memory.hpp>
#include "../eye_composition.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scripting.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapon_laser
{
 namespace
 {
  using namespace hands;
  namespace designator_visual=equipment::special::designator_visual;
  struct orientation {vec origin;std::array<vec,3> axis;};
  static_assert(sizeof(orientation)==48);
  struct sample {weapons::muzzle_frame pose;designator_visual::surface surface;controller_input::clock::time_point at;
   spatial_panel::vec4 color{1,.002f,.001f,1};bool receiver_tag{},nightvision_only{};};
  std::mutex mutex;std::array<sample,16> samples{};unsigned cursor{};
  utils::hook::detour draw_hook;bool installed{};
  std::atomic_uint64_t traced{},suppressed{},pairs{},drawn{},missed{};
  struct pair_snapshot {std::uint64_t id{},publication{},device{};std::array<world_beam::projected,2> eyes;bool valid{};};
  thread_local pair_snapshot pair;
  thread_local world_beam::renderer renderer;
  void draw(int client,const void* context,const void* entity,const orientation* input,void* effects,int flags,game::LaserDef* definition,float range)
  {
   const auto original=[&]{draw_hook.invoke<void>(client,context,entity,input,effects,flags,definition,range);};
   if(client!=0 || !entity || !weapons::carry::active()){original();return;}
   unsigned short owner{};
   if(!utils::native_memory::read_bytes(&owner,static_cast<const std::byte*>(entity)+0x1b4,sizeof(owner)) || owner!=0){original();return;}
   const auto* ps=game::CG_GetPredictedPlayerState(0);unsigned token{};
   if(!ps || !utils::native_memory::read_bytes(&token,reinterpret_cast<const std::byte*>(ps)+0x3bc,sizeof(token))){original();return;}
   token&=511;const auto* def=game::weapon_defs[token];
   if(!token || !def || !def->szInternalName){original();return;}
   const bool designator=std::string_view(def->szInternalName)=="usp_laserdesignator";
   auto pose=weapons::current_muzzle();const auto held=weapons::sample_native_equipped();const auto tracking=controller_input::latest();const auto now=controller_input::clock::now();
   if(token!=held.weapon){original();return;}
   if(!scripted_control::predicted_allowed()){if(designator)++suppressed;else original();return;}
   game::LaserDef laser{};
   if(!weapons::pose_ready(pose,held,tracking.reference_generation,now) ||
    !utils::native_memory::read_bytes(&laser,definition,sizeof(laser)))
   {++suppressed;return;}
   bool receiver_tag=!designator;
   if(designator && (pose.profile_id!="laserdesignator" || !held.can_fire())){++suppressed;return;}
   if(!designator && laser.laserTag)
   {
    const auto* tag=unsigned(laser.laserTag)<0x40000?game::SL_ConvertToString(laser.laserTag):nullptr;
    if(tag && std::string_view(tag)=="tag_flash")receiver_tag=false;
    else if(!tag || std::string_view(tag)!="tag_laser"){++suppressed;return;}
   }
   if(receiver_tag && !weapons::laser_pose(pose,pose)){++suppressed;return;}
   spatial_panel::vec4 color{1,.002f,.001f,1};
   if(!designator)for(unsigned c=0;c<3;++c)
   {const auto value=laser.hdrColorScale[c];if(!std::isfinite(value) || value<0 || value>64){++suppressed;return;}color[c]=value;}
   const float reach=laser.laserSightLaser && std::isfinite(range) && range>0?range:laser.range;
   if(!std::isfinite(reach) || reach<=0 || reach>20000){++suppressed;return;}
   const auto end=add(pose.position,scale(pose.axis[0],reach));game::trace_t hit{};game::Bounds point{};unsigned trace_flags{};
   // Exact native client query on its original frontend thread. No engine calls in eye composition.
   utils::hook::invoke<void>(0x1403C70D0,&hit,pose.position.data(),end.data(),&point,static_cast<unsigned short>(0),context?0x280e821:0x280e861,&trace_flags);
   if(hit.startsolid || hit.allsolid || !std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1){++suppressed;return;}
   const vec normal{hit.normal[0],hit.normal[1],hit.normal[2]};const bool contact=hit.fraction<1;
   if(contact && (!std::isfinite(length(normal)) || std::abs(length(normal)-1)>.01f)){++suppressed;return;}
   const sample value{pose,{add(pose.position,scale(pose.axis[0],reach*hit.fraction)),contact?normal:vec{0,0,1},reach,contact},now,color,receiver_tag,laser.nightvisionOnly};
   {const std::lock_guard lock(mutex);samples[cursor++%samples.size()]=value;}++traced;
   // Replace the early beam and its post_light with a scene-bound depth-tested draw.
  }
  void prepare(const eye_composition::event& event)
  {
   pair={};pair.id=event.pair_id;pair.publication=event.views.eyes[0].publication;pair.device=event.device_generation;
   const auto held=weapons::carry::current_hold();weapon_render_pose::snapshot rendered;
   if(!event.model_origins.valid || !valid_hand(held.holding_hand()) || !weapon_render_pose::for_scene(event.views,rendered,held.id()))return;
   auto pose=rendered.muzzle;if(pose.owner.id()!=held.id() || pose.owner.rear_revision!=held.rear_revision || !weapons::valid_model_anchor(pose.model))return;
   sample trace;const auto now=controller_input::clock::now();
   {const std::lock_guard lock(mutex);for(const auto& value:samples)
    if(weapons::same_render_carrier(value.pose.owner,held) && value.pose.reference_generation==pose.reference_generation && value.pose.input_sequence<=pose.input_sequence &&
     now>=value.at && now-value.at<150ms && value.at>trace.at)trace=value;}
   if(!trace.pose.valid)return;
   if(trace.receiver_tag && !weapons::laser_pose(pose,pose))return;
   if(trace.nightvision_only)
   {const auto* ps=game::CG_GetPredictedPlayerState(0);unsigned flags{};
    if(!ps || !utils::native_memory::read_bytes(&flags,reinterpret_cast<const std::byte*>(ps)+0x3c0,sizeof(flags)) || !(flags&0x40))return;}
   const auto start=weapons::place_model_anchor(pose.model,event.model_origins.placement);vec end;
   if(!designator_visual::endpoint(start,pose.axis[0],trace.surface,pose.units_per_meter,end))return;
   const auto eye=scale(add(event.model_origins.eyes[0],event.model_origins.eyes[1]),.5f);
   const auto world=designator_visual::geometry(start,end,trace.surface.normal,eye,pose.units_per_meter);
   for(unsigned i=0;i<2;++i){spatial_panel::matrix vp;std::memcpy(vp.data(),event.views.eyes[i].bytes.data()+engine_stereo_view::h2_current_view_projection_offset,sizeof(vp));
    pair.eyes[i]=designator_visual::project(world,event.model_origins.eyes[i],vp,trace.surface.hit);pair.eyes[i].color=trace.color;}
   pair.valid=true;++pairs;
  }
  void compose(const eye_composition::event& event,ID3D11DeviceContext* context,ID3D11ShaderResourceView*,ID3D11RenderTargetView* target)noexcept
  {
   if(!installed || event.eye>1 || !weapons::carry::active())return;
   if(pair.id!=event.pair_id || pair.publication!=event.views.eyes[0].publication || pair.device!=event.device_generation)prepare(event);
   if(!pair.valid)return;
   if(renderer.draw(context,target,event.scene_depth,pair.eyes[event.eye],event.width,event.height))++drawn;else ++missed;
  }
 }
 class component final:public component_interface
 {
  void post_unpack()override
  {
   constexpr std::array<unsigned char,16> bytes{0x4c,0x8b,0xdc,0x55,0x53,0x56,0x57,0x41,0x57,0x49,0x8d,0xab,0xf8,0xfe,0xff,0xff};std::array<unsigned char,16> mask;mask.fill(255);
   constexpr std::array<unsigned char,4> trace{0x48,0x83,0xec,0x68},trace_mask{255,255,255,255};
   if(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14037D470),{bytes.data(),mask.data(),bytes.size()}) &&
    utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403C70D0),{trace.data(),trace_mask.data(),trace.size()}))
   {draw_hook.create(0x14037D470,draw);installed=true;}
   eye_composition::set_consumer(compose,eye_composition::layer::world_equipment);
   scripting::on_shutdown([](bool,bool after){if(!after){const std::lock_guard lock(mutex);samples={};cursor=0;}});
   const auto status=[]{console::info("[VR weapon laser] installed=%d traces=%llu suppressed=%llu scene_pairs=%llu eye_draws=%llu depth_failures=%llu\n",installed,traced.load(),suppressed.load(),pairs.load(),drawn.load(),missed.load());};
   command::add("vr_weapon_laser_status",status);command::add("vr_designator_beam_status",status);
  }
 };
}
REGISTER_COMPONENT(vr::gameplay::weapon_laser::component)
