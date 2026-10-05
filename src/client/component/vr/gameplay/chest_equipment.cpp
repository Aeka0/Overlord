#include <std_include.hpp>
#include "component/scene_model_record.hpp"
#include "chest_equipment.hpp"
#include "grenade_runtime.hpp"
#include "grenade_policy.hpp"
#include "native_ammunition.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_scripted_control.hpp"
#include "hands/attachment_pose.hpp"
#include <utils/native_memory.hpp>
#include "../engine_stereo_view.hpp"
#include "component/scene_models.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>
#include <mutex>

namespace vr::gameplay::equipment::chest_items
{
	namespace
	{
		using clock=controller_input::clock;
		struct item {std::uint32_t weapon{};game::XModel* model{};vec center{},extent{};int quantity{};grenades::kind type{grenades::kind::count};float scale{1};};
		struct snapshot {std::array<item,2> items{};clock::time_point at{};std::uint64_t reference{};};
		std::mutex mutex;snapshot published,submitted;
		std::array<unsigned short,2> lighting{};
		std::atomic_bool alive{true};bool verified{};game::dvar_t* enabled{};
		std::atomic_uint64_t submissions{},placements{};
		bool running() noexcept {return alive && verified && enabled && enabled->current.enabled && weapons::carry::active();}
		bool fresh(const snapshot& s) noexcept {const auto now=clock::now();return s.reference && now>=s.at && now-s.at<=150ms;}
		template<class T> bool read(const void* p,size_t offset,T& value) noexcept
		{return p && utils::native_memory::read_bytes(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));}
		bool finite(vec v,float limit=10000) noexcept {for(float x:v)if(!std::isfinite(x) || std::abs(x)>limit)return false;return true;}
		void sample()
		{
			snapshot next;
			const auto input=controller_input::latest();
			if(running() && input.focused && scripted_control::predicted_allowed() && game::CL_IsCgameInitialized())
			{
				const auto* ps=game::g_entities[0].client;
				std::array<std::byte,0x3bc> bytes{};
				if(ps && utils::native_memory::read_bytes(bytes.data(),ps,bytes.size()))
				{
					const auto tokens=selected_chest_items(bytes);
					for(unsigned i=0;i<tokens.size();++i)
					{
						const auto token=tokens[i];if(!token)continue;
						const auto* definition=game::weapon_defs[token];
						if(!definition || definition->inventoryType!=game::WEAPINVENTORY_OFFHAND)continue;
						const auto ammo=weapons::native_ammunition::observe_carried(ps,token);
						if(!ammo.valid || !show_chest_item(ammo.loaded,ammo.reserve))continue;
						// Native world asset, including its native LODs and materials.
						// No runtime mesh clone or acquisition/throw authority is needed.
						auto* model=definition->worldModel ? definition->worldModel[0]:nullptr;
						const auto type=grenades::classify(definition->szInternalName?definition->szInternalName:"",model && model->name?model->name:"");
						if(grenades::valid(type) && grenades::behaviors[unsigned(type)].chest_model)
						{
							model=definition->projectileModel;
							if(!model || !model->name || std::string_view(model->name)!=grenades::behaviors[unsigned(type)].chest_model)continue;
						}
						if(!model || !model->numBones || !model->numLods || !model->numsurfs || model->numCompositeModels)continue;
						const vec center{model->bounds.midPoint[0],model->bounds.midPoint[1],model->bounds.midPoint[2]};
						const vec extent{model->bounds.halfSize[0],model->bounds.halfSize[1],model->bounds.halfSize[2]};
						if(!finite(center) || !finite(extent) || std::any_of(extent.begin(),extent.end(),[](float x){return x<=0 || x>100;}))continue;
						next.items[i]={token,model,center,extent,ammo.loaded+ammo.reserve,type,1};
					}
					next.reference=input.reference_generation;next.at=input.sampled_at;
				}
			}
			const std::lock_guard lock(mutex);published=next;
		}
		void submit()
		{
			if(!running() || !scripted_control::predicted_allowed() || !sequences::chest_equipment_visible())return;
			snapshot s;{const std::lock_guard lock(mutex);s=published;}
			if(!fresh(s))return;
			head_pose_bridge::spatial_frame body;if(!head_pose_bridge::get_spatial_frame(body) || body.generation!=s.reference)return;
			const auto now=clock::now();if(now<body.captured_at || now-body.captured_at>150ms)return;
			const auto chest=locate_chest(body);if(!chest.valid)return;
			for(auto& item:s.items)item.scale=grenades::stowed_scale(item.type,item.extent,body.units_per_meter);
			{const std::lock_guard lock(mutex);submitted=s;}
			for(unsigned i=0;i<s.items.size();++i)
			{
				const auto& item=s.items[i];if(!item.model || item.quantity<=0 || grenades::occupies_slot(i))continue;
				const auto world=stowed_chest_item(chest,item_slots[i],scale(item.center,item.scale));
				game::GfxScaledPlacement placement{};placement.scale=item.scale;
				std::copy(world.position.begin(),world.position.end(),placement.base.origin);
				std::copy(world.rotation.begin(),world.rotation.end(),placement.base.quat);
				float color[4]{1,1,1,1};scene_models::submit(item.model,&placement,scene_models::no_cast_shadow,&lighting[i],color,color,color,8.f);++submissions;
			}
		}
		scene_models::placement_result prepare(const scene_models::preparation& p,const void* entry,game::GfxPlacement& current,game::GfxPlacement& previous) noexcept
		{
			using result=scene_models::placement_result;std::uintptr_t handle{};
			if(!scene_models::native_entry::lighting(entry,handle))return result::unchanged;
			unsigned index=2;for(unsigned i=0;i<2;++i)if(handle==reinterpret_cast<std::uintptr_t>(&lighting[i]))index=i;
			if(index==2)return result::unchanged;
			if(!sequences::chest_equipment_visible())return result::omit;
			snapshot s,live;{const std::lock_guard lock(mutex);s=submitted;live=published;}
			const auto& item=s.items[index];game::XModel* model{};
			if(grenades::occupies_slot(index) || !running() || !fresh(s) || !fresh(live) || s.reference!=live.reference || !item.model ||
				item.weapon!=live.items[index].weapon || item.model!=live.items[index].model || live.items[index].quantity<=0 ||
				!scene_models::native_entry::model(entry,model) || model!=item.model)return result::omit;
			std::array<float,12> camera{};vec origin{};hands::attachments::solved pose;
			if(!read(p.record,engine_stereo_view::h2_view_origin_offset,camera) ||
				!hands::attachments::for_record(p.record,camera,pose) || pose.reference!=s.reference)return result::retain;
			if(!read(p.record,engine_stereo_view::h2_current_model_placement_origin_offset,origin) || !finite(origin,1e7f))return result::omit;
			const auto body=hands::attachments::body_frame(pose,origin);
			const auto chest=locate_chest(body);if(!chest.valid)return result::omit;
			const auto world=stowed_chest_item(chest,item_slots[index],scale(item.center,item.scale));
			std::copy(world.position.begin(),world.position.end(),current.origin);std::copy(world.rotation.begin(),world.rotation.end(),current.quat);
			previous=current;++placements;return result::replace;
		}
		void retire(){const std::lock_guard lock(mutex);published={};submitted={};}
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			static_assert(offsetof(game::WeaponDef,worldModel)==0x470);
			static_assert(offsetof(game::WeaponDef,inventoryType)==0x5e0);
			constexpr std::uint8_t primary[]{0x8b,0x9e,0xb4,0x03,0,0},secondary[]{0x8b,0x9e,0xb8,0x03,0,0};
			constexpr std::uint8_t mask[]{255,255,255,255,255,255};
			verified=bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140697a88),{primary,mask,sizeof(primary)})) &&
				bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140697a99),{secondary,mask,sizeof(secondary)}));
			enabled=dvars::register_bool("vr_chestEquipment",true,game::DVAR_FLAG_SAVED,"Display native tactical and lethal equipment on the chest");
			scheduler::loop(sample,scheduler::pipeline::server,16ms);
			scene_models::on_submit(submit);scene_models::on_prepare_placement(prepare);fastfiles::on_pre_unload(retire);
			command::add("vr_chestEquipment_status",[] {
				snapshot s;{const std::lock_guard lock(mutex);s=published;}
				std::string report=std::format("[VR chest equipment] enabled={} verified={} submissions={} prepared={} order=knife,tactical,lethal interactive=knife_and_grenades\n",running(),verified,submissions.load(),placements.load());
				for(unsigned i=0;i<2;++i)report+=std::format("slot={} weapon={} quantity={} visible={}\n",i==0?"tactical":"lethal",s.items[i].weapon,s.items[i].quantity,s.items[i].model!=nullptr);
				console::print_text(console::con_type_info,report);scheduler::once([report]{utils::io::write_file("minidumps/h2-mod-vr-chest-equipment.txt",report);},scheduler::pipeline::async);
			});
		}
		void pre_destroy() override {alive=false;retire();}
	};
}
REGISTER_COMPONENT(vr::gameplay::equipment::chest_items::component)
