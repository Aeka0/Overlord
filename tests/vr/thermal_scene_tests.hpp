#pragma once
#include "component/vr/thermal_scene_policy.hpp"
#include <limits>

template<class Check> void thermal_scene_tests(Check check)
{
	using namespace vr;
	thermal_scene::record native;native.fill(0x5a);
	thermal_scene::write(native.data(),{0x81,0});
	engine_stereo_view::scene_record_pair eyes;
	eyes.left=eyes.right=native;eyes.pair_id=eyes.publication=1;
	engine_stereo_view::slot_pair views;
	const auto before=eyes;
	check(thermal_scene::normalize_world(eyes,{1,true},views),"carried thermal world recovers the public SSR weight");
	check(thermal_scene::read(eyes.left.data()).flags==0x80 && thermal_scene::read(eyes.right.data()).ssr==1,
		"both world eyes remove only thermal bits and receive the same SSR policy");
	for(std::size_t i=0;i<native.size();++i)
		if(i!=thermal_scene::flags_offset && (i<thermal_scene::ssr_offset || i>=thermal_scene::ssr_offset+4))
			check(eyes.left[i]==native[i] && eyes.right[i]==native[i],
				"ordinary/thermal vision, damage, flash, camera, geometry and history bytes are preserved");
	check(thermal_scene::read(native.data()).flags==0x81 && thermal_scene::read(native.data()).ssr==0,
		"native source is retained for the optical scene before normalizing world eyes");
	// Actual eye 0 borrows this arena record. Native target changes made during
	// the owner must survive restoration; a whole-record rollback would lose them.
	auto arena=native;thermal_scene::arena_lease lease;
	check(lease.install(arena.data(),eyes.left) && thermal_scene::read(arena.data()).flags==0x80 &&
		thermal_scene::read(arena.data()).ssr==1,"borrowed left arena uses the ordinary policy through render/display");
	check(!lease.install(arena.data(),eyes.left),"a repeated lease cannot overwrite its original restoration state");
	arena[0x2c94]=0x44;lease.restore();
	check(thermal_scene::read(arena.data()).flags==0x81 && thermal_scene::read(arena.data()).ssr==0 && arena[0x2c94]==0x44 &&
		!lease.destination,"left completion/cancellation restores only the five borrowed bytes");
	const auto restored=arena;lease.restore();check(arena==restored,"restoration is idempotent");
	eyes=before;check(thermal_scene::normalize_world(eyes,{0,true},views) && thermal_scene::read(eyes.left.data()).ssr==0,
		"a user-disabled SSR scale stays disabled");
	for(const auto bad:{-1.f,11.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
	{
		eyes=before;check(!thermal_scene::normalize_world(eyes,{bad,true},views) && eyes.left==before.left && eyes.right==before.right,
			"invalid SSR policy rejects both eyes without a partial mutation");
	}
	for(unsigned condition=0;condition<6;++condition)
	{
		eyes=before;views={};thermal_scene::world_request request{1,true};
		switch(condition)
		{
		case 0:views.screen_scope_epoch=1;break;
		case 1:views.weapon_display_epoch=1;break;
		case 2:request.valid=false;break;
		case 3:eyes.left[0x204]=eyes.right[0x204]=3;break;
		case 4:eyes.left[0x204]=eyes.right[0x204]=0;break;
		case 5:eyes.right[0x204]=0;break;
		}
		const auto source=eyes;
		check(!thermal_scene::normalize_world(eyes,request,views) && eyes.left==source.left && eyes.right==source.right,
			"story M82, launcher display, native forced thermal and ordinary/ambiguous views retain native rendering");
	}
}
