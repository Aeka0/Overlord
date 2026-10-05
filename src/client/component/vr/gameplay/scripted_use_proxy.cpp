#include <std_include.hpp>
#include "scripted_use_proxy.hpp"
#include "native_carry.hpp"
#include "native_use.hpp"
#include "weapon_carry_runtime.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/game.hpp"
#include "component/gsc/script_extension.hpp"
#include "game/scripting/execution.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>

namespace vr::gameplay::interaction::scripted_use
{
	namespace
	{
		std::vector<snapshot> cached;const void* player{};int command_time{};bool sampled{};
		const char* gaze_call{};
		struct ammo_link {target_key trigger{},visual{};};
		std::vector<ammo_link> links;const void* link_player{};int link_time{};bool links_sampled{};
		void reset(){cached.clear();links.clear();player=link_player=nullptr;sampled=links_sampled=false;}
		template<class T>T read(const void* base,size_t offset){T out{};std::memcpy(&out,static_cast<const std::byte*>(base)+offset,sizeof(out));return out;}
		target_key key(const scripting::entity& entity)
		{
			const auto ref=entity.get_entity_reference();if(ref.classnum || !ref.entnum || ref.entnum>=4000)return {};
			if(!read<unsigned char>(&game::g_entities[ref.entnum],0xbc))return {};
			const auto id=weapons::native_carry::entity_key(ref.entnum);return {id.entity,id.generation};
		}
		// Share the same tag/bind transform for installation objects and ammo
		// caches. Names, dimensions and poses all come from the current model.
		bool observe_visual(const scripting::entity& model_entity,snapshot& out,std::string_view required_tag={})
		{
			out.visual=key(model_entity);if(!out.visual)return false;
			const auto* object=utils::hook::invoke<const void*>(0x1405A6ED0,&game::g_entities[out.visual.entity]);
			if(!object || read<unsigned char>(object,15)!=1)return false;
			const auto models=read<game::XModel* const*>(object,0xd8);if(!models || !models[0])return false;
			const auto* model=models[0];
			if(!model->numBones || !model->numRootBones || !model->boneNames || !model->baseMat)return false;
			const auto* root_name=game::SL_ConvertToString(model->boneNames[0]);
			if(!root_name || !*root_name || (!required_tag.empty() && required_tag!=root_name))return false;
			const std::string tag(root_name);
			const auto origin=model_entity.call("gettagorigin",{tag}).as<scripting::vector>();
			const auto angles=model_entity.call("gettagangles",{tag}).as<scripting::vector>();
			float rotation[3][3]{};const float euler[]{angles[0],angles[1],angles[2]};game::AnglesToAxis(euler,rotation);
			std::array<vec,3> axis;std::memcpy(axis.data(),rotation,sizeof(rotation));
			const auto& bind=model->baseMat[0];
			const auto pose=hands::pose_math::compose(hands::anchor{{origin[0],origin[1],origin[2]},hands::from_axis(axis)},
				hands::pose_math::inverse(hands::anchor{{bind.trans[0],bind.trans[1],bind.trans[2]},hands::normalize({bind.quat[0],bind.quat[1],bind.quat[2],bind.quat[3]})}));
			out.volume.origin=pose.position;
			out.volume.axis={hands::rotate(pose.rotation,{1,0,0}),hands::rotate(pose.rotation,{0,1,0}),hands::rotate(pose.rotation,{0,0,1})};
			std::copy_n(model->bounds.midPoint,3,out.volume.center.begin());std::copy_n(model->bounds.halfSize,3,out.volume.half.begin());
			out.volume.valid=true;return true;
		}
		void observe_dsm(const binding& definition,snapshot& out)
		{
			const auto trigger=scripting::call("getent",{std::string(definition.trigger),"targetname"});
			if(!trigger.is<scripting::entity>())return;
			out.trigger=key(trigger.as<scripting::entity>());if(!out.trigger)return;
			out.native_center=read<vec>(&game::g_entities[out.trigger.entity],0xdc);
			const auto visual=scripting::call("getent",{std::string(definition.visual),"targetname"});
			if(!visual.is<scripting::entity>())return;
			out.visual=key(visual.as<scripting::entity>());
			const scripting::entity level{*game::levelEntityId};const auto flags=level.get("flag");
			if(!flags.is<scripting::array>())return;
			const auto ready=flags.as<scripting::array>().get(std::string("dsm_ready_to_use"));
			if(!ready.is<int>() || !ready.as<int>())return;
			out.enabled=observe_visual(visual.as<scripting::entity>(),out,definition.tag);
		}
		bool live_key(target_key value)
		{
			if(!value || !read<unsigned char>(&game::g_entities[value.entity],0xbc))return false;
			return weapons::native_carry::entity_key(value.entity).generation==value.generation;
		}
		const std::vector<ammo_link>& ammo_links()
		{
			const auto* ps=game::g_entities[0].client;
			if(!ps){links.clear();links_sampled=false;return links;}
			const auto time=read<int>(ps,0x4c);
			if(links_sampled && link_player==ps && link_time==time)return links;
			link_player=ps;link_time=time;links_sampled=false;links.clear();
			// maps/_load::ammo_cache_think_global publishes this exact relation on
			// every map. Share discovery between both hands and all gaze threads;
			// never bind by nearest entity, fixed offset or model name.
			const auto value=scripting::call("getentarray",{"ammo_cache","targetname"});
			if(!value.is<scripting::array>())return links;
			const auto list=value.as<scripting::array>();const int count=list.size();if(count<0 || count>4000)return links;
			for(int i=0;i<count;++i)
			{
				const auto item=list.get(static_cast<unsigned>(i));if(!item.is<scripting::entity>())continue;
				const auto visual=item.as<scripting::entity>();if(!key(visual))continue;
				const auto use=visual.get("use_trigger");if(!use.is<scripting::entity>())continue;
				const auto trigger=use.as<scripting::entity>();const auto identity=key(trigger);
				if(!identity || read<unsigned char>(&game::g_entities[identity.entity],0)!=5)continue;
				links.push_back({identity,key(visual)});
			}
			std::sort(links.begin(),links.end(),[](const auto& a,const auto& b){return a.trigger.entity<b.trigger.entity;});
			// Ambiguous script ownership fails closed for every duplicate.
			for(std::size_t first=0;first<links.size();)
			{
				auto end=first+1;while(end<links.size() && links[end].trigger==links[first].trigger)++end;
				if(end-first>1)for(auto i=first;i<end;++i)links[i].visual={};
				first=end;
			}
			links_sampled=true;return links;
		}
		void bind_gaze()
		{
			reset();gaze_call=nullptr;
			const auto file=scripting::script_function_table_sort.find("maps/_utility");
			if(file==scripting::script_function_table_sort.end())return;
			const char* begin{},*end{};
			const auto function=scripting::get_token_single(0xab48);
			for(const auto& [name,pos]:file->second)if(name==function)begin=pos;
			if(!begin)return;
			for(const auto& [name,pos]:file->second)if(pos>begin && (!end || pos<end))end=pos;
			if(!end)return;
			if(const auto offset=ammo_gaze_call({reinterpret_cast<const std::byte*>(begin),std::size_t(end-begin)}))gaze_call=begin+*offset;
		}
		bool gaze(game::BuiltinFunction)
		{
			// Cheap callsite rejection precedes any dvar, ownership or script read.
			if(!gaze_call || game::scr_function_stack->pos!=gaze_call || game::scr_VmPub->outparamcount!=2)return false;
			const auto* enabled=game::Dvar_FindVar("vr_worldInteraction");
			const bool vr=native::ready() && enabled && enabled->current.enabled && weapons::carry::active();if(!vr)return false;
			try
			{
				const auto local=game::scr_VmPub->function_frame->fs.localId;
				const scripting::entity self{game::scr_VarGlob->objectVariableValue[local].u.f.next};
				const auto identity=key(self);const auto& all=ammo_links();
				const auto link=std::lower_bound(all.begin(),all.end(),identity.entity,[](const auto& a,int entity){return a.trigger.entity<entity;});
				if(link==all.end() || !ammo_gaze_override(vr,true,identity,link->trigger) || !live_key(link->visual))return false;
				// Replace only the camera dot product; the next native expression
				// still checks _id_CA6E (supply animation lock) and owns usability.
				game::Scr_ClearOutParams();scripting::push_value(1.f);return true;
			}
			catch(const std::exception&){return false;}
		}
	}
	std::span<const snapshot> sample(const ray& aim)
	{
		if(!scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized() || !*game::levelEntityId)return {};
		const auto* ps=game::g_entities[0].client;if(!ps)return {};
		const auto time=read<int>(ps,0x4c);
		if(sampled && player==ps && command_time==time)return cached;
		player=ps;command_time=time;sampled=true;cached.clear();
		const auto* map=game::Dvar_FindVar("mapname");const auto* definition=map && map->current.string?for_map(map->current.string):nullptr;
		if(definition)
		{
			snapshot value;try{observe_dsm(*definition,value);}catch(const std::exception&){}
			if(value.trigger)cached.push_back(value);
		}
		if(gaze_call)try
		{
			const scripting::entity local_player{game::scr_entref_t{0,0}};
			const bool allowed=local_player.get("dont_allow_ammo_cache").get_raw().type==game::SCRIPT_NONE;
			for(const auto& link:ammo_links())
			{
				if(!live_key(link.trigger) || !live_key(link.visual))continue;
				snapshot value;value.trigger=link.trigger;value.visual=link.visual;value.prompt=prompt_kind::resupply;
				value.native_center=read<vec>(&game::g_entities[link.trigger.entity],0xdc);
				// Reject remote crates before model/tag queries. Final selection uses
				// the exact visual volume, not this conservative discovery envelope.
				const auto distance=hands::length(hands::sub(value.native_center,aim.head));
				if(!std::isfinite(distance) || distance>(aim.distance_meters+model_origin_margin_m+.75f)*aim.units)continue;
				const scripting::entity trigger{game::scr_entref_t{static_cast<unsigned short>(link.trigger.entity),0}};
				const scripting::entity visual{game::scr_entref_t{static_cast<unsigned short>(link.visual.entity),0}};
				const auto ready=trigger.get(scripting::get_token_single(0xca6e));
				if(allowed && ready.is<int>() && ready.as<int>())value.enabled=observe_visual(visual,value);
				cached.push_back(value);
			}
		}
		catch(const std::exception&){/* Preserve native failure, never force a mission event. */}
		return cached;
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			gsc::intercept_builtin("vectordot",gaze);scripting::on_level_start(bind_gaze);
			scripting::on_shutdown([](bool,bool after){if(!after){reset();gaze_call=nullptr;}});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::interaction::scripted_use::component)
