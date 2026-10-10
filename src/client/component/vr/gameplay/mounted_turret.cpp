#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "component/vr/gameplay/hands/rig_builder.hpp"
#include "mounted_turret.hpp"
#include "mounted_turret_pose.hpp"
#include "campaign/sequences/camera_policies.hpp"
#include "shoulder_anchors.hpp"
#include "native_carry.hpp"
#include "native_player_life.hpp"
#include "hands/position_offset.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "viewmodel_visibility.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/fastfiles.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>
#include <sstream>

namespace vr::gameplay::mounted
{
	namespace
	{
		using namespace hands;
		using namespace hands::pose_math;
		using clock=controller_input::clock;
		utils::hook::detour aim_hook,scene_hook;
		std::atomic_bool installed{},alive{true};
		std::atomic_uint64_t asset_epoch{1};
		std::mutex mutex,hand_mutex;
		struct geometry
		{
			int entity{-1},root_bone{-1};const mount_profile* profile{};
			anchor root{},gun{},seat{};std::array<anchor,2> buttons{};vec pivot{},yaw_pivot{};
			clock::time_point at{};
		} rendered;
		struct calibration
		{
			int entity{-1};std::uint64_t generation{};std::array<anchor,2> wrists{};bool valid{};
		} authored;
		struct publication
		{
			int entity{-1};std::uint64_t generation{},reference{};controller state{};
			controls sampled{};
			clock::time_point at{};
			const mount_profile* profile{};
			std::uint64_t instance{};
			std::array<float,2> requested_angles{};
		} published;
		controller control;
		view_motion camera_motion;
		struct view_publication
		{
			std::uint64_t instance{},reference{};
			view_pose pose{};
			clock::time_point at{};
			int frame_time{};
		} rendered_view;
		int last_entity=-1,last_time{};
		const mount_profile* last_profile{};
		std::uint64_t last_generation{};
		std::uint64_t mount_instance{};
		const char* reason="waiting for mounted turret";
		std::uint64_t updates{},camera_updates{},wrist_captures{},client_updates{},hand_applications{};
		const char* hand_reason="waiting for attached hand skeleton";
		const char* camera_reason="waiting for native camera callback";
		std::uint64_t camera_calls{},client_calls{};
		std::atomic_uint64_t world_depth_submissions{};
		std::atomic_uint64_t model_pose_updates{},shield_publications{};
		std::atomic_uint64_t render_preparations{};
		std::atomic_uint64_t skin_preparations{},skin_pose_changes{},skin_rejections{};
		template<class T> T read(const void* p,std::size_t offset)
		{T v{};std::memcpy(&v,static_cast<const std::byte*>(p)+offset,sizeof(v));return v;}
		template<class T> void write(void* p,std::size_t offset,const T& v)
		{std::memcpy(static_cast<std::byte*>(p)+offset,&v,sizeof(v));}
		struct assembly
		{
			const mount_profile* profile{};game::XModel* gun{},*hand{};unsigned hand_offset{};
		};
		assembly mounted_model(const void* object)
		{
			if(!object)return {};
			const unsigned count=read<std::uint8_t>(object,0xf);if(count<2 || count>3)return {};
			const auto* models=read<game::XModel* const*>(object,0xd8);if(!models)return {};
			std::array<std::string_view,3> names{};unsigned bones{};
			for(unsigned i=0;i<count;++i)
			{
				const auto* m=models[i];
				if(!m || !m->name || strnlen_s(m->name,128)>=128 || !m->numBones || !m->baseMat || !m->boneNames)return {};
				names[i]=m->name;bones+=m->numBones;
			}
			if(bones!=read<std::uint8_t>(object,0x10))return {};
			for(const auto* p:profiles)if(matches(*p,{names.data(),count}))
			{
				unsigned offset{};for(unsigned i=0;i<p->hand_model;++i)offset+=models[i]->numBones;
				return {p,models[0],models[p->hand_model],offset};
			}
			return {};
		}
		bool fresh(clock::time_point at)
		{const auto now=clock::now();return at!=clock::time_point{} && now>=at && now-at<=150ms;}
		bool predicted(int entity,mount_kind kind)
		{
			if (!alive || !installed || entity<=0 || entity>=4000 || !game::CL_IsCgameInitialized()) return false;
			const auto* ps=game::CG_GetPredictedPlayerState(0);
			return ps && attached(ps->e_flags,kind) && read<std::uint16_t>(ps,0x1e)==entity &&
				(kind!=mount_kind::blackhawk || !player_life::dead(ps));
		}
		const mount_profile* supported(const game::gentity_s* entity)
		{
			const unsigned token=read<unsigned>(entity,0x80)&511;
			const auto* definition=token ? game::weapon_defs[token] : nullptr;
			const auto* name=definition ? read<const char*>(definition,0) : nullptr;
			if(name && strnlen_s(name,128)<128)for(const auto* p:profiles)if(p->weapon==name)return p;
			return nullptr;
		}
		publication latest()
		{const std::lock_guard lock(mutex);return published;}
		void scene_stub(void* object,void* pose,unsigned entity,unsigned flags,const float* light,float scale,int a,int b)
		{
			// Common DObj insertion, reached by BOTH queued world entities and
			// direct viewmodels. The viewmodel-only enqueue hook misses this gun.
			if ((flags&1u) && owns_model(object)) {flags=world_scene_flags(flags);++world_depth_submissions;}
			scene_hook.invoke<void>(object,pose,entity,flags,light,scale,a,b);
		}
		bone* render_pose_stub(const void* pose,void* object,std::uint32_t* bits)
		{
			auto* matrices=utils::hook::invoke<bone*>(0x14038C9E0,pose,object,bits);
			if(matrices && owns_model(object) && read<const bone*>(object,0xa8)==matrices)
			{
				// CG_DObjCalcPose returns cached matrices without DObjCalcSkel when
				// camera tag queries already completed the skeleton. This scene call
				// runs after HMD composition, still inside the native DObj lock and
				// before bounds, culling or skin jobs consume the resulting pose.
				apply_pose(object,false);++render_preparations;
			}
			return matrices;
		}
		bool admitted(const publication& p)
		{return p.profile && predicted(p.entity,p.profile->kind) && fresh(p.at) && head_pose_bridge::get_status().enabled;}
		bool camera_owned(const publication& p)
		{
			// A paused server stops publishing. Retain the current seat transform
			// while native ownership persists; input projection still requires freshness.
			const auto* ps=game::CG_GetPredictedPlayerState(0);
			return p.profile==&suburban ? p.instance && ps && !player_life::dead(ps) && predicted(p.entity,p.profile->kind) &&
				head_pose_bridge::get_status().enabled : admitted(p);
		}
		game_view::camera_request request_for_mount(const publication& p)
		{
			if(!camera_owned(p))return {};
			const auto epoch=p.instance|(1ull<<62);
			return p.profile==&suburban ? game_view::camera_request{sequences::scene_cameras::mounted_orbit,epoch,epoch} :
				game_view::camera_request{sequences::scene_cameras::mounted,epoch};
		}
		void track_controls(controls& c,const controller_input::frame& input,const geometry& g,const calibration& a,
			const head_pose_bridge::spatial_frame& body)
		{
			c.tracked=0;c.units=body.units_per_meter;c.pivot=g.pivot;c.yaw_pivot=g.yaw_pivot;
			const auto root_inverse=inverse(g.root);
			if(g.profile==&suburban)c.tracking_frame=compose(root_inverse,{body.world_origin,from_axis(body.world_yaw_axis)});
			const auto* inward=game::Dvar_FindVar(vr::settings::active_hand_alignment()[0].name);const auto* back=game::Dvar_FindVar(vr::settings::active_hand_alignment()[1].name);const auto* up=game::Dvar_FindVar(vr::settings::active_hand_alignment()[2].name);
			for(unsigned h=0;h<2;++h)
			{
				c.handles[h]=compose(root_inverse,c.calibrated?compose(g.gun,a.wrists[h]):g.buttons[h]).position;
				head_pose_bridge::world_pose grip,aim;anchor wrist;
				if(inward && back && up && input.grip[h].valid && input.aim[h].valid &&
					head_pose_bridge::tracking_to_world(body,input.grip[h].tracking,grip) && head_pose_bridge::tracking_to_world(body,input.aim[h].tracking,aim) &&
					tracked_wrist(input,body,{},h,{inward->current.value,back->current.value,up->current.value},wrist))
				{c.wrists[h]=compose(root_inverse,wrist).position;c.tracked|=1u<<h;}
			}
		}
		void client_controller_stub(const void* pose,void* object,std::uint32_t* bits)
		{
			{const std::lock_guard lock(mutex);++client_calls;}
			const auto p=latest();
			// This is the centity pose passed by the native mounted camera path.
			// Match the current owner's object, never all pose-type-8 world turrets.
			if (admitted(p) && p.profile==&suburban && pose==reinterpret_cast<void*>(0x141C328F0ull+std::uintptr_t(p.entity)*0x200) && owns_hands(object))
			{
				alignas(16) std::array<std::byte,0xb8> local{};
				if (independent_client_pose({static_cast<const std::byte*>(pose),local.size()},p.state.angles,local))
				{
					utils::hook::invoke<void>(0x14038D100,local.data(),object,bits);
					const std::lock_guard lock(mutex);++client_updates;return;
				}
			}
			utils::hook::invoke<void>(0x14038D100,pose,object,bits);
		}
		void vehicle_controller_stub(const void* pose,void* object,std::uint32_t* bits)
		{
			const auto p=latest();
			if(admitted(p) && p.profile==&blackhawk && pose==reinterpret_cast<void*>(0x141C328F0ull+std::uintptr_t(p.entity)*0x200) && owns_hands(object))
			{
				alignas(16) std::array<std::byte,0xb8> local{};
				if(independent_vehicle_pose({static_cast<const std::byte*>(pose),local.size()},p.state.angles,local))
				{utils::hook::invoke<void>(0x14038D2E0,local.data(),object,bits);const std::lock_guard lock(mutex);++client_updates;return;}
			}
			utils::hook::invoke<void>(0x14038D2E0,pose,object,bits);
		}
		bool tag_matrix(void* centity,void* object,unsigned tag,anchor& out)
		{
			if (!tag) return false;
			std::array<vec,3> axis{};vec origin{};
			if (!utils::hook::invoke<int>(0x140370E10,centity,object,tag,axis.data(),origin.data()) || !finite(origin)) return false;
			if (!valid_offset_axis(axis)) return false;
			out={origin,from_axis(axis)};return true;
		}
		void camera_geometry(void* centity,void* object,float* origin,float* output_axis=nullptr)
		{
			{const std::lock_guard lock(mutex);++camera_calls;}
			const auto reject=[](const char* message){const std::lock_guard lock(mutex);camera_reason=message;};
			if (!installed || !alive || !centity || !object || !origin || !head_pose_bridge::get_status().enabled)
			{reject("camera runtime inactive");return;}
			const int entity=read<std::uint16_t>(centity,0x1b4);
			const auto assembly=mounted_model(object);const auto* profile=assembly.profile;
			if(!profile || !predicted(entity,profile->kind)){reject("predicted entity flags/owner rejected");return;}
			if (!owns_hands(object)) {reject("current owner DObj/model rejected");return;}
			const auto* model=assembly.gun;
			// Resolve asset names, never session-specific script-string IDs or bone indices.
			const std::array names{profile->root,profile->gun,profile->left_button,profile->right_button,profile->seat,profile->pitch,profile->yaw};
			std::array<int,7> bones{};bones.fill(-1);
			for (unsigned b=0;b<model->numBones;++b)
			{
				const auto* name=game::SL_ConvertToString(model->boneNames[b]);if (!name) continue;
				for (unsigned n=0;n<names.size();++n) if (!names[n].empty() && name==names[n])
				{if (bones[n]>=0) {reject("ambiguous turret tag");return;}bones[n]=int(b);}
			}
			for(unsigned i=0;i<bones.size();++i)if(!names[i].empty() && bones[i]<0){reject("required turret tag missing");return;}
			// tag_cover is this profile's root, above both native aim controllers.
			if (profile==&suburban && bones[0]>=model->numRootBones) {reject("seat root is an aiming descendant");return;}
			geometry g;g.entity=entity;g.profile=profile;g.root_bone=bones[0];g.at=clock::now();
			if (!tag_matrix(centity,object,model->boneNames[bones[0]],g.root) ||
				!tag_matrix(centity,object,model->boneNames[bones[1]],g.gun)) {reject("root/gun world tag rejected");return;}
			for (int h=0;h<2;++h) if (bones[2+h]>=0 && !tag_matrix(centity,object,model->boneNames[bones[2+h]],g.buttons[h])) {reject("grip world tag rejected");return;}
			const auto bind=[&](int b) {const auto& m=model->baseMat[b];return anchor{{m.trans[0],m.trans[1],m.trans[2]},normalize({m.quat[0],m.quat[1],m.quat[2],m.quat[3]})};};
			const auto seat=compose(g.root,compose(inverse(bind(bones[0])),bind(bones[4])));
			if (!finite(seat.position) || length(sub(seat.position,g.root.position))>200.f) {reject("stable seat transform rejected");return;}
			g.seat=seat;
			// The horizontal joint is at the base; pitch rotates about the raised
			// gun joint. Using j_mg for both reverses horizontal hand response.
			anchor pitch_joint,yaw_joint;
			if (!tag_matrix(centity,object,model->boneNames[bones[5]],pitch_joint) ||
				!tag_matrix(centity,object,model->boneNames[bones[6]],yaw_joint)) {reject("aim joints unavailable");return;}
			g.pivot=compose(inverse(g.root),pitch_joint).position;
			g.yaw_pivot=compose(inverse(g.root),yaw_joint).position;
			// Suburban's tag_player is under the aiming gun and needs rebasing.
			// Blackhawk's tag_player is a PLAYER base, with native rotated view
			// height added above it. Lower that height to one head above the neutral
			// gun. Use bind geometry so aiming/barrel animation cannot move the eye.
			if(profile==&suburban)std::copy(seat.position.begin(),seat.position.end(),origin);
			else if(profile==&blackhawk)
			{
				const auto* ps=game::CG_GetPredictedPlayerState(0);
				const auto up=rotate(g.root.rotation,{0,0,1});
				const auto local_gun=rotate(conjugate(bind(bones[0]).rotation),sub(bind(bones[1]).position,bind(bones[4]).position));
				const float desired=local_gun[2]+blackhawk_eye_clearance_meters*head_pose_bridge::get_status().world_scale;
				if(ps && std::isfinite(desired) && std::isfinite(ps->viewHeightCurrent))
					for(unsigned i=0;i<3;++i)origin[i]+=up[i]*(desired-ps->viewHeightCurrent);
			}
			if (output_axis)
			{
				const std::array<vec,3> axis{rotate(seat.rotation,{1,0,0}),rotate(seat.rotation,{0,1,0}),rotate(seat.rotation,{0,0,1})};
				std::memcpy(output_axis,axis.data(),sizeof(axis));
			}
			// Resolve the authored hand submodel, including the Blackhawk interior
			// between the gun and hands. Never use the personal weapon's handle 4000.
			std::array<anchor,2> wrists{};bool have_wrists=false;
			if (!hands_active())
			{
				const auto* hands_model=assembly.hand;
				if (!hands_model->boneNames) return;
				std::array<unsigned,2> tags{};
				for (unsigned b=0;b<hands_model->numBones;++b)
				{
					const auto* name=game::SL_ConvertToString(hands_model->boneNames[b]);if (!name) continue;
					for (unsigned h=0;h<2;++h) if (!std::strcmp(name,h ? "j_wrist_ri" : "j_wrist_le"))
					{if (tags[h]) return;tags[h]=hands_model->boneNames[b];}
				}
				have_wrists=tag_matrix(centity,object,tags[0],wrists[0]) && tag_matrix(centity,object,tags[1],wrists[1]);
			}
			if(profile==&blackhawk && have_wrists)g.buttons=wrists;
			{const std::lock_guard lock(mutex);rendered=g;++camera_updates;camera_reason="stable seat applied";}
			if(have_wrists)observe_wrists(wrists);
		}
		int origin_stub(void* centity,void* object,unsigned tag,float* out)
		{
			const auto result=utils::hook::invoke<int>(0x140370FC0,centity,object,tag,out);
			if (result) camera_geometry(centity,object,out);return result;
		}
		int matrix_stub(void* centity,void* object,unsigned tag,float* axis,float* out)
		{
			const auto result=utils::hook::invoke<int>(0x140370E10,centity,object,tag,axis,out);
			if (result) camera_geometry(centity,object,out,axis);return result;
		}
		controls advance_control(game::gentity_s* mount,const mount_profile* profile,const void* ps,
			std::array<float,2> native_angles,std::array<float,2> low,std::array<float,2> high,const controller_input::frame& input)
		{
			const auto entity=read<std::uint16_t>(mount,0x8c);
			const auto key=weapons::native_carry::entity_key(entity);
			const int time=read<int>(ps,0x4c);
			if (profile!=last_profile || entity!=last_entity || key.generation!=last_generation || time<last_time || !fresh(latest().at))
			{
				control.reset(native_angles);++mount_instance;
				const std::lock_guard lock(mutex);authored={};
			}
			last_entity=entity;last_generation=key.generation;last_time=time;last_profile=profile;
			geometry g;calibration a;{const std::lock_guard lock(mutex);g=rendered;a=authored;}
			head_pose_bridge::spatial_frame body;
			const auto* paused=game::Dvar_FindVar("cl_paused");
			controls c;
			c.valid=paused && !paused->current.integer && !*game::keyCatchers && !(read<unsigned>(ps,0xe908)&4u) && input.focused && fresh(input.sampled_at) &&
				g.profile==profile && g.entity==entity && fresh(g.at) && head_pose_bridge::get_spatial_frame(body) && fresh(body.captured_at) && body.generation==input.reference_generation;
			c.calibrated=a.valid && a.entity==entity && a.generation==key.generation;
			c.low=low;c.high=high;
			if(c.valid)track_controls(c,input,g,a,body);
			control.update(input,c);return c;
		}
		void publish_control(const mount_profile* profile,const controller_input::frame& input,const controls& c,std::array<float,2> native_angles)
		{
			const std::lock_guard lock(mutex);published={last_entity,last_generation,input.reference_generation,control.native_presentation(native_angles),
				c,clock::now(),profile,mount_instance,control.angles};++updates;
			reason=!c.valid ? "mounted input/geometry unavailable" : !c.calibrated ? "waiting for authored turret wrists" : control.free_hands ? "mounted hands active" : "squeeze and release to take over";
		}
		void aim_stub(game::gentity_s* turret,game::gentity_s* player)
		{
			const auto original=[&]{aim_hook.invoke<void>(turret,player);};
			if (!alive || !installed || player!=&game::g_entities[0] || supported(turret)!=&suburban) {original();return;}
			auto* ps=read<std::byte*>(player,0x118);
			const auto entity=read<std::uint16_t>(turret,0x8c);
			if (!ps || entity<=0 || entity>=4000 || !attached(read<unsigned>(ps,0x58)) ||
				read<std::uint16_t>(ps,0x1e)!=entity || read<std::uint16_t>(turret,0x10c)!=1 || !head_pose_bridge::get_status().enabled)
			{original();return;}
			const auto* native=read<const std::byte*>(turret,0x138);
			if (!native) {original();return;}
			const auto input=controller_input::latest_interaction();
			const auto c=advance_control(turret,&suburban,ps,read<std::array<float,2>>(turret,0x58),
				read<std::array<float,2>>(native,0xc),read<std::array<float,2>>(native,0x14),input);
			const auto saved=read<std::array<float,2>>(ps,0x108);
			const auto base=read<std::array<float,2>>(turret,0x100);
			const std::array<float,2> desired{base[0]+control.angles[0],base[1]+control.angles[1]};
			if (!std::isfinite(desired[0]) || !std::isfinite(desired[1])) {original();return;}
			// This native function only normalizes/clamps turret angles and updates
			// its ownership/animation flags. Its sole calls are floor(). Restore the
			// player's view immediately; neither command angles nor HMD history change.
			write(ps,0x108,desired);
			{const auto restore=gsl::finally([&]{write(ps,0x108,saved);});original();}
			control.angles=read<std::array<float,2>>(turret,0x58);
			// Final server boundary also rejects stale/invalid grip fire, including
			// a native mouse bit left over from the preceding usercmd.
			if (!c.valid || !control.firing(input)) write(ps,0xe90c,read<unsigned>(ps,0xe90c)&~1u);
			publish_control(&suburban,input,c,control.angles);
		}
		std::byte* vehicle_player(const game::gentity_s* vehicle)
		{
			if(!alive || !installed || !vehicle || !head_pose_bridge::get_status().enabled || supported(vehicle)!=&blackhawk)return nullptr;
			auto* ps=read<std::byte*>(&game::g_entities[0],0x118);
			const unsigned entity=read<std::uint16_t>(vehicle,0x8c);
			return ps && !player_life::dead(ps) && entity>0 && entity<4000 && read<std::uint16_t>(vehicle,0x10c)==1 &&
				attached(read<unsigned>(ps,0x58),mount_kind::blackhawk) && read<std::uint16_t>(ps,0x1e)==entity ? ps : nullptr;
		}
		bool vehicle_world_angles(game::gentity_s* vehicle,vec& angles)
		{
			geometry g;{const std::lock_guard lock(mutex);g=rendered;}
			const auto* native=read<const std::byte*>(vehicle,0x130);
			if(g.profile!=&blackhawk || g.entity!=read<std::uint16_t>(vehicle,0x8c) || !fresh(g.at) || g.root_bone<0 || g.root_bone>=55 ||
				!native || read<int>(native,0x620)!=g.root_bone)return false;
			// G_DObjGetWorldBoneMatrix uses the current server vehicle transform.
			// A previous rendered world position must not drag a fast-moving gun.
			std::array<vec,4> matrix{};
			utils::hook::invoke<void>(0x1405161D0,vehicle,g.root_bone,matrix.data());
			const std::array<vec,3> root{matrix[0],matrix[1],matrix[2]};if(!valid_offset_axis(root))return false;
			const float pitch=control.angles[0]*.00872664625997f,yaw=control.angles[1]*.00872664625997f;
			const auto rotation=normalize(multiply(from_axis(root),multiply(quat{0,0,std::sin(yaw),std::cos(yaw)},quat{0,std::sin(pitch),0,std::cos(pitch)})));
			const std::array<vec,3> axis{rotate(rotation,{1,0,0}),rotate(rotation,{0,1,0}),rotate(rotation,{0,0,1})};
			static_assert(sizeof(axis) == 9 * sizeof(float));
			game::AxisToAngles(reinterpret_cast<const float(*)[3]>(axis.data()),angles.data());return finite(angles);
		}
		void vehicle_target_stub(game::gentity_s* vehicle)
		{
			const auto original=[&]{utils::hook::invoke<void>(0x1406BCCB0,vehicle);};
			auto* ps=vehicle_player(vehicle);const auto* native=read<const std::byte*>(vehicle,0x130);
			if(!ps || !native){original();return;}
			const auto* definition=utils::hook::invoke<const game::VehicleDef*>(0x1406BEEF0,read<unsigned>(native,0x270));
			if(!definition){original();return;}
			static_assert(offsetof(game::VehicleDef,turretHorizSpanLeft)==0x590 && offsetof(game::VehicleDef,turretVertSpanUp)==0x598);
			const auto input=controller_input::latest_interaction();
			const auto c=advance_control(vehicle,&blackhawk,ps,read<std::array<float,2>>(vehicle,0x6c),
				{-definition->turretVertSpanUp,-definition->turretHorizSpanRight},{definition->turretVertSpanDown,definition->turretHorizSpanLeft},input);
			// Native vehicle notification reads the current usercmd at e860;
			// script attackbuttonpressed reads e90c|e918. Gate all three witnesses.
			if(!c.valid || !control.firing(input))for(const auto offset:{0xe860,0xe90c,0xe918})write(ps,offset,read<unsigned>(ps,offset)&~1u);
			vec desired{};const auto saved=read<vec>(ps,0x108);
			if(vehicle_world_angles(vehicle,desired))write(ps,0x108,desired);
			{const auto restore=gsl::finally([&]{write(ps,0x108,saved);});original();}
			publish_control(&blackhawk,input,c,read<std::array<float,2>>(vehicle,0x6c));
		}
		void vehicle_aim_stub(game::gentity_s* vehicle)
		{
			const auto original=[&]{utils::hook::invoke<void>(0x1406BBF70,vehicle);};
			auto* ps=vehicle_player(vehicle);const auto p=latest();
			if(!ps || p.profile!=&blackhawk || p.entity!=read<std::uint16_t>(vehicle,0x8c) || !fresh(p.at)){original();return;}
			vec desired{};const auto saved=read<vec>(ps,0x108);
			if(vehicle_world_angles(vehicle,desired))write(ps,0x108,desired);
			{const auto restore=gsl::finally([&]{write(ps,0x108,saved);});original();}
			// The eye-ray/barrel convergence changes pitch even with neutral input.
			// Its solved angles belong to presentation, never the next user target:
			// feeding them into control would accumulate that correction every tick.
			const auto presentation=control.native_presentation(read<std::array<float,2>>(vehicle,0x6c));
			const std::lock_guard lock(mutex);published.state=presentation;published.at=clock::now();
		}
		template<std::size_t N> bool verify(std::uintptr_t address,const std::array<std::uint8_t,N>& bytes)
		{std::array<std::uint8_t,N> mask{};mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes.data(),mask.data(),N}));}
		bool call(std::uintptr_t address,std::uintptr_t target)
		{const auto* p=reinterpret_cast<const std::byte*>(address);return p[0]==std::byte{0xe8} && address+5+read<std::int32_t>(p,1)==target;}
	}
	bool active() noexcept {return admitted(latest());}
	bool hands_active() noexcept {const auto p=latest();return admitted(p) && p.state.free_hands;}
	game_view::camera_request camera_request() noexcept {return request_for_mount(latest());}
	camera_view prepare_camera(float* origin,float (*axis)[3]) noexcept
	{
		const auto p=latest();if(!origin || !axis || !camera_owned(p))return {};
		auto* object=vr::h2::sp::client_entity_dobj(p.entity,0);
		if(mounted_model(object).profile!=p.profile)return {};
		// Blackhawk's default vehicle-camera mode skips CG_VehicleView. Use the
		// existing final native-camera boundary, before HMD composition, in every
		// vehicle camera mode. The native flight/animation still supplies the seat.
		vec seat{origin[0],origin[1],origin[2]};std::array<vec,3> basis{};
		camera_geometry(reinterpret_cast<void*>(0x141C328F0ull+std::uintptr_t(p.entity)*0x200),object,seat.data(),basis[0].data());
		if(!valid_offset_axis(basis))return {};
		if(p.profile==&suburban)
		{
			geometry g;calibration a;view_publication previous;
			{const std::lock_guard lock(mutex);g=rendered;a=authored;previous=rendered_view;}
			const auto input=controller_input::latest_interaction();const auto now=clock::now();
			const auto frame_time=game::CG_GetGameTime(0);
			head_pose_bridge::spatial_frame body;auto c=p.sampled;
			const auto* paused=game::Dvar_FindVar("cl_paused");
			c.valid=c.valid && fresh(p.at) && g.profile==p.profile && g.entity==p.entity && fresh(g.at) &&
				input.reference_generation==p.reference && head_pose_bridge::get_spatial_frame(body) &&
				fresh(body.captured_at) && body.generation==p.reference;
			if(c.valid)track_controls(c,input,g,a,body);
			const bool projecting=paused && !paused->current.integer && !*game::keyCatchers && c.valid &&
				p.state.free_hands && input.focused && !input.orientation_settling && fresh(input.sampled_at);
			const auto pose=projecting && previous.instance==p.instance && previous.reference==p.reference && previous.frame_time==frame_time ?
				previous.pose : camera_motion.update(p.state,c,p.instance,input,projecting,now);
			const auto orbit=orbit_seat(g.root,g.seat,g.yaw_pivot,pose.yaw);
			seat=orbit.position;
			basis={rotate(orbit.rotation,{1,0,0}),rotate(orbit.rotation,{0,1,0}),rotate(orbit.rotation,{0,0,1})};
			// The later skeleton/skin refresh uses this exact camera projection,
			// rather than projecting again at another time or XR sample.
			const std::lock_guard lock(mutex);rendered_view={p.instance,p.reference,pose,now,frame_time};
		}
		camera_view view;view.request=request_for_mount(p);
		static_assert(sizeof(basis) == 9 * sizeof(float));
		game::AxisToAngles(reinterpret_cast<const float(*)[3]>(basis.data()),view.angles.data());
		std::copy(seat.begin(),seat.end(),origin);std::memcpy(axis,basis.data(),sizeof(basis));return view;
	}
	bool owns_model(const void* object) noexcept
	{return installed && alive && head_pose_bridge::get_status().enabled && game::CL_IsCgameInitialized() && mounted_model(object).profile;}
	bool owns_hands(const void* object) noexcept
	{
		if (!installed || !alive || !head_pose_bridge::get_status().enabled || !game::CL_IsCgameInitialized()) return false;
		const auto* ps=game::CG_GetPredictedPlayerState(0);
		const auto assembly=mounted_model(object);
		if (!ps || !assembly.profile || !attached(ps->e_flags,assembly.profile->kind)) return false;
		const int entity=read<std::uint16_t>(ps,0x1e);
		return entity>0 && entity<4000 && object &&
			object==vr::h2::sp::client_entity_dobj(entity,0);
	}
	void apply_pose(void* object,bool rebuilt) noexcept
	{
		if(!owns_model(object))return;const auto p=latest();
		const auto assembly=mounted_model(object);
		// The server's Blackhawk also carries viewhands. Its native tag queries
		// must not seed or modify the client's cached presentation skeleton.
		if(assembly.profile==&blackhawk && !owns_hands(object))return;
		// Only the caller's locked DObj is accessed. Publications are copied with
		// short locks; neither tag queries nor native calls occur under that mutex.
		struct cache
		{
			const game::XModel* model{},*gun_model{};const game::DObjAnimMat* bind{};
			rig layout{};std::array<quat,2> basis{};bool valid{};
			std::array<bone,256> hand_bind{};vec head_anchor{};
			model_pose gun{};std::array<const game::XSurface*,8> hidden{};
			std::array<bone,256> source{};unsigned count{};
			void* object{};bone* matrices{};std::uint32_t timestamp{};
			std::uint64_t generation{},reference{},sequence{},asset_epoch{};
			clock::time_point camera_at{},owner_at{};vec view_offset{};
		};
		// Native render workers can request the same DObj in one epoch. Share
		// the completion key so stereo/partial queries never apply IK twice.
		const std::lock_guard hand_lock(hand_mutex);
		static cache cached;
		const auto report=[](const char* text){const std::lock_guard lock(mutex);hand_reason=text;};
		const auto* model=assembly.hand;const auto* gun_model=assembly.gun;
		if (cached.model!=model || cached.bind!=model->baseMat || cached.gun_model!=gun_model || cached.asset_epoch!=asset_epoch.load())
		{
			cached={};cached.model=model;cached.bind=model->baseMat;cached.gun_model=gun_model;cached.asset_epoch=asset_epoch.load();
			std::array<bone_definition,256> gun_bones{};
			if(!gun_model->parentList)return;
			for (unsigned b=0;b<gun_model->numBones;++b)
			{
				const auto* name=game::SL_ConvertToString(gun_model->boneNames[b]);
				if(!name)return;gun_bones[b].name=name;
				gun_bones[b].parent=b<gun_model->numRootBones?-1:int(b)-gun_model->parentList[b-gun_model->numRootBones];
				std::memcpy(&gun_bones[b].bind,&gun_model->baseMat[b],sizeof(bone));
			}
			cached.gun=resolve_model_pose({gun_bones.data(),gun_model->numBones},assembly.profile->kind);
			if(!cached.gun.valid){report("turret presentation hierarchy rejected");return;}
			unsigned hidden{};
			if(!assembly.profile->hidden_material.empty())for(const auto& lod:gun_model->lodInfo)for(unsigned n=0;lod.surfs && n<lod.numsurfs && n<gun_model->numsurfs;++n)
			{
				if(lod.surfIndex+n>=gun_model->numsurfs || !gun_model->materialHandles)continue;
				const auto* material=gun_model->materialHandles[lod.surfIndex+n];
				if(!material || !material->name || strnlen_s(material->name,128)>=128 || std::string_view(material->name)!=assembly.profile->hidden_material || (lod.surfs[n].flags&game::SURF_FLAG_SKINNED))continue;
				const auto* surface=&lod.surfs[n];if(std::find(cached.hidden.begin(),cached.hidden.end(),surface)!=cached.hidden.end())continue;
				if(hidden==cached.hidden.size()){report("turret shield surface bound rejected");return;}cached.hidden[hidden++]=surface;
			}
			if(!model->baseMat || !model->boneNames || !model->numBones || !model->numRootBones ||
				model->numRootBones>model->numBones || (model->numBones>model->numRootBones && !model->parentList))
			{report("attached hand model data unavailable");}
			else
			{
				std::array<bone_definition,256> bones{};
				int head=-1;
				for (int b=0;b<model->numBones;++b)
				{
					const auto* name=game::SL_ConvertToString(model->boneNames[b]);if (!name) return;
					bones[b].name=name;bones[b].parent=b<model->numRootBones ? -1 : b-model->parentList[b-model->numRootBones];
					std::memcpy(&bones[b].bind,&model->baseMat[b],sizeof(bone));
					cached.hand_bind[b]=bones[b].bind;cached.hand_bind[b].weight=2.f;
					if(!std::strcmp(name,"tag_view")){if(head>=0)return;head=b;}
				}
				const model_definition descriptor{model->name ? model->name : "",0,model->numBones};
				const auto resolved=resolve_rig({&descriptor,1},{bones.data(),model->numBones},rig_kind::hands_only);
				if (resolved.rejection) report(resolved.rejection);
				else if(head>=0)
				{
					cached.layout=resolved.layout;cached.valid=true;
					cached.head_anchor=bones[head].bind.position;
					const auto basis=conjugate(normalize(bones[cached.layout.weapon_tag].bind.rotation));
					for (unsigned h=0;h<2;++h) cached.basis[h]=normalize(multiply(basis,bones[cached.layout.arms[h].wrist].bind.rotation));
				}
			}
		}
		if (!cached.gun.valid) return;
		auto* matrices=read<bone*>(object,0xa8);const auto timestamp=read<std::uint32_t>(object,0xb0);
		if (!matrices) return;
		for (unsigned b=0;b<read<std::uint8_t>(object,0x10);++b)
			if (!(read<std::uint32_t>(object,0x80+(b/32)*4)&(0x80000000u>>(b%32)))) return;
		const auto input=controller_input::latest_interaction();head_pose_bridge::spatial_frame body;
		const bool source_changed=rebuilt || cached.object!=object || cached.matrices!=matrices || cached.timestamp!=timestamp;
		if(source_changed)
		{
			cached.count=read<std::uint8_t>(object,0x10);std::copy_n(matrices,cached.count,cached.source.begin());
			cached.object=object;cached.matrices=matrices;cached.timestamp=timestamp;cached.sequence=0;
		}
		const bool spatial=head_pose_bridge::get_spatial_frame(body);
		const auto* view=*reinterpret_cast<const std::byte* const*>(0x141E39D30);
		const auto offset=view?read<vec>(view,0x58):vec{};
		// Camera tag queries can solve this world DObj before this frame's HMD
		// camera is composed. A later skin query must refresh the same input sample.
		if(!source_changed && cached.sequence==input.sequence && cached.generation==p.generation && cached.reference==input.reference_generation &&
			cached.camera_at==body.captured_at && cached.owner_at==p.at && cached.view_offset==offset)return;
		cached.sequence=input.sequence;cached.generation=p.generation;cached.reference=input.reference_generation;
		cached.camera_at=body.captured_at;cached.owner_at=p.at;cached.view_offset=offset;
		std::copy_n(cached.source.begin(),cached.count,matrices);
		const auto* paused=game::Dvar_FindVar("cl_paused");
		const bool tracked=admitted(p) && p.state.free_hands && owns_hands(object) && input.focused && paused && !paused->current.integer && !*game::keyCatchers &&
			fresh(input.sampled_at) && input.reference_generation==p.reference && spatial && view && fresh(body.captured_at) && body.generation==p.reference;
		auto angles=p.state.angles;
		calibration a;
		if(tracked)
		{
			geometry g;view_publication display;{const std::lock_guard lock(mutex);g=rendered;a=authored;display=rendered_view;}
			auto c=p.sampled;c.valid=c.valid && g.entity==p.entity && fresh(g.at);
			if(c.valid)track_controls(c,input,g,a,body);
			angles=p.profile==&suburban && display.instance==p.instance && display.reference==p.reference && fresh(display.at) ?
				display.pose.aim : p.state.displayed_angles(input,c);
		}
		if(!project_model_pose(cached.gun,{cached.source.data(),size_t(cached.gun.layout.count)},
			{matrices,size_t(cached.gun.layout.count)},angles,tracked))return;
		++model_pose_updates;
		if(!assembly.profile->hidden_material.empty() && weapons::viewmodel_visibility::ready(weapons::part_visibility::rigid_groups))
		{weapons::viewmodel_visibility::publish(object,matrices,timestamp,{},weapons::part_visibility::rigid_groups,-1,cached.hidden);++shield_publications;}
		if(!tracked || !cached.valid)return;
		const auto setting=[](const char* name,float fallback){const auto* d=game::Dvar_FindVar(name);return d ? d->current.value : fallback;};
		shoulder_offsets shoulders_config;
		shoulders_config={setting("vr_shoulderHalfWidth",shoulders_config.half_width_meters),setting("vr_shoulderDown",shoulders_config.down_meters),setting("vr_shoulderBack",shoulders_config.back_meters)};
		std::array<vec,2> shoulders;if (!make_shoulders(body,offset,shoulders_config,shoulders)) return;
		position_offsets hand_offsets;
		hand_offsets={setting(vr::settings::active_hand_alignment()[0].name,hand_offsets.inward_meters),setting(vr::settings::active_hand_alignment()[1].name,hand_offsets.back_meters),setting(vr::settings::active_hand_alignment()[2].name,hand_offsets.up_meters)};
		std::array<anchor,2> targets;
		for (unsigned h=0;h<2;++h)
		{
			head_pose_bridge::world_pose grip,aim;
			if (input.grip[h].valid && input.aim[h].valid && head_pose_bridge::tracking_to_world(body,input.grip[h].tracking,grip) &&
				head_pose_bridge::tracking_to_world(body,input.aim[h].tracking,aim) && tracked_wrist(input,body,offset,h,hand_offsets,targets[h]))
				targets[h].rotation=normalize(multiply(targets[h].rotation,cached.basis[h]));
			else targets[h]={add(shoulders[h],vec{0,0,-.35f*body.units_per_meter}),cached.basis[h]};
		}
		// Match the gun in THIS completed skeleton. The camera publication is
		// produced later and therefore contains the preceding frame here.
		const auto& current_gun=matrices[cached.gun.gun];
		const unsigned held=a.valid && a.entity==p.entity && a.generation==p.generation?p.state.held_hands(input):0;
		for(unsigned h=0;h<2;++h)if(held&(1u<<h))targets[h]=compose(as_anchor(current_gun),a.wrists[h]);
		const auto& layout=cached.layout;auto* hand_matrices=matrices+assembly.hand_offset;
		std::array<bone,256> rest{};
		if(!seed_arm_pose(layout,{cached.hand_bind.data(),size_t(layout.count)},cached.head_anchor,sub(body.head_position,offset),
			{cached.source.data()+assembly.hand_offset,size_t(layout.count)},held,{rest.data(),size_t(layout.count)}))return;
		std::array<bone,256> solved{};std::array<bool,2> limited{};
		if (!solve_arms(layout,{rest.data(),std::size_t(layout.count)},targets,shoulders,body.head_yaw_axis,
			{solved.data(),std::size_t(layout.count)},limited)) {report("attached arm solver rejected pose");return;}
		// Arm IK uses the same projected gun pose; native fire state stays server-owned.
		for (int b=0;b<layout.count;++b) if (arm_bone(layout,b)) hand_matrices[b]=solved[b];
		const std::lock_guard lock(mutex);++hand_applications;hand_reason="attached turret arms controlled";
	}
	void prepare_skin(void* object,const hands::bone* matrices) noexcept
	{
		// Blackhawk is a world/vehicle DObj: a completed scene skeleton can be
		// reused until skin dispatch even after the camera has advanced. Refresh
		// at the existing final consumer, before any CPU/GPU skin job is queued.
		if(!matrices || !owns_hands(object) || mounted_model(object).profile!=&blackhawk)return;
		utils::hook::invoke<void>(0x140659010,object);
		const auto unlock=gsl::finally([&]{utils::hook::invoke<void>(0x1406596C0,object);});
		if(read<const bone*>(object,0xa8)!=matrices){++skin_rejections;return;}
		std::uint64_t before{};{const std::lock_guard lock(mutex);before=hand_applications;}
		apply_pose(object,false);++skin_preparations;
		{const std::lock_guard lock(mutex);if(hand_applications!=before)++skin_pose_changes;}
	}
	bool command(const controller_input::frame& input,bool gameplay,int& buttons) noexcept
	{
		const auto p=latest();if (!admitted(p)) return false;
		buttons&=~1;
		if(p.profile==&blackhawk && (!gameplay || !p.state.held_hands(input)))buttons&=~game::BUTTON_ADS;
		if (gameplay && fresh(input.sampled_at) && input.reference_generation==p.reference && p.state.firing(input)) buttons|=1;
		return true;
	}
	void observe_wrists(const std::array<anchor,2>& world) noexcept
	{
		const auto p=latest();if (!admitted(p) || p.state.free_hands) return;
		geometry g;{const std::lock_guard lock(mutex);if (authored.valid && authored.entity==p.entity && authored.generation==p.generation) return;g=rendered;}
		if (g.entity!=p.entity || !fresh(g.at)) return;
		calibration a;a.entity=p.entity;a.generation=p.generation;a.valid=true;
		for (unsigned h=0;h<2;++h)
		{
			if (!finite(world[h].position) || length(sub(world[h].position,g.buttons[h].position))>12.f) return;
			float norm{};for (const float x:world[h].rotation) {if (!std::isfinite(x)) return;norm+=x*x;}
			if (std::abs(norm-1.f)>.02f) return;
			a.wrists[h]=compose(inverse(g.gun),world[h]);
		}
		const std::lock_guard lock(mutex);authored=a;++wrist_captures;
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			fastfiles::on_pre_unload([]{++asset_epoch;});
			const std::array<std::uintptr_t,3> vehicle_sites{0x14038D083,0x1406C2A29,0x1406C2A31};
			const std::array<std::uintptr_t,3> vehicle_targets{0x14038D2E0,0x1406BCCB0,0x1406BBF70};
			const std::array<void*,3> vehicle_stubs{reinterpret_cast<void*>(vehicle_controller_stub),
				reinterpret_cast<void*>(vehicle_target_stub),reinterpret_cast<void*>(vehicle_aim_stub)};
			for(unsigned i=0;i<vehicle_sites.size();++i)
				if(!call(vehicle_sites[i],vehicle_targets[i]) || utils::hook::is_relatively_far(reinterpret_cast<void*>(vehicle_sites[i]),vehicle_stubs[i]))
				{console::error("[VR turret] Blackhawk native call contract rejected\n");return;}
			// Saved disassembly proves the aim ABI and both existing camera tag-call ABIs.
			if (!verify(0x140534250,std::array<std::uint8_t,15>{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30}) ||
				!verify(0x14053428A,std::array<std::uint8_t,8>{0xf3,0x0f,0x10,0xb3,0x08,0x01,0,0}) ||
				!verify(0x1405342E4,std::array<std::uint8_t,8>{0xf3,0x0f,0x10,0xb3,0x0c,0x01,0,0}) ||
				!call(0x140537669,0x140534250) || !call(0x1403ABDA7,0x140370FC0) || !call(0x1403ABD80,0x140370E10) ||
				!call(0x14038D093,0x14038D100) || !call(0x1407763E9,0x140776F10) || !call(0x140776EF5,0x140776F10) ||
				!call(0x14075ECF4,0x140659010) || !call(0x14075ED07,0x14038C9E0) || !call(0x14075ED12,0x1406596C0) ||
				!verify(0x14038C9E0,std::array<std::uint8_t,10>{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10}) ||
				!verify(0x1405161D0,std::array<std::uint8_t,10>{0x48,0x8b,0xc4,0x48,0x89,0x58,0x20,0x55,0x56,0x57}) ||
				!verify(0x140659010,std::array<std::uint8_t,10>{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20}) ||
				!verify(0x1406596C0,std::array<std::uint8_t,8>{0xc7,0x41,0x18,0,0,0,0,0xc3}) ||
				!verify(0x1406BCD7B,std::array<std::uint8_t,8>{0x41,0xf6,0x86,0x60,0xe8,0,0,1}) ||
				!verify(0x14038D405,std::array<std::uint8_t,8>{0x45,0x0f,0xb6,0x87,0x87,0,0,0}) ||
				!verify(0x140776F10,std::array<std::uint8_t,14>{0x40,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x78}) ||
				!verify(0x14038D100,std::array<std::uint8_t,18>{0x4c,0x8b,0xdc,0x55,0x56,0x57,0x41,0x56,0x49,0x8d,0x6b,0xb8,0x48,0x81,0xec,0x28,0x01,0x00}))
			{console::error("[VR turret] Native contracts rejected\n");return;}
			for (const auto target:{reinterpret_cast<void*>(origin_stub),reinterpret_cast<void*>(matrix_stub)})
				if (utils::hook::is_relatively_far(reinterpret_cast<void*>(0x1403ABD80),target)) return;
			if (utils::hook::is_relatively_far(reinterpret_cast<void*>(0x14038D093),reinterpret_cast<void*>(client_controller_stub))) return;
			if (utils::hook::is_relatively_far(reinterpret_cast<void*>(0x14075ED07),reinterpret_cast<void*>(render_pose_stub))) return;
			aim_hook.create(0x140534250,aim_stub);
			scene_hook.create(0x140776F10,scene_stub);
			utils::hook::call(0x1403ABDA7,origin_stub);utils::hook::call(0x1403ABD80,matrix_stub);
			utils::hook::call(0x14038D093,client_controller_stub);
			utils::hook::call(0x14075ED07,render_pose_stub);
			for(unsigned i=0;i<vehicle_sites.size();++i)utils::hook::call(vehicle_sites[i],vehicle_stubs[i]);
			installed=true;
			// Attachment can end without another turret aim callback. Invalidate on
			// the existing server scheduler so reusing the same turret requires a new takeover.
			scheduler::loop([] {
				const auto* ps=game::g_entities[0].client;
				if (last_entity>=0 && (!ps || !last_profile || !attached(read<unsigned>(ps,0x58),last_profile->kind) || read<std::uint16_t>(ps,0x1e)!=last_entity ||
					(last_profile==&blackhawk && player_life::dead(ps))))
				{last_entity=-1;last_generation=0;last_profile=nullptr;control.reset();const std::lock_guard lock(mutex);published={};authored={};reason="turret ownership ended";}
			},scheduler::pipeline::server);
			::command::add("vr_turret_status",[] {
				std::ostringstream out;{const std::lock_guard lock(mutex);
				out<<"installed="<<installed<<" entity="<<published.entity<<" state="<<reason<<" updates="<<updates<<" camera_updates="<<camera_updates
					<<" profile="<<(published.profile?published.profile->weapon:std::string_view{"none"})
					<<" wrist_captures="<<wrist_captures<<" calibrated="<<authored.valid<<" free_hands="<<published.state.free_hands
					<<" client_updates="<<client_updates<<" hand_applications="<<hand_applications<<" hand_state="<<hand_reason
					<<" camera_calls="<<camera_calls<<" camera_state="<<camera_reason<<" client_calls="<<client_calls
					<<" world_depth_submissions="<<world_depth_submissions.load()
					<<" model_pose_updates="<<model_pose_updates.load()<<" shield_publications="<<shield_publications.load()
					<<" render_preparations="<<render_preparations.load()
					<<" skin_preparations="<<skin_preparations.load()<<" skin_pose_changes="<<skin_pose_changes.load()<<" skin_rejections="<<skin_rejections.load()
					<<" gripped="<<published.state.gripped<<" fire_armed="<<published.state.fire_armed<<" pitch="<<published.state.angles[0]<<" yaw="<<published.state.angles[1]
					<<" requested_pitch="<<published.requested_angles[0]<<" requested_yaw="<<published.requested_angles[1]
					<<" view_frame="<<rendered_view.frame_time<<" view_yaw="<<rendered_view.pose.yaw<<'\n';}
				const auto text=out.str();console::info("%s",text.c_str());utils::io::write_file_atomic("minidumps/overlord-turret.txt",text);
			});
		}
		void pre_destroy() override {alive=false;}
	};
}
REGISTER_COMPONENT(vr::gameplay::mounted::component)
