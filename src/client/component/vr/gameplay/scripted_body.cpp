#include <std_include.hpp>
#include "scripted_body.hpp"
#include "campaign/scripted_sequences.hpp"
#include "viewmodel_visibility.hpp"
#include <utils/native_memory.hpp>
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::sequences::body
{
	namespace
	{
		struct binding{const void* object{};game::XModel* model{};weapons::part_mask roots{};int entity{-1};};
		std::mutex mutex;binding prepared;
		std::array<binding,4> hidden_entities{};
		template<class T>bool read(const void* p,size_t offset,T& out)
		{return p && utils::native_memory::read_bytes(&out,static_cast<const std::byte*>(p)+offset,sizeof(out));}
	}
	void invalidate() noexcept{const std::lock_guard lock(mutex);prepared={};hidden_entities={};}
	void prepare()
	{
		const auto story=latest();
		for(unsigned slot=0;slot<story.hidden_entities.size();++slot)
		{
			const int entity=story.hidden_entities[slot];if(entity<=0 || entity>=4000)continue;
			short index{};if(!read(reinterpret_cast<void*>(0x14b113080),size_t(entity)*2,index) || index<=0 || index>=4096)continue;
			const void* object=reinterpret_cast<void*>(0x14ae2dff0+size_t(index)*0x240);
			unsigned char count{},bones{};game::XModel** models{};game::XModel* model{};
			if(!read(object,15,count) || !count || count>32 || !read(object,16,bones) || !bones || bones>254 ||
				!read(object,0xd8,models) || !read(models,0,model) || !model)continue;
			game::XModel value{};if(!read(model,0,value) || !value.name)continue;
			const std::string_view name=value.name;
			if(name!="viewbody_tf141_wet" && name!="viewbody_tf141_injured" && name!="weapon_commando_knife" && name!="weapon_commando_knife_bloody" &&
				!(slot==3 && story.scene==scenario::ending && name=="body_desert_tf141_assault_a"))continue;
			binding next{object,model,{},entity};for(unsigned b=0;b<bones;++b)next.roots[b/32]|=0x80000000u>>(b%32);
			const std::lock_guard lock(mutex);hidden_entities[slot]=next;
		}
		if(!story.hide_body_arms || story.linked_entity<=0 || story.linked_entity>=4000)return;
		// Client DObj handles are already used by the native viewmodel/hand
		// adapters. Restrict this binding to the one linked scripted body instance.
		short index{};
		if(!read(reinterpret_cast<void*>(0x14b113080),size_t(story.linked_entity)*2,index) || index<=0 || index>=4096)return;
		const void* object=reinterpret_cast<void*>(0x14ae2dff0+size_t(index)*0x240);
		unsigned char count{};game::XModel** models{};game::XModel* model{};
		if(!read(object,15,count) || count!=1 || !read(object,0xd8,models) || !read(models,0,model) || !model)return;
		game::XModel value{};if(!read(model,0,value) || !value.name || std::string_view(value.name)!="viewbody_tf141_forest")return;
		{const std::lock_guard lock(mutex);if(prepared.object==object && prepared.model==model)return;}
		constexpr std::array<std::string_view,2> names{"j_shoulder_le","j_shoulder_ri"};
		if(!weapons::viewmodel_visibility::prepare_skinned_parts(model,names,true))return;
		binding next{object,model};next.entity=story.linked_entity;unsigned found{};
		for(unsigned i=0;i<value.numBones;++i)
		{
			game::scr_string_t token{};if(!read(value.boneNames,i*sizeof(token),token))return;
			const auto* name=game::SL_ConvertToString(token);
			if(name && std::find(names.begin(),names.end(),name)!=names.end()){next.roots[i/32]|=0x80000000u>>(i%32);++found;}
		}
		if(found!=2)return;
		const std::lock_guard lock(mutex);prepared=next;
	}
	void apply(const void* object,const void* matrices) noexcept
	{
		binding value;std::array<binding,4> full;{const std::lock_guard lock(mutex);value=prepared;full=hidden_entities;}
		const auto story=latest();
		for(const auto& candidate:full)if(candidate.object==object && object)
		{
			std::uint32_t epoch{};if(!read(object,0xb0,epoch))return;
			game::XModel** models{};game::XModel* model{};
			const bool same_model=read(object,0xd8,models) && read(models,0,model) && model==candidate.model;
			const bool hide=same_model && std::find(story.hidden_entities.begin(),story.hidden_entities.end(),candidate.entity)!=story.hidden_entities.end();
			weapons::viewmodel_visibility::publish(object,matrices,epoch,hide?candidate.roots:weapons::part_mask{},weapons::part_visibility::surface);
			return;
		}
		if(object!=value.object || !object)return;
		std::uint32_t epoch{};
		if(!read(object,0xb0,epoch))return;
		const auto roots=story.hide_body_arms && story.linked_entity==value.entity?value.roots:weapons::part_mask{};
		weapons::viewmodel_visibility::publish(object,matrices,epoch,roots,weapons::part_visibility::skinned_groups);
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			scheduler::loop(prepare,scheduler::pipeline::main);
			fastfiles::on_pre_unload(invalidate);
		}
		void pre_destroy()override{invalidate();}
	};
}
REGISTER_COMPONENT(vr::gameplay::sequences::body::component)
