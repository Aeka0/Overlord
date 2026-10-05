#include <std_include.hpp>
#include "optic_runtime.hpp"
#include "optic_geometry.hpp"
#include "weapon_render_pose.hpp"
#include "weapon_interaction.hpp"
#include "viewmodel_visibility.hpp"
#include "../eye_composition.hpp"
#include <utils/native_memory.hpp>
#include "../native_thermal.hpp"
#include "../thermal_scene_policy.hpp"
#include "../spatial_panel_renderer.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::weapons::optics
{
	namespace
	{
		game::dvar_t* enabled{};
		game::dvar_t* magnification{};
		game::dvar_t* render_scene{};
		std::atomic_bool alive{true};
		bool world_contract{};
		struct request_state
		{
			hold owner{};
			std::uint64_t reference{}, continuity{};
			controller_input::clock::time_point at{};
			ads_control control{};
		};
		std::mutex request_mutex;
		request_state requested;
		std::atomic_uint64_t bound{}, rejected{}, drawn{}, missed{};
		std::atomic_uint64_t render_requests{}, rendered_images{};
		std::atomic<const char*> reason{"waiting for a supported optic"};
		std::atomic<const char*> binding_reason{"no supported optic sampled"};
		std::string_view native_name(const char* value) noexcept
		{
			return value ? std::string_view(value, strnlen_s(value, 256)) : std::string_view{};
		}
		reticle_image_state reticle_state(const game::GfxImage* address, const game::GfxImage& image) noexcept
		{
			return {reinterpret_cast<std::uintptr_t>(address),
			        reinterpret_cast<std::uintptr_t>(image.texture.shaderView),
			        image.width,
			        image.height,
			        image.mapType == game::MAPTYPE_2D};
		}
		std::uintptr_t reticle_image(const game::Material* material) noexcept
		{
			if (!material || !material->textureTable || !material->textureCount ||
			    material->textureCount > 16)
				return 0;
			const game::GfxImage* selected{};
			for (unsigned i = 0; i < material->textureCount; ++i)
			{
				const auto& texture = material->textureTable[i];
				if (material->textureCount != 1 && texture.semantic != 2)
					continue; // native color map
				const auto* image = texture.u.image;
				if (!image || !reticle_state(image, *image).identity() || (selected && selected != image))
					return 0;
				selected = image;
			}
			return reinterpret_cast<std::uintptr_t>(selected);
		}
		struct pair_view
		{
			std::uint64_t pair{}, publication{}, device{};
			std::array<projected_view, 2> eyes{};
			std::array<float, 2> near_distance{};
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> reticle;
			std::uint64_t owner{}, generation{};
			std::uint64_t reference{}, revision{};
			bool ready{}, independent{}, thermal{};
		};
		thread_local pair_view pair;
		thread_local spatial_panel::renderer renderer;
		bool prepare(const eye_composition::event& event)
		{
			pair = {};
			pair.pair = event.pair_id;
			pair.publication = event.views.eyes[0].publication;
			pair.device = event.device_generation;
			pair.independent = render_scene && render_scene->current.enabled;
			if (!event.model_origins.valid)
			{
				reason = "missing current model origins";
				return false;
			}
			const auto owner = current_hold();
			weapon_render_pose::snapshot pose;
			if (!owner.can_fire() || !weapon_render_pose::for_scene(event.views, pose, owner.id()) ||
			    pose.muzzle.owner.id() != owner.id() || pose.muzzle.owner.rear != owner.rear ||
			    pose.muzzle.owner.rear_revision != owner.rear_revision || !pose.muzzle.optic.active)
			{
				reason = "no scene-bound active optic";
				return false;
			}
			const auto& lens = pose.muzzle.optic;
			pair.thermal = lens.thermal;
			if (pair.thermal && (!pair.independent || !native_thermal::ready()))
			{
				reason = "thermal optic requires verified native scene rendering";
				return false;
			}
			pair.owner = owner.id().weapon;
			pair.generation = owner.id().generation;
			pair.reference = pose.muzzle.reference_generation;
			pair.revision = owner.rear_revision;
			if (!valid_model_anchor(lens.center))
			{
				reason = "invalid lens anchor";
				return false;
			}
			game::GfxImage image{};
			if (!lens.reticle_image ||
			    !utils::native_memory::read_bytes(
			        &image, reinterpret_cast<const void*>(lens.reticle_image), sizeof(image)) ||
			    !reticle_state(reinterpret_cast<const game::GfxImage*>(lens.reticle_image), image).ready())
			{
				reason = "native reticle image unavailable";
				return false;
			}
			pair.reticle = image.texture.shaderView;
			const auto center = place_model_anchor(lens.center, event.model_origins.placement);
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				spatial_panel::matrix vp{};
				std::memcpy(vp.data(),
				            event.views.eyes[eye].bytes.data() +
				                engine_stereo_view::h2_current_view_projection_offset,
				            sizeof(vp));
				pair.eyes[eye] = project(lens,
				                         center,
				                         event.model_origins.eyes[eye],
				                         vp,
				                         pose.muzzle.units_per_meter,
				                         pair.independent);
				pair.near_distance[eye] =
				    scene_near_distance(lens, center, event.model_origins.eyes[eye], vp);
			}
			pair.ready = pair.eyes[0].valid || pair.eyes[1].valid;
			return pair.ready;
		}
		thermal_scene::world_request plan_world(const eye_composition::event& event) noexcept
		{
			if (!alive.load() || !world_contract || event.views.screen_scope_epoch ||
			    event.views.weapon_display_epoch)
				return {};
			const auto owner = current_hold();
			weapon_render_pose::snapshot pose;
			if (!owner.can_fire() || !weapon_render_pose::for_scene(event.views, pose, owner.id()) ||
			    pose.muzzle.owner.rear != owner.rear ||
			    pose.muzzle.owner.rear_revision != owner.rear_revision || !pose.muzzle.optic.thermal)
				return {};
			// This native public scale is independent of CG's thermal fade. Read
			// once for the admitted pair; never set either global control or replay
			// script/vision state on a renderer or native worker thread.
			const auto* scale = *reinterpret_cast<game::dvar_t**>(0x14EE3ABE8);
			if (!scale || std::uint32_t(scale->hash) != 0xABCCE045 || scale->type != game::value ||
			    !thermal_scene::valid_scale(scale->current.value))
				return {};
			return {scale->current.value, true};
		}
		auxiliary_scene::request plan_scene(const eye_composition::event& event) noexcept
		{
			auxiliary_scene::request result;
			if (!alive.load() || !enabled || !enabled->current.enabled || !render_scene ||
			    !render_scene->current.enabled)
				return result;
			try
			{
				if (!prepare(event))
					return result;
				thread_local unsigned preferred = 2;
				thread_local std::uint64_t owner{}, generation{};
				if (owner != pair.owner || generation != pair.generation)
					preferred = 2;
				owner = pair.owner;
				generation = pair.generation;
				std::array<spatial_panel::vec4, 2> windows{};
				std::array<bool, 2> valid{};
				std::array<float, 2> distance{};
				for (unsigned eye = 0; eye < 2; ++eye)
				{
					valid[eye] = sampling_window(pair.eyes[eye], windows[eye]);
					distance[eye] =
					    std::hypot(pair.eyes[eye].parameters[0] - .5f, pair.eyes[eye].parameters[1] - .5f);
				}
				unsigned selected = valid[0] ? 0u : valid[1] ? 1u : 2u;
				if (valid[0] && valid[1])
				{
					selected = distance[0] <= distance[1] ? 0u : 1u;
					if (preferred < 2 && distance[preferred] <= distance[selected] + .1f)
						selected = preferred;
				}
				preferred = selected;
				if (selected >= 2)
				{
					reason = "no covered optical eye box";
					return result;
				}
				result = {selected, windows[selected], pair.owner, pair.generation, true};
				result.reference = pair.reference;
				result.revision = pair.revision;
				result.full_thermal = pair.thermal;
				result.near_distance = pair.near_distance[selected];
				++render_requests;
				return result;
			}
			catch (...)
			{
				pair = {};
				reason = "optic planning exception";
				return {};
			}
		}
		void compose(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             ID3D11ShaderResourceView* source,
		             ID3D11RenderTargetView* target) noexcept
		{
			if (!alive.load() || !enabled || !enabled->current.enabled || event.eye > 1)
				return;
			try
			{
				if (pair.pair != event.pair_id || pair.device != event.device_generation ||
				    pair.publication != event.views.eyes[event.eye].publication)
					(void)prepare(event);
				if (!pair.ready || pair.pair != event.pair_id || pair.device != event.device_generation ||
				    pair.publication != event.views.eyes[event.eye].publication ||
				    !pair.eyes[event.eye].valid)
				{
					++missed;
					return;
				}
				const auto& eye = pair.eyes[event.eye];
				auto parameters = eye.parameters;
				spatial_panel::vec4 window{0, 0, 1, 1};
				auto* image = source;
				bool independent_image{};
				if (pair.independent)
				{
					independent_image =
					    event.auxiliary && event.auxiliary->valid && event.auxiliary->eye == event.eye &&
					    event.auxiliary->owner == pair.owner &&
					    event.auxiliary->generation == pair.generation &&
					    event.auxiliary->reference == pair.reference &&
					    event.auxiliary->revision == pair.revision &&
					    event.auxiliary->full_thermal == pair.thermal && event.auxiliary_image;
					if (independent_image)
					{
						image = event.auxiliary_image;
						window = event.auxiliary->window;
					}
					else
						parameters[3] = 0;
					if (!event.scene_depth)
					{
						++missed;
						reason = "optic scene depth unavailable";
						return;
					}
				}
				if (renderer.draw_optic(context,
				                        image,
				                        pair.reticle.Get(),
				                        target,
				                        eye.corners,
				                        event.width,
				                        event.height,
				                        parameters,
				                        pair.independent ? event.scene_depth : nullptr,
				                        window,
				                        eye.pupil_radius,
				                        eye.eye_box_scale))
				{
					++drawn;
					if (independent_image && parameters[3] > 0)
					{
						++rendered_images;
						reason = pair.thermal ? "native thermal scene lens" : "independent scene lens";
					}
					else
						reason = pair.independent    ? "no independent image for this eye"
						         : parameters[3] > 0 ? "same-eye magnified lens"
						                             : "outside optical eye box";
				}
				else
				{
					++missed;
					reason = "optic GPU contract rejected";
				}
			}
			catch (...)
			{
				pair = {};
				++missed;
				reason = "optic presentation exception";
			}
		}
	}
	binding bind(std::span<game::XModel* const> models,
	             std::span<const hands::model_definition> definitions) noexcept
	{
		binding output;
		if (models.size() != definitions.size() || models.size() > 32 ||
		    !viewmodel_visibility::ready(part_visibility::rigid_groups))
			return {};
		for (size_t m = 0; m < models.size(); ++m)
		{
			const auto* def = find(definitions[m].name);
			if (!def)
				continue;
			if (output.lens)
			{
				++rejected;
				binding_reason = "multiple magnified optics in one assembly";
				return {};
			}
			const auto* model = models[m];
			if (!model || model->numLods != 1 || !model->numBones || !model->boneNames ||
			    !model->materialHandles || !model->numsurfs || model->numsurfs > 32 ||
			    model->lodInfo[0].surfIndex != 0 || model->lodInfo[0].numsurfs != model->numsurfs ||
			    !model->lodInfo[0].surfs ||
			    native_name(game::SL_ConvertToString(model->boneNames[0])) != def->root)
			{
				++rejected;
				binding_reason = "unsupported native optic model metadata";
				return {};
			}
			binding candidate;
			candidate.lens = def;
			candidate.root = definitions[m].begin;
			unsigned count{};
			bool has_lens{};
			for (unsigned s = 0; s < model->numsurfs; ++s)
			{
				const auto* material = model->materialHandles[s];
				if (!material)
					continue;
				const auto name = native_name(material->info.name);
				const auto role = classify_material(*def, name);
				const bool lens = role == material_role::lens, reticle = role == material_role::reticle;
				if (reticle)
				{
					const auto image = reticle_image(material);
					if (candidate.reticle_image && candidate.reticle_image != image)
					{
						++rejected;
						binding_reason = "ambiguous native reticle image";
						return {};
					}
					candidate.reticle_image = image;
				}
				if (role == material_role::unrelated)
					continue;
				const auto* surface = model->lodInfo[0].surfs + s;
				// Reuse the established rigid-group filter. Unknown/skinned optics
				// stay opaque rather than changing another mesh or global material.
				if ((surface->flags & 4) || surface->subdivLevelCount || !surface->rigidVertLists ||
				    !surface->rigidVertListCount || surface->rigidVertListCount > 32 ||
				    count == candidate.hidden.size())
				{
					++rejected;
					binding_reason = "unsupported native lens surface layout";
					return {};
				}
				candidate.hidden[count++] = surface;
				has_lens |= lens;
			}
			if (!has_lens || !candidate.reticle_image)
			{
				++rejected;
				binding_reason =
				    has_lens ? "native reticle color map missing" : "native lens material missing";
				return {};
			}
			output = candidate;
		}
		if (output.lens)
		{
			++bound;
			binding_reason = "native lens and reticle matched";
		}
		return output;
	}
	ads_comfort::binding bind_comfort(std::span<game::XModel* const> models,
	                                  std::span<const hands::model_definition> definitions) noexcept
	{
		ads_comfort::binding output;
		if (models.size() != definitions.size() || models.size() > 32)
			return {};
		for (size_t m = 0; m < models.size(); ++m)
		{
			const auto optic = ads_comfort::classify(definitions[m].name);
			if (optic.type == ads_comfort::sight::unchanged)
				continue;
			if (output.root >= 0)
				return {};
			const auto* model = models[m];
			if (!model || !model->numBones || !model->boneNames || definitions[m].begin < 0 ||
			    native_name(game::SL_ConvertToString(model->boneNames[0])) != optic.root)
				return {};
			output.optic = optic;
			output.root = definitions[m].begin;
			if (optic.lens)
				continue;
			if (!model->baseMat)
				return {};
			const auto& base = model->baseMat[0];
			const hands::quat rotation{base.quat[0], base.quat[1], base.quat[2], base.quat[3]};
			const hands::vec origin{base.trans[0], base.trans[1], base.trans[2]};
			if (!ads_comfort::valid_rotation(rotation) || !ads_comfort::finite(origin))
				return {};
			const auto inverse = hands::conjugate(hands::normalize(rotation));
			for (unsigned c = 0; c < 8; ++c)
			{
				hands::vec point;
				for (unsigned axis = 0; axis < 3; ++axis)
				{
					const float center = model->bounds.midPoint[axis], extent = model->bounds.halfSize[axis];
					if (!std::isfinite(center) || std::abs(center) > 1000 || !std::isfinite(extent) ||
					    extent <= 0 || extent > 1000)
						return {};
					point[axis] = center + ((c & (1u << axis)) ? extent : -extent);
				}
				output.corners[c] = hands::rotate(inverse, hands::sub(point, origin));
			}
		}
		return output;
	}
	void request(const hold& owner, ads_control control, const controller_input::frame& input) noexcept
	{
		const std::lock_guard lock(request_mutex);
		requested = {
		    owner, input.reference_generation, input.continuity_generation, input.sampled_at, control};
	}
	ads_control control_for(const hold& owner, const controller_input::frame& input) noexcept
	{
		if (!alive.load() || !ads_alignment::tracked_hold(input, owner) || !input.sequence ||
		    !input.focused || input.orientation_settling)
			return {};
		request_state state;
		{
			const std::lock_guard lock(request_mutex);
			state = requested;
		}
		const auto now = controller_input::clock::now();
		if (!state.control.allowed || state.owner.id() != owner.id() || state.owner.rear != owner.rear ||
		    state.owner.attachment != owner.attachment || state.owner.rear_revision != owner.rear_revision ||
		    state.reference != input.reference_generation ||
		    state.continuity != input.continuity_generation || now < state.at ||
		    now - state.at > std::chrono::milliseconds(150) || now < input.sampled_at ||
		    now - input.sampled_at > std::chrono::milliseconds(150))
			return {};
		// Keep the presentation permission across a support-only revision so
		// release/regrip can ease the existing offset. Never retain native ADS
		// through that revision, even before the next command sees the new grip.
		return {true,
		        state.control.active && ads_alignment::supported(input, owner) &&
		            state.owner.support == owner.support && state.owner.revision == owner.revision};
	}
	view present(const binding& bound_lens,
	             const hold& owner,
	             const controller_input::frame& input,
	             std::span<const hands::bone> bones,
	             const hands::vec& view_offset,
	             float units,
	             bool ads_requested) noexcept
	{
		view result;
		result.thermal = bound_lens.lens && bound_lens.lens->thermal;
		// ADS intent validates the translated muzzle before the lens is active.
		// Keep the admitted capability so a valid >20 cm thermal approach cannot
		// cancel the very ADS request needed to publish its image next frame.
		if (!ads_requested || !alive.load() || !enabled || !enabled->current.enabled || !bound_lens.lens ||
		    bound_lens.root < 0 || size_t(bound_lens.root) >= bones.size() || !owner.can_fire() ||
		    !input.focused || input.orientation_settling || !input.grip[int(owner.rear)].valid ||
		    !std::isfinite(units) || units <= 0 || units > 10000)
			return result;
		if (bound_lens.lens->thermal &&
		    (!render_scene || !render_scene->current.enabled || !native_thermal::ready()))
			return result;
		game::GfxImage image{};
		if (!bound_lens.reticle_image ||
		    !utils::native_memory::read_bytes(
		        &image, reinterpret_cast<const void*>(bound_lens.reticle_image), sizeof(image)) ||
		    !reticle_state(reinterpret_cast<const game::GfxImage*>(bound_lens.reticle_image), image).ready())
			return result;
		const auto& root = bones[bound_lens.root];
		const auto rotation = hands::normalize(root.rotation);
		result.center = {
		    true,
		    hands::add(root.position,
		               hands::rotate(rotation, hands::scale(bound_lens.lens->center_meters, units))),
		    view_offset};
		result.axis = {hands::rotate(rotation, {1, 0, 0}),
		               hands::rotate(rotation, {0, 1, 0}),
		               hands::rotate(rotation, {0, 0, 1})};
		result.radius = bound_lens.lens->radius_meters * units;
		result.pupil_radius = bound_lens.lens->pupil_radius;
		result.scene_clearance = bound_lens.lens->scene_clearance_meters * units;
		result.thermal = bound_lens.lens->thermal;
		result.magnification = magnification && magnification->current.value >= 1
		                           ? magnification->current.value
		                           : bound_lens.lens->magnification;
		result.reticle_image = bound_lens.reticle_image;
		result.active = valid_model_anchor(result.center);
		for (const auto& axis : result.axis)
			for (float value : axis)
				if (!std::isfinite(value))
					return {};
		return result;
	}
	class component final : public component_interface
	{
		eye_composition::consumer_registration composition_registration;
		void post_unpack() override
		{
			enabled = dvars::register_bool("vr_scopeZoom",
			                               true,
			                               game::DVAR_FLAG_SAVED,
			                               "Present magnification inside supported physical scopes");
			magnification =
			    dvars::register_float("vr_scopeMagnification",
			                          0,
			                          0,
			                          12,
			                          game::DVAR_FLAG_SAVED,
			                          "Scope image magnification override; 0 uses prototype optic defaults");
			render_scene =
			    dvars::register_bool("vr_scopeRender",
			                         true,
			                         game::DVAR_FLAG_SAVED,
			                         "Render one additional narrow scene view for the active scope");
			world_contract = native_thermal::world_ready();
			if (world_contract)
				thermal_scene::plan = plan_world;
			else
				console::error("[VR optics] native thermal world contract rejected\n");
			auxiliary_scene::plan.store(plan_scene);
			composition_registration =
			    eye_composition::register_consumer(compose, eye_composition::layer::optics);
			command::add(
			    "vr_optics_status",
			    []
			    {
				    console::info(
				        "[VR optics] enabled=%d bound=%llu rejected=%llu draws=%llu misses=%llu binding=%s presentation=%s mode=%s render_requests=%llu rendered_images=%llu world_contract=%d world_pairs=%llu native_ssr=%g world_ssr=%g\n",
				        enabled && enabled->current.enabled,
				        bound.load(),
				        rejected.load(),
				        drawn.load(),
				        missed.load(),
				        binding_reason.load(),
				        reason.load(),
				        render_scene && render_scene->current.enabled ? "extra_scene" : "same_eye_image",
				        render_requests.load(),
				        rendered_images.load(),
				        world_contract,
				        thermal_scene::world_pairs.load(),
				        thermal_scene::last_native_ssr.load(),
				        thermal_scene::last_world_ssr.load());
			    });
		}
		void pre_destroy() override
		{
			alive = false;
			thermal_scene::plan = nullptr;
			auxiliary_scene::plan.store(nullptr);
			composition_registration.reset();
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::optics::component)
