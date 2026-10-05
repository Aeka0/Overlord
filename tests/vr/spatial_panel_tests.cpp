#include "component/vr/hand.hpp"
using vr::hand;
#include <std_include.hpp>
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/spatial_panel_renderer.hpp"
#include "component/vr/spatial_lines_renderer.hpp"
#include "component/vr/world_beam_renderer.hpp"
#include "component/vr/gameplay/designator_visual.hpp"
#include "component/vr/gameplay/reload_well_geometry.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/scene_model_culling.hpp"
#include <utils/native_memory.hpp>
#include "component/vr/scene_snapshot_cache.hpp"
#include "component/vr/eye_composition.hpp"
#include "component/vr/gameplay/weapon_hud_smoothing.hpp"
#include "component/vr/gameplay/weapon_model_anchor.hpp"
#include "component/vr/gameplay/weapon_hud_profile.hpp"
#include "component/vr/gameplay/weapon_hud_native.hpp"
#include "weapon_hud_pose_audit.hpp"
#include "hk_slap_debug_tests.hpp"
#include "narrative_ui_tests.hpp"
#include "native_display_backup_tests.hpp"
#include "fullscreen_blur_policy_tests.hpp"
#include "damage_screen_tests.hpp"
#include "game_text_tests.hpp"
#include "hud_prompt_tests.hpp"
#include "weapon_hud_warning_tests.hpp"
#include "directional_ui_tests.hpp"
#include "component/vr/gameplay/weapon_render_pose_cache.hpp"
#include "reload_item_attachment_tests.hpp"
#include "optic_tests.hpp"
#include "component/vr/recording_frame_layout.hpp"
#include "component/vr/overlay_text_texture.hpp"
#include "component/vr/gameplay/world_interaction_prompt.hpp"
#include "game/assets.hpp"
#include <chrono>
#include <cmath>
#include <limits>
#include <fstream>
#include <iterator>

int main(int argc,char** argv)
{
	using namespace vr::spatial_panel;
	int failures{};
	auto check = [&](bool value, const char* label) {
		if (!value) { std::cerr << "FAIL " << label << '\n'; ++failures; }
	};
	hk_slap_debug_tests::run(check);
	narrative_ui_tests::run(check);
	damage_screen_tests::run(check);
	game_text_tests(check);
	hud_prompt_tests(check);
	weapon_hud_warning_tests(check);
	directional_ui_tests::run(check);
	optic_tests(check);
	reload_item_attachment_tests::run(check);
	{
		namespace p=vr::gameplay::interaction::prompt_text;
		const auto lookup=[](std::string_view key)->std::string {
			if (key=="WEAPON_M21") return "M21 EBR";
			if (key=="WEAPON_M4_CARBINE") return "M4A1";
			if (key=="WEAPON_M4M203") return "M4A1 Grenade Launcher";
			if (key=="WEAPON_USP") return "USP .45";
			return {};
		};
		auto name=p::split("WEAPON_M21_HEARTBEAT","M21 EBR Heartbeat Sensor",lookup);
		check(name.name=="M21 EBR" && name.attachments=="Heartbeat Sensor","pickup attachment split preserves spaced base names");
		name=p::split("WEAPON_M4M203_EOTECH","M4A1 Grenade Launcher + Holographic",lookup);
		check(name.name=="M4A1" && name.attachments=="Grenade Launcher + Holographic","pickup compound attachments share the second line");
		name=p::split("WEAPON_M14EBR_SCOPED","M14 EBR Scoped",lookup);
		check(name.name=="M14 EBR" && name.attachments=="Scoped","native M14 display alias retains EBR in base name");
		name=p::split("WEAPON_DESERTEAGLE","Desert Eagle",lookup);
		check(name.name=="Desert Eagle" && name.attachments.empty(),"unknown base hierarchy never splits ordinary weapon words");
		const std::string utf8="\xe6\xb6\x88\xe9\x9f\xb3";
		name=p::split("WEAPON_USP_SILENCER","USP .45 "+utf8,lookup);
		check(name.name=="USP .45" && name.attachments==utf8,"localized attachment bytes remain intact");
		name=p::split("WEAPON_UNKNOWN","Example\r\nAttachment",lookup);
		check(name.name=="Example" && name.attachments=="Attachment","native multiline names retain an attachment line");
		name=p::split("WEAPON_USP_VARIANT","Unrelated translation",lookup);
		check(name.name=="Unrelated translation" && name.attachments.empty(),"non-prefix translation is not truncated");
	}
	{
		check(scene_models::local_radius_padding(10,1)==10 && scene_models::local_radius_padding(10,2)==5,
			"model-specific cull padding stays constant in world units under scaling");
		check(scene_models::local_radius_padding(-1,1)==0 && scene_models::local_radius_padding(1000,1)==128 &&
			scene_models::local_radius_padding(10,0)==0 && scene_models::local_radius_padding(10,1e-30f)==0 &&
			scene_models::local_radius_padding(std::numeric_limits<float>::quiet_NaN(),1)==0,
			"negative oversized nonfinite and tiny-scale cull padding bounded");
		using namespace vr::spatial_lines;
		vec4 a{0,0,0,-1},b{0,.5f,0,1};
		check(clip(a,b,.1f) && a[3]>=.09999f && a[3]>0,"diagnostic line crossing near plane is clipped, not discarded");
		a={0,0,0,-2}; b={0,0,0,-1}; check(!clip(a,b,.1f),"behind-eye line omitted");
		a={-2,0,0,1}; b={2,0,0,1}; check(clip(a,b,.1f) && a[0]==-1 && b[0]==1,"side planes clip both ends");
		a={2,0,0,1}; b={3,0,0,1}; check(!clip(a,b,.1f),"offscreen line omitted");
		a={0,0,0,1}; b=a; b[0]=std::numeric_limits<float>::quiet_NaN(); check(!clip(a,b,.1f),"NaN diagnostic rejected");
		batch bounded;
		for (size_t n=0;n<capacity;++n) check(bounded.add({1,0,0},{1,1,0},{1,1,1,1}),"bounded line admission");
		check(!bounded.add({},{},{1,1,1,1}) && bounded.count==capacity,"line overflow cannot overwrite memory");
		using namespace vr::gameplay::weapons;
		for (const auto* definition : reload_profiles)
		{
			physical_reload::well_debug::sample s;
			s.definition=definition; s.units=40; s.well_model={{40,0,0},{0,0,0,1}};
			s.held=s.examined=s.occupied=true; s.alignment=1;
			s.raw_tip=s.visual_tip=s.examined_tip={0,0,-.02f};
			const auto geometry=physical_reload::well_debug::geometry_for(s,{});
			check(geometry.count==117,"all profiles share bounded actual-volume debug builder");
			const auto& p=definition->interaction;
			check(std::abs(geometry.lines[0].a[0]-(40+p.well_radius*40))<.00001f &&
				std::abs(geometry.lines[0].a[2]+p.well_capture_below*40)<.00001f,"capture radius and lower extent from active profile");
			check(std::abs(geometry.lines[1].a[2]-p.well_contact_depth*40)<.00001f,
				"capture upper extent follows the active profile, including belt-fed weapons");
			check(geometry.lines[106].color==vec4{0,1,0,1},"inside aligned raw tip is green independent of occupied gate");
			s.requires_withdrawal=true;
			const auto blocked=physical_reload::well_debug::geometry_for(s,{});
			check(std::abs(blocked.lines[52].a[0]-(40+(p.well_radius+p.well_withdraw_margin)*40))<.00001f,
				"withdrawal overlay uses the smaller clearance margin instead of the retention volume");
			check(blocked.lines[112].color==vec4{1,0,0,1} && blocked.lines[106].color==vec4{0,1,0,1},
				"historical withdrawal diamond cannot overwrite current geometric verdict");
			s.alignment=-1; // Perpendicular is now allowed by the 95-degree pistol policy; reversed is not.
			check(physical_reload::well_debug::geometry_for(s,{}).lines[106].color==vec4{1,0,1,1},"misaligned current tip magenta");
			s.raw_tip={0,0,.04f}; s.alignment=std::cos(75.f*3.14159265359f/180.f);
			check(physical_reload::well_debug::geometry_for(s,{}).lines[106].color==vec4{0,1,0,1},
				"75-degree tip above mouth is green with the actual insertion profile");
			s.raw_tip={0,0,-.10f};
			check(physical_reload::well_debug::geometry_for(s,{}).lines[106].color==vec4{1,1,1,1},
				"removed remote capture area is white even at an accepted angle");
			matrix camera{0,0,0,1,-1,0,0,0,0,1,0,0,0,0,.01f,0};
			const auto left=vr::spatial_lines::project(geometry,{0,1.28f,0},camera,.12f);
			const auto right=vr::spatial_lines::project(geometry,{0,-1.28f,0},camera,.12f);
			check(left.count>0 && right.count>0 && left.lines[0].a[0]!=right.lines[0].a[0],"well has real binocular disparity");
			const auto moved=physical_reload::well_debug::geometry_for(s,{100,-200,30});
			const auto moved_eye=vr::spatial_lines::project(moved,{100,-198.72f,30},camera,.12f);
			check(moved_eye.count==left.count && std::abs(moved_eye.lines[0].a[0]-left.lines[0].a[0])<.0001f,
				"same-record locomotion cancels, without temporal smoothing");
		}
		// Current authored profiles have symmetric axial bounds. An asymmetric
		// fixture catches accidental reuse of the lower extent for the upper ring
		// or a hard-coded pistol/belt-fed default in the shared debug builder.
		{
			auto asymmetric=m9::physical;
			asymmetric.interaction.well_capture_below=.025f;
			asymmetric.interaction.well_contact_depth=.085f;
			for (const float units:{20.f,80.f})
			{
				physical_reload::well_debug::sample s;
				s.definition=&asymmetric; s.units=units; s.well_model={{0,0,0},{0,0,0,1}};
				const auto geometry=physical_reload::well_debug::geometry_for(s,{});
				check(geometry.count>=2 && std::abs(geometry.lines[0].a[2]+.025f*units)<.00001f &&
					std::abs(geometry.lines[1].a[2]-.085f*units)<.00001f,
					"asymmetric capture bounds retain their separate dimensions under world scaling");
			}
		}
		static unsigned layer_order{};
		vr::engine_stereo_view::slot_pair views{};
		vr::eye_composition::set_consumer([](const auto&,auto*,auto*,auto*) noexcept { layer_order=layer_order*10+1; });
		vr::eye_composition::set_consumer([](const auto&,auto*,auto*,auto*) noexcept { layer_order=layer_order*10+2; },vr::eye_composition::layer::diagnostics);
		vr::eye_composition::compose({views},nullptr,nullptr,nullptr);
		check(layer_order==12,"diagnostic composes after HUD without replacing it");
		vr::eye_composition::set_consumer(nullptr);
		layer_order=0; vr::eye_composition::compose({views},nullptr,nullptr,nullptr);
		check(layer_order==2,"HUD disabled does not disable diagnostic layer");
		vr::eye_composition::set_consumer(nullptr,vr::eye_composition::layer::diagnostics);
	}
	// Live native command reads must be bounded and fault-contained without a
	// VirtualQuery syscall per field. No mapping cache survives a protection change.
	using utils::native_memory::read_bytes;
	std::array<std::byte, 104> native_copy{}, native_bytes{};
	native_bytes[1] = std::byte{0x5a};
	check(read_bytes(native_copy.data(), native_bytes.data()+1, 103) && native_copy[0] == std::byte{0x5a}, "unaligned native read");
	check(!read_bytes(native_copy.data(), nullptr, 4), "null native pointer");
	check(!read_bytes(native_copy.data(), reinterpret_cast<void*>(0x10), 4), "low native pointer");
	check(!read_bytes(native_copy.data(), native_bytes.data(), 0), "empty native read");
	check(!read_bytes(native_copy.data(), native_bytes.data(), 0x100001), "oversized native read");
	check(!read_bytes(native_copy.data(), reinterpret_cast<void*>((std::numeric_limits<std::uintptr_t>::max)()-1), 4), "native pointer overflow");
	SYSTEM_INFO system_info{}; GetSystemInfo(&system_info);
	const auto page_size = system_info.dwPageSize;
	auto* pages = static_cast<std::byte*>(VirtualAlloc(nullptr, page_size*2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	check(pages != nullptr, "reader test pages");
	if (pages)
	{
		const auto free_pages = gsl::finally([&] { VirtualFree(pages, 0, MEM_RELEASE); });
		DWORD old_protection{};
		check(read_bytes(native_copy.data(), pages+page_size-52, 104), "read across committed pages");
		check(VirtualProtect(pages+page_size, page_size, PAGE_NOACCESS, &old_protection) != 0, "protect second test page");
		check(!read_bytes(native_copy.data(), pages+page_size-52, 104), "cross-page fault contained");
		check(!read_bytes(native_copy.data(), pages+page_size, 104), "prior readable page now inaccessible");
		check(read_bytes(native_copy.data(), pages+page_size-104, 104), "exact readable page boundary");
		check(VirtualProtect(pages+page_size, page_size, PAGE_READONLY, &old_protection) != 0, "restore readable page");
		check(read_bytes(native_copy.data(), pages+page_size, 104), "no stale protection cache");
	}
	const auto reader_start = std::chrono::steady_clock::now();
	bool reads_ok = true;
	for (unsigned i = 0; i < 1000000; ++i)
		reads_ok &= read_bytes(native_copy.data(), native_bytes.data(), native_bytes.size());
	check(reads_ok, "one million bounded hot-path reads");
	std::cout << "native reader 1M x104B: " << std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now()-reader_start).count() << " us (observational, no timing pass threshold)\n";
	quad world{};
	// Backend may render N after frontend has already published N+1. Scene
	// identity, not a latest-pose lookup, selects the HUD for both eyes.
	vr::scene_snapshot_cache<vec3, 2> scene_cache;
	vr::engine_stereo_view::slot_pair scene_a{}, scene_b{};
	scene_a.eyes[0].pair_id = scene_a.eyes[1].pair_id = 1;
	scene_a.eyes[0].publication = scene_a.eyes[1].publication = 7;
	scene_a.natural_camera = {10,0,0, 1,0,0, 0,1,0, 0,0,1};
	scene_b = scene_a; scene_b.eyes[0].pair_id = scene_b.eyes[1].pair_id = 2;
	scene_b.natural_camera[0] = 30;
	check(scene_cache.put(scene_a, {11,0,0}) && scene_cache.put(scene_b, {31,0,0}), "two queued scene poses");
	vec3 frozen{};
	check(scene_cache.get(scene_a, frozen) && frozen[0] == 11, "older scene never uses next-frame pose");
	check(scene_cache.get(scene_a, frozen) && frozen[0]-scene_a.source_origin()[0] == 1, "scene A translation cancels");
	check(scene_cache.get(scene_b, frozen) && frozen[0]-scene_b.source_origin()[0] == 1, "scene B translation cancels without spring");
	auto foreign_scene = scene_a; foreign_scene.natural_camera[0] += 1;
	check(!scene_cache.get(foreign_scene, frozen), "camera identity mismatch rejected");
	foreign_scene = scene_a; ++foreign_scene.eyes[1].publication;
	check(!scene_cache.get(foreign_scene, frozen), "mismatched eye publication rejected");
	foreign_scene = scene_a; foreign_scene.eyes[0].pair_id = foreign_scene.eyes[1].pair_id = 3;
	check(scene_cache.put(foreign_scene, {100,0,0}) && !scene_cache.get(scene_a, frozen), "bounded eviction never returns foreign pose");
	static std::uintptr_t observed_record{};
	vr::eye_composition::scene_record_consumer.store([](const vr::engine_stereo_view::slot_pair&, std::uintptr_t record) noexcept { observed_record = record; });
	vr::eye_composition::register_scene(scene_a, 0);
	check(!observed_record, "absent native record does not register a binding");
	vr::eye_composition::register_scene(scene_a, 0x10000);
	check(observed_record == 0x10000, "scene registration carries its native record address");
	{
		using namespace vr::eye_composition;
		static unsigned hud_draws{},menu_draws{};
		const auto hud=+[](const event&,ID3D11DeviceContext*,ID3D11ShaderResourceView*,ID3D11RenderTargetView*) noexcept {++hud_draws;};
		set_consumer(hud,layer::spatial_hud);set_consumer(hud,layer::indicators);
		set_consumer(+[](const event&,ID3D11DeviceContext*,ID3D11ShaderResourceView*,ID3D11RenderTargetView*) noexcept {++menu_draws;},layer::menu_backdrop);
		vr::presentation_options::hide_hud=true;
		compose(event{scene_a},nullptr,nullptr,nullptr);
		check(hud_draws==0 && menu_draws==1,"hide HUD excludes spatial ammo, warnings and waypoints but preserves menu composition");
		vr::presentation_options::hide_hud=false;
		event right_eye{scene_a};right_eye.eye=1;compose(right_eye,nullptr,nullptr,nullptr);
		check(hud_draws==0 && menu_draws==2,"HUD toggle between eyes does not split a stereo pair");
		menu_draws=1;
		compose(event{scene_a},nullptr,nullptr,nullptr);
		check(hud_draws==2 && menu_draws==2,"disabling hide HUD restores independent HUD layers");
		set_consumer(nullptr,layer::spatial_hud);set_consumer(nullptr,layer::indicators);set_consumer(nullptr,layer::menu_backdrop);
		check(!vr::presentation_options::capture_narrative(true,false) && vr::presentation_options::capture_narrative(true,true),
			"hidden narrative ink retains native scene fades");
	}
	vr::eye_composition::scene_record_consumer.store(nullptr);
	vr::eye_composition::register_scene(scene_a, 0x20000);
	check(observed_record == 0x10000, "record observer lifetime");
	{
		using namespace vr::gameplay::weapon_render_pose;
		using namespace vr::gameplay::weapons;
		auto cache = std::make_unique<pose_cache>();
		auto views = scene_a; views.eyes[1].output_eye = 1;
		solved_pose first{};
		first.object = 0x10000; first.matrices = 0x20000; first.epoch = 42; first.muzzle_bone = 73;
		first.bone = {{0,0,0,1}, {20,2,3}, 2};
		first.muzzle.valid = true; first.muzzle.owner = {1,2,hand::right,hand::none,hold_source::engine_default,3};
		first.muzzle.reference_generation = 9; first.muzzle.input_sequence = 10;
		first.muzzle.axis = {{{1,0,0},{0,1,0},{0,0,1}}}; first.muzzle.units_per_meter = 40;
		first.muzzle.model = {true, first.bone.position, {100,200,300}};
		first.control_grip = {true,{10,2,0},{100,200,300}};
		first.muzzle.position = {120,202,303};
		first.committed_at = first.muzzle.sampled_at = first.muzzle.camera_at = vr::controller_input::clock::now();
		{
			auto laser=first;laser.laser_bone=81;laser.laser={{0,0,0,1},{16,4,5},2};
			laser.muzzle.laser={true,laser.laser.position,first.muzzle.model.solve_origin};
			laser.muzzle.laser_axis=first.muzzle.axis;
			muzzle_frame beam;
			check(laser_pose(laser.muzzle,beam) && beam.position==vr::gameplay::hands::vec{116,204,305} &&
				beam.model.position!=first.muzzle.model.position,"laser origin preserves the emitter offset and shared solve origin");
			auto emitter_cache=std::make_unique<pose_cache>();snapshot bound;
			check(emitter_cache->publish(laser),"solved laser witness accompanies the firearm pose");
			check(!emitter_cache->skin(0x40000,0x50000,laser,laser.bone),"matching muzzle alone cannot admit an unchecked laser bone");
			auto changed=laser.laser;changed.position[1]+=1;
			check(!emitter_cache->skin(0x40000,0x50000,laser,laser.bone,&changed),"changed emitter bone rejects the stale laser pose");
			check(emitter_cache->begin(views,0x30000,laser.muzzle.owner,9) &&
				emitter_cache->skin(0x40000,0x50000,laser,laser.bone,&laser.laser) &&
				emitter_cache->submit(0x30000,views.natural_camera,0x40000,0x50000,laser.object) &&
				emitter_cache->get(views,bound) && bound.muzzle.laser.position==laser.laser.position,
				"laser and muzzle retain the same native skin and stereo scene lifetime");
		}
		check(cache->publish(first), "committed bone pose admitted");
		solved_pose found{}; snapshot out{};
		check(cache->find(first.object, first.matrices, first.epoch, found), "exact object/buffer/epoch lookup");
		check(!cache->find(first.object, first.matrices, first.epoch+1, found), "foreign skeleton epoch rejected");
		check(!cache->find(first.object+1, first.matrices, first.epoch, found), "foreign object rejected");
		check(!cache->find(first.object, first.matrices+32, first.epoch, found), "foreign bone buffer rejected");
		check(cache->begin(views, 0x30000, first.muzzle.owner, 9) && !cache->get(views, out), "scene awaits native submission, not latest pose");
		auto changed_bone = first.bone; changed_bone.position[1] += 1;
		check(!cache->skin(0x40000,0x50000,first,changed_bone), "native consumed bone must equal cached solve");
		check(cache->skin(0x40000,0x50000,first,first.bone), "skin input matched exactly");
		// Publish N+1 before scene N submits. N must still receive its consumed
		// bone, even with the same controller input but a newer spatial camera.
		auto second = first; ++second.epoch; second.matrices += 0x1000;
		second.muzzle.camera_at += std::chrono::milliseconds(20);
		second.committed_at += std::chrono::milliseconds(20);
		second.bone.position[1] += 5; second.muzzle.model.position = second.bone.position;
		check(cache->publish(second), "next-camera solve may overlap previous scene");
		check(!cache->for_record(0x30000,views.natural_camera,out),
			"prepared attachment awaits this record submission, not latest solve or unsubmitted skin");
		check(cache->prepare(0x30000,views.natural_camera,0x40000,0x50000,first.object) &&
			cache->for_record(0x30000,views.natural_camera,out) && out.epoch==first.epoch,
			"viewmodel-scoped skin publishes exact record pose before ordinary rigid preparation");
		check(!cache->get(views,out), "early viewmodel preparation does not make the HUD submission ready prematurely");
		auto wrong_camera = views.natural_camera; wrong_camera[0] += 1;
		check(!cache->submit(0x30000,wrong_camera,0x40000,0x50000,first.object), "foreign record camera rejected");
		check(!cache->submit(0x30000,views.natural_camera,0x40000,0x50001,first.object), "foreign native surface rejected");
		check(!cache->submit(0x30000,views.natural_camera,0x40000,0x50000,first.object+1), "surface association cannot cross objects");
		check(cache->submit(0x30000,views.natural_camera,0x40000,0x50000,first.object) && cache->get(views,out), "native skin-to-scene binding accepted");
		check(out.control_grip.position==first.control_grip.position && out.control_grip.position!=out.muzzle.model.position,
			"HUD rear grip stays distinct from the muzzle through native scene binding");
		check(out.epoch == first.epoch && out.muzzle.model.position == first.bone.position, "queued N never selects latest N+1 pose");
		check(cache->for_record(0x30000,views.natural_camera,out) && out.epoch==first.epoch,
			"rigid attachment follows submitted viewmodel N, independent of ordinary entity counts or latest N+1");
		check(!cache->for_record(0x30001,views.natural_camera,out) && !cache->for_record(0x30000,wrong_camera,out),
			"attachment cannot cross record or camera identities");
		check(cache->submit(0x30000,views.natural_camera,0x40000,0x50000,first.object), "identical multi-list submission is idempotent");
		auto other_eye = views; ++other_eye.eyes[1].publication;
		check(!cache->get(other_eye,out), "mixed stereo publication rejected");
		other_eye = views; other_eye.eyes[1].output_eye = 0;
		check(!cache->get(other_eye,out), "swapped eye rejected");
		// Record addresses and even cameras repeat at rest; registration must
		// select the new scene without invalidating a queued old stereo snapshot.
		auto next = views; next.eyes[0].pair_id = next.eyes[1].pair_id = 2;
		check(cache->begin(next,0x30000,first.muzzle.owner,9) && !cache->get(next,out), "same-address same-camera reuse starts pending");
		check(!cache->for_record(0x30000,next.natural_camera,out), "reused record never falls back to previous ready registration");
		check(cache->skin(0x40000,0x50000,second,second.bone) &&
			cache->submit(0x30000,next.natural_camera,0x40000,0x50000,second.object), "reused native surface binds new epoch");
		check(cache->get(next,out) && out.epoch == second.epoch, "new scene has new bone epoch");
		check(cache->get(views,out) && out.epoch == first.epoch, "old eye pair retains immutable bone snapshot");
		cache->invalidate_skin(0x40000);
		check(cache->for_record(0x30000,next.natural_camera,out) && out.epoch==second.epoch,
			"submitted attachment survives transient entity reuse and skin-cache invalidation");
		check(!cache->submit(0x30000,next.natural_camera,0x40000,0x50000,second.object), "failed skin attempt cannot reuse an old surface association");
		check(cache->skin(0x40000,0x50000,first,first.bone), "alternate valid epoch fixture");
		check(!cache->submit(0x30000,next.natural_camera,0x40000,0x50000,first.object) && !cache->get(next,out),
			"conflicting bone epochs in one scene rejected, never last-wins");
		check(!cache->for_record(0x30000,next.natural_camera,out), "attachment rejects conflicting scene submissions");
		check(cache->begin(next,0x30000,first.muzzle.owner,10) &&
			!cache->submit(0x30000,next.natural_camera,0x40000,0x50000,first.object), "recenter cannot import old-reference pose");
		auto switched = first.muzzle.owner; ++switched.weapon;
		check(cache->begin(next,0x30000,switched,9) &&
			!cache->submit(0x30000,next.natural_camera,0x40000,0x50000,first.object), "weapon switch rejects old model pose");
		auto invalid = first; invalid.muzzle.model.position[0] = std::numeric_limits<float>::quiet_NaN();
		check(!cache->publish(invalid), "nonfinite solved bone metadata rejected");
		invalid=first;invalid.control_grip.position[0]=std::numeric_limits<float>::quiet_NaN();
		check(!cache->publish(invalid),"invalid rear grip cannot publish a HUD pose");
		check(vr::gameplay::weapon_hud::screen_side(vr::gameplay::weapon_hud::generic,false)==.12f &&
			std::abs(vr::gameplay::weapon_hud::screen_side(vr::gameplay::weapon_hud::generic,true)+.12f)<.0001f,
			"left and right HUD anchors are symmetric without extra left compensation");
		{
			using namespace vr::gameplay::weapon_hud;
			const auto main=for_feed(generic,feed::primary),secondary=for_feed(generic,feed::underbarrel);
			for(unsigned hand=0;hand<hand_count;++hand)
			{
				check(source_index(hand,feed::primary)!=source_index(hand,feed::underbarrel),"host and module have separate capture slots in either hand");
				check(source_hand(source_index(hand,feed::underbarrel))==hand &&
					source_feed(source_index(hand,feed::underbarrel))==feed::underbarrel,"module route preserves hand and feed");
				check(screen_side(main,hand==0)==screen_side(secondary,hand==0) && main.from_grip_meters==secondary.from_grip_meters,
					"secondary row shares the primary anchor and horizontal alignment in either hand");
			}
			check(std::abs(main.screen_up_meters-secondary.screen_up_meters-.05f)<.0001f && main.width_meters==secondary.width_meters,
				"underbarrel ammo is a full-width row directly below the original HUD");
			check(source_index(2,feed::primary)==source_count && source_index(0,static_cast<feed>(2))==source_count,
				"invalid hands and feeds cannot alias an existing capture slot");
		}
		for (unsigned i = 0; i < 130; ++i) { auto pose = second; pose.epoch = 100+i; check(cache->publish(pose), "bounded solve ring"); }
		check(!cache->find(first.object,first.matrices,first.epoch,found), "evicted bone epoch cannot fall back to latest");
		auto evicted = views; evicted.eyes[0].pair_id = evicted.eyes[1].pair_id = 65;
		check(cache->begin(evicted,0x60000,first.muzzle.owner,9) && !cache->get(views,out), "bounded scene eviction never returns foreign pose");
		// Two objects and two mechanical consumers share a record, never a pose.
		auto dual=std::make_unique<pose_cache>();
		auto left=first;left.object+=0x100;left.matrices+=0x100;
		left.muzzle.owner.weapon=2;left.muzzle.owner.rear=hand::left;
		check(dual->publish(first) && dual->publish(left),"independent solved objects admitted");
		check(dual->begin(views,0x70000,left.muzzle.owner,9) && dual->begin(views,0x70000,first.muzzle.owner,9),"both hands register in one stereo pair");
		check(dual->skin(0x71000,0x72000,first,first.bone) && dual->skin(0x73000,0x74000,left,left.bone),"both independent skin buffers retained");
		check(dual->prepare(0x70000,views.natural_camera,0x73000,0x74000,left.object) &&
			dual->prepare(0x70000,views.natural_camera,0x71000,0x72000,first.object),"opposite preparation order preserves both instances");
		check(dual->for_record(0x70000,views.natural_camera,out,left.muzzle.owner.id()) && out.object==left.object &&
			dual->for_record(0x70000,views.natural_camera,out,first.muzzle.owner.id()) && out.object==first.object,"magazine placement selects its own gun skin");
		check(dual->for_record(0x70000,views.natural_camera,out) && out.object==first.object,"legacy HUD selects explicit primary registration");
		check(dual->begin(next,0x70000,first.muzzle.owner,9) && !dual->for_record(0x70000,next.natural_camera,out,left.muzzle.owner.id()),
			"record reuse cannot resurrect the other gun from a previous stereo pair");
		// Same definition, distinct physical lifetimes, even with equal grip revisions.
		auto matched=std::make_unique<pose_cache>();auto right_copy=first,left_copy=left;
		right_copy.muzzle.owner.instance_generation=901;
		left_copy.muzzle.owner.weapon=right_copy.muzzle.owner.weapon;
		left_copy.muzzle.owner.instance_generation=902;
		check(matched->publish(right_copy) && matched->publish(left_copy) &&
			matched->begin(views,0x80000,right_copy.muzzle.owner,9) && matched->begin(views,0x80000,left_copy.muzzle.owner,9),
			"same-model guns register independent render lifetimes");
		check(matched->skin(0x81000,0x82000,right_copy,right_copy.bone) && matched->skin(0x83000,0x84000,left_copy,left_copy.bone) &&
			matched->submit(0x80000,views.natural_camera,0x81000,0x82000,right_copy.object) &&
			matched->submit(0x80000,views.natural_camera,0x83000,0x84000,left_copy.object),"same-model native skin packets bind to their own instances");
		check(matched->get(views,out,right_copy.muzzle.owner.id()) && out.object==right_copy.object &&
			matched->for_record(0x80000,views.natural_camera,out,left_copy.muzzle.owner.id()) && out.object==left_copy.object,
			"same-model HUD and magazine queries select distinct native objects");
		auto reused=left_copy.muzzle.owner;++reused.instance_generation;
		check(matched->begin(next,0x80000,reused,9) &&
			!matched->submit(0x80000,next.natural_camera,0x83000,0x84000,left_copy.object),"recycled same-model object cannot bind an old instance pose");
		for(auto support:{hand::left,hand::right})
		{
			for(bool acquired:{false,true})for(bool skin_ahead:{false,true})
			{
				auto before=first;before.muzzle.owner.rear=hand(1-int(support));before.muzzle.owner.instance_generation=905;
				before.muzzle.owner.support=acquired?hand::none:support;
				auto after=before;after.muzzle.owner.support=acquired?support:hand::none;++after.muzzle.owner.revision;
				const auto& registered=skin_ahead?before:after;const auto& consumed=skin_ahead?after:before;
				auto transition=std::make_unique<pose_cache>();snapshot attachment;
				check(transition->begin(views,0xA0000,registered.muzzle.owner,9) && transition->publish(consumed) &&
					transition->skin(0xA1000,0xA2000,consumed,consumed.bone),"support transition retains the exact natively consumed object and bone");
				check(transition->prepare(0xA0000,views.natural_camera,0xA1000,0xA2000,consumed.object) &&
					transition->for_record(0xA0000,views.natural_camera,attachment,consumed.muzzle.owner.id()) && attachment.epoch==consumed.epoch,
					"support-only change between scene registration and skinning cannot hide rigid attachments");
				check(transition->submit(0xA0000,views.natural_camera,0xA1000,0xA2000,consumed.object) &&
					transition->get(views,attachment,consumed.muzzle.owner.id()) && attachment.muzzle.owner.support==consumed.muzzle.owner.support,
					"render publication retains actual skin ownership rather than rewriting it to registration state");
				check(!ready(consumed.muzzle,registered.muzzle.owner,9,consumed.committed_at),
					"render support continuity does not relax firing ownership validation");
			}
			auto carried=first;carried.muzzle.owner.rear=hand::none;carried.muzzle.owner.support=support;
			++carried.muzzle.owner.revision;++carried.muzzle.owner.rear_revision;
			auto attachments=std::make_unique<pose_cache>();
			check(!ready(carried.muzzle,carried.muzzle.owner,9,carried.committed_at) && attachments->publish(carried),
				"foregrip-only gun admits its skinned attachment pose without acquiring firing authority");
			check(attachments->begin(views,0x90000,carried.muzzle.owner,9) &&
				attachments->skin(0x91000,0x92000,carried,carried.bone) &&
				attachments->prepare(0x90000,views.natural_camera,0x91000,0x92000,carried.object) &&
				attachments->for_record(0x90000,views.natural_camera,out,carried.muzzle.owner.id()) && out.epoch==carried.epoch,
				"held magazine obtains the exact prepared bone snapshot with either foregrip hand");
			auto changed=carried.muzzle.owner;changed.support=hand(1-int(support));
			check(!pose_ready(carried.muzzle,changed,9,carried.committed_at),"support identity must match even if caller supplies the same revision");
			check(attachments->begin(next,0x90000,changed,9) &&
				!attachments->prepare(0x90000,next.natural_camera,0x91000,0x92000,carried.object),
				"changing the foregrip hand cannot borrow the previous hand's skinned record");
			carried.muzzle.owner.support=hand::none;
			check(!attachments->publish(carried),"unheld/holstered gun cannot publish held attachment pose");
			carried.muzzle.owner.support=support;carried.committed_at+=std::chrono::milliseconds(151);
			check(!attachments->publish(carried),"carry-only pose retains tracking and camera freshness limits");
		}
	}
	{
		using namespace vr::gameplay::weapons;
		vr::gameplay::weapon_hud::pose_audit audit;
		muzzle_frame frozen_pose{};
		frozen_pose.valid = true; frozen_pose.owner = {1,2,hand::right};
		frozen_pose.reference_generation = 3; frozen_pose.input_sequence = 4;
		frozen_pose.model = {true, {1,2,3}, {100,200,300}};
		auto later = frozen_pose;
		audit.observe(1, frozen_pose, later);
		check(audit.samples == 1 && audit.changed == 0, "identical audit pose does not report motion");
		later.model.position[1] += 2;
		audit.observe(2, frozen_pose, later);
		check(audit.changed == 1 && audit.same_input_changed == 1 && audit.same_camera_changed == 1 &&
			audit.peak_units == 2 && audit.local_delta == vec3{0,2,0}, "same-input same-camera bone change distinguished");
		later.camera_at += std::chrono::milliseconds(20);
		audit.observe(3, frozen_pose, later);
		check(audit.same_input_changed == 2 && audit.same_camera_changed == 1, "same input with new camera classified separately");
		++later.input_sequence; later.model.position[1] += 1;
		audit.observe(4, frozen_pose, later);
		check(audit.changed == 3 && audit.same_input_changed == 2 && audit.peak_pair == 4 && audit.camera_delta_ms == 20,
			"later input and peak diagnostic timestamp");
		++later.owner.weapon;
		audit.observe(5, frozen_pose, later);
		later = frozen_pose; later.model.position[0] = std::numeric_limits<float>::quiet_NaN();
		audit.observe(6, frozen_pose, later);
		check(audit.rejected == 2 && audit.samples == 4 && audit.peak_units == 3,
			"foreign and nonfinite audit samples do not poison metrics");
		check(frozen_pose.model.position == vec3{1,2,3}, "audit never changes the rendering snapshot");
	}
	check(billboard({1,0,0}, {0,-1,0}, {0,0,1}, .2f, .1f, world), "camera-facing corners");
	// H2 camera forward X, left Y, up Z; symmetric unit-tangent projection.
	matrix vp{0,0,.0f,1, -1,0,0,0, 0,1,0,0, 0,0,.01f,0};
	projected_quad left{}, right{};
	check(project(world, {0,.032f,0}, vp, .12f, left) &&
		project(world, {0,-.032f,0}, vp, .12f, right), "two-eye projection");
	check(left[0][0] > right[0][0], "crossed disparity at near weapon depth");
	check(std::abs(left[0][1]-right[0][1]) < 1e-6f, "no invented vertical disparity");
	const float disparity = left[0][0]/left[0][3] - right[0][0]/right[0][3];
	for (auto& p : world) p[0] += 1;
	check(project(world, {0,.032f,0}, vp, .12f, left) &&
		project(world, {0,-.032f,0}, vp, .12f, right), "farther panel");
	check(std::abs(disparity*.5f - (left[0][0]/left[0][3]-right[0][0]/right[0][3])) < 1e-6f,
		"disparity scales with inverse depth");
	auto translated = world;
	for (auto& p : translated) { p[0]+=100; p[1]-=200; p[2]+=30; }
	projected_quad moved{};
	check(project(translated, {100, -200+.032f, 30}, vp, .12f, moved) &&
		std::abs(moved[0][0]-left[0][0]) < .0001f, "world translation cancels once");
	auto off_axis = vp; off_axis[0] = .2f;
	check(project(world, {0,.032f,0}, off_axis, .12f, moved) &&
		std::abs(moved[0][0]/moved[0][3] - left[0][0]/left[0][3] - .2f) < 1e-6f,
		"native asymmetric projection retained");
	for (auto& p : world) p[0] = -.1f;
	check(!project(world, {}, vp, .12f, moved), "behind camera rejected");
	for (auto& p : world) p[0] = .01f;
	check(!project(world, {}, vp, .12f, moved), "near crossing rejects entire quad");
	world[0][0]=world[2][0]=-.5f;world[1][0]=world[3][0]=1;
	check(project(world,{},vp,.12f,moved,true,clip_mode::hardware)&&moved[0][3]<0&&moved[1][3]>0,
		"spatial menu crossing the near plane preserves homogeneous corners for GPU clipping");
	for(auto& p:world)p[0]=-.5f;
	check(!project(world,{},vp,.12f,moved,true,clip_mode::hardware),"entirely behind-head menu still rejected");
	check(!billboard({}, {0,-1,0}, {0,-1,0}, 1, 1, world), "degenerate billboard rejected");
	check(!billboard({}, {0,-1,0}, {0,0,1}, std::numeric_limits<float>::infinity(), 1, world), "infinite size");
	{
		// Native shader matrix layout corroborates the disassembled paths:
		// +0x800 is history; +0x900 is CURRENT WORLD_MATRIX0.
		static_assert((game::CONST_SRC_CODE_PREV_FRAME_WORLD_MATRIX -
			game::CONST_SRC_CODE_VIEW_MATRIX) * 64 == 0x800);
		static_assert((game::CONST_SRC_CODE_WORLD_MATRIX0 -
			game::CONST_SRC_CODE_VIEW_MATRIX) * 64 == 0x900);
		static_assert(vr::engine_stereo_view::h2_current_model_placement_origin_offset == 0x188 + 0x08);
		static_assert(vr::engine_stereo_view::h2_view_origin_offset == 0x100);
		// Current and history inputs coexist in ONE immutable scene record.
		auto records = std::make_unique<vr::engine_stereo_view::scene_record_pair>();
		auto views = scene_a; views.eyes[1].output_eye = 1;
		records->pair_id = 1; records->publication = 7;
		const auto set_origins = [&](const vec3& center) {
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				auto origin = center; origin[1] += eye ? -.032f : .032f;
				auto& record = eye == 0 ? records->left : records->right;
				std::memcpy(record.data()+vr::engine_stereo_view::h2_view_origin_offset,
					origin.data(), sizeof(origin));
				const vec3 history_eye{999,888,777}, history_placement{666,555,444};
				std::memcpy(record.data()+0x2DC0, history_eye.data(), sizeof(history_eye));
				std::memcpy(record.data()+0x19C, history_placement.data(), sizeof(history_placement));
			}
		};
		const vec3 model_center{100,-200,30};
		set_origins(model_center);
		const auto origins = vr::eye_composition::model_origins_for(views, *records);
		check(origins.valid && origins.eyes[0][0] == 100 && origins.placement == vec3{},
			"current model origins exported, history excluded");
		quad local{}; billboard({2,0,0}, {0,-1,0}, {0,0,1}, .2f, .1f, local);
		auto model_world = local;
		for (auto& p : model_world) for (unsigned c=0; c<3; ++c) p[c] += model_center[c];
		for (unsigned eye = 0; eye < 2; ++eye)
		{
			const vec3 local_eye{0, eye ? -.032f : .032f, 0};
			projected_quad expected{}, corrected{}, wrong{};
			check(project(local, local_eye, off_axis, .12f, expected) &&
				project(model_world, origins.eyes[eye], off_axis, .12f, corrected), "model/HUD native placement equivalence");
			for (unsigned vertex=0; vertex<4; ++vertex) for (unsigned c=0; c<4; ++c)
				check(std::abs(expected[vertex][c]-corrected[vertex][c]) < .0001f, "model origin cancels translation once in both eyes");
			auto stale_eye = origins.eyes[eye]; stale_eye[1] += 2;
			check(project(model_world, stale_eye, off_axis, .12f, wrong) &&
				std::abs(wrong[0][0]-corrected[0][0]) > 1, "mixed-time eye reproduces movement offset");
		}
		set_origins({300,-400,50});
		check(origins.eyes[0][0] == 100, "model metadata owns snapshot, not mutable record pointers");
		++records->pair_id;
		check(!vr::eye_composition::model_origins_for(views, *records).valid, "foreign model pair rejected");
		records->pair_id = 1; ++records->publication;
		check(!vr::eye_composition::model_origins_for(views, *records).valid, "foreign model publication rejected");
		records->publication = 7; views.eyes[1].output_eye = 0;
		check(!vr::eye_composition::model_origins_for(views, *records).valid, "swapped model eye rejected");
		views.eyes[1].output_eye = 1; ++views.eyes[1].publication;
		check(!vr::eye_composition::model_origins_for(views, *records).valid, "mixed model publication rejected");
		views.eyes[1].publication = 7;
		const vec3 placement{10,20,30};
		std::memcpy(records->left.data()+vr::engine_stereo_view::h2_current_model_placement_origin_offset, placement.data(), sizeof(placement));
		check(!vr::eye_composition::model_origins_for(views, *records).valid, "mixed stereo placement rejected");
		std::memcpy(records->right.data()+vr::engine_stereo_view::h2_current_model_placement_origin_offset, placement.data(), sizeof(placement));
		check(vr::eye_composition::model_origins_for(views, *records).placement == placement, "shared native placement exported");
		const vec3 invalid_placement{0,0,std::numeric_limits<float>::quiet_NaN()};
		std::memcpy(records->right.data()+vr::engine_stereo_view::h2_current_model_placement_origin_offset, invalid_placement.data(), sizeof(invalid_placement));
		check(!vr::eye_composition::model_origins_for(views, *records).valid, "nonfinite native placement rejected");
		std::memcpy(records->right.data()+vr::engine_stereo_view::h2_current_model_placement_origin_offset, placement.data(), sizeof(placement));
		set_origins({std::numeric_limits<float>::quiet_NaN(),0,0});
		check(!vr::eye_composition::model_origins_for(views, *records).valid, "nonfinite model origins rejected");
		set_origins({1e8f,0,0});
		check(!vr::eye_composition::model_origins_for(views, *records).valid, "oversized current eye rejected");
		check(!vr::eye_composition::event{views,1,1,0,32,32}.model_origins.valid, "optional metadata absent is explicit");
	}
	{
		using namespace vr::gameplay::weapons;
		// Advance, reverse, stop: current placement/eye translations cancel for
		// a stationary local weapon. History has a different relative offset,
		// so neither replacing one input nor replacing both can pass this test.
		auto records = std::make_unique<vr::engine_stereo_view::scene_record_pair>();
		auto views = scene_a; views.eyes[1].output_eye = 1;
		records->pair_id = 1; records->publication = 7;
		const model_anchor bone{true, {20,0,0}, {999,999,999}};
		const std::array<vec3, 4> positions{{{100,200,30}, {110,220,35}, {90,180,25}, {90,180,25}}};
		vr::gameplay::weapon_hud::anchor_smoother filter;
		for (unsigned step = 0; step < positions.size(); ++step)
		{
			const auto& current = positions[step];
			const vec3 history_placement{current[0]-1, current[1]+4, current[2]-1};
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				auto& record = eye ? records->right : records->left;
				auto origin = current; origin[1] += eye ? -1.f : 1.f;
				auto history_eye = origin; history_eye[0] -= 2; history_eye[1] += 7;
				// Literal recovered record offsets prevent a changed constant from
				// silently changing both fixture and production in the same way.
				std::memcpy(record.data()+0x190, current.data(), sizeof(current));
				std::memcpy(record.data()+0x100, origin.data(), sizeof(origin));
				std::memcpy(record.data()+0x19C, history_placement.data(), sizeof(history_placement));
				std::memcpy(record.data()+0x2DC0, history_eye.data(), sizeof(history_eye));
			}
			const auto origins = vr::eye_composition::model_origins_for(views, *records);
			check(origins.valid && origins.placement == current, "locomotion uses current placement");
			const auto placed = place_model_anchor(bone, origins.placement);
			const auto smoothed = filter.apply(placed, current, scene_a.natural_camera, 40,
				step * .025, {1,2,3}, true);
			check(smoothed == placed, "current-frame smoothing introduces no locomotion tail");
			quad raw{}, local{}, stale{};
			billboard(placed, {0,-1,0}, {0,0,1}, 10,3,raw);
			billboard(bone.position, {0,-1,0}, {0,0,1}, 10,3,local);
			billboard(place_model_anchor(bone, history_placement), {0,-1,0}, {0,0,1}, 10,3,stale);
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				const vec3 local_eye{0, eye ? -1.f : 1.f, 0};
				projected_quad actual{}, expected{}, wrong{};
				check(project(raw, origins.eyes[eye], off_axis, 4.8f, actual) &&
					project(local, local_eye, off_axis, 4.8f, expected), "current-frame stereo projection");
				for (unsigned vertex=0; vertex<4; ++vertex) for (unsigned c=0; c<4; ++c)
					check(std::abs(actual[vertex][c]-expected[vertex][c]) < .0001f,
						"raw HUD stays weapon-relative through locomotion and stop");
				auto history_eye = origins.eyes[eye]; history_eye[0] -= 2; history_eye[1] += 7;
				check(project(stale, history_eye, off_axis, 4.8f, wrong) &&
					std::abs(wrong[0][0]-actual[0][0]) > 1, "old history/history formula fails regression");
				check(project(stale, origins.eyes[eye], off_axis, 4.8f, wrong) &&
					std::abs(wrong[0][0]-actual[0][0]) > 1, "fixing eye alone is insufficient");
				check(project(raw, history_eye, off_axis, 4.8f, wrong) &&
					std::abs(wrong[0][0]-actual[0][0]) > 1, "fixing placement alone is insufficient");
			}
		}
	}
	{
		using namespace vr::gameplay::weapons;
		// Historical AK samples exercise placement algebra ONLY. These offsets
		// were later identified as history, not evidence of current render inputs.
		// Current-vs-history selection is covered by the adapter tests above.
		const model_anchor forward{true, {35.0503234863f,22.4162387848f,-30.963891983f},
			{-14797.23046875f,24379.259765625f,-16219.875f}};
		const vec3 placement_forward{-14796.9609375f,24377.1796875f,-16219.875f};
		const model_anchor backward{true, {38.6725387573f,-26.9436454773f,-31.6014232635f},
			{-14777.634765625f,24320.591796875f,-16219.875f}};
		const vec3 placement_backward{-14777.572265625f,24322.1171875f,-16219.875f};
		for (unsigned direction = 0; direction < 2; ++direction)
		{
			const auto& bone = direction ? backward : forward;
			const auto& placement = direction ? placement_backward : placement_forward;
			check(valid_model_anchor(bone), "live model pose valid");
			const auto placed = place_model_anchor(bone, placement);
			const auto old_world = place_model_anchor(bone, bone.solve_origin);
			check(direction ? old_world[1]-placed[1] < -1.5f : old_world[1]-placed[1] > 2.f,
				"captured solve/history placement differences have opposite signs");
			auto newer_solve = bone; newer_solve.solve_origin[1] += 5;
			check(place_model_anchor(newer_solve, placement) == placed, "HUD does not follow a newer solve origin");
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				vec3 origin = placement; origin[1] += eye ? -1.28f : 1.28f;
				quad placed_quad{}, bone_quad{};
				billboard(placed, {0,-1,0}, {0,0,1}, 10,3,placed_quad);
				billboard(bone.position, {0,-1,0}, {0,0,1}, 10,3,bone_quad);
				projected_quad actual{}, expected{};
				const vec3 native_delta{0, origin[1]-placement[1], 0};
				check(project(placed_quad, origin, off_axis, 4.8f, actual) &&
					project(bone_quad, native_delta, off_axis, 4.8f, expected), "model-placement projection for both eyes");
				for (unsigned vertex=0; vertex<4; ++vertex) for (unsigned c=0; c<4; ++c)
					check(std::abs(actual[vertex][c]-expected[vertex][c]) < .002f, "HUD matches native skinned placement algebra");
			}
		}
		const model_anchor stationary{true, {2,3,4}, {100,200,300}};
		check(place_model_anchor(stationary, stationary.solve_origin) == vec3{102,203,304},
			"stationary placement uses no compensation delay");
		auto invalid = forward; invalid.valid = false;
		check(!valid_model_anchor(invalid), "missing model coordinates rejected");
		invalid = forward; invalid.position[0] = std::numeric_limits<float>::infinity();
		check(!valid_model_anchor(invalid), "nonfinite model coordinates rejected");
	}
	{
		using vr::gameplay::weapon_hud::anchor_smoother;
		const auto camera = scene_a.natural_camera;
		const anchor_smoother::identity owner{1,2,3};
		anchor_smoother filter;
		check(filter.apply({1,0,0}, {}, camera, 1, 1, owner, true) == vec3{1,0,0}, "smoothing seeds exactly");
		auto value = filter.apply({1,.2f,0}, {}, camera, 1, 1.025, owner, true);
		check(std::abs(value[1]-.1f) < 1e-6f, "25 ms smoothing half-life");
		// Player movement follows the CURRENT model center, not an old world anchor.
		value = filter.apply({101,-199.8f,30}, {100,-200,30}, camera, 1, 1.05, owner, true);
		check(std::abs(value[0]-101) < .0001f && std::abs(value[1]+199.85f) < .0001f,
			"smoothing follows locomotion immediately while filtering local hand motion");
		check(filter.apply({2,0,0}, {}, camera, 1, 1.075, owner, false) == vec3{2,0,0}, "off is exact raw anchor");
		check(filter.apply({2,.2f,0}, {}, camera, 1, 1.1, owner, true) == vec3{2,.2f,0}, "reenable reseeds without old tail");
		check(filter.apply({2,.3f,0}, {}, camera, 1, 2, owner, true) == vec3{2,.3f,0}, "long pause reseeds");
		check(filter.apply({2,.4f,0}, {}, camera, 1, 1, owner, true) == vec3{2,.4f,0}, "backward scene time reseeds");
		check(filter.apply({2,.3f,0}, {}, camera, 1, 1.02, {1,2,4}, true) == vec3{2,.3f,0}, "recenter reseeds");
		check(filter.apply({2,.2f,0}, {}, camera, 1, 1.04, {2,2,4}, true) == vec3{2,.2f,0}, "weapon switch reseeds");
		check(filter.apply({2,.1f,0}, {}, camera, 1, 1.06, {2,3,4}, true) == vec3{2,.1f,0}, "rear hand change reseeds");
		check(filter.apply({20,0,0}, {}, camera, 1, 1.08, {2,3,4}, true) == vec3{20,0,0}, "large pose jump snaps");
		anchor_smoother fast, slow;
		(void)fast.apply({1,0,0}, {}, camera, 1, 0, owner, true);
		(void)slow.apply({1,0,0}, {}, camera, 1, 0, owner, true);
		vec3 a{}, b{};
		for (int n=1; n<=12; ++n) a = fast.apply({1,.2f,0}, {}, camera, 1, n/120., owner, true);
		for (int n=1; n<=6; ++n) b = slow.apply({1,.2f,0}, {}, camera, 1, n/60., owner, true);
		check(std::abs(a[1]-b[1]) < 1e-6f, "smoothing independent of frame subdivision");
		// A rotated camera and changed scale use the same local-coordinate filter.
		anchor_smoother rotated;
		const std::array<float,12> yaw_camera{0,0,0, 0,1,0, -1,0,0, 0,0,1};
		(void)rotated.apply({0,40,0}, {}, yaw_camera, 40, 0, owner, true);
		value = rotated.apply({-8,40,0}, {}, yaw_camera, 40, .025, owner, true);
		check(std::abs(value[0]+4) < .0001f && value[1] == 40, "smoothing respects camera axes and units");
	}

	{
		using namespace vr::gameplay::weapon_hud::native;
		using vr::native_hud_quad::field;
		std::array<std::byte,104> source{};
		write(source,0,std::uint16_t(104));write(source,2,std::uint8_t(17));
		const std::array<std::array<float,2>,4> vertices{{{170,20},{190,20},{190,40},{170,40}}};
		for(unsigned i=0;i<4;++i){write(source,16+i*16,vertices[i][0]);write(source,20+i*16,vertices[i][1]);}
		write(source,88,1.f);write(source,92,1.f);
		auto background=source,label=source;
		check(mirror_quad(background,300,false) && mirror_quad(label,300,true),"native background and label commands mirror");
		for(unsigned i=0;i<4;++i)
			check(field<float>(background.data(),16+i*16)==field<float>(label.data(),16+i*16) &&
				field<float>(background.data(),20+i*16)==vertices[i][1],"mirrored labels and background share opposite-side layout and winding");
		check(field<float>(label.data(),16)==110 && field<float>(source.data(),16)==170 &&
			field<float>(background.data(),80)==1 && field<float>(background.data(),88)==0 &&
			field<float>(label.data(),80)==0 && field<float>(label.data(),88)==1,"scratch preserves source and labels stay readable while art flips");
		check(upright_image("h1_hud_weapwidget_firearms_labels_9x19l_nightvision") &&
			upright_image("h1_hud_weapwidget_nullnum_nimbus") && !upright_image("h1_hud_weapwidget_ammopip_smalla") &&
			border("h1_hud_weapwidget_border_nightvision"),"native text images and night vision border are classified");
		std::array<std::byte,240> text{};write(text,2,std::uint8_t(20));write(text,4,180.f);write(text,28,1.5f);
		check(mirror_text(text,300,12) && field<float>(text.data(),4)==102 && field<float>(text.data(),28)==1.5f,
			"native glyph advance mirrors the text box without negating font scale");
		write(text,56,std::uint32_t(0x4000));
		check(!mirror_text(text,300,12),"unknown transformed text cannot guess a mirror");
	}
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &device, nullptr, &context))) return 2;
	native_display_backup_tests(device.Get(),context.Get(),check);
	fullscreen_blur_policy_tests(check);
	constexpr unsigned size = 32;
	std::array<std::uint32_t, size*size> pixels{};
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = desc.Height = size; desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	auto texture = [&](const auto& data, Microsoft::WRL::ComPtr<ID3D11Texture2D>& output) {
		D3D11_SUBRESOURCE_DATA initial{data.data(), size*4, 0};
		return SUCCEEDED(device->CreateTexture2D(&desc, &initial, &output));
	};
	Microsoft::WRL::ComPtr<ID3D11Texture2D> bg, ink, output;
	for (unsigned y=0; y<size; ++y) for (unsigned x=0; x<size; ++x)
		pixels[y*size+x] = (x&1) ? 0xffffffff : 0xff000000;
	check(texture(pixels, bg), "background texture");
	pixels.fill(0); // Transparent rim, black half-alpha center and white native ink.
	for (unsigned y=4; y<28; ++y) for (unsigned x=4; x<28; ++x) pixels[y*size+x] = 0x80000000;
	pixels[16*size+16] = 0xffffffff;
	check(texture(pixels, ink), "native-like premultiplied ink texture");
	pixels.fill(0xff336699);
	check(texture(pixels, output), "linear destination");
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> bg_view, ink_view;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output_view;
	check(SUCCEEDED(device->CreateShaderResourceView(bg.Get(), nullptr, &bg_view)) &&
		SUCCEEDED(device->CreateShaderResourceView(ink.Get(), nullptr, &ink_view)) &&
		SUCCEEDED(device->CreateRenderTargetView(output.Get(), nullptr, &output_view)), "texture views");
	if (failures) return 1;
	const projected_quad fullscreen{{{-1,1,.5f,1}, {1,1,.5f,1}, {-1,-1,.5f,1}, {1,-1,.5f,1}}};
	const D3D11_VIEWPORT sentinel{3,4,7,9,.2f,.7f};
	context->RSSetViewports(1, &sentinel);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	ID3D11ShaderResourceView* saved = ink_view.Get(); context->PSSetShaderResources(5, 1, &saved);
	renderer panel;
	check(panel.draw(context.Get(), bg_view.Get(), ink_view.Get(), output_view.Get(), fullscreen, size, size, 0, 1), "WARP no-blur pass");
	D3D11_VIEWPORT restored{}; unsigned viewport_count=1;
	context->RSGetViewports(&viewport_count, &restored);
	check(std::memcmp(&sentinel, &restored, sizeof(sentinel)) == 0, "native viewport restored");
	D3D11_PRIMITIVE_TOPOLOGY topology{}; context->IAGetPrimitiveTopology(&topology);
	check(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "native topology restored");
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> restored_srv;
	context->PSGetShaderResources(5, 1, &restored_srv);
	check(restored_srv.Get() == saved, "unrelated native SRV restored");
	auto readback = [&] {
		Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
		auto staging_desc=desc; staging_desc.Usage=D3D11_USAGE_STAGING;
		staging_desc.BindFlags=0; staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		std::array<std::uint32_t, size*size> result{};
		if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, &staging))) { ++failures; return result; }
		context->CopyResource(staging.Get(), output.Get());
		D3D11_MAPPED_SUBRESOURCE map{};
		if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) { ++failures; return result; }
		for (unsigned y=0; y<size; ++y) std::memcpy(result.data()+y*size,
			static_cast<const char*>(map.pData)+y*map.RowPitch, size*4);
		context->Unmap(staging.Get(), 0); return result;
	};
	const auto clear = readback();
	check(clear[0] == 0xff336699, "transparent art padding leaves destination untouched");
	check(clear[16*size+16] == 0xffffffff, "opaque native white ink preserved");
	const auto gray = clear[10*size+11] & 255;
	check(gray >= 53 && gray <= 56, "native encoded half-alpha black then linear decode");
	{
		// Shared bytecode must not make renderer instances or device objects share
		// mutable draw state. Exercise a second layer, device replacement, and return.
		renderer other_panel;
		check(other_panel.draw(context.Get(),bg_view.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,0,1) &&
			readback()==clear,"second panel instance preserves the same pixel output");
		Microsoft::WRL::ComPtr<ID3D11Device> other_device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> other_context;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> other_output,other_staging;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> other_view;
		auto staging_desc=desc;staging_desc.Usage=D3D11_USAGE_STAGING;
		staging_desc.BindFlags=0;staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		const bool created=SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
			D3D11_SDK_VERSION,&other_device,nullptr,&other_context)) &&
			SUCCEEDED(other_device->CreateTexture2D(&desc,nullptr,&other_output)) &&
			SUCCEEDED(other_device->CreateRenderTargetView(other_output.Get(),nullptr,&other_view)) &&
			SUCCEEDED(other_device->CreateTexture2D(&staging_desc,nullptr,&other_staging));
		check(created,"second WARP device fixture");
		if(created)
		{
			const float neutral[]{.4f,.4f,.4f,1};other_context->ClearRenderTargetView(other_view.Get(),neutral);
			check(panel.draw_recording_frame(other_context.Get(),other_view.Get(),size,size,{8,8,24,24},1,.5f),
				"panel rebuilds device objects using shared bytecode");
			other_context->CopyResource(other_staging.Get(),other_output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const bool mapped_ok=SUCCEEDED(other_context->Map(other_staging.Get(),0,D3D11_MAP_READ,0,&mapped));
			check(mapped_ok,"replacement device readback");
			if(mapped_ok)
			{
				std::uint32_t corner{},center{};
				std::memcpy(&corner,mapped.pData,sizeof(corner));
				std::memcpy(&center,static_cast<const char*>(mapped.pData)+16*mapped.RowPitch+16*4,sizeof(center));
				other_context->Unmap(other_staging.Get(),0);
				check(center==0xff666666 && (corner&255)>=50 && (corner&255)<=52,
					"replacement device has independent constants and shader objects");
			}
		}
		check(panel.draw(context.Get(),bg_view.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,0,1) &&
			readback()==clear,"panel restores original device after replacement");
	}
	vr::presentation_options::disable_blur=true;
	check(panel.draw(context.Get(),bg_view.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,8,1),"disabled panel blur still draws ink");
	check(readback()==clear,"disabled panel blur matches sharp baseline pixels");
	check(panel.draw_masked_blur(context.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,14,3) && readback()==clear,
		"disabled menu blur leaves the scene unchanged");
	vr::presentation_options::disable_blur=false;
	check(panel.draw(context.Get(), bg_view.Get(), ink_view.Get(), output_view.Get(), fullscreen, size, size, 1, 1), "WARP blur pass");
	const auto blurred = readback();
	check((blurred[10*size+10] & 255) > (clear[10*size+10] & 255), "blur samples neighboring background");
	check((blurred[10*size+11] & 255) < gray, "blur reduces background contrast");
	check(blurred[0] == clear[0] && blurred[16*size+16] == clear[16*size+16], "blur leaves rim and opaque glyph unchanged");
	{
		std::array<std::uint32_t,size*size> mask_pixels{};
		for(unsigned y=0;y<size;++y)for(unsigned x=size/2;x<size;++x)mask_pixels[y*size+x]=0xff0000ff;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> mask_texture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mask_view;
		check(texture(mask_pixels,mask_texture) && SUCCEEDED(device->CreateShaderResourceView(mask_texture.Get(),nullptr,&mask_view)),
			"asymmetric native blur-mask fixture");
		const float untouched[]{153/255.f,102/255.f,51/255.f,1};
		context->ClearRenderTargetView(output_view.Get(),untouched);
		check(panel.draw(context.Get(),bg_view.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,1,.9f,mask_view.Get()),
			"native mask drives blur independently of captured ink");
		const auto right_mask=readback();
		check(right_mask[2*size+20]!=clear[0] && right_mask[2*size+4]==clear[0] && right_mask[16*size+16]==0xffffffff,
			"native mask blurs transparent interior without tinting glyphs or unmasked exterior");
		context->ClearRenderTargetView(output_view.Get(),untouched);
		check(panel.draw(context.Get(),bg_view.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,1,.9f,mask_view.Get(),{1,0,-1,1}),
			"left native blur mask mirrors with background art");
		const auto left_mask=readback();
		check(left_mask[2*size+4]==right_mask[2*size+20] && left_mask[2*size+20]==clear[0],"mirrored mask swaps coverage without changing strength");
		context->ClearRenderTargetView(output_view.Get(),untouched);
		check(panel.draw(context.Get(),bg_view.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,1,0,mask_view.Get()) && readback()==clear,
			"native zero-alpha blur keeps sharp ink and transparent exterior");
		check(!panel.draw(context.Get(),bg_view.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,1,1,mask_view.Get(),{0,0,0,1}),
			"degenerate native mask window is rejected");
		check(panel.draw(context.Get(),bg_view.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,1,1),"restore general panel blur fixture");
	}
	check(!panel.draw(context.Get(), bg_view.Get(), ink_view.Get(), output_view.Get(), fullscreen, 0, size, 1, 1), "invalid extent rejected");
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> alias; device->CreateRenderTargetView(bg.Get(), nullptr, &alias);
	check(!panel.draw(context.Get(), bg_view.Get(), ink_view.Get(), alias.Get(), fullscreen, size, size, 1, 1), "source/output alias rejected");
	vr::spatial_lines::renderer wire;
	vr::spatial_lines::batch wire_batch;
	wire_batch.count=1; wire_batch.lines[0]={{-.8f,0,.5f,1},{.8f,0,.5f,1},{0,1,0,1}};
	check(wire.draw(context.Get(),output_view.Get(),wire_batch,size,size),"WARP diagnostic line shader and instanced draw");
	const auto wired=readback();
	check(wired[16*size+16]==0xff00ff00 && wired[0]==blurred[0],"line covers center without filling target");
	viewport_count=1; context->RSGetViewports(&viewport_count,&restored);
	context->IAGetPrimitiveTopology(&topology); context->PSGetShaderResources(5,1,&restored_srv);
	check(std::memcmp(&sentinel,&restored,sizeof(sentinel))==0 && topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST &&
		restored_srv.Get()==saved,"diagnostic draw preserves native viewport topology and unrelated SRV");
	wire_batch.lines[0].color={0,0,0,1};
	check(wire.draw(context.Get(),output_view.Get(),wire_batch,size,size,5.5f),"diagnostic text outline uses bounded wider strokes");
	wire_batch.lines[0].color={1,1,1,1};
	check(wire.draw(context.Get(),output_view.Get(),wire_batch,size,size),"foreground lettering draws over its dark outline");
	const auto outlined=readback();
	check(outlined[16*size+16]==0xffffffff && outlined[18*size+16]==0xff000000,
		"outlined diagnostic remains legible on bright backgrounds");
	check(!wire.draw(context.Get(),output_view.Get(),wire_batch,size,size,0) &&
		!wire.draw(context.Get(),output_view.Get(),wire_batch,size,size,std::numeric_limits<float>::quiet_NaN()),"invalid line thickness is rejected");
	wire_batch.count=vr::spatial_lines::capacity+1;
	check(!wire.draw(context.Get(),output_view.Get(),wire_batch,size,size),"oversized GPU batch rejected");
	// Narrative composition must read the current target (including preceding
	// weapon/diagnostic pixels), preserve native encoded-space alpha, and fade
	// the entire eye outside the smaller head-relative text canvas as well.
	const float green[4]{0,1,0,1}; context->ClearRenderTargetView(output_view.Get(), green);
	check(panel.draw_screen_layer(context.Get(), ink_view.Get(), output_view.Get(), fullscreen, size,size,{}),
		"WARP narrative over already composed target");
	const auto narrative = readback();
	check(narrative[0] == 0xff00ff00 && narrative[16*size+16] == 0xffffffff,
		"transparent subtitle pixels retain previous layers and opaque glyph remains white");
	check((narrative[10*size+10] & 0xff00ff) == 0 &&
		((narrative[10*size+10] >> 8) & 255) >= 53 && ((narrative[10*size+10] >> 8) & 255) <= 56,
		"native half-alpha black dims current green HUD with correct gamma");
	const projected_quad narrative_canvas{{{-.5f,.5f,.5f,1},{.5f,.5f,.5f,1},{-.5f,-.5f,.5f,1},{.5f,-.5f,.5f,1}}};
	context->ClearRenderTargetView(output_view.Get(), green);
	check(panel.draw_screen_layer(context.Get(), ink_view.Get(), output_view.Get(), narrative_canvas, size,size,{0,0,0,.5f}),
		"WARP partial fade extends beyond the text canvas");
	const auto fading = readback();
	check((fading[0] & 0xff00ff) == 0 && ((fading[0] >> 8) & 255) >= 53 && ((fading[0] >> 8) & 255) <= 56,
		"corner outside native canvas follows fade alpha");
	Microsoft::WRL::ComPtr<ID3D11Texture2D> black_ink;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> black_view;
	pixels.fill(0xff000000); pixels[16*size+16] = 0xffffffff;
	check(texture(pixels, black_ink) && SUCCEEDED(device->CreateShaderResourceView(black_ink.Get(), nullptr, &black_view)),
		"native black opening card with text");
	check(panel.draw_screen_layer(context.Get(), black_view.Get(), output_view.Get(), narrative_canvas,size,size,{0,0,0,1}),
		"WARP fully black opening card");
	const auto blackened = readback();
	check(blackened[0] == 0xff000000 && blackened[size-1] == 0xff000000 &&
		blackened[size*(size-1)] == 0xff000000 && blackened.back() == 0xff000000,
		"all four eye corners black without HUD leakage");
	check(!panel.draw_screen_layer(context.Get(), black_view.Get(), output_view.Get(), narrative_canvas,0,size,{0,0,0,1}) &&
		!panel.draw_screen_layer(context.Get(), black_view.Get(), output_view.Get(), narrative_canvas,size,size,{0,0,0,std::numeric_limits<float>::quiet_NaN()}),
		"invalid narrative dimensions and fade rejected");
	// Replay the observed legacy alpha steps through both eye canvas projections.
	// The source texture represents the native premultiplied black draw; its
	// alpha must agree with the separately extended eye corners at every step.
	for (const unsigned alpha : {255u,206u,204u,59u,0u})
	{
		auto command = narrative_ui_tests::ending_fade();
		command[51] = static_cast<std::uint8_t>(alpha);
		vr::native_hud_quad::quad native{};
		check(vr::native_hud_quad::decode(command.data(), command.size(), native), "decode captured legacy fade step");
		const float fade = (native.color >> 24) / 255.f;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> step_ink;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> step_view;
		pixels.fill(alpha << 24);
		check(texture(pixels, step_ink) && SUCCEEDED(device->CreateShaderResourceView(step_ink.Get(), nullptr, &step_view)),
			"create captured fade coverage");
		const float encoded = 1-fade;
		const float expected = 255*(encoded <= .04045f ? encoded/12.92f : std::pow((encoded+.055f)/1.055f,2.4f));
		for (float eye_offset : {-.1f,.1f})
		{
			auto eye_canvas = narrative_canvas;
			for (auto& vertex : eye_canvas) vertex[0] += eye_offset;
			context->ClearRenderTargetView(output_view.Get(), green);
			check(panel.draw_screen_layer(context.Get(), step_view.Get(), output_view.Get(), eye_canvas,size,size,{0,0,0,fade}),
				"compose observed legacy alpha into an eye");
			const auto frame = readback();
			for (unsigned index : {0u,size-1,size*(size-1),size*size-1,16*size+16})
				check((frame[index] & 0xff00ff) == 0 && std::abs(static_cast<float>((frame[index]>>8)&255)-expected) <= 1.5f,
					"legacy fade dims eye center and all corners equally through native alpha sequence");
		}
	}
	// Second Sun uses a native white fullscreen overlay. Both eye centers and
	// the area outside their shifted text canvases must use the same RGBA.
	for(unsigned alpha:{0u,128u,255u})
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> white_ink;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white_view;
		pixels.fill(alpha*0x01010101u);
		check(texture(pixels,white_ink) && SUCCEEDED(device->CreateShaderResourceView(white_ink.Get(),nullptr,&white_view)),
			"native white transition fixture");
		const auto fade=vr::narrative_ui::fade_color("white",(alpha<<24)|0xffffff);
		const float expected=(fade[0]<=.04045f?fade[0]/12.92f:std::pow((fade[0]+.055f)/1.055f,2.4f))*255;
		for(float shift:{-.12f,.12f})
		{
			auto eye_canvas=narrative_canvas;for(auto& corner:eye_canvas)corner[0]+=shift;
			context->ClearRenderTargetView(output_view.Get(),green);
			check(panel.draw_screen_layer(context.Get(),white_view.Get(),output_view.Get(),eye_canvas,size,size,fade),
				"white fade covers the whole eye through the native alpha sequence");
			const auto frame=readback();
			for(unsigned index:{0u,size-1,size*(size-1),size*size-1,16*size+16})
				check(((frame[index]>>8)&255)==255 && std::abs(float(frame[index]&255)-expected)<=1.5f &&
					std::abs(float((frame[index]>>16)&255)-expected)<=1.5f,
					"white transition has matching eye-center and corner color without black margins");
		}
	}
	check(!panel.draw_screen_layer(context.Get(),black_view.Get(),output_view.Get(),narrative_canvas,size,size,{1,1,1,.5f}),
		"non-premultiplied fade metadata is rejected");
	// A split opening title reuses one scene fade. Native text before the fade
	// has dimmed RGB with unchanged coverage; text drawn afterwards stays bright.
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> fade_ink,title_ink;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> fade_view,title_view;
		pixels.fill(0x80000000);
		check(texture(pixels,fade_ink) && SUCCEEDED(device->CreateShaderResourceView(fade_ink.Get(),nullptr,&fade_view)),
			"separate opening scene fade fixture");
		pixels.fill(0);pixels[16*size+8]=0xff7f7f7f;pixels[16*size+24]=0xffffffff;
		check(texture(pixels,title_ink) && SUCCEEDED(device->CreateShaderResourceView(title_ink.Get(),nullptr,&title_view)),
			"opening title before/after fade ink fixture");
		context->ClearRenderTargetView(output_view.Get(),green);
		check(panel.draw_screen_layer(context.Get(),fade_view.Get(),output_view.Get(),narrative_canvas,size,size,{0,0,0,128.f/255.f}) &&
			panel.draw_screen_layer(context.Get(),title_view.Get(),output_view.Get(),fullscreen,size,size,{}),
			"near title composites over the existing story fade");
		const auto split=readback();
		check((split[0]&0xff00ff)==0 && ((split[0]>>8)&255)>=53 && ((split[0]>>8)&255)<=55,
			"separate title plane never dims the background twice");
		check((split[16*size+8]&255)>=53 && (split[16*size+8]&255)<=55 &&
			split[16*size+24]==0xffffffff,"native fade dims earlier title glyphs but preserves titles above black");
	}
	{
		pixels.fill(0);pixels[16*size+16]=0xffffffff;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> hint_ink;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hint_view;
		check(texture(pixels,hint_ink) && SUCCEEDED(device->CreateShaderResourceView(hint_ink.Get(),nullptr,&hint_view)),
			"hint with native sharp glyph and transparent background");
		{
			// Captured trainer border is white at alpha 13/255 with REV_SUBTRACT.
			// Positive subtractand/coverage is captured separately from the text.
			pixels.fill(0);pixels[16*size+12]=pixels[16*size+16]=0x0d0d0d0d;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> backing;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> backing_view;
			check(texture(pixels,backing)&&SUCCEEDED(device->CreateShaderResourceView(backing.Get(),nullptr,&backing_view)),"native subtractive border fixture");
			const float white[]{1,1,1,1};context->ClearRenderTargetView(output_view.Get(),white);
			check(panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),fullscreen,size,size,{},{},backing_view.Get()),"native hint border and text composition");
			const auto joined=readback();const float encoded=1-26.f/255.f;
			const int expected=int(std::round(std::pow((encoded+.055f)/1.055f,2.4f)*255));
			check(std::abs(int(joined[16*size+12]&255)-expected)<=1&&joined[16*size+16]==0xffffffff&&joined[16*size+4]==0xffffffff,
				"reverse subtraction lost its source, erased foreground text or changed unrelated pixels");
			auto shifted_border=fullscreen;for(auto& corner:shifted_border)corner[0]+=.5f;
			context->ClearRenderTargetView(output_view.Get(),white);
			check(panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),shifted_border,size,size,{},{},backing_view.Get()),"other-eye hint border placement");
			const auto shifted_pixels=readback();
			check(shifted_pixels[16*size+12]==0xffffffff&&std::abs(int(shifted_pixels[16*size+20]&255)-expected)<=1&&shifted_pixels[16*size+24]==0xffffffff,
				"subtractive border and hint text used different eye canvases");
		}
		std::array<blur_region,1> box{{{{.25f,.25f,.75f,.75f},1.f}}};
		context->CopyResource(output.Get(),bg.Get());
		check(panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),fullscreen,size,size,{},box),
			"WARP hint background samples the eye behind the same text canvas");
		const auto hint=readback();const auto full_blur=hint[16*size+12]&255;
		check(full_blur>20 && full_blur<235 && hint[16*size+16]==0xffffffff,
			"background is blurred while foreground text stays sharp");
		check(hint[16*size+2]==0xff000000 && hint[16*size+3]==0xffffffff,
			"pixels outside the hint rectangle are untouched");
		vr::presentation_options::disable_blur=true;context->CopyResource(output.Get(),bg.Get());
		check(panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),fullscreen,size,size,{},box),"disabled narrative backdrop draws sharp ink");
		const auto sharp_hint=readback();
		check(sharp_hint[16*size+12]==0xff000000 && sharp_hint[16*size+13]==0xffffffff && sharp_hint[16*size+16]==0xffffffff,
			"disable blur preserves background contrast and foreground glyphs");
		vr::presentation_options::disable_blur=false;
		box[0].alpha=.5f;context->CopyResource(output.Get(),bg.Get());
		check(panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),fullscreen,size,size,{},box),"hint backdrop native fade");
		const auto faded_hint=readback()[16*size+12]&255;
		check(faded_hint>0 && faded_hint<full_blur,"native backdrop alpha fades only the background effect");
		box[0].alpha=1;
		auto shifted=fullscreen;for(auto& corner:shifted)corner[0]+=.5f;
		context->CopyResource(output.Get(),bg.Get());
		check(panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),shifted,size,size,{},box),
			"other-eye canvas moves text and backdrop together");
		const auto moved=readback();
		check(moved[16*size+12]==0xff000000 && (moved[16*size+20]&255)>20 && moved[16*size+24]==0xffffffff,
			"blur has no stale screen rectangle after stereo canvas displacement");
		for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)pixels[y*size+x]=(x&1)?0xff0000ff:0xffff0000;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> other_eye;
		check(texture(pixels,other_eye),"other-eye colored backdrop fixture");
		context->CopyResource(output.Get(),other_eye.Get());
		check(panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),shifted,size,size,{},box),"independent eye backdrop sample");
		const auto eye_pixel=readback()[16*size+20];
		check((eye_pixel&255)>0 && ((eye_pixel>>16)&255)>0 && (eye_pixel&0xff00)==0,
			"right-eye blur uses its own red/blue scene instead of retained left-eye pixels");
		box[0].bounds={-1,-1,2,2};context->CopyResource(output.Get(),bg.Get());
		check(panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),narrative_canvas,size,size,{},box),
			"oversized native hint rectangle is clipped to its source canvas");
		check(readback()[16*size+2]==0xff000000,"background cannot extend beyond the text canvas after normalization");
		box[0].alpha=std::numeric_limits<float>::quiet_NaN();
		check(!panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),fullscreen,size,size,{},box),"invalid hint alpha is rejected");
		const std::array<blur_region,blur_region_capacity+1> overflow{};
		check(!panel.draw_screen_layer(context.Get(),hint_view.Get(),output_view.Get(),fullscreen,size,size,{},overflow),"hint backdrop count is bounded");
	}
	// Multiple atlas sprites overlap before a SINGLE scene composite. A second
	// sprite must not sample the original scene and erase the first one's alpha.
	context->ClearRenderTargetView(output_view.Get(),green);
	const std::array<image_layer,2> layers{{{ink_view.Get(),fullscreen,{0,0,1,1}},
		{ink_view.Get(),fullscreen,{0,0,1,1}}}};
	check(panel.draw_layers(context.Get(),output_view.Get(),layers.data(),2,size,size),"WARP native indicator batch");
	const auto stacked=readback();
	check(stacked[0]==0xff00ff00 && stacked[16*size+16]==0xffffffff,
		"indicator batching preserves transparent borders and opaque native art");
	const auto stacked_green=(stacked[10*size+10]>>8)&255;
	check(stacked_green>=11 && stacked_green<=14,"overlapping half-alpha indicators compose twice in encoded space");
	{
		// UAV instruments may have opaque backing. The actual compositor order
		// must leave the red target above it after the instrument plane grows.
		pixels.fill(0xff00ff00);Microsoft::WRL::ComPtr<ID3D11Texture2D> instrument,target;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> instrument_view,target_view;
		check(texture(pixels,instrument),"opaque UAV instrument fixture");
		pixels.fill(0);for(unsigned y=12;y<20;++y)for(unsigned x=12;x<20;++x)pixels[y*size+x]=0xff0000ff;
		check(texture(pixels,target),"red UAV target fixture");
		check(SUCCEEDED(device->CreateShaderResourceView(instrument.Get(),nullptr,&instrument_view)) &&
			SUCCEEDED(device->CreateShaderResourceView(target.Get(),nullptr,&target_view)),"UAV layer fixture views");
		std::array<image_layer,vr::remote_hud::frame_count> ordered{};unsigned n{};
		for(const auto index:vr::remote_hud::composition_order)
			ordered[n++]={index/vr::remote_hud::stream_count==unsigned(vr::remote_hud::layer::targets)?target_view.Get():instrument_view.Get(),fullscreen};
		check(panel.draw_layers(context.Get(),output_view.Get(),ordered.data(),n,size,size),"WARP UAV target/instrument composition");
		const auto remote=readback();check(remote[16*size+16]==0xff0000ff && remote[0]==0xff00ff00,
			"red UAV targets survive opaque enlarged instrument layers from both native UI streams");
	}
	{
		// Native additive capture keeps emitted RGB and zero alpha, so the eye
		// background is not attenuated. Alpha alone does not mean empty ink.
		pixels.fill(0);
		for(unsigned y=12;y<20;++y)for(unsigned x=12;x<20;++x)pixels[y*size+x]=0x00000080;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> additive_target;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> additive_view;
		check(texture(pixels,additive_target) &&
			SUCCEEDED(device->CreateShaderResourceView(additive_target.Get(),nullptr,&additive_view)),"zero-alpha native additive UAV target fixture");
		const float blue[]{0,0,1,1};
		const image_layer additive_layer{additive_view.Get(),fullscreen};
		context->ClearRenderTargetView(output_view.Get(),blue);
		check(panel.draw_layers(context.Get(),output_view.Get(),&additive_layer,1,size,size),"WARP additive UAV target composition");
		const auto remote=readback();const auto center=remote[16*size+16];
		check((center&255)>=54 && (center&255)<=56 && (center&0xffffff00)==0xffff0000,
			"zero-alpha red target emission survives and adds to the unattenuated blue scene");
		check(remote[0]==0xffff0000,"empty target pixels preserve the eye image");
	}
	const image_layer atlas_region{ink_view.Get(),fullscreen,{.25f,.25f,.125f,.125f}};
	context->ClearRenderTargetView(output_view.Get(),green);
	check(panel.draw_layers(context.Get(),output_view.Get(),&atlas_region,1,size,size),"WARP indicator atlas subregion");
	const auto region=readback();
	check(((region[0]>>8)&255)>=53 && ((region[0]>>8)&255)<=56 && region[0]==region.back(),
		"atlas UV selection samples requested sprite instead of stretching full atlas");
	check(panel.draw_screen_layer(context.Get(),black_view.Get(),output_view.Get(),narrative_canvas,size,size,{0,0,0,1}),
		"native black curtain composes after indicators");
	check(readback()[0]==0xff000000,"full-field black curtain covers earlier indicator pixels");
	check(!panel.draw_layers(context.Get(),output_view.Get(),layers.data(),65,size,size),"oversized indicator batch rejected before access");
	auto invalid_layer=atlas_region; invalid_layer.uv={.9f,0,.2f,1};
	check(!panel.draw_layers(context.Get(),output_view.Get(),&invalid_layer,1,size,size),"invalid atlas region rejected");
	viewport_count=1; context->RSGetViewports(&viewport_count,&restored);
	context->IAGetPrimitiveTopology(&topology); context->PSGetShaderResources(5,1,&restored_srv);
	check(std::memcmp(&sentinel,&restored,sizeof(sentinel))==0 && topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST &&
		restored_srv.Get()==saved,"narrative copy and composition restore native D3D state");
	// A narrow white target in this eye's image must occupy more lens pixels
	// after zoom. The opposite-eye source must produce its own distinct color.
	Microsoft::WRL::ComPtr<ID3D11Texture2D> optic_scene,optic_reticle,other_scene;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> optic_source,optic_ink,other_source;
	pixels.fill(0xff000000);
	for (unsigned y=0;y<size;++y) for (unsigned x=15;x<=16;++x) pixels[y*size+x]=0xffffffff;
	check(texture(pixels,optic_scene) && SUCCEEDED(device->CreateShaderResourceView(optic_scene.Get(),nullptr,&optic_source)),"WARP optic target source");
	pixels.fill(0);
	check(texture(pixels,optic_reticle) && SUCCEEDED(device->CreateShaderResourceView(optic_reticle.Get(),nullptr,&optic_ink)),"WARP transparent reticle source");
	context->ClearRenderTargetView(output_view.Get(),green);
	check(panel.draw_optic(context.Get(),optic_source.Get(),optic_ink.Get(),output_view.Get(),fullscreen,size,size,{.5f,.5f,1,1}),"WARP unzoomed lens");
	const auto ordinary_lens=readback();
	check(panel.draw_optic(context.Get(),optic_source.Get(),optic_ink.Get(),output_view.Get(),fullscreen,size,size,{.5f,.5f,4,1}),"WARP magnified lens");
	const auto zoomed_lens=readback();
	check((ordinary_lens[16*size+13]&255)<5 && (zoomed_lens[16*size+13]&255)>100,"4x zoom enlarges actual target pixels inside lens");
	check(zoomed_lens[0]==0xff00ff00,"optic circle preserves world outside lens");
	check(panel.draw_optic(context.Get(),optic_source.Get(),ink_view.Get(),output_view.Get(),fullscreen,size,size,{.5f,.5f,4,1}),"WARP native reticle overlay");
	check(readback()[16*size+16]==0xffffffff,"reticle is composed at its original size after target magnification");
	pixels.fill(0xff0000ff);
	check(texture(pixels,other_scene) && SUCCEEDED(device->CreateShaderResourceView(other_scene.Get(),nullptr,&other_source)),"WARP other eye source");
	check(panel.draw_optic(context.Get(),other_source.Get(),optic_ink.Get(),output_view.Get(),fullscreen,size,size,{.5f,.5f,4,1}),"WARP other eye optic");
	check(readback()[16*size+16]==0xff0000ff,"optic uses each eye source without retaining previous eye image");
	context->ClearRenderTargetView(output_view.Get(),green);
	check(panel.draw_optic(context.Get(),other_source.Get(),optic_ink.Get(),output_view.Get(),fullscreen,size,size,{.8f,.5f,4,1}),"WARP laterally displaced aiming eye");
	const auto offset_lens_pixels=readback();
	check(offset_lens_pixels[16*size+3]==0xff000000,"reticle UV overflow stays under scope shadow instead of exposing a square edge");
	check(offset_lens_pixels[16*size+24]==0xff0000ff && offset_lens_pixels[0]==0xff00ff00,"off-axis pupil preserves visible target and exterior world");
	check(panel.draw_optic(context.Get(),other_source.Get(),optic_ink.Get(),output_view.Get(),fullscreen,size,size,{.72f,.72f,4,1}),"WARP diagonally displaced aiming eye");
	const auto diagonal_pupil=readback();
	check(diagonal_pupil[11*size+8]==0xff000000,"texture square corners inside the physical lens are covered by the circular exit pupil");
	check(diagonal_pupil[22*size+22]==0xff0000ff,"diagonal eye offset retains the central target");
	check(panel.draw_optic(context.Get(),other_source.Get(),optic_ink.Get(),output_view.Get(),fullscreen,size,size,{.5f,.5f,4,0}),"WARP outside eye box");
	check(readback()[16*size+16]==0xff000000,"outside eye box blocks target and reticle");
	check(!panel.draw_optic(context.Get(),optic_source.Get(),optic_ink.Get(),output_view.Get(),fullscreen,size,size,{.5f,.5f,0,1}),"invalid zoom rejected");
	check(!panel.draw_optic(context.Get(),optic_source.Get(),optic_ink.Get(),output_view.Get(),fullscreen,size*2,size,{.5f,.5f,4,1}),"optic source/destination extent mismatch rejected");
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> optic_alias;
	check(SUCCEEDED(device->CreateShaderResourceView(output.Get(),nullptr,&optic_alias)) &&
		!panel.draw_optic(context.Get(),optic_alias.Get(),optic_ink.Get(),output_view.Get(),fullscreen,size,size,{.5f,.5f,4,1}),"optic input/output alias rejected");
	viewport_count=1;context->RSGetViewports(&viewport_count,&restored);context->IAGetPrimitiveTopology(&topology);
	context->PSGetShaderResources(5,1,&restored_srv);
	check(std::memcmp(&sentinel,&restored,sizeof(sentinel))==0 && topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST && restored_srv.Get()==saved,"optic composition restores native GPU state");
	{
		const auto frame=vr::recording_frame::make_layout({.3f,.2f,.7f,.8f,90,false},size,size);
		const float neutral[]{.4f,.4f,.4f,1};context->ClearRenderTargetView(output_view.Get(),neutral);
		check(frame.valid && panel.draw_recording_frame(context.Get(),output_view.Get(),size,size,frame.protected_pixels,frame.line_pixels,
			vr::recording_frame::outside_dim),"WARP recording frame");
		const auto recording_pixels=readback();
		check(recording_pixels[16*size+16]==0xff666666,"recorded crop interior is unchanged");
		for (unsigned y=0;y<size;++y) for (unsigned x=0;x<size;++x)
			if (x+.5f>=frame.protected_pixels[0] && x+.5f<=frame.protected_pixels[2] &&
				y+.5f>=frame.protected_pixels[1] && y+.5f<=frame.protected_pixels[3])
				check(recording_pixels[y*size+x]==0xff666666,"every protected recording texel remains bit-identical");
		check((recording_pixels[0]&255)>=35 && (recording_pixels[0]&255)<=36,"frame exterior darkens by 65 percent in linear light");
		check((recording_pixels[16*size+6]&255)>230 && (recording_pixels[16*size+7]&255)<40,"white frame with dark outline");
		check(!panel.draw_recording_frame(context.Get(),output_view.Get(),size+1,size,frame.protected_pixels,frame.line_pixels,.65f),"recording extent mismatch rejected");
		check(!panel.draw_recording_frame(context.Get(),output_view.Get(),size,size,{1,1,0,0},1,.65f),"inverted recording frame rejected");
		for (const float dim : {0.f,1.f})
		{
			context->ClearRenderTargetView(output_view.Get(),neutral);
			check(panel.draw_recording_frame(context.Get(),output_view.Get(),size,size,frame.protected_pixels,frame.line_pixels,dim),
				"WARP recording dimming endpoint");
			const auto dimmed_pixels=readback();
			check(dimmed_pixels[0]==(dim==0?0xff666666u:0xff000000u),"zero dim preserves brightness; full dim is opaque black");
			for (unsigned y=0;y<size;++y) for (unsigned x=0;x<size;++x)
				if (x+.5f>=frame.protected_pixels[0] && x+.5f<=frame.protected_pixels[2] &&
					y+.5f>=frame.protected_pixels[1] && y+.5f<=frame.protected_pixels[3])
					check(dimmed_pixels[y*size+x]==0xff666666,"dimming endpoints preserve every captured texel");
		}
		for (const float dim : {-1.f,1.01f,(std::numeric_limits<float>::quiet_NaN)()})
			check(!panel.draw_recording_frame(context.Get(),output_view.Get(),size,size,frame.protected_pixels,frame.line_pixels,dim),
				"nonfinite or out-of-range dimming rejected");
		vr::overlay_text_texture caption;
		if(argc>1)
		{
			std::ifstream input(argv[1],std::ios::binary);
			const std::string data{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
			auto font=vr::native_caption_font::make_bank_face({reinterpret_cast<const std::byte*>(data.data()),data.size()});
			check(bool(font),"captured native BankGothic TTF registers as a private resource");
			check(font && caption.ensure_utf8(device.Get(),"32 / 32",48,true,font),"native BankGothic digits produce an outlined WARP texture without font substitution");
			const auto view=caption.view();
			check(font && caption.ensure_utf8(device.Get(),"32 / 32",48,true,font) && caption.view()==view,"native font caption keeps the cached texture");
			font.reset();check(caption.view()!=nullptr,"caption retains a font lease after asset owner retires");
		}
		const auto chinese=game_text::text(game_text::key::recording_preview,game_text::locale::simplified_chinese);
		const auto english=game_text::text(game_text::key::recording_preview,game_text::locale::english);
		check(caption.ensure_utf8(device.Get(),chinese,32) && caption.width()>400 && caption.height()>32,
			"Shared UTF-8 Chinese preview caption rasterizes with system font");
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cached=caption.view();
		const auto chinese_width=caption.width();
		check(caption.ensure_utf8(device.Get(),chinese,32) && caption.view()==cached.Get(),
			"unchanged caption reuses immutable GPU texture");
		check(caption.ensure_utf8(device.Get(),english,32) && caption.view()!=cached.Get() && caption.width()!=chinese_width,
			"game language change replaces cached caption and remeasures its translated width");
		cached=caption.view();
		check(caption.ensure_utf8(device.Get(),english,32) && caption.view()==cached.Get(),"English caption is cached too");
		check(caption.ensure_utf8(device.Get(),chinese,32) && caption.width()==chinese_width,
			"switching back to Chinese restores the full original caption");
		check(!caption.ensure_utf8(device.Get(),"\xff",32) && !caption.ensure_utf8(device.Get(),"\xe7\x9b",32) &&
			!caption.ensure_utf8(device.Get(),std::string(129,'x'),32) &&
			!caption.ensure_utf8(device.Get(),std::string(513,'x'),32),"invalid and oversized UTF-8 are rejected without mojibake or truncation");
		check(!caption.ensure_utf8(device.Get(),"",32) && !caption.ensure_utf8(device.Get(),chinese,0),
			"empty and unbounded font requests are rejected");
		vr::overlay_text_texture chamber;
		vr::overlay_text_texture quick_loading;
		check(quick_loading.ensure_utf8(device.Get(),game_text::text(game_text::key::weapon_quick_loading,
			game_text::locale::simplified_chinese),48,true),"quick reload caption rasterizes as outlined UTF-8");
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> quick_texture=quick_loading.view();
		for (int language=0;language<game::LANGUAGE_COUNT;++language)
			check(chamber.ensure_utf8(device.Get(),game_text::text(game_text::key::weapon_needs_chamber,
				game_text::native_locale(language)),48,true),"every game-language chamber prompt passes strict UTF-8 rasterization");
		const auto warning=game_text::text(game_text::key::weapon_needs_chamber,game_text::locale::simplified_chinese);
		check(quick_loading.view()==quick_texture.Get(),"other hand's chamber captions cannot replace the steady quick reload texture");
		check(chamber.ensure_utf8(device.Get(),warning,48),"unoutlined warning fixture");
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> plain_warning=chamber.view();
		check(chamber.ensure_utf8(device.Get(),warning,48,true) && chamber.view()!=plain_warning.Get(),
			"outline style participates in the immutable text cache key");
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> outlined_warning=chamber.view();
		check(chamber.ensure_utf8(device.Get(),warning,48,true) && chamber.view()==outlined_warning.Get(),
			"blinking never reallocates the warning texture");
		Microsoft::WRL::ComPtr<ID3D11Resource> warning_resource;outlined_warning->GetResource(&warning_resource);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> warning_texture;warning_resource.As(&warning_texture);
		D3D11_TEXTURE2D_DESC warning_desc{};warning_texture->GetDesc(&warning_desc);
		warning_desc.Usage=D3D11_USAGE_STAGING;warning_desc.BindFlags=0;warning_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> warning_readback;
		const bool warning_staged=SUCCEEDED(device->CreateTexture2D(&warning_desc,nullptr,&warning_readback));
		check(warning_staged,"warning alpha readback fixture");
		if (warning_staged)
		{
			context->CopyResource(warning_readback.Get(),warning_texture.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const bool mapped_ok=SUCCEEDED(context->Map(warning_readback.Get(),0,D3D11_MAP_READ,0,&mapped));
			check(mapped_ok,"warning alpha readback maps");
			if (mapped_ok)
			{
				unsigned white{},black{},transparent_pixels{};bool premultiplied=true;
				for (unsigned y=0;y<warning_desc.Height;++y)
				{
					const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const std::byte*>(mapped.pData)+y*mapped.RowPitch);
					for (unsigned x=0;x<warning_desc.Width;++x)
					{
						const auto rgb=row[x]&255u,alpha=row[x]>>24;
						white+=rgb>200;black+=alpha>200 && rgb==0;transparent_pixels+=alpha==0;
						premultiplied=premultiplied && rgb<=alpha;
					}
				}
				context->Unmap(warning_readback.Get(),0);
				check(white && black && transparent_pixels && premultiplied,"warning has white glyphs, black outline and transparent padding with valid premultiplied alpha");
			}
		}
		if(warning_staged)for(const auto line:{vr::gameplay::weapon_hud::status_line::chamber,vr::gameplay::weapon_hud::status_line::magazine_empty})
		{
			const auto tint=vr::gameplay::weapon_hud::status_color(line);
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> old=chamber.view();
			check(chamber.ensure_utf8(device.Get(),warning,48,true,{},tint) && chamber.view()!=old.Get(),"text color participates in the GPU cache key");
			Microsoft::WRL::ComPtr<ID3D11Resource> colored;chamber.view()->GetResource(&colored);
			context->CopyResource(warning_readback.Get(),colored.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
			const bool mapped_ok=SUCCEEDED(context->Map(warning_readback.Get(),0,D3D11_MAP_READ,0,&mapped));
			check(mapped_ok,"colored warning readback maps");
			if(mapped_ok)
			{
				unsigned glyphs{},outline{};bool correct=true;
				for(unsigned y=0;y<warning_desc.Height;++y)for(unsigned x=0;x<warning_desc.Width;++x)
				{
					const auto* pixel=static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch+x*4;
					glyphs+=pixel[0]>200;outline+=pixel[3]>200 && pixel[0]==0;
					correct=correct && pixel[0]<=pixel[3] && pixel[1]==(pixel[0]*tint[1]+127)/255 && pixel[2]==(pixel[0]*tint[2]+127)/255;
				}
				context->Unmap(warning_readback.Get(),0);check(glyphs && outline && correct,"yellow/soft-red glyphs preserve black outlines and premultiplied coverage");
			}
		}
		// Use a small deterministic ink fixture to prove caption blending, rather
		// than depending on machine-specific font antialiasing for exact pixels.
		const auto short_frame=vr::recording_frame::make_layout({.3f,.1f,.7f,.4f},size,size);
		context->ClearRenderTargetView(output_view.Get(),neutral);
		check(panel.draw_recording_frame(context.Get(),output_view.Get(),size,size,short_frame.protected_pixels,
			short_frame.line_pixels,vr::recording_frame::outside_dim,ink_view.Get(),{0,20,32,12}),"WARP caption below preview frame");
		const auto labeled=readback();
		check((labeled[26*size+16]&255)>150 && (labeled[26*size]&255)>=35 && (labeled[26*size]&255)<=36,
			"caption glyph blends over dim exterior and transparent padding retains dimming");
		for (unsigned y=0;y<size;++y) for (unsigned x=0;x<size;++x)
			if (x+.5f>=short_frame.protected_pixels[0] && x+.5f<=short_frame.protected_pixels[2] &&
				y+.5f>=short_frame.protected_pixels[1] && y+.5f<=short_frame.protected_pixels[3])
				check(labeled[y*size+x]==0xff666666,"caption leaves entire captured footprint bit-identical");
		check(!panel.draw_recording_frame(context.Get(),output_view.Get(),size,size,short_frame.protected_pixels,
			1,vr::recording_frame::outside_dim,caption.view(),{0,4,32,12}),"caption cannot overlap capture rectangle");
		viewport_count=1;context->RSGetViewports(&viewport_count,&restored);context->IAGetPrimitiveTopology(&topology);
		context->PSGetShaderResources(5,1,&restored_srv);
		check(std::memcmp(&sentinel,&restored,sizeof(sentinel))==0 && topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST && restored_srv.Get()==saved,"recording frame restores native GPU state");
	}
	{
		vr::world_beam::renderer beam;vr::world_beam::projected p;
		std::copy(fullscreen.begin(),fullscreen.end(),p.points.begin());
		auto dd=desc;dd.Format=DXGI_FORMAT_D32_FLOAT;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> dt;Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
		check(SUCCEEDED(device->CreateTexture2D(&dd,nullptr,&dt)) && SUCCEEDED(device->CreateDepthStencilView(dt.Get(),nullptr,&dsv)),"world beam depth fixture");
		const float black[4]{0,0,0,1};context->ClearRenderTargetView(output_view.Get(),black);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.9f,0);
		check(beam.draw(context.Get(),output_view.Get(),dsv.Get(),p,size,size),"world beam shader with native reverse-Z depth test");
		check((readback()[16*size+16]&255)==0,"nearer scene surface occludes laser instead of an x-ray overlay");
		context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.1f,0);
		check(beam.draw(context.Get(),output_view.Get(),dsv.Get(),p,size,size) && (readback()[16*size+16]&255)>100,"same beam becomes visible with the other eye's farther depth");
		p.color={0,1,0,1};context->ClearRenderTargetView(output_view.Get(),black);
		check(beam.draw(context.Get(),output_view.Get(),dsv.Get(),p,size,size) &&
			(readback()[16*size+16]&255)==0 && ((readback()[16*size+16]>>8)&255)>100,
			"shared laser renderer carries the native beam color instead of forcing designator red");
		check(!beam.draw(context.Get(),output_view.Get(),nullptr,p,size,size),"missing depth never degrades to a flat laser overlay");
		viewport_count=1;context->RSGetViewports(&viewport_count,&restored);context->IAGetPrimitiveTopology(&topology);
		check(std::memcmp(&sentinel,&restored,sizeof(sentinel))==0 && topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"world laser restores the native graphics pipeline");
		using namespace vr::gameplay::equipment::special::designator_visual;
		const surface wall{{100,0,0},{-1,0,0},3000,true};vec hit;
		check(endpoint({2,3,0},{1,0,0},wall,40,hit) && std::abs(hit[0]-100)<.001f && std::abs(hit[1]-3)<.001f,"moving gun intersects the same world wall immediately without endpoint smoothing");
		check(!endpoint({2,100,0},{1,0,0},wall,40,hit),"large aim changes cannot extend a stale collision plane indefinitely");
		const auto mesh=geometry({2,0,0},{100,0,0},{-1,0,0},{0,0,0},40);
		vr::spatial_panel::matrix projection{};projection[3]=1;projection[4]=-1;projection[9]=1;projection[14]=.1f;
		const auto l=vr::gameplay::equipment::special::designator_visual::project(mesh,{0,1,0},projection,true);
		const auto r=vr::gameplay::equipment::special::designator_visual::project(mesh,{0,-1,0},projection,true);
		check(std::abs((1-l.points[4][0])-(-1-r.points[4][0]))<1e-5f && l.points[4][2]==.1f,
			"both eye projections reconstruct the same world spot and retain real reverse-Z depth");
		check(std::abs(vr::gameplay::hands::length(vr::gameplay::hands::sub(mesh[5],mesh[4]))-.32f)<1e-5f,"spot diameter is a fixed eight millimetres rather than a view-distance flare");
	}
	std::cout << "spatial panel tests: " << failures << " failures\n";
	return failures ? 1 : 0;
}
