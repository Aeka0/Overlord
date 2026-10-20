#pragma once
#include "spent_case_geometry.hpp"
#include "native_weapon_family.hpp"
#include "component/scene_models.hpp"
#include "component/scheduler_context.hpp"
#include "game/game.hpp"
#include <utils/native_memory.hpp>

namespace vr::gameplay::weapons::spent_case
{
	struct source_recipe
	{
		const char* source;
		const char* bone;
		const recipe* geometry;
	};
	// Stable save identities, including the actual loaded camouflage material.
	inline constexpr std::array<source_recipe, 3> sources{{
	    {"h2_viewmodel_cheytac_base", "j_bullet", &cheytac},
	    {"h2_viewmodel_cheytac_base_desert", "j_bullet", &cheytac},
	    {"h2_viewmodel_m82_base", "tag_bullet_single", &barrett},
	}};
	inline unsigned variant_for(std::string_view weapon, std::string_view receiver = {}) noexcept
	{
		if (native_weapon_family(weapon, "barrett"))
			return 3;
		if (native_weapon_family(weapon, "cheytac"))
			return receiver == sources[1].source || weapon.ends_with("_desert") ? 2 : 1;
		return 0;
	}
	class assets
	{
		struct prepared
		{
			scene_models::rigid_part part;
			game::XModel* source{};
			game::XSurface* surfaces{};
			unsigned variant{};
		};
		std::array<std::unique_ptr<prepared>, 12> retained_;
		std::array<const char*, 3> reasons_{"not prepared", "not prepared", "not prepared"};
		std::array<game::XModel*, 3> selected_{};
		std::array<std::chrono::steady_clock::time_point, 3> attempts_{};

	  public:
		game::XModel* peek(unsigned variant) const noexcept
		{
			return variant && variant <= selected_.size() ? selected_[variant - 1] : nullptr;
		}
		game::XModel* get(unsigned variant) noexcept
		try
		{
			if (!variant || variant > sources.size() || !scene_models::ready() ||
			    !scheduler::is_executing(scheduler::pipeline::main))
				return nullptr;
			const auto& r = sources[variant - 1];
			auto& reason = reasons_[variant - 1];
			auto* source = game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, r.source, 0).model;
			game::XModel model{};
			if (!source || !utils::native_memory::read_bytes(&model, source, sizeof(model)) || !model.name ||
			    std::string_view(model.name) != r.source || !model.boneNames || !model.baseMat ||
			    !model.numBones || !model.lodInfo[0].surfs)
			{
				reason = "source receiver unavailable";
				return nullptr;
			}
			for (const auto& p : retained_)
				if (p && p->variant == variant && p->source == source &&
				    p->surfaces == model.lodInfo[0].surfs)
				{
					selected_[variant - 1] = p->part.model();
					return p->part.model();
				}
			const auto now = std::chrono::steady_clock::now();
			if (now >= attempts_[variant - 1] && now - attempts_[variant - 1] < std::chrono::seconds(2))
				return nullptr;
			attempts_[variant - 1] = now;
			unsigned bone = 256;
			for (unsigned n = 0; n < model.numBones; ++n)
			{
				const char* name = game::SL_ConvertToString(model.boneNames[n]);
				if (name && std::string_view(name) == r.bone)
				{
					if (bone != 256)
						return nullptr;
					bone = n;
				}
			}
			if (bone == 256)
			{
				reason = "single-round bone unavailable";
				return nullptr;
			}
			auto slot = std::find_if(retained_.begin(), retained_.end(), [](const auto& p) { return !p; });
			if (slot == retained_.end())
			{
				reason = "retained case asset budget exhausted";
				return nullptr;
			}
			auto next = std::make_unique<prepared>();
			if (!create(next->part, source, bone, *r.geometry))
			{
				reason = "single-round geometry or mouth validation rejected";
				return nullptr;
			}
			const std::array<scene_models::runtime_model, 1> identities{{{next->part.model(), source}}};
			if (!scene_models::register_runtime_models(identities))
			{
				reason = "case model registration rejected";
				return nullptr;
			}
			next->source = source;
			next->surfaces = model.lodInfo[0].surfs;
			next->variant = variant;
			auto* result = next->part.model();
			*slot = std::move(next);
			selected_[variant - 1] = result;
			reason = "native case with hollow mouth ready";
			return result;
		}
		catch (...)
		{
			if (variant && variant <= sources.size())
				reasons_[variant - 1] = "case allocation failed";
			return nullptr;
		}
		const char* status(unsigned variant) const noexcept
		{
			return variant && variant <= sources.size() ? reasons_[variant - 1] : "unknown case variant";
		}
		void clear() noexcept
		{
			selected_ = {};
			for (auto& p : retained_)
				p.reset();
			attempts_ = {};
			reasons_.fill("waiting for loaded receiver");
		}
	};
}
