#include <std_include.hpp>
#include "falling_item_presenter.hpp"
#include "falling_rail_presentation.hpp"
#include "physical_reload_presenter.hpp"
#include "../controller_input.hpp"
#include <utils/native_memory.hpp>
#include "component/scene_models.hpp"
#include "component/scene_submission_pool.hpp"
#include "component/scene_pose_match.hpp"
#include "component/fastfiles.hpp"
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include <mutex>

namespace vr::gameplay::falling_item_presentation
{
	namespace
	{
		using namespace hands;
		using clock=::vr::gameplay::motion::clock;
		struct ticket
		{
			game::XModel* model{};motion flight{};anchor submitted{};
			std::uint64_t reference{};
		};
		scene_models::submission_pool<ticket,4096> submissions;
		struct rail_state
		{
			weapons::weapon_identity origin{};std::uint64_t reference{};clock::time_point born{};
			const weapons::reload_profile* profile{};::vr::gameplay::motion::rail_presentation motion{};
		};
		std::array<rail_state,16> rails{};std::mutex rail_mutex;
		rail_state* rail_for(const motion& flight,std::uint64_t reference,clock::time_point now) noexcept
		{
			for(auto& state:rails)if(state.origin==flight.origin && state.reference==reference &&
				state.born==flight.path.born && state.profile==flight.rail_profile)return &state;
			for(auto& state:rails)if(!state.origin.weapon || (now>=state.born && now-state.born>::vr::gameplay::motion::flight_lifetime+150ms))
			{state={flight.origin,reference,flight.path.born,flight.rail_profile,{}};return &state;}
			return nullptr;
		}
		scene_models::placement_result prepare(const scene_models::preparation& preparing,const void* entry,
			game::GfxPlacement& placed,game::GfxPlacement& previous)noexcept
		{
			using result=scene_models::placement_result;
			const auto lease=submissions.lookup(entry);if(!lease)return result::unchanged;
			const auto& frozen=lease->payload;const auto now=clock::now();game::XModel* model{};
			if(!scene_models::submission_pool<ticket,4096>::fresh(*lease,now) || !frozen.model ||
				!scene_models::native_entry::model(entry,model) || model!=frozen.model ||
				frozen.reference!=controller_input::latest().reference_generation || !frozen.flight.path.alive(preparing.at) ||
				!scene_models::same_placement(frozen.submitted.position,frozen.submitted.rotation,placed))return result::omit;
			const auto at=std::max(preparing.at,lease->at);anchor root;const anchor* override=nullptr;
			if(frozen.flight.path.rail_seconds>0 && frozen.flight.origin.weapon && frozen.flight.rail_profile)
			{
				bool follow{};{const std::lock_guard lock(rail_mutex);const auto* state=rail_for(frozen.flight,frozen.reference,now);
					if(!state)return result::omit;follow=state->motion.needs_parent(frozen.flight.path,at);}
				anchor attached;const anchor* parent=nullptr;
				if(follow && weapons::physical_reload::magazine_rail_for_record(preparing,frozen.flight.origin,
					frozen.flight.rail_profile,frozen.reference,attached))parent=&attached;
				{const std::lock_guard lock(rail_mutex);auto* state=rail_for(frozen.flight,frozen.reference,now);
					if(!state)return result::omit;root=state->motion.pose(frozen.flight.path,at,parent);}
				override=&root;
			}
			const auto world=frozen.flight.pose(at,override);
			std::copy(world.position.begin(),world.position.end(),placed.origin);
			std::copy(world.rotation.begin(),world.rotation.end(),placed.quat);previous=placed;return result::replace;
		}
	}
	void submit(game::XModel* model,const motion& flight,std::uint64_t reference,float padding)
	{
		const auto now=clock::now();if(!model || !reference || !flight.path.alive(now))return;
		const auto world=flight.pose(now);
		const auto index=submissions.acquire({model,flight,world,reference},now);if(!index)return;
		game::GfxScaledPlacement placed{};placed.scale=1;
		std::copy(world.position.begin(),world.position.end(),placed.base.origin);
		std::copy(world.rotation.begin(),world.rotation.end(),placed.base.quat);
		float white[]{1,1,1,1};scene_models::submit(model,&placed,0,submissions.handle(*index),white,white,white,padding);
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			scene_models::on_prepare_placement(prepare);
			fastfiles::on_pre_unload([]{submissions.clear_after_drain();const std::lock_guard lock(rail_mutex);rails={};});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::falling_item_presentation::component)
