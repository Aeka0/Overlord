#include <std_include.hpp>
#include "native_magazine_assets.hpp"
#include "weapon_reload_profiles.hpp"
#include "viewmodel_visibility.hpp"
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler_context.hpp"
#include <utils/native_memory.hpp>
#include "game/game.hpp"

namespace vr::gameplay::weapons::physical_reload::magazine_assets
{
	namespace
	{
		constexpr size_t max_population_states = 12;
		using bone_selection = std::array<unsigned, 16>;
		constexpr unsigned missing_bone = 256;

		struct prepared_magazine
		{
			std::array<scene_models::rigid_part, max_population_states> parts;
			std::array<asset, max_population_states> views;
			scene_models::rigid_part cartridge;
			asset cartridge_view{};
			const reload_profile* definition{};
			game::XModel* source{};
			game::XSurface* surfaces{};
		};

		// Native queued packets borrow these immutable descriptors. Selection can
		// change at a checkpoint; ownership lasts until the native unload drain.
		std::array<std::unique_ptr<prepared_magazine>, reload_profiles.capacity() * 2> retained;
		std::array<std::atomic<const prepared_magazine*>, reload_profiles.capacity()> selected{};
		std::array<std::atomic<game::XModel*>, reload_profiles.capacity()> ordinary{};
		std::array<std::atomic<const char*>, reload_profiles.capacity()> reasons{};
		std::array<clock::time_point, reload_profiles.capacity()> last_attempt{};

		const char* ready_status(const reload_profile& definition)
		{
			return definition.magazine_fill() ? "counted magazine 0/1/2/3 ready"
			                                  : "exact receiver magazine/round subsets ready";
		}

		game::XModel* find_source(const char* name)
		{
			if (!name)
				return nullptr;
			auto* model = game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, name, 0).model;
			return model && model->name && std::string_view(model->name) == name ? model : nullptr;
		}

		bool prepare_receiver_visibility(const reload_profile& definition)
		{
			if (!definition.skinned_receiver)
				return true;
			auto* receiver = find_source(definition.skinned_receiver);
			return receiver && viewmodel_visibility::prepare_skinned_part(receiver, definition.magazine_bone);
		}

		bool matches_fill_topology(game::XModel* source, const magazine_fill_recipe& recipe)
		{
			game::XModel model;
			if (!valid_magazine_fill(recipe) ||
			    !utils::native_memory::read_bytes(&model, source, sizeof(model)) ||
			    model.numBones != recipe.bones || model.numsurfs != recipe.surfaces.size() ||
			    model.numLods != 1 || !model.lodInfo[0].surfs)
				return false;

			for (size_t index = 0; index < recipe.surfaces.size(); ++index)
			{
				game::XSurface surface;
				if (!utils::native_memory::read_bytes(
				        &surface, model.lodInfo[0].surfs + index, sizeof(surface)) ||
				    surface.vertCount != recipe.surfaces[index][0] ||
				    surface.triCount != recipe.surfaces[index][1])
					return false;
			}
			return true;
		}

		bool bind_mesh_bones(const game::XModel& source,
		                     const magazine_mesh_recipe& recipe,
		                     bone_selection& bones)
		{
			if (!source.boneNames || !recipe || recipe.count > bones.size() ||
			    recipe.subsets > max_population_states)
				return false;
			bones.fill(missing_bone);
			for (unsigned bone = 0; bone < source.numBones; ++bone)
			{
				const auto* name = game::SL_ConvertToString(source.boneNames[bone]);
				if (!name)
					return false;
				for (size_t part = 0; part < recipe.count; ++part)
				{
					if (recipe.names[part] != name)
						continue;
					if (bones[part] != missing_bone)
						return false;
					bones[part] = bone;
				}
			}
			return std::find(bones.begin(), bones.begin() + recipe.count, missing_bone) ==
			       bones.begin() + recipe.count;
		}

		bool matches_fill_bounds(const game::XModel& model,
		                         const magazine_fill_recipe& recipe,
		                         size_t population)
		{
			const auto& bounds = model.bounds;
			for (unsigned axis = 0; axis < 3; ++axis)
			{
				const float low = bounds.midPoint[axis] - bounds.halfSize[axis];
				const float high = bounds.midPoint[axis] + bounds.halfSize[axis];
				if (!(std::abs(low - recipe.low[population][axis]) < .02f) ||
				    !(std::abs(high - recipe.high[population][axis]) < .02f))
					return false;
			}
			return true;
		}

		asset part_view(scene_models::rigid_part& part)
		{
			const auto bind = part.bind();
			const hands::anchor source_pose{{bind[0], bind[1], bind[2]},
			                                {bind[3], bind[4], bind[5], bind[6]}};
			return {part.model(), inverse_reload(source_pose)};
		}

		bool create_population(scene_models::rigid_part& part,
		                       game::XModel* source,
		                       const magazine_mesh_recipe& recipe,
		                       const bone_selection& bones,
		                       const magazine_fill_recipe* fill,
		                       size_t population,
		                       const char*& reason)
		{
			bool created;
			if (fill)
			{
				// Face recipes can select several rounds within one bone, or a
				// nested round bone. The rigid factory validates every face owner.
				created = part.create_face_partition(
				    source, bones[0], std::span(bones).first(recipe.count), fill->faces[population]);
			}
			else
			{
				bone_selection included{};
				const auto count = recipe.selected_count(population);
				for (size_t index = 0; index < count; ++index)
					included[index] = bones[recipe.selected_index(population, index)];
				created = part.create(source, bones[0], std::span(included).first(count));
			}
			if (!created)
			{
				reason = part.status();
				return false;
			}
			if (fill && !matches_fill_bounds(*part.model(), *fill, population))
			{
				reason = "counted magazine geometry bounds rejected";
				return false;
			}
			return true;
		}

		bool same_face_selection(std::span<const scene_models::surface_face_range> a,
		                         std::span<const scene_models::surface_face_range> b)
		{
			return a.size() == b.size() &&
			       std::equal(a.begin(),
			                  a.end(),
			                  b.begin(),
			                  [](const auto& x, const auto& y)
			                  { return x.surface == y.surface && x.first == y.first && x.last == y.last; });
		}

		bool create_stack_populations(prepared_magazine& prepared,
		                              game::XModel* source,
		                              const magazine_mesh_recipe& recipe,
		                              const bone_selection& bones,
		                              const magazine_fill_recipe& fill,
		                              const char*& reason)
		{
			std::array<scene_models::rigid_part, 4> components;
			std::array<scene_models::rigid_part::instance, 4> instances{};
			if (recipe.body_count >= recipe.count)
			{
				reason = "authored stack has no cartridge source bones";
				return false;
			}
			for (size_t index = 0; index < components.size(); ++index)
			{
				const auto faces = index ? fill.stack->rounds[index - 1].faces : fill.faces[0];
				const scene_models::rigid_part* geometry{};
				for (size_t previous = 1; previous < index; ++previous)
				{
					const auto earlier = fill.stack->rounds[previous - 1].faces;
					if (same_face_selection(faces, earlier))
					{
						geometry = instances[previous].geometry;
						break;
					}
				}
				if (geometry)
				{
					instances[index] = {geometry, fill.stack->rounds[index - 1].translation};
					continue;
				}
				const auto members =
				    index ? std::span(bones).subspan(recipe.body_count, recipe.count - recipe.body_count)
				          : std::span(bones).first(recipe.body_count);
				// Templates retain source-bind coordinates. Only the body's anchor
				// becomes the combined model anchor; donor bones cannot select body faces.
				if (!components[index].create_static_face_partition(source, members.front(), members, faces))
				{
					reason = components[index].status();
					return false;
				}
				instances[index] = {&components[index],
				                    index ? fill.stack->rounds[index - 1].translation : hands::vec{}};
			}
			scene_models::rigid_part follower;
			if (!fill.stack->follower_faces.empty() &&
			    !follower.create_static_face_partition(
			        source, bones[0], std::span(bones).first(recipe.body_count), fill.stack->follower_faces))
			{
				reason = follower.status();
				return false;
			}
			for (size_t population = 0; population < prepared.parts.size() && population < 4; ++population)
			{
				std::array<scene_models::rigid_part::instance, 5> draws{};
				auto count = population + 1;
				std::copy_n(instances.begin(), count, draws.begin());
				if (follower.model())
					draws[count++] = {&follower, fill.stack->follower_translations[population]};
				auto& part = prepared.parts[population];
				if (!part.create_instances(std::span(draws).first(count)))
				{
					reason = part.status();
					return false;
				}
				if (!matches_fill_bounds(*part.model(), fill, population))
				{
					reason = "authored magazine stack bounds rejected";
					return false;
				}
			}
			return true;
		}

		std::unique_ptr<prepared_magazine> prepare_subsets(game::XModel* source,
		                                                   const reload_profile& definition,
		                                                   const char*& reason)
		{
			// A counted replacement still needs the native receiver's removal mask.
			// In particular M4 owns magazine faces inside a shared skinned surface.
			if (!prepare_receiver_visibility(definition))
			{
				reason = "native magazine visibility preparation rejected";
				return {};
			}
			const auto recipe = definition.magazine_mesh();
			const auto* fill = definition.magazine_fill();
			if (fill && !matches_fill_topology(source, *fill))
			{
				reason = "counted magazine topology rejected";
				return {};
			}
			bone_selection bones;
			if (!bind_mesh_bones(*source, recipe, bones))
			{
				reason = "magazine/round skeleton rejected";
				return {};
			}

			auto prepared = std::make_unique<prepared_magazine>();
			const bool stacked = fill && fill->stack;
			if (stacked && !create_stack_populations(*prepared, source, recipe, bones, *fill, reason))
				return {};
			std::array<scene_models::runtime_model, max_population_states + 1> identities{};
			size_t identity_count = recipe.subsets;
			for (size_t population = 0; population < recipe.subsets; ++population)
			{
				auto& part = prepared->parts[population];
				if (!stacked && !create_population(part, source, recipe, bones, fill, population, reason))
					return {};
				prepared->views[population] = part_view(part);
				identities[population] = {part.model(), part.source()};
			}
			if (definition.interaction.manual_bolt)
			{
				auto& cartridge = prepared->cartridge;
				const auto cartridge_bone = bones[recipe.body_count];
				if (!cartridge.create(source, cartridge_bone, std::span(bones).subspan(recipe.body_count, 1)))
				{
					reason = cartridge.status();
					return {};
				}
				prepared->cartridge_view = part_view(cartridge);
				identities[identity_count++] = {cartridge.model(), cartridge.source()};
			}
			std::fill(prepared->views.begin() + recipe.subsets,
			          prepared->views.end(),
			          prepared->views[recipe.subsets - 1]);

			// No native identity or render pointer is published until the entire
			// set is valid. A failure destroys only this unpublished candidate.
			if (!scene_models::register_runtime_models(std::span(identities).first(identity_count)))
			{
				reason = "native magazine subset identity registration rejected";
				return {};
			}
			prepared->definition = &definition;
			prepared->source = source;
			prepared->surfaces = source->lodInfo[0].surfs;
			return prepared;
		}

		const prepared_magazine* find_prepared(game::XModel* source, const reload_profile& definition)
		{
			for (const auto& prepared : retained)
				if (prepared && prepared->source == source &&
				    prepared->surfaces == source->lodInfo[0].surfs && prepared->definition == &definition)
					return prepared.get();
			return nullptr;
		}

		void refresh_ordinary(size_t index, game::XModel* source, const reload_profile& definition)
		{
			auto* model = source->numBones == 1 ? source : nullptr;
			if (model && !prepare_receiver_visibility(definition))
				model = nullptr;
			ordinary[index] = model;
			reasons[index] =
			    model ? "native independent magazine ready" : "native magazine/visibility rejected";
		}

		void refresh_profile(size_t index)
		{
			const auto& definition = *reload_profiles[index];
			const auto* name = definition.rigid_magazine_source ? definition.rigid_magazine_source
			                                                    : definition.magazine_model;
			auto* source = find_source(name);
			if (!source)
			{
				selected[index] = nullptr;
				ordinary[index] = nullptr;
				reasons[index] = "required native asset not loaded";
				return;
			}
			if (!definition.rigid_magazine_source)
			{
				refresh_ordinary(index, source, definition);
				return;
			}

			const auto* cached = find_prepared(source, definition);
			selected[index] = cached;
			if (cached)
			{
				reasons[index] = ready_status(definition);
				return;
			}
			if (!viewmodel_visibility::ready(part_visibility::rigid_groups) || !scene_models::ready())
				return;
			const auto now = clock::now();
			if (now >= last_attempt[index] && now - last_attempt[index] < std::chrono::seconds(1))
				return;
			last_attempt[index] = now;

			auto slot =
			    std::find_if(retained.begin(), retained.end(), [](const auto& entry) { return !entry; });
			if (slot == retained.end())
			{
				reasons[index] = "retained magazine asset capacity reached";
				return;
			}
			const char* reason{};
			auto prepared = prepare_subsets(source, definition, reason);
			if (!prepared)
			{
				reasons[index] = reason;
				return;
			}
			*slot = std::move(prepared);
			selected[index] = slot->get();
			reasons[index] = ready_status(definition);
		}
	}

	asset get(const reload_profile* definition, int rounds) noexcept
	{
		const auto index = reload_profile_index(definition);
		if (index >= reload_profiles.size() || rounds < 0)
			return {};
		if (!definition->rigid_magazine_source)
			return {ordinary[index].load(), definition->rigid_in_magazine};
		const auto* prepared = selected[index].load();
		return prepared ? prepared->views[definition->magazine_subset(rounds)] : asset{};
	}

	const char* status(const reload_profile* definition) noexcept
	{
		const auto index = reload_profile_index(definition);
		const auto* reason = index < reload_profiles.size() ? reasons[index].load() : nullptr;
		return reason ? reason : "not prepared";
	}

	asset cartridge(const reload_profile* definition) noexcept
	{
		const auto index = reload_profile_index(definition);
		const auto* prepared = index < reload_profiles.size() ? selected[index].load() : nullptr;
		return prepared ? prepared->cartridge_view : asset{};
	}

	void clear() noexcept
	{
		for (auto& entry : selected)
			entry = nullptr;
		for (auto& entry : ordinary)
			entry = nullptr;
	}

	void retire_after_drain() noexcept
	{
		clear();
		for (auto& entry : retained)
			entry.reset();
		for (auto& reason : reasons)
			reason = "waiting for loaded assets after retirement";
		last_attempt = {};
	}

	void refresh()
	{
		if (!scheduler::is_executing(scheduler::pipeline::main))
			return;
		// Checkpoints reset player/time, not necessarily the native assets.
		if (!game::CL_IsCgameInitialized() || !game::g_entities[0].client)
		{
			clear();
			return;
		}
		for (size_t index = 0; index < reload_profiles.size(); ++index)
			refresh_profile(index);
	}
}
