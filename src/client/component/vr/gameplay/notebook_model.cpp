#include <std_include.hpp>
#include "notebook_model.hpp"
#include "component/scene_models.hpp"
#include "weapon_carry_runtime.hpp"
#include "game/game.hpp"

namespace vr::gameplay::equipment::special::notebook
{
	static_assert(scene_models::skeletal_model::scene_begin==4000+weapons::carry::visible_instance_capacity);
	bool model::create(game::XModel* asset)
	{
		if(!asset || !asset->name || std::string_view(asset->name)!="h2_viewmodel_uav_control_unit" || asset->numBones!=11 || asset->numRootBones!=1 || asset->numLods!=1 || asset->numsurfs!=8 ||
			!asset->baseMat || !asset->boneNames || !asset->parentList || !asset->lodInfo[0].surfs || !asset->materialHandles)return false;
		for(const auto& entry:std::array<std::pair<unsigned,const char*>,3>{{{5,"j_info_display"},{6,"j_screen_display"},{10,"j_wire"}}})
		{const auto* name=game::SL_ConvertToString(asset->boneNames[entry.first]);if(!name || std::string_view(name)!=entry.second)return false;}
		for(unsigned i=0;i<bind.size();++i)
		{
			const auto& b=asset->baseMat[i];bind[i]={{b.trans[0],b.trans[1],b.trans[2]},hands::normalize({b.quat[0],b.quat[1],b.quat[2],b.quat[3]})};
			if(i){const auto distance=asset->parentList[i-1];if(!distance || distance>i)return false;group[i]=group[i-distance];}
			if(i==5)group[i]=1;else if(i==6 || i==10)group[i]=2;
		}
		if(!skeleton.create(asset))return false;source=asset;return true;
	}
	bool model::submit(anchor root,float angle)
	{
		if(!source || !std::isfinite(angle))return false;
		std::array<game::DObjAnimMat,11> bones{};
		for(unsigned i=0;i<bones.size();++i)
		{
			// The original wire's bone and blend weights use the same continuous
			// hinge delta as the screen. Native skinning keeps its body end fixed.
			const auto local=group[i]?compose(lid(angle,group[i]==1),bind[i]):bind[i];
			const auto at=compose(root,local);auto& b=bones[i];
			std::copy(at.rotation.begin(),at.rotation.end(),b.quat);std::copy(at.position.begin(),at.position.end(),b.trans);b.transWeight=2;
		}
		const bool accepted=skeleton.submit(bones,scene_models::no_cast_shadow);if(accepted)++submissions;else ++rejected;return accepted;
	}
}
