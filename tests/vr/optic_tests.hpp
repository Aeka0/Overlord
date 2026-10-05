#pragma once
#include "component/vr/gameplay/optic_geometry.hpp"
#include "component/vr/gameplay/optic_catalog.hpp"
#include <limits>
#include <string>

template<class Check>
void optic_tests(Check check)
{
	using namespace vr::gameplay::weapons;
	using namespace vr::gameplay::weapons::optics;
	check(find("attach_h2_cheytac_scope_vm") && find("attach_h2_cheytac_scope_vm_desert"),"optic catalog accepts reviewed M200 variants");
	check(find("attach_h2_acog_2_vm_tan") && !find("attach_h2_acog_2_vm_unknown"),"optic catalog bounds variant suffixes");
	check(!find("attach_h2_red_dot_sight_vm") && !find("attach_h2_tavor_scope_vm"),
		"ordinary red dots are not reclassified as magnified optics");
	{
		const auto& thermal=*find("attach_h2_thermal_scope_2_vm");
		check(thermal.thermal && find("attach_h2_thermal_scope_2_vm_tan")==&thermal &&
			find("attach_h2_thermal_scope_2_vm_arctic")==&thermal && !find("attach_h2_thermal_scope_2_vm_unknown"),
			"reviewed thermal skins use one bounded physical optic policy");
		// Loaded AK thermal surfaces, including both rear glass disks. Body,
		// native ADS shell and forward glass must remain visible.
		constexpr std::string_view materials[]{"m/h2og_mtl_weapon_thermal_screen","mc/mtl_attach_h2_optic_thermal_body_base",
			"m/mtl_wpn_h2_thermal_scope_ret_ads","mc/mtl_attach_h2_optic_thermal_glass_ads_blend",
			"mc/mtl_attach_h2_optic_thermal_vignette","m/h2og_mtl_weapon_thermal_screen_scanlines",
			"mc/mtl_attach_h2_optic_thermal_glass_ads_blend","m/mtl_wpn_h2_thermal_scope_ret_new",
			"mc/mtl_thermal_scope_zoomed_base","m/mtl_attach_h2_optic_thermal_glass"};
		unsigned hidden{};
		for(unsigned i=0;i<std::size(materials);++i)
			if(classify_material(thermal,materials[i])!=material_role::unrelated)hidden|=1u<<i;
		check(hidden==0xfd,"thermal replaces seven rear display surfaces while preserving both housings and front glass");
		for(const auto& other:catalog)if(!other.thermal)
			for(const auto material:materials)check(classify_material(other,material)==material_role::unrelated,
				"thermal display masks cannot hide ordinary optics");
		check(thermal.center_meters[0]<-.057f && thermal.radius_meters>.01037f && thermal.radius_meters<.01208f &&
			thermal.scene_clearance_meters>.06590f-thermal.center_meters[0],
			"thermal plane covers native rear glass inside the housing and its private clip plane clears the front");
		check(thermal.pupil_radius==.94f,"thermal reticle is masked within its measured opaque annulus without resizing the lens");
	}
	{
		const auto& m200=*find("attach_h2_cheytac_scope_vm");
		check(m200.radius_meters>.0202f && m200.radius_meters<.0259f,
			"M200 display overlaps the measured rear inner wall without reaching its outer wall");
		const auto& wa2000=*find("attach_h2_wa2000_scope_vm");
		check(wa2000.radius_meters==.0195f && wa2000.pupil_radius==.94f && m200.pupil_radius==1.f,
			"WA2000 masks the reticle's transparent rim independently of physical lens size and other optics");
		const auto& dragunov=*find("attach_h2_dragunov_scope_vm");
		check(classify_material(dragunov,"m/mtl_wpn_h1_shared_lens")==material_role::unrelated &&
			classify_material(dragunov,"m/mtl_wpn_h1_sni_dragunov_scope_base")==material_role::unrelated &&
			classify_material(m200,"m/mtl_wpn_h1_shared_lens")==material_role::unrelated,
			"Dragunov front glass and housing remain in the ordinary weapon scene");
		// Captured native surface order: the ordinary rear eyepiece and native
		// ADS rear shell use distinct materials; front tube/mount share another.
		constexpr std::string_view dragunov_materials[]{
			"m/mtl_wpn_h1_sni_dragunov_scope_eyepiece_base","m/mtl_wpn_h1_sni_dragunov_scope_base",
			"m/mtl_wpn_h1_sni_dragunov_scope_base","m/mtl_wpn_h1_shared_lens",
			"m/mtl_dragnunov_scope_ads_base","m/mtl_attach_h2_dragunov_ads_ret","m/mtl_wpn_h1_sni_m21_scope"};
		unsigned dragunov_hidden{};
		for(unsigned i=0;i<std::size(dragunov_materials);++i)
			if(classify_material(dragunov,dragunov_materials[i])!=material_role::unrelated)dragunov_hidden|=1u<<i;
		check(dragunov_hidden==0x60,"ADS replaces only lens and reticle, not Dragunov near eyepiece or housing");
		check(find("attach_h2_dragunov_scope_vm_woodland")==&dragunov,"Dragunov woodland inherits the same bounded optic policy");
		view scoped;scoped.active=true;scoped.axis[0]={1,0,0};scoped.scene_clearance=dragunov.scene_clearance_meters*40;
		vr::spatial_panel::matrix camera{};camera[3]=1;
		check(std::abs(scene_near_distance(scoped,{10,0,0},{0,0,0},camera)-22.56f)<.001f,
			"private scope near plane clears front housing rather than deleting rear surfaces");
		camera[3]=.8f;camera[7]=.6f;
		check(std::abs(scene_near_distance(scoped,{10,2,0},{0,0,0},camera)-(22.56f*.8f+1.2f))<.001f,
			"angled optical crop uses camera-forward depth instead of a spherical distance");
		scoped.scene_clearance=0;check(scene_near_distance(scoped,{10,0,0},{},camera)==0,"other optics inherit native scene clipping");
		scoped.scene_clearance=std::numeric_limits<float>::quiet_NaN();
		check(scene_near_distance(scoped,{10,0,0},{},camera)==0,"invalid clearance cannot poison scene matrices");
		for(const auto& other:catalog)if(other.model!=dragunov.model)
			check(classify_material(other,dragunov_materials[0])==material_role::unrelated &&
				classify_material(other,dragunov_materials[4])==material_role::unrelated,"rear housing rule cannot hide other weapons' surfaces");
	}
	{
		// Read-only runtime capture: M200's eight LOD0 surfaces, 2026-09-21.
		// Export names alone missed the m/ and mc/ categories and bound zero lenses.
		const auto& m200=*find("attach_h2_cheytac_scope_vm");
		constexpr std::string_view materials[]{"m/mtl_cheytac_scope_zoomed_base","m/mtl_h2_sni_cheytac_scope_base",
			"m/mtl_h2_sni_cheytac_scope_base","mc/mtl_wpn_h2_sniper_wa2000_glass_base_blend",
			"m/mtl_wpn_h2_cheytac_ret","m/mtl_h2_sni_cheytac_lens_base",
			"m/mtl_h2_sni_cheytac_laser_base","m/mtl_h2_sni_cheytac_body_base"};
		unsigned selected{};
		for (unsigned i=0;i<std::size(materials);++i)
		{
			const auto role=classify_material(m200,materials[i]);
			if (role!=material_role::unrelated) selected|=1u<<i;
			check(role==(i==4 ? material_role::reticle : i==5 ? material_role::lens : material_role::unrelated),
				"captured M200 material roles preserve housing and select lens/reticle only");
		}
		check(selected==0x30,"captured M200 binding finds both required surfaces");
		check(classify_material(m200,"other/mtl_h2_sni_cheytac_lens_base")==material_role::unrelated &&
			classify_material(m200,"m/m/mtl_h2_sni_cheytac_lens_base")==material_role::unrelated,
			"material normalization never strips arbitrary or nested prefixes");
		for (const auto& optic:catalog)
		{
			check(classify_material(optic,std::string("m/")+std::string(optic.lens))==material_role::lens,
				"runtime m material category accepted for reviewed lenses");
			check(classify_material(optic,std::string("mc/")+std::string(optic.lens))==material_role::lens,
				"runtime mc material category accepted for reviewed lenses");
			check(classify_material(optic,optic.lens)==material_role::lens,"unprefixed export evidence remains accepted");
		}
	}
	view lens; lens.active=true;lens.axis={{{1,0,0},{0,1,0},{0,0,1}}};lens.radius=.8f;lens.magnification=4;
	// Row-vector H2 projection: horizontal=-Y, vertical=Z, W=X.
	const vr::spatial_panel::matrix vp{0,0,0,1,-1,0,0,0,0,1,0,0,0,0,0,0};
	auto result=project(lens,{8,0,0},{0,0,0},vp,40);
	check(result.valid && result.parameters==vr::spatial_panel::vec4{.5f,.5f,4,1},"aligned eye sees magnification centered on gun axis");
	check(result.corners[0][0]<0 && result.corners[0][1]>0,"physical lens projects with native handedness");
	const auto displaced=project(lens,{8,0,0},{0,.2f,0},vp,40);
	check(displaced.valid && displaced.parameters[0]<.5f && displaced.parameters[3]==1,"reticle follows optical axis under eye translation");
	const auto other=project(lens,{8,0,0},{0,2.6f,0},vp,40);
	check(other.valid && other.parameters[3]==0,"non-aiming eye receives a dark aperture instead of the aiming eye image");
	check(!project(lens,{-8,0,0},{0,0,0},vp,40).valid,"lens behind head rejected");
	check(!project(lens,{.1f,0,0},{0,0,0},vp,40).valid,"lens intersecting head rejected");
	check(!project(lens,{40,0,0},{0,0,0},vp,40).valid,"distant lens has no floating magnifier");
	check(!project(lens,{8,0,0},{0,0,0},vp,0).valid,"invalid world scale rejected");
	{
		auto thermal=lens;thermal.thermal=true;
		const auto strict=project(lens,{8,0,0},{0,1.2f,0},vp,40,true);
		const auto wide=project(thermal,{8,0,0},{0,1.2f,0},vp,40,true);
		vr::spatial_panel::vec4 crop{};
		check(strict.valid && strict.parameters[3]==0 && wide.valid && wide.parameters[3]==1 && sampling_window(wide,crop),
			"thermal supplies a covered scene at 1.5 lens radii where an ordinary scope remains dark");
		check(wide.corners==strict.corners && wide.parameters[0]==strict.parameters[0] && wide.parameters[1]==strict.parameters[1] &&
			wide.eye_box_scale==2 && strict.eye_box_scale==1 && wide.pupil_radius==strict.pupil_radius,
			"thermal tolerance changes neither physical size nor aiming-axis reticle coordinates");
		check(project(thermal,{8,0,0},{0,2.f,0},vp,40,true).parameters[3]==1 &&
			project(thermal,{8,0,0},{0,2.5f,0},vp,40,true).parameters[3]==0,
			"thermal admits partially covered pupils but retains a bounded outer eye box");
	}
	auto broken=lens;broken.radius=std::numeric_limits<float>::quiet_NaN();
	check(!project(broken,{8,0,0},{0,0,0},vp,40).valid,"NaN lens rejected");
	broken=lens;broken.axis[0]={0,0,0};check(!project(broken,{8,0,0},{0,0,0},vp,40).valid,"invalid lens basis rejected");
	broken=lens;broken.active=false;check(!project(broken,{8,0,0},{0,0,0},vp,40).valid,"inactive lens has no overlay");
	check(same_view(lens,lens) && !same_view(lens,broken),"scene identity includes lens activation");
	broken=lens;broken.magnification=6;check(!same_view(lens,broken),"scene identity includes magnification");
	broken=lens;broken.thermal=true;check(!same_view(lens,broken),"scene identity includes native thermal mode");
	broken=lens;broken.pupil_radius=.94f;check(!same_view(lens,broken),"scene identity includes authored pupil masking");
	const auto masked=project(broken,{8,0,0},{0,0,0},vp,40);
	check(masked.valid && masked.corners==result.corners && masked.pupil_radius==.94f,
		"pupil masking does not resize or move the physical display plane");
	broken.pupil_radius=0;check(!project(broken,{8,0,0},{0,0,0},vp,40).valid,"invalid pupil calibration fails closed");
	lens.radius=1.6f;
	const auto scaled=project(lens,{16,0,0},{0,0,0},vp,80);
	check(scaled.valid && scaled.parameters==result.parameters &&
		std::abs(scaled.corners[0][0]/scaled.corners[0][3]-result.corners[0][0]/result.corners[0][3])<.0001f,"optic projection respects world scale");
}
