#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "notebook_runtime.hpp"
#include "vehicles/runtime.hpp"
#include "campaign/cliffhanger/physical.hpp"
#include "native_scripted_control.hpp"
#include "native_viewmodel_policy.hpp"
#include "native_hide_tags.hpp"
#include "empty_hands_native.hpp"
#include "hands/rig_builder.hpp"
#include "weapon_interaction.hpp"
#include "weapon_carry_runtime.hpp"
#include "viewmodel_visibility.hpp"
#include "component/scheduler.hpp"
#include "component/scene_models.hpp"
#include "../controller_input.hpp"
#include "../head_pose_bridge.hpp"
#include "component/console.hpp"
#include "component/fastfiles.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <atomic>
#include <sstream>

namespace vr::gameplay::hands::empty_native
{
	namespace
	{
		constexpr std::uintptr_t add_view_weapon = 0x1403B49A0;
		constexpr std::uintptr_t create_object = 0x140653180, free_object = 0x1406539D0;
		constexpr std::uintptr_t calc_pose = 0x14038C9E0, submit_viewmodel = 0x140776E10;
		constexpr std::uintptr_t lock_object = 0x140659010, unlock_object = 0x1406596C0;
		constexpr std::uintptr_t config_string = 0x1403C9D60;
		constexpr std::uintptr_t predicted_state = 0x141BB3C30;
		// The native primary viewmodel's scene identity, used only while it is
		// absent. This is not a client DObj handle or inventory slot allocation.
		constexpr unsigned scene_entity = 3999;
		// R_InitGraphicsGlobals receives entCount=4032, ordinaryEnd=3998,
		// none=3999 (0x1403D5A19..7B). Both entity-to-scene tables allocate
		// entCount entries. Use its bounded padding, not live server entity IDs
		// or the empty hands' 3999 entry, for the additional stored skeletons.
		constexpr unsigned stored_scene_begin=4000,scene_entity_capacity=4032;
		static_assert(stored_scene_begin+weapons::carry::visible_instance_capacity<=scene_entity_capacity);
		struct model_entry { game::XModel* model; std::uint32_t tag; std::uint8_t collision; std::byte pad[3]; };
		static_assert(sizeof(model_entry)==16);
		struct alignas(16) object_storage { std::array<std::byte,0x240> bytes{}; };
		struct pose_storage
		{
			std::byte header[0x1c]{};
			vec origin{}, angles{}, previous_origin{}, previous_angles{};
			std::byte tail[0xb8-0x4c]{};
		};
		static_assert(sizeof(pose_storage)==0xb8 && offsetof(pose_storage,origin)==0x1c &&
			offsetof(pose_storage,previous_origin)==0x34);
		object_storage object;
		pose_storage pose;
		utils::hook::detour add_hook;
		utils::hook::detour submit_hook;
		thread_local bool suppress_native{};
		thread_local bool filter_native_firearms{};
		struct firearm_cache {unsigned count{};std::array<const game::XModel*,32> models{};};
		std::array<firearm_cache,8> firearm_models{};size_t firearm_cursor{};
		std::atomic_uint64_t suppressed_firearms{};
		bool firearm_object(const void* candidate_object)
		{
			static_assert(offsetof(game::WeaponDef,weapType)==0x5cc && offsetof(game::WeaponDef,weapClass)==0x5d0 && offsetof(game::WeaponDef,inventoryType)==0x5e0);
			if(!candidate_object)return false;
			unsigned char count{};const game::XModel* const* native_models{};
			const auto* bytes=static_cast<const std::byte*>(candidate_object);
			if(!utils::native_memory::read_bytes(&count,bytes+15,1) || !count || count>32 ||
				!utils::native_memory::read_bytes(&native_models,bytes+0xd8,sizeof(native_models)))return false;
			firearm_cache value;value.count=count;
			if(!utils::native_memory::read_bytes(value.models.data(),native_models,count*sizeof(native_models[0])))return false;
			for(const auto& cached:firearm_models)if(cached.count==count && cached.models==value.models)return true;
			for(unsigned token=1;token<512;++token)
			{
				const auto* definition=game::weapon_defs[token];
				std::array<int,6> shape{};game::XModel* const* gun_models{};const game::XModel* root{};
				// Not every packed weapon index is populated. Validate every read
				// when scanning beyond the currently selected/inventory definitions.
				if(!definition || !utils::native_memory::read_bytes(shape.data(),reinterpret_cast<const std::byte*>(definition)+0x5cc,sizeof(shape)))continue;
				std::array<char,32> device_name{};const char* native_name{};
				if(utils::native_memory::read_bytes(&native_name,definition,sizeof(native_name)))
					(void)utils::native_memory::read_bytes(device_name.data(),native_name,device_name.size()-1);
				if(!personal_firearm_definition(shape[0],shape[1],shape[5],device_name.data()) ||
					!utils::native_memory::read_bytes(&gun_models,reinterpret_cast<const std::byte*>(definition)+offsetof(game::WeaponDef,gunModel),sizeof(gun_models)) ||
					!utils::native_memory::read_bytes(&root,gun_models,sizeof(root)))continue;
				std::array<const game::XModel*,64> pending{},seen{};size_t size=1,visited{};pending[0]=root;
				while(size && visited<seen.size())
				{
					const auto* model=pending[--size];if(!model || std::find(seen.begin(),seen.begin()+visited,model)!=seen.begin()+visited)continue;
					seen[visited++]=model;
					if(std::find(value.models.begin(),value.models.begin()+count,model)!=value.models.begin()+count)
					{firearm_models[firearm_cursor++%firearm_models.size()]=value;return true;}
					game::XModel metadata{};
					if(!utils::native_memory::read_bytes(&metadata,model,sizeof(metadata)))continue;
					if(metadata.numCompositeModels && metadata.numCompositeModels<=32 && metadata.compositeModels && size+metadata.numCompositeModels<=pending.size() &&
						utils::native_memory::read_bytes(pending.data()+size,metadata.compositeModels,metadata.numCompositeModels*sizeof(model)))size+=metadata.numCompositeModels;
				}
			}
			return false;
		}
		struct weapon_resource
		{
			object_storage object;
			pose_storage pose;
			std::atomic_uint32_t weapon{};
			std::atomic_uint64_t generation{};
			std::atomic<const void*> matrices{};
			std::atomic_uint32_t epoch{};
			game::XModel* hands{}, *gun{};
			const void* hands_bind{}, *gun_bind{};
			bool created{}, visible{};
			std::atomic_uint64_t instance{};
			std::atomic<weapons::carry::location> location{weapons::carry::location::absent};
			std::uint32_t visibility_weapon{};
			weapons::part_mask initial_hidden{}, weapon_hidden{};
		};
		std::array<weapon_resource,weapons::carry::visible_instance_capacity> weapon_objects;
		std::atomic<game::XModel*> prepared_hands{};
		std::atomic<const void*> prepared_bind{};
		std::atomic_uint64_t weapon_builds{}, weapon_submissions{}, weapon_rejections{};
		controller_input::clock::time_point prepare_attempt{};
		game::XModel* attempted_hands{};
		std::atomic<const void*> published_object{};
		game::XModel* source{};
		const void* source_bind{};
		bool created{}, visible{};
		std::atomic_bool alive{true}, installed{};
		std::atomic<const void*> solved_matrices{};
		std::atomic_uint32_t solved_epoch{};
		std::atomic_uint64_t submitted{}, builds{}, retired{}, rejected{};
		std::atomic<const char*> reason{"not initialized"};

		template<class T> T read(std::size_t offset)
		{ T result{}; std::memcpy(&result,object.bytes.data()+offset,sizeof(result)); return result; }

		void release(bool all=true)
		{
			// Native zone unload already owns its render/asset barrier. Ordinary
			// model changes explicitly drain rendering before reaching this function.
			published_object=nullptr;
			solved_matrices=nullptr;
			if (created)
			{
				utils::hook::invoke<void>(free_object,&object);
				created=false; ++retired;
			}
			object={}; pose={}; source=nullptr; source_bind=nullptr; visible=false;
			reason="native hand object released";
			// A new empty-hand skin must not retire weapon objects already
			// submitted in this same frontend frame. Each weapon tracks its own skin.
			if(!all)return;
			prepared_hands=nullptr; prepared_bind=nullptr;
			for (auto& v:weapon_objects)
			{
				v.weapon=0; v.matrices=nullptr;
				if (v.created) utils::hook::invoke<void>(free_object,&v.object);
				v.created=v.visible=false; v.hands=v.gun=nullptr;
				v.object={}; v.pose={}; ++v.generation;
			}
		}

		bool valid_model(game::XModel* model)
		{
			if (!model || !model->name || !model->numBones || model->numBones>254 ||
				model->numRootBones!=1 || !model->boneNames || !model->parentList || !model->baseMat ||
				(model->flags&0x400)) return false;
			std::array<bone_definition,256> bones{};
			for (int i=0;i<model->numBones;++i)
			{
				const auto* name=game::SL_ConvertToString(model->boneNames[i]);
				if (!name || strnlen_s(name,128)==128) return false;
				const int distance=i ? model->parentList[i-1] : 0;
				if (i && (distance<=0 || distance>i)) return false;
				bones[i].name=name; bones[i].parent=i ? i-distance : -1;
			}
			const model_definition definition{model->name,0,model->numBones};
			return !resolve_rig({&definition,1},{bones.data(),model->numBones},rig_kind::hands_only).rejection;
		}

		game::XModel* character_model()
		{
			// Same character selector as native CG_UpdateViewWeapon: PS +0x3e is
			// the hand-model configstring index, independent of the selected gun.
			const auto index=*reinterpret_cast<const std::uint16_t*>(predicted_state+0x3e);
			if (!index || index>=2048) return nullptr;
			const auto* name=utils::hook::invoke<const char*>(config_string,index+0xa81);
			if (!name || !*name || strnlen_s(name,256)==256) return nullptr;
			auto* model=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,name,0).model;
			return model && model->name && std::string_view(model->name)==name ? model : nullptr;
		}
		bool owned_requested(head_pose_bridge::spatial_frame& spatial)
		{
			if (!alive || !installed || !game::CL_IsCgameInitialized() || !weapons::carry::active()) return false;
			const auto* ps=reinterpret_cast<const std::byte*>(predicted_state);
			if ((std::to_integer<unsigned>(ps[2])>=2 && std::to_integer<unsigned>(ps[2])<=3) ||
				(*reinterpret_cast<const std::uint32_t*>(ps+0x58)&0x103000)) return false;
			auto* model=prepared_hands.load();
			return model && model==character_model() && prepared_bind.load()==model->baseMat && head_pose_bridge::get_spatial_frame(spatial);
		}
		void prepare_hands()
		{
			if (!alive || !game::CL_IsCgameInitialized()) return;
			auto* model=character_model();
			if (!model || (model==prepared_hands.load() && model->baseMat==prepared_bind.load()) || !valid_model(model)) return;
			const auto now=controller_input::clock::now();
			if (attempted_hands==model && now>=prepare_attempt && now-prepare_attempt<1s) return;
			attempted_hands=model;
			prepare_attempt=now;
			if (!weapons::viewmodel_visibility::prepare_skinned_part(model,"j_shoulder_le",true) ||
				!weapons::viewmodel_visibility::prepare_skinned_part(model,"j_shoulder_ri",true)) return;
			prepared_bind=model->baseMat; prepared_hands=model;
		}
		bool valid_gun(game::XModel* root,unsigned hand_bones)
		{
			if (!root) return false;
			unsigned count=1,bones=hand_bones;
			const auto add=[&](game::XModel* model) {
				if (!model || !model->numBones || !model->baseMat || !model->boneNames ||
					!model->numRootBones || model->numRootBones>model->numBones || model->numCompositeModels ||
					(model->numBones>model->numRootBones && !model->parentList)) return false;
				bones+=model->numBones; return ++count<=32 && bones<=254;
			};
			// DObjCreate expands one native composite level itself. Validate its
			// exact bounded input before invoking it; do not flatten a second time.
			if (root->flags&0x400)
			{
				if (!root->numCompositeModels || root->numCompositeModels>31 || !root->compositeModels) return false;
				for (unsigned i=0;i<root->numCompositeModels;++i) if (!add(root->compositeModels[i])) return false;
				return true;
			}
			return add(root);
		}
		bool bind_visibility(weapon_resource& value,const game::WeaponDef& definition)
		{
			static_assert(offsetof(game::WeaponDef,hideTags)==0x60);
			std::array<std::uint32_t,32> tags{};
			if (definition.hideTags && !utils::native_memory::read_bytes(tags.data(),definition.hideTags,sizeof(tags))) return false;
			const auto count=std::to_integer<unsigned>(value.object.bytes[0xf]);
			const auto bone_count=std::to_integer<unsigned>(value.object.bytes[0x10]);
			if (!count || count>32 || !bone_count || bone_count>254) return false;
			const game::XModel* const* models{};
			std::memcpy(&models,value.object.bytes.data()+0xd8,sizeof(models));
			std::array<const game::XModel*,32> assembled{};
			if (!utils::native_memory::read_bytes(assembled.data(),models,count*sizeof(models[0]))) return false;
			std::array<std::uint32_t,254> names{};unsigned total{};
			for (unsigned i=0;i<count;++i)
			{
				game::XModel model{};
				if (!utils::native_memory::read_bytes(&model,assembled[i],sizeof(model)) ||
					!model.numBones || model.numBones>bone_count-total ||
					!utils::native_memory::read_bytes(names.data()+total,model.boneNames,model.numBones*sizeof(names[0]))) return false;
				total+=model.numBones;
			}
			weapons::part_mask hidden{};
			if (total!=bone_count || !weapons::bind_native_hide_tags({names.data(),total},tags,hidden)) return false;
			value.weapon_hidden=weapons::combine_part_masks(value.initial_hidden,hidden);
			return true;
		}
		void add_owned(const head_pose_bridge::spatial_frame& spatial,bool include_held)
		{
			const auto held=weapons::carry::visible_instances(include_held);
			for (const auto& carried:held)
			{
				if (!carried.id || !carried.id.weapon || carried.id.weapon>=512) continue;
				const bool stowed=weapons::carry::visible_storage(carried.at);
				if(stowed && !weapons::carry::stored_scene(carried.id).item.id)continue;
				weapon_resource* slot=nullptr;
				for (auto& v:weapon_objects) if (v.weapon.load()==carried.id.weapon && v.instance.load()==carried.id.generation) {slot=&v;break;}
				if (!slot) for (auto& v:weapon_objects)
				{
					const auto token=v.weapon.load();
					if (std::none_of(held.begin(),held.end(),[&](const auto& h){return h.id==weapons::weapon_identity{token,v.instance.load()} && token;})) {slot=&v;break;}
				}
				const auto* def=game::weapon_defs[carried.id.weapon];
				auto* gun=def && def->gunModel ? def->gunModel[0] : nullptr;
				auto* hands=prepared_hands.load();
				if (!slot || !hands || !valid_gun(gun,hands->numBones)) {++weapon_rejections;continue;}
				auto& v=*slot;
				if (!v.created || v.hands!=hands || v.gun!=gun || v.hands_bind!=hands->baseMat || v.gun_bind!=gun->baseMat)
				{
					if (v.created) { game::R_SyncRenderThread(); v.weapon=0; utils::hook::invoke<void>(free_object,&v.object); }
					v.object={}; v.pose={}; v.matrices=nullptr; v.visible=false;
					std::uint32_t tag{};
					for (unsigned b=0;b<hands->numBones;++b) if (std::string_view(game::SL_ConvertToString(hands->boneNames[b]))=="tag_weapon") tag=hands->boneNames[b];
					const std::array<model_entry,2> entries{{{hands,0,0,{}},{gun,tag,0,{}}}};
					utils::hook::invoke<void>(create_object,entries.data(),2,nullptr,&v.object,0);
					// The native getter at 0x140658AB0 reads these 8 words. Our
					// independent DObj must inherit its own WeaponDef hide tags;
					// copying the currently selected native DObj breaks dual wield.
					std::memcpy(v.initial_hidden.data(),v.object.bytes.data()+0xb8,sizeof(v.initial_hidden));
					v.visibility_weapon=0;
					v.created=true; v.hands=hands; v.gun=gun; v.hands_bind=hands->baseMat; v.gun_bind=gun->baseMat;
					++v.generation; ++weapon_builds;
				}
				if (v.weapon.load()!=carried.id.weapon || v.instance!=carried.id.generation || v.location!=carried.at)
				{++v.generation;v.instance=carried.id.generation;v.location=carried.at;v.matrices=nullptr;}
				v.weapon=carried.id.weapon;
				if (v.visibility_weapon!=carried.id.weapon)
				{
					if (!weapons::viewmodel_visibility::ready(weapons::part_visibility::surface) || !bind_visibility(v,*def))
					{v.visible=false;++weapon_rejections;continue;}
					v.visibility_weapon=carried.id.weapon;
				}
				v.pose.previous_origin=v.visible ? v.pose.origin : spatial.head_position;
				v.pose.origin=spatial.head_position;
				alignas(16) std::array<std::uint32_t,8> bits{};
				const auto count=std::to_integer<unsigned>(v.object.bytes[0x10]);
				for (unsigned i=0;i<count;++i) bits[i/32]|=0x80000000u>>(i%32);
				utils::hook::invoke<void>(lock_object,&v.object);
				auto hidden=v.weapon_hidden;
				// Apply for every submission, including a reused skeleton epoch.
				// All hand-model bones precede the receiver in these owned DObjs.
				if(stowed)for(unsigned b=0;b<hands->numBones;++b)hidden[b/32]|=0x80000000u>>(b%32);
				std::memcpy(v.object.bytes.data()+0xb8,hidden.data(),sizeof(hidden));
				const auto* matrices=utils::hook::invoke<const bone*>(calc_pose,&v.pose,&v.object,bits.data());
				std::uint32_t epoch{}; std::memcpy(&epoch,v.object.bytes.data()+0xb0,sizeof(epoch));
				const bool solved=matrices && v.matrices.load()==matrices && v.epoch.load()==epoch;
				utils::hook::invoke<void>(unlock_object,&v.object);
				if (!solved) {v.visible=false;++weapon_rejections;continue;}
				const unsigned entity=stowed ? stored_scene_begin+unsigned(slot-weapon_objects.data()) :
					scene_entity-unsigned(carried.owner.holding_hand());
				// Storage owns an ordinary world-depth skeleton, never hand/firing authority.
				auto light=spatial.head_position;
				if(stowed)game::R_AddDObjToScene(&v.object,&v.pose,entity,scene_models::no_cast_shadow,light.data(),0.f,0,0);
				else submit_hook.invoke<void>(&v.object,&v.pose,entity,0x1000800,light.data(),0.f,0,0);
				v.visible=true; ++weapon_submissions;
			}
		}
		void submit(void* obj,void* placement,unsigned entity,unsigned flags,const float* light,float scale,int a,int b)
		{
			const bool firearm=filter_native_firearms && !suppress_native && firearm_object(obj);
			if(firearm)++suppressed_firearms;
			if (!suppress_legacy_viewmodel(filter_native_firearms || suppress_native,suppress_native,firearm)) submit_hook.invoke<void>(obj,placement,entity,flags,light,scale,a,b);
		}

		bool requested(head_pose_bridge::spatial_frame& spatial)
		{
			if (!alive || !installed || !game::CL_IsCgameInitialized()) return false;
			const auto* enabled=game::Dvar_FindVar("vr_independentHands");
			if (!enabled || !enabled->current.enabled) return false;
			// Respect native death/spectator/cinematic suppression as well as the
			// carry owner's hand. A delayed native selection is not an empty hand.
            const auto* ps=reinterpret_cast<const std::byte*>(predicted_state);
            const bool climbing=cliffhanger_physical::independent_hands() || sequences::independent_hands(game::CG_GetPredictedPlayerState(0));
            if(player_life::dead(game::CG_GetPredictedPlayerState(0)))return false;
            if ((std::to_integer<unsigned>(ps[2])>=2 && std::to_integer<unsigned>(ps[2])<=3) ||
                (!climbing && (((*reinterpret_cast<const std::uint32_t*>(ps+0x58)&0x103000) && !vehicles::presentation_allowed()) ||
                (!weapons::carry::active() && ((*reinterpret_cast<const std::uint32_t*>(ps+0x3bc)&0x1ff) ||
                    vr::h2::sp::weapon_selection_request.read())) ||
                (weapons::current_hold().weapon && !vehicles::presentation_allowed())))) return false;
			const auto input=controller_input::latest();
			const auto now=controller_input::clock::now();
			if (!input.focused || !input.sequence || now<input.sampled_at ||
				now-input.sampled_at>std::chrono::milliseconds(150) ||
				!head_pose_bridge::get_spatial_frame(spatial) || spatial.generation!=input.reference_generation) return false;
			unsigned tracked{};for (int h=0;h<2;++h) if (input.grip[h].valid && input.aim[h].valid) tracked|=1u<<h;
			return tracked==3;
		}

		void add(int client)
		{
			if(!client && head_pose_bridge::get_status().enabled && equipment::special::notebook::camera_epoch())
			{
				visible=false;reason="native UAV camera owns view";
				const auto previous=suppress_native;suppress_native=true;
				const auto restore=gsl::finally([&]{suppress_native=previous;});
				add_hook.invoke<void>(client);return;
			}
			const auto* independent_setting=game::Dvar_FindVar("vr_independentHands");
			const bool filter=!client && head_pose_bridge::get_status().enabled && independent_setting && independent_setting->current.enabled;
			const auto previous_filter=filter_native_firearms;
			filter_native_firearms=filter;
			const auto restore_filter=gsl::finally([&]{filter_native_firearms=previous_filter;});
            const bool climbing=cliffhanger_physical::independent_hands() || sequences::independent_hands(game::CG_GetPredictedPlayerState(0));
            if (!client && !scripted_control::predicted_allowed() && !climbing &&
				(player_life::dead(game::CG_GetPredictedPlayerState(0)) || !vehicles::presentation_allowed()))
			{
				// Native cinematic props and world bodies remain native-owned.
				// Ordinary firearm DObjs are still VR-owned during this handoff.
				visible=false;reason="native script owns weapons";
				const auto previous=suppress_native;
				// The real scene body is submitted separately. Reserving its arms
				// also suppresses any transient ordinary first-person hand pair.
				suppress_native=filter && sequences::owns_arms(game::CG_GetPredictedPlayerState(0));
				const auto restore=gsl::finally([&]{suppress_native=previous;});
				add_hook.invoke<void>(client);return;
			}
			head_pose_bridge::spatial_frame spatial;
            const bool independent=filter && owned_native::active() && !vehicles::presentation_allowed() && !climbing;
            const bool spatial_ready=!vehicles::presentation_allowed() && !climbing && owned_requested(spatial);
			const auto previous=suppress_native;
            suppress_native=independent || climbing;
			{ const auto restore=gsl::finally([&]{suppress_native=previous;}); add_hook.invoke<void>(client); }
			filter_native_firearms=previous_filter; // MOD-owned submissions are not legacy fallbacks.
			if (client) return;
			if(spatial_ready)add_owned(spatial,independent);
			if (independent && weapons::carry::current_hold().weapon) { visible=false; return; }
			if (!requested(spatial)) { visible=false; reason="not empty or tracking unavailable"; return; }
			auto* model=character_model();
			if (!model) { visible=false; reason="native character hand model unavailable"; ++rejected; return; }
			if (!created || source!=model || source_bind!=model->baseMat)
			{
				if (!valid_model(model)) { visible=false; reason="character hand skeleton rejected"; ++rejected; return; }
				// Only asset changes synchronize; steady-state frames allocate no
				// new DObj and never wait for another gameplay module.
				if (created) { game::R_SyncRenderThread(); release(false); }
				const model_entry entry{model,0,0,{}};
				utils::hook::invoke<void>(create_object,&entry,1,nullptr,&object,0);
				created=true; source=model; source_bind=model->baseMat; ++builds;
				published_object=&object;
			}
			pose.previous_origin=visible ? pose.origin : spatial.head_position;
			pose.origin=spatial.head_position;
			// Skin first, at the native frontend boundary. Only a completely solved
			// controller pose is submitted; tracking failure never renders a bind pose.
			alignas(16) std::array<std::uint32_t,8> bits{};
			for (int i=0;i<model->numBones;++i) bits[i/32]|=0x80000000u>>(i%32);
			utils::hook::invoke<void>(lock_object,&object);
			const auto* matrices=utils::hook::invoke<const bone*>(calc_pose,&pose,&object,bits.data());
			const bool solved=matrices && solved_matrices.load()==matrices && solved_epoch.load()==read<std::uint32_t>(0xb0);
			utils::hook::invoke<void>(unlock_object,&object);
			if (!solved) { visible=false; reason="empty-hand IK not accepted"; ++rejected; return; }
			utils::hook::invoke<void>(submit_viewmodel,&object,&pose,scene_entity,0x1000800,
				spatial.head_position.data(),0.f,0,0);
			visible=true; ++submitted; reason="native empty hands submitted";
		}

		template<size_t N> bool check(std::uintptr_t address,const std::array<std::uint8_t,N>& bytes)
		{
			std::array<std::uint8_t,N> mask; mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes.data(),mask.data(),N}));
		}
	}
	bool owns(const void* value) noexcept { return value && published_object.load()==value; }
	std::uint64_t resource_generation() noexcept { return builds.load(); }
	void accept(const void* value,const void* matrices,std::uint32_t epoch) noexcept
	{ if (owns(value)) { solved_epoch=epoch; solved_matrices=matrices; } }
	std::string status()
	{
		std::ostringstream out;
		out << "empty_hands installed=" << installed << " builds=" << builds << " retired=" << retired
			<< " submitted=" << submitted << " rejected=" << rejected << " state=" << reason.load() << '\n';
		out << "owned_viewmodels prepared=" << bool(prepared_hands.load()) << " builds=" << weapon_builds
			<< " submitted=" << weapon_submissions << " rejected=" << weapon_rejections << " legacy_firearms_suppressed=" << suppressed_firearms << '\n';
		for (const auto& v:weapon_objects) out << "owned_object=" << &v.object << " weapon=" << v.weapon
			<< " instance=" << v.instance << " resource=" << v.generation << " epoch=" << v.epoch << " location=" << int(v.location.load()) << '\n';
		return out.str();
	}
	class component final: public component_interface
	{
		void post_unpack() override
		{
			if (!check(0x1403D5A19,std::array<std::uint8_t,8>{0xc7,0x44,0x24,0x2c,0xc0,0x0f,0,0}) ||
				!check(add_view_weapon,std::array<std::uint8_t,8>{0x40,0x55,0x53,0x56,0x41,0x54,0x41,0x56}) ||
				!check(create_object,std::array<std::uint8_t,9>{0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55}) ||
				!check(free_object,std::array<std::uint8_t,9>{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9}) ||
				!check(config_string,std::array<std::uint8_t,10>{0x48,0x63,0xc1,0x48,0x8d,0x0d,0x3a,0xfc,0xc6,0x01}) ||
				!check(submit_viewmodel,std::array<std::uint8_t,10>{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10}) ||
				!check(lock_object,std::array<std::uint8_t,10>{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20}) ||
				!check(unlock_object,std::array<std::uint8_t,8>{0xc7,0x41,0x18,0,0,0,0,0xc3}) ||
				!check(calc_pose,std::array<std::uint8_t,10>{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10}))
			{ reason="native empty-hand contract rejected"; console::error("[VR hands] %s\n",reason.load()); return; }
			fastfiles::on_pre_unload([]{firearm_models={};firearm_cursor=0;release();});
			add_hook.create(add_view_weapon,add);
			submit_hook.create(submit_viewmodel,submit);
			scheduler::loop(prepare_hands,scheduler::pipeline::main);
			installed=true;
		}
		void pre_destroy() override { alive=false; }
	};
}
REGISTER_COMPONENT(vr::gameplay::hands::empty_native::component)

namespace vr::gameplay::hands::owned_native
{
	bool active() noexcept
	{
		const auto* enabled=game::Dvar_FindVar("vr_independentHands");
		return empty_native::alive && empty_native::installed && empty_native::prepared_hands.load() &&
			game::CL_IsCgameInitialized() && weapons::carry::active() && enabled && enabled->current.enabled;
	}
	bool owns(const void* object) noexcept
	{ for (const auto& v:empty_native::weapon_objects) if (object==&v.object && v.weapon.load()) return true; return false; }
	weapons::hold owner(const void* object) noexcept
	{ for (const auto& v:empty_native::weapon_objects) if (object==&v.object) return weapons::carry::held({v.weapon.load(),v.instance.load()}); return {}; }
	weapons::weapon_identity identity(const void* object) noexcept
	{for(const auto& v:empty_native::weapon_objects)if(object==&v.object)return {v.weapon.load(),v.instance.load()};return {};}
	std::uint64_t resource_generation(const void* object) noexcept
	{ for (const auto& v:empty_native::weapon_objects) if (object==&v.object) return v.generation.load(); return 0; }
	void accept(const void* object,const void* matrices,std::uint32_t epoch) noexcept
	{ for (auto& v:empty_native::weapon_objects) if (object==&v.object) {v.epoch=epoch;v.matrices=matrices;return;} }
}
