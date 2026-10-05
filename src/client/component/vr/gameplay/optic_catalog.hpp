#pragma once
#include "hands/pose_solver.hpp"
#include <array>
#include <string_view>

namespace vr::gameplay::weapons::optics
{
	// Rear lens centers/radii from reviewed native SEModel geometry, centimeters
	// converted to meters. These are physical lenses, not flattened ADS meshes.
	struct definition
	{
		std::string_view model, root, lens, reticle;
		std::array<std::string_view,4> extra_hidden; // Exact additional materials, hidden only while the optic is active.
		hands::vec center_meters;
		float radius_meters, magnification;
		float pupil_radius{1.f}; // <1 calibrates the opaque reticle rim; independent of display size/eye-box tolerance.
		float scene_clearance_meters{}; // Rear lens to front housing; private optical scene near plane only.
		bool thermal{}; // Native thermal PostFX in the private scene, requires native activation.
	};
	inline constexpr definition catalog[]{
		{"attach_h2_acog_2_vm","tag_acog_2","mtl_attach_h2_optic_acog_glass","mtl_attach_h2_acog_mildot",{"mtl_attach_h2_acog_ret"},
			{-.08186f,.000094f,.033903f},.0125f,4.f},
		{"attach_h2_cheytac_scope_vm","tag_cheytac_scope","mtl_h2_sni_cheytac_lens_base","mtl_wpn_h2_cheytac_ret",{},
			{-.17269f,-.000027f,-.005308f},.0204f,6.f}, // Rear inner wall 19.9-20.2 mm; outer wall >=25.9 mm.
		{"attach_h2_dragunov_scope_vm","tag_sight_on","mtl_wpn_h1_sni_m21_scope","mtl_attach_h2_dragunov_ads_ret",
			{},
			{-.19220f,.010261f,.042312f},.0135f,6.f,1.f,.314f}, // Front housing ends at +11.16 cm; rear lens at -19.22 cm, plus 1 cm margin.
		{"attach_h2_m14ebr_scope_vm","tag_sight_on","mtl_wpn_h1_sni_m21_scope","mtl_attach_h2_m14ebr_ads_ret",{},
			{-.14929f,.000001f,.028256f},.0198f,6.f},
		{"attach_h2_m82_scope_vm","tag_sight_on","mtl_wpn_h2_sni_m82_glass_base","mtl_attach_h2_barrett_ads_ret",{},
			{-.17266f,0,.040126f},.0172f,6.f},
		{"attach_h2_wa2000_scope_vm","tag_wa2000_scope","mtl_wpn_h2_sniper_wa2000_glass_base","mtl_h2_attach_h2_wa2000_ads_ret_2",{},
			{-.16448f,0,.000060f},.0195f,6.f,.94f}, // Clip within the opaque ring, before its transparent outer margin.
		{"attach_h2_steyr_scope_vm","tag_steyr_scope","mtl_wpn_h2_sniper_wa2000_glass_base_blend","mtl_wpn_h2_steyr_ret",{},
			{-.15368f,.000232f,.044789f},.0128f,4.f},
		// Loaded base/tan/arctic meshes: rear glass at -5.706 cm, 20.75 mm
		// diameter. Replace the layered screen, retaining body and front glass.
		{"attach_h2_thermal_scope_2_vm","tag_thermal_scope","mtl_attach_h2_optic_thermal_glass_ads_blend","mtl_wpn_h2_thermal_scope_ret_ads",
			{"h2og_mtl_weapon_thermal_screen","h2og_mtl_weapon_thermal_screen_scanlines","mtl_attach_h2_optic_thermal_vignette","mtl_wpn_h2_thermal_scope_ret_new"},
			{-.05706f,.000089f,.024504f},.0105f,4.f,.94f,.133f,true}, // Native reticle alpha is opaque at .90-.94, then fades outside.
	};
	inline bool variant_name(std::string_view name, std::string_view base) noexcept
	{
		if (base.empty() || !name.starts_with(base)) return false;
		const auto suffix=name.substr(base.size());
		return suffix.empty() || suffix=="_arctic" || suffix=="_desert" || suffix=="_tan" || suffix=="_digital" || suffix=="_woodland";
	}
	inline const definition* find(std::string_view model) noexcept
	{
		for (const auto& entry:catalog) if (variant_name(model,entry.model)) return &entry;
		return nullptr;
	}
	enum class material_role { unrelated, lens, reticle, extra_hidden };
	inline material_role classify_material(const definition& optic, std::string_view name) noexcept
	{
		// H2 runtime material categories are absent from SEModel export names.
		// Strip only witnessed prefixes, once; never accept arbitrary path tails.
		if (name.starts_with("mc/")) name.remove_prefix(3);
		else if (name.starts_with("m/")) name.remove_prefix(2);
		if (variant_name(name,optic.lens)) return material_role::lens;
		if (variant_name(name,optic.reticle)) return material_role::reticle;
		for(const auto extra:optic.extra_hidden)
			if(variant_name(name,extra))return material_role::extra_hidden;
		return material_role::unrelated;
	}
}
