#pragma once
#include "hand_rig_builder.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"
#include "mounted_turret_profile.hpp"
#include <cstring>

namespace vr::gameplay::mounted
{
	struct model_pose
	{
		hands::rig layout{};
		int root{-1},cover{-1},yaw{-1},pitch{-1},animated_pitch{-1},weapon{-1},gun{-1};
		hands::anchor fixed_cover{};
		bool valid{};
	};
	inline model_pose resolve_model_pose(std::span<const hands::bone_definition> bones,mount_kind kind=mount_kind::suburban)noexcept
	{
		model_pose out;if((kind==mount_kind::suburban && bones.size()!=48) ||
			(kind==mount_kind::blackhawk && bones.size()!=55) || kind==mount_kind::none)return out;
		out.layout.count=int(bones.size());
		const std::array<std::string_view,7> names=kind==mount_kind::blackhawk?
			std::array<std::string_view,7>{"tag_turret_base","tag_player","tag_turret","tag_barrel","turret_animate_jnt","tag_flash","tag_barrel"}:
			std::array<std::string_view,7>{"tag_cover","j_cover","tag_aim_pivot","tag_aim","tag_aim_animated","tag_weapon","j_mg"};
		std::array<int,7> found{};found.fill(-1);
		for(int i=0;i<out.layout.count;++i)
		{
			if(bones[i].parent>=i || bones[i].parent<-1)return {};
			out.layout.parent[i]=bones[i].parent;
			for(size_t n=0;n<names.size();++n)if(bones[i].name==names[n])
			{if(found[n]>=0)return {};found[n]=i;}
		}
		for(auto i:found)if(i<0)return {};
		out.root=found[0];out.cover=found[1];out.yaw=found[2];out.pitch=found[3];out.animated_pitch=found[4];out.weapon=found[5];out.gun=found[6];
		if(kind==mount_kind::blackhawk)
		{
			if(bones[out.yaw].parent!=out.root || bones[out.animated_pitch].parent!=out.yaw ||
				bones[out.pitch].parent!=out.animated_pitch || bones[out.cover].parent!=out.root ||
				!hands::descendant(out.weapon,out.pitch,out.layout))return {};
			out.cover=out.animated_pitch=-1;out.valid=true;return out;
		}
		if(bones[out.root].parent!=-1 || bones[out.yaw].parent!=out.root ||
			!hands::descendant(out.cover,out.yaw,out.layout) || !hands::descendant(out.gun,out.animated_pitch,out.layout) ||
			!hands::descendant(out.weapon,out.pitch,out.layout) || !hands::descendant(out.pitch,out.yaw,out.layout) ||
			!hands::descendant(out.animated_pitch,out.yaw,out.layout) || hands::descendant(out.pitch,out.animated_pitch,out.layout) ||
			hands::descendant(out.animated_pitch,out.pitch,out.layout))return {};
		using namespace hands::pose_math;
		out.fixed_cover=compose(inverse(as_anchor(bones[out.root].bind)),as_anchor(bones[out.cover].bind));out.valid=true;return out;
	}
	// Completed native matrices are the immutable input for each render sample.
	// Reuse the native local-angle joints, preserving spin, belt and wire animation.
	inline bool project_model_pose(const model_pose& binding,std::span<const hands::bone> source,
		std::span<hands::bone> output,std::array<float,2> angles,bool aiming)noexcept
	{
		if(!binding.valid || source.size()!=size_t(binding.layout.count) || output.size()!=source.size() ||
			!std::isfinite(angles[0]) || !std::isfinite(angles[1]))return false;
		using namespace hands;using namespace hands::pose_math;
		for(const auto& b:source)
		{
			for(float v:b.position)if(!std::isfinite(v))return false;
			for(float v:b.rotation)if(!std::isfinite(v))return false;
		}
		std::copy(source.begin(),source.end(),output.begin());
		const auto joint=[&](int bone,float degrees,bool yaw){
			const float half=degrees*.00872664625997f;
			const quat local=yaw?quat{0,0,std::sin(half),std::cos(half)}:quat{0,std::sin(half),0,std::cos(half)};
			auto target=as_anchor(output[bone]);target.rotation=normalize(multiply(output[binding.layout.parent[bone]].rotation,local));
			move_part(binding.layout,bone,target,output);
		};
		if(aiming){joint(binding.yaw,angles[1],true);joint(binding.pitch,angles[0],false);if(binding.animated_pitch>=0)joint(binding.animated_pitch,angles[0],false);}
		if(binding.cover>=0)
		{
			const auto cover=compose(as_anchor(output[binding.root]),binding.fixed_cover);
			output[binding.cover].position=cover.position;output[binding.cover].rotation=cover.rotation;
		}
		return true;
	}
	inline bool independent_vehicle_pose(std::span<const std::byte> source,
		const std::array<float,2>& angles,std::span<std::byte> output)noexcept
	{
		if(source.size()!=0xb8 || output.size()!=source.size() ||
			!std::isfinite(angles[0]) || !std::isfinite(angles[1]))return false;
		std::memcpy(output.data(),source.data(),source.size());
		// Native type-9 controller consumes SHORT2ANGLE yaw/pitch at these fields.
		const auto packed=[](float x){return static_cast<std::int16_t>(std::lround(std::remainder(x,360.f)*(65536.f/360.f)));};
		const auto pitch=packed(angles[0]),yaw=packed(angles[1]);
		std::memcpy(output.data()+0x66,&yaw,sizeof(yaw));std::memcpy(output.data()+0x6a,&pitch,sizeof(pitch));
		return true;
	}
	// The turret controller's local-angle branch consumes these fields only.
	// Keep the real pose (including its view-angle pointer) immutable, and retain
	// native bone indices and barrel spin in the private controller invocation.
	inline bool independent_client_pose(std::span<const std::byte> source,
		const std::array<float,2>& angles, std::span<std::byte> output) noexcept
	{
		if (source.size()!=0xb8 || output.size()!=source.size() ||
			!std::isfinite(angles[0]) || !std::isfinite(angles[1])) return false;
		std::memcpy(output.data(),source.data(),source.size());
		const std::array<float,3> local{angles[0],angles[1],0};
		std::memcpy(output.data()+0x60,local.data(),sizeof(local));
		output[0x70]=std::byte{};
		return true;
	}

	inline bool arm_bone(const hands::rig& layout,int index) noexcept
	{
		return hands::descendant(index,layout.arms[0].shoulder,layout) ||
			hands::descendant(index,layout.arms[1].shoulder,layout);
	}
	// Like ordinary empty hands, solve from this hand model's stable bind pose
	// at the current head. Only a constrained hand keeps native finger articulation.
	inline bool seed_arm_pose(const hands::rig& layout,std::span<const hands::bone> bind,hands::vec bind_head,
		hands::vec head,std::span<const hands::bone> native,unsigned held,std::span<hands::bone> output)noexcept
	{
		if(layout.count<=0 || layout.count>256 || bind.size()<size_t(layout.count) || native.size()<size_t(layout.count) || output.size()<size_t(layout.count))return false;
		using namespace hands;
		for(auto v:{bind_head,head})for(float x:v)if(!std::isfinite(x))return false;
		for(int i=0;i<layout.count;++i)
		{
			if(layout.parent[i]<-1 || layout.parent[i]>=i)return false;
			output[i]=bind[i];output[i].position=add(sub(bind[i].position,bind_head),head);
		}
		for(unsigned h=0;h<2;++h)if(held&(1u<<h))
		{
			const auto wrist=layout.arms[h].wrist;if(wrist<0 || wrist>=layout.count)return false;
			const auto from=native[wrist],to=output[wrist];
			const auto rotation=normalize(multiply(to.rotation,conjugate(normalize(from.rotation))));
			for(int i=wrist;i<layout.count;++i)if(descendant(i,wrist,layout))
				output[i]=transformed(native[i],from.position,to.position,rotation);
		}
		return true;
	}
}
