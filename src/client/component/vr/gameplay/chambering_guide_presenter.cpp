#include <std_include.hpp>
#include "chambering_guide_presenter.hpp"
#include "chambering_guide.hpp"
#include "chambering_guide_pulse.hpp"
#include "physical_reload_runtime.hpp"
#include "tube_runtime.hpp"
#include "weapon_render_pose.hpp"
#include "weapon_render_owner.hpp"
#include "../eye_composition.hpp"
#include "component/scene_pose_match.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::weapons::chambering_guide
{
	namespace
	{
		using clock=controller_input::clock;
		std::atomic_bool alive{true};
		std::mutex mutex;
		std::array<sample,128> poses{};
		std::array<sample,15> latest{};
		size_t cursor{};
		struct guide_draw
		{
			std::shared_ptr<const opaque_mesh::mesh> geometry;
			std::array<opaque_mesh::matrix,2> transform{};
		};
		struct eye_pair
		{
			std::uint64_t id{},publication{},device{};
			std::array<guide_draw,90> draws{};
			modulation strength{};
			size_t count{};bool left_drawn{};
		};
		thread_local eye_pair pair;
		thread_local opaque_mesh::renderer renderer;
		std::atomic_uint64_t pairs{},draws{},misses{},failures{};
		bool current(const sample& s) noexcept
		{
			const auto now=clock::now();if(!s.instance || now<s.at || now-s.at>150ms)return false;
			if(s.reload)
			{
				const auto v=physical_reload::current(s.owner.id());
				return v.active && !v.fault && v.definition==s.reload && v.ammo.instance_generation==s.instance &&
					v.reference_generation==s.reference && same_render_carrier(s.owner,v.owner) && needed(*v.definition,v.ammo,v.slide_held);
			}
			if(s.tube)
			{
				const auto v=tube::current(s.owner.id());
				return v.active && !v.fault && v.definition==s.tube && v.ammo.instance_generation==s.instance &&
					v.reference==s.reference && same_render_carrier(s.owner,v.owner) && needed(*v.definition,v.ammo,v.rack_held);
			}
			return false;
		}
		void present(const eye_composition::event& event,ID3D11DeviceContext* context,
			ID3D11ShaderResourceView*,ID3D11RenderTargetView* target) noexcept
		{
			if(!alive.load() || event.eye>1)return;
			if(event.eye==0)
			{
				pair={};pair.id=event.pair_id;pair.publication=event.views.eyes[0].publication;pair.device=event.device_generation;
				pair.strength=pulse(std::chrono::duration<double>(clock::now().time_since_epoch()).count());
				if(!enabled() || !event.model_origins.valid)return;
				std::array<weapon_identity,15> owners{};
				{const std::lock_guard lock(mutex);for(size_t i=0;i<latest.size();++i)owners[i]=latest[i].owner.id();}
				std::array<opaque_mesh::matrix,2> vp{};
				for(unsigned eye=0;eye<2;++eye)
				{
					if(event.views.eyes[eye].pair_id!=event.pair_id || event.views.eyes[eye].output_eye!=eye)return;
					std::memcpy(vp[eye].data(),event.views.eyes[eye].bytes.data()+engine_stereo_view::h2_current_view_projection_offset,sizeof(vp[eye]));
				}
				for(const auto owner:owners)
				{
					if(!owner)continue;
					weapon_render_pose::snapshot rendered;if(!weapon_render_pose::for_scene(event.views,rendered,owner))continue;
					sample selected;
					{
						const std::lock_guard lock(mutex);
						if(!scene_models::latest_skeleton_pose(poses,cursor,rendered,
							[&](const sample& v)noexcept{return v.owner.id()==owner;},selected)){++misses;continue;}
					}
					if(!selected.count || !current(selected))continue;
					for(size_t i=0;i<selected.count && pair.count<pair.draws.size();++i)
					{
						const auto& part=selected.parts[i];if(!part.asset.geometry)continue;
						auto& draw=pair.draws[pair.count++];draw.geometry=part.asset.geometry;
						const auto model=hands::pose_math::compose(part.pose,part.asset.in_part);
						for(unsigned eye=0;eye<2;++eye)draw.transform[eye]=opaque_mesh::transform(model,event.model_origins.placement,event.model_origins.eyes[eye],vp[eye]);
					}
				}
				if(pair.count)++pairs;
			}
			if(pair.id!=event.pair_id || pair.publication!=event.views.eyes[0].publication || pair.device!=event.device_generation ||
				!pair.count || (event.eye==1 && !pair.left_drawn))return;
			std::array<opaque_mesh::draw,90> batch;
			for(size_t i=0;i<pair.count;++i)batch[i]={pair.draws[i].geometry.get(),pair.draws[i].transform[event.eye]};
			const bool ok=renderer.render(context,target,event.scene_depth,{batch.data(),pair.count},event.width,event.height,pair.strength.tint,pair.strength.emission);
			if(ok)++draws;else ++failures;
			if(event.eye==0)pair.left_drawn=ok;
		}
	}
	void publish(const sample& value) noexcept
	{
		if(!alive || !enabled() || !value.owner.id() || !value.object || !value.matrices || value.count>value.parts.size())return;
		const std::lock_guard lock(mutex);
		auto* slot=&latest[0];
		for(auto& s:latest)if(s.owner.id()==value.owner.id()){slot=&s;break;}else if(!s.owner.id() || s.at<slot->at)slot=&s;
		*slot=value;poses[cursor++%poses.size()]=value;
	}
	std::string render_status()
	{return status()+std::format("guide_pairs={} eye_draws={} pose_misses={} gpu_failures={}\n",pairs.load(),draws.load(),misses.load(),failures.load());}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			initialize();eye_composition::set_consumer(present,eye_composition::layer::weapon_guides);
			scheduler::loop([]{if(alive.load())refresh();},scheduler::pipeline::main,250ms);
			fastfiles::on_pre_unload([]{
				{const std::lock_guard lock(mutex);latest={};poses={};cursor=0;}
				retire_after_drain();
			});
		}
		void pre_destroy() override
		{alive=false;eye_composition::set_consumer(nullptr,eye_composition::layer::weapon_guides);clear();}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::chambering_guide::component)
