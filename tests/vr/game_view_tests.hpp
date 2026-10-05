#pragma once

#include "component/vr/game_view.hpp"
#include "component/vr/remote_view.hpp"
#include "component/vr/head_pose_bridge.hpp"
#include "component/vr/continuous_view_angles.hpp"
#include "component/vr/scripted_position.hpp"
#include "component/vr/scripted_view.hpp"
#include "component/vr/gameplay/shoulder_anchors.hpp"
#include "test_support.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

inline void expect_native_game_view()
{
	using vr::tests::require;
	const auto close = [](const float a, const float b) { return std::abs(a - b) < 0.002f; };
	{
		vr::game_view::scripted_position position;
		require(position.offset(1,1,{.4f,.3f,.2f},vr::game_view::camera_profiles::aligned)==std::array<float,3>{},"script entry anchors current physical head at authored camera");
		const auto small_offset=position.offset(1,1,{.6f,.3f,.2f},vr::game_view::camera_profiles::aligned);
		require(close(small_offset[0],.02f),"20cm physical movement gives 2cm scripted adjustment");
		const auto large=position.offset(1,1,{4,3,2},vr::game_view::camera_profiles::aligned);
		require(close(std::sqrt(large[0]*large[0]+large[1]*large[1]+large[2]*large[2]),.05f),"large physical step stays within the scripted head envelope");
		require(position.offset(1,2,{4,3,2},vr::game_view::camera_profiles::aligned)==std::array<float,3>{},"recenter establishes a new scripted head baseline");
		require(position.offset(0,2,{.4f,.3f,.2f},vr::game_view::camera_profiles::aligned)==std::array<float,3>{.4f,.3f,.2f},"ordinary head translation remains one to one");
	}
	{
		vr::game_view::scripted_view view;
		view.compose(0,1,10,40,10);
		require(close(view.follow_authored(7,1,11,-100,10,21,100),40),"authored rotation starts from the existing free viewing reference");
		require(close(view.follow_authored(7,1,12,170,30,21,110),50),"script and head yaw increments are independent");
		require(close(view.follow_authored(7,1,12,-80,30,21,110),50),"repeating a native command cannot apply scripted yaw twice");
		require(close(view.follow_authored(7,2,13,-80,0,21,115),85),"recenter preserves world look while the script keeps turning");
		require(close(view.follow_authored(7,2,14,160,0,22,-90),85),"replacement rig rebases script angles without forcing the head to its initial angle");
		require(close(view.follow_authored(7,2,15,0,0,22,-80),95),"new rig contributes only its subsequent yaw change");
		require(close(view.follow_authored(7,2,16,80,20,0,0),95),"missing script reference holds the base and retains free physical head rotation");
		require(close(view.follow_authored(7,2,17,80,20,22,50),95),"restored camera data does not replay a missing rotation as a jump");
		require(close(view.follow_authored(7,2,18,80,20,22,55),100),"valid subsequent reference motion resumes normally");
		float yaw{};require(view.restore_command(0,2,yaw,5,20) && close(yaw,115),"unlink hands back accumulated scripted plus physical yaw exactly once");
		view={};view.follow_authored(8,1,100,20,0,30,179);
		require(close(view.follow_authored(8,1,101,20,0,30,-179),22),"authored yaw wraps through 180 degrees by the short signed delta");
		require(close(view.follow_authored(8,1,1,70,0,30,40),70),"checkpoint rollback discards old authored yaw accumulation");
	}
	const auto full_axis = [](float pitch, float yaw, float roll) {
		constexpr float radians = 0.017453292519943295f;
		pitch *= radians; yaw *= radians; roll *= radians;
		const auto cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
		const auto cr = std::cos(roll), sr = std::sin(roll);
		return vr::head_pose_bridge::matrix3{{{cp*cy,cp*sy,sp},
			{-cy*sp*sr-sy*cr,-sy*sp*sr+cy*cr,cp*sr},
			{-cy*sp*cr+sy*sr,-sy*sp*cr-cy*sr,cp*cr}}};
	};
	{
		vr::game_view::remote_view remote;float pitch=60,yaw=90;
		require(remote.apply(pitch,yaw,{20,30,0},11,1,100) && close(pitch,60) && close(yaw,90),
			"UAV entry keeps its native down-looking aim instead of replacing it with the physical head pitch");
		remote.record(200);
		const auto same=[](const auto& a,const auto& b){for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)if(std::abs(a[i][j]-b[i][j])>.002f)return false;return true;};
		require(same(remote.correction(11,1,200,full_axis(20,30,0)),vr::pose_filter::identity),
			"the head pose already consumed by native UAV input is not applied twice to the camera");
		require(remote.apply(pitch,yaw,{30,35,0},11,1,110) && close(pitch,50) && close(yaw,95),
			"physical head pitch and yaw produce relative native UAV aim commands");
		remote.record(201);
		require(same(remote.correction(11,1,201,full_axis(30,35,0)),vr::pose_filter::identity),
			"completed UAV head input contributes no extra render rotation");
		const auto late=remote.correction(11,1,201,full_axis(35,35,0));
		require(!same(late,vr::pose_filter::identity) && same(vr::pose_filter::multiply(late,full_axis(30,35,0)),full_axis(35,35,0)),
			"late UAV tracking applies only the unconsumed physical rotation");
		pitch=52;require(remote.apply(pitch,yaw,{30,35,0},11,1,120) && close(pitch,52),
			"native UAV pitch changes and clamps survive repeated unchanged head input");
		require(remote.apply(pitch,yaw,{-15,0,0},11,2,130) && close(pitch,52) && close(yaw,95),
			"recenter rearms remote input without steering the UAV");
		require(same(remote.correction(11,2,201,full_axis(-15,0,0)),vr::pose_filter::identity),
			"remote render history cannot cross tracking reference generations");
		require(remote.apply(pitch,yaw,{40,50,0},11,2,500) && close(pitch,52) && close(yaw,95),
			"remote input recovery does not replay head motion from a paused interval");
	}
	{
		vr::game_view::remote_view remote;std::array<std::int8_t,2> command{};
		const std::array<float,2> rates{30,60};
		require(remote.apply_control(command,{0,0,0},15,1,100,1000,rates,{0,0}) && command==std::array<std::int8_t,2>{},
			"native remote axes enter at neutral without an artificial steering jump");
		command={};require(remote.apply_control(command,{3,6,0},15,1,200,1100,rates,{0,0}) && command[0]==127 && command[1]==-127,
			"HMD angular delivery keeps its independent pitch/yaw convention and native degree rates");
		remote.record(1100);
		const auto correction=remote.correction(15,1,1100,full_axis(3,6,0));
		for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)require(close(correction[i][j],i==j?1.f:0.f),
			"native remote command delivery is removed from late camera correction exactly once");
		for(int i=0;i<3;++i)
		{
			command={};require(remote.apply_control(command,{12,6,0},15,1,300+i*100,1200+i*100,rates,{0,0}) && command[0]==127,
				"head motion beyond one native steering step remains pending instead of being discarded by byte saturation");
		}
		command={};remote.apply_control(command,{12,6,0},15,1,600,1500,rates,{0,0});
		require(command[0]==0 && command[1]==0,"holding the head still stops steering once its requested turn is delivered");
		command={-40,20};remote.apply_control(command,{12,6,0},15,1,700,1600,rates,{.5f,1.f});
		require(command[0]==-103 && command[1]==-107,"native mouse axes and VR stick axes combine without overwriting or overflowing the signed protocol");
		for(const auto stick:std::array<std::array<float,2>,4>{{{1,0},{-1,0},{0,1},{0,-1}}})
		{
			vr::game_view::remote_view direction;command={};direction.apply_control(command,{},16,1,100,1000,rates,stick);
			require(command[0]==std::int8_t(-stick[0]*127) && command[1]==std::int8_t(-stick[1]*127),
				"all four stick directions use the native default remote-controller polarity");
		}
		command={};remote.apply_control(command,{-60,90,0},15,2,710,1610,rates,{0,0});
		require(command==std::array<std::int8_t,2>{},"recenter discards pending missile turns instead of replaying the previous reference");
	}
	for (const auto direction : {-1, 1})
	{
		vr::game_view::continuous_angles angles{};
		for (int pitch = 0; pitch <= 270; ++pitch)
		{
			require(angles.update(full_axis(float(pitch * direction), 23, 17)), "full-range angle decomposition");
			require(close(std::remainder(angles.pitch - pitch * direction, 360.f), 0) &&
				close(angles.yaw, 23) && close(angles.roll, 17),
				"crossing either pole retains heading and roll instead of folding pitch");
		}
	}
	for (const auto rotate_yaw : {false, true})
	{
		vr::game_view::continuous_angles angles{};
		for (int turn = 0; turn <= 360; ++turn)
		{
			require(angles.update(full_axis(20, rotate_yaw ? float(turn) : 0,
				rotate_yaw ? 0 : float(turn))) && close(angles.pitch, 20),
				"ordinary yaw and roll turns do not masquerade as over-pitch");
		}
	}
	{
		vr::game_view::state view;
		float pitch = -72, yaw = 27;
		require(view.apply(pitch, yaw, 17, 30, 90, 1) && close(pitch, -47) && close(yaw, 117),
			"HMD drives actual native pitch/yaw, including native pitch delta");
		view.record(100);
		constexpr int packed_backlean = -5592405; // -120 degrees in H2 command units.
		view.record(101, packed_backlean, true);
		float restored{};
		require(view.resolve_pitch(101, packed_backlean, 1, 5, restored) && close(restored, -115),
			"exact tracked command restores over-pitch including native delta");
		require(!view.resolve_pitch(100, 0, 1, 0, restored) &&
			!view.resolve_pitch(101, packed_backlean + 1, 1, 0, restored) &&
			!view.resolve_pitch(101, packed_backlean, 2, 0, restored),
			"untracked, different packed command and old reference retain native pitch rules");
		for (int i = 0; i < 1000; ++i)
			require(view.apply(pitch, yaw, 17, 30, 90, 1) && close(yaw, 117),
				"repeated command sampling does not accumulate the same head yaw");
		yaw += 30; // Native mouse or right-stick turn is still authoritative.
		require(view.apply(pitch, yaw, 17, 30, 100, 1) && close(yaw, 157),
			"native turning and physical turning each contribute once");
		view.record(110);
		float removed = -1;
		require(view.resolve(100, 1, removed) && close(removed, 90),
			"prediction of older command removes its own head contribution");
		require(!view.resolve(105, 1, removed) && close(removed, 90),
			"missing command never borrows a neighboring or latest sample");
		require(view.resolve(110, 2, removed) && removed == 0,
			"recenter never subtracts yaw from a different tracking reference");
		require(view.apply(pitch, yaw, 0, 0, 0, 2) && close(yaw, 157),
			"recenter preserves current real game facing");
		require(view.apply(pitch, yaw, 0, 0, 179, 2), "first wrapped yaw");
		const auto before_wrap = yaw;
		require(view.apply(pitch, yaw, 0, 0, -179, 2) &&
			close(std::remainder(yaw - before_wrap, 360.f), 2), "yaw wrap takes short two-degree step");
		const auto before_bad = yaw;
		require(!view.apply(pitch, yaw, 0, 0, std::numeric_limits<float>::quiet_NaN(), 2) && yaw == before_bad,
			"invalid head cannot poison native state or the next delta");
		require(view.apply(pitch, yaw, 0, 0, -178, 2) &&
			close(std::remainder(yaw - before_bad, 360.f), 1), "invalid sample did not advance yaw history");
		for (int i = 0; i < 65; ++i) view.record(200 + i);
		require(!view.resolve(200, 2, removed) && view.resolve(201, 2, removed), "command history stays bounded to 64");
		view.record(10);
		require(!view.resolve(264, 2, removed) && view.resolve(10, 2, removed), "map time reset clears old history");
		for (int i = 0; i < 100; ++i) view.record(300 + i);
		require(view.resolve(10, 2, removed), "paused render retains its exact command beyond ring eviction");
		require(!view.resolve(11, 2, removed), "paused cache cannot match a different time");
	}
	using namespace vr::head_pose_bridge;
	{
		vr::game_view::state view;float pitch{},yaw=35,removed{};
		require(view.apply(pitch,yaw,0,170,-150,1,30),"alternate input branch is paired with physical heading");
		const float old_native=yaw;view.record(10);
		require(view.apply(pitch,yaw,0,170,180,2,0),"reference recovery retains physical heading on alternate branch");
		view.record(20);
		require(view.resolve(10,2,removed) && close(std::remainder(old_native-removed,360.f),65),
			"older prediction is rebased into the recovered tracking reference without a half turn");
		require(view.resolve(20,2,removed) && close(std::remainder(yaw-removed,360.f),65),"new prediction shares that same world heading");
		require(view.apply(pitch,yaw,0,10,0,3,0) && close(yaw,65),"canonical branch recovery preserves physical native facing");
	}
	constexpr matrix3 identity{{{1,0,0},{0,1,0},{0,0,1}}};
	const auto tracking_yaw = [](const float degrees) {
		const auto r = degrees * 0.017453292519943295f;
		return matrix3{{{std::cos(r),0,std::sin(r)}, {0,1,0}, {-std::sin(r),0,std::cos(r)}}};
	};
	const auto native_axis = [](const float degrees, float (&axis)[3][3]) {
		const auto r = degrees * 0.017453292519943295f;
		const float result[3][3]{{std::cos(r),std::sin(r),0}, {-std::sin(r),std::cos(r),0}, {0,0,1}};
		std::memcpy(axis, result, sizeof(result));
	};
	const auto heading = [](const float (&axis)[3][3]) {
		return std::atan2(axis[0][1],axis[0][0]) * 57.29577951308232f;
	};
	reset(); configure_target(true); set_enabled(true); set_world_scale(40);
	float angles[]{-72,27,0};
	const float delta[]{17,0,0};
	require(!apply_game_view(angles, delta), "native view requires valid tracking");
	publish_tracking_pose({{0,1.6f,0},identity});
	publish_tracking_pose({{0,1.6f,0},tracking_yaw(90)});
	require(apply_game_view(angles,delta) && close(angles[1],117) && close(angles[0]+delta[0],0),
		"real tracking-space pose reaches native view without controller input");
	record_game_command(100);
	float origin[]{0,0,0}, axis[3][3]{};
	native_axis(117,axis);
	require(apply_camera(origin,axis,100) && close(heading(axis),117), "camera does not double native HMD yaw");
	spatial_frame spatial{};
	require(get_spatial_frame(spatial) && close(get_status().base_yaw_degrees,27),
		"hand and room-scale tracking basis retains native virtual-world heading");
	// Command 110 is ahead of prediction, and the render pose is newer again.
	publish_tracking_pose({{0,1.6f,0},tracking_yaw(120)});
	require(apply_game_view(angles,delta) && close(angles[1],147), "next command tracks real head turn");
	record_game_command(110);
	publish_tracking_pose({{0,1.6f,0},tracking_yaw(140)});
	native_axis(117,axis);
	require(apply_camera(origin,axis,100) && close(heading(axis),167) && close(get_status().base_yaw_degrees,27),
		"late tracking with older prediction keeps room-scale world fixed and applies new pose once");
	native_axis(147,axis);
	require(apply_camera(origin,axis,110) && close(heading(axis),167), "advancing prediction cannot add a second turn");
	native_axis(147,axis);
	require(!apply_camera(origin,axis,105) && close(heading(axis),147) && !get_spatial_frame(spatial),
		"unmatched command fails camera transaction instead of guessing its input epoch");
	request_recenter();
	require(!apply_game_view(angles,delta), "pending recenter cannot mutate native view");
	publish_tracking_pose({{0,1.6f,0},tracking_yaw(140)});
	require(apply_game_view(angles,delta) && close(angles[1],147), "recenter preserves native yaw");
	record_game_command(120);
	native_axis(147,axis);
	require(apply_camera(origin,axis,120) && close(heading(axis),147), "new reference cannot reuse old head offset");
	invalidate_pose();
	require(!apply_game_view(angles,delta), "lost tracking does not update native view");
	set_enabled(false);
	require(!apply_game_view(angles,delta), "disabled VR retains native controls");
	set_enabled(true);
	publish_tracking_pose({{0,1.6f,0},identity});
	// Physical pitch drives the native forward vector; physical roll does not
	// tilt movement. At vertical pitch the left axis supplies a finite yaw.
	for (const auto& rotation : std::array<matrix3, 3>{
		matrix3{{{1,0,0},{0,.8f,-.6f},{0,.6f,.8f}}},
		matrix3{{{.8f,-.6f,0},{.6f,.8f,0},{0,0,1}}},
		matrix3{{{1,0,0},{0,0,-1},{0,1,0}}}})
	{
		publish_tracking_pose({{0,1.6f,0},rotation});
		require(apply_game_view(angles,delta) && close(angles[1],147),
			"pitch/roll including vertical look cannot introduce native yaw");
		const auto physical_pitch = std::asin(rotation[2][1]) * 57.29577951308232f;
		require(close(angles[0]+delta[0],-physical_pitch), "native pitch matches physical forward direction");
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(160));
	require(!apply_game_view(angles,delta), "stale head pose cannot drive native commands");
	reset(); configure_target(true); set_enabled(true);
	publish_tracking_pose({{0,1.6f,0},identity});
	angles[0] = 0; angles[1] = 35;
	for (int pitch = 0; pitch <= 120; ++pitch)
	{
		const auto r = pitch * 0.017453292519943295f;
		publish_tracking_pose({{0,1.6f,0},matrix3{{{1,0,0}, {0,std::cos(r),-std::sin(r)},
			{0,std::sin(r),std::cos(r)}}}});
		require(apply_game_view(angles,delta) && close(angles[0] + delta[0], -float(pitch)) && close(angles[1],35),
			"headset backlean passes 90 degrees without yaw reversal in native command");
		record_game_command(500 + pitch);
		const auto native = full_axis(float(pitch), 35, 0);
		std::memcpy(axis, native.data(), sizeof(axis));
		require(apply_camera(origin,axis,500 + pitch,angles), "over-pitch camera uses native Euler heading");
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t col = 0; col < 3; ++col)
				require(close(axis[row][col],native[row][col]), "camera applies full tracked pose once past pole");
		require(get_spatial_frame(spatial) && close(spatial.head_yaw_axis[0][0], std::cos(35.f * .017453292519943295f)),
			"body slots retain horizontal heading while looking backward over head");
	}
	request_recenter();
	const auto r = 120.f * .017453292519943295f;
	publish_tracking_pose({{0,1.6f,0},matrix3{{{1,0,0},{0,std::cos(r),-std::sin(r)},
		{0,std::sin(r),std::cos(r)}}}});
	require(apply_game_view(angles,delta) && close(angles[0] + delta[0], -120) && close(angles[1],35),
		"recentering while leaned past pole preserves physical pitch and heading");
	// Reproduce the retained alternate Euler branch from the real standby report.
	// Native over-pitch input stays continuous, but shoulders/body must be upright.
	const auto tracking_from_h2=[](const matrix3& a) {
		return matrix3{{{a[1][1],-a[2][1],a[0][1]},
			{-a[1][2],a[2][2],-a[0][2]},{a[1][0],-a[2][0],a[0][0]}}};
	};
	reset();configure_target(true);set_enabled(true);set_world_scale(40);
	publish_tracking_pose({{0,1.6f,0},identity});
	float recovered_angles[]{0,35,0},zero_delta[]{0,0,0};
	for(int p=0;p<=162;p+=3)
		publish_tracking_pose({{0,1.6f,0},tracking_from_h2(full_axis(float(p),0,0))});
	for(int step=1;step<=120;++step)
		publish_tracking_pose({{0,1.6f,0},tracking_from_h2(full_axis(162,73.f*step/120,-177.f*step/120))});
	const auto recovered_pose=tracking_from_h2(full_axis(162,73,-177));
	for(bool reacquire : {false,true})
	{
		if(reacquire){invalidate_pose();publish_tracking_pose({{0,1.6f,0},recovered_pose});}
		require(apply_game_view(recovered_angles,zero_delta),"resumed head still supplies native view");
		record_game_command(9000+int(reacquire));native_axis(recovered_angles[1],axis);
		origin[0]=origin[1]=origin[2]=0;
		require(apply_camera(origin,axis,9000+int(reacquire),recovered_angles) && get_spatial_frame(spatial),
			"alternate-branch recovery publishes a coherent camera/hand frame");
		const auto horizontal=std::hypot(axis[0][0],axis[0][1]);
		const vector3 actual_left{-axis[0][1]/horizontal,axis[0][0]/horizontal,0};
		std::array<vr::gameplay::hands::vec,2> shoulders{};
		require(vr::gameplay::hands::make_shoulders(spatial,{}, {},shoulders),"recovered shoulders remain valid");
		const auto separation=vr::gameplay::hands::sub(shoulders[0],shoulders[1]);
		require(vr::gameplay::hands::dot(separation,actual_left)>14.f &&
			spatial.body.yaw_axis[0][0]*axis[0][0]+spatial.body.yaw_axis[0][1]*axis[0][1]>.94f,
			"left shoulder stays left and body faces forward before and after tracking reacquisition");
	}
	reset();configure_target(true);set_enabled(true);
	publish_tracking_pose({{0,1.6f,0},identity});
	float story_angles[]{0,35,0},story_delta[]{0,0,0};
	require(apply_game_view(story_angles,story_delta),"establish pre-story tracking frame");
	record_game_command(100,0,true);native_axis(35,axis);
	require(apply_camera(origin,axis,100,story_angles),"establish pre-story rendered heading");
	publish_tracking_pose({{0,1.6f,0},tracking_yaw(60)});
	story_angles[1]=8;
	require(!apply_game_view(story_angles,story_delta,77) && close(story_angles[1],8),
		"scripted HMD look does not fight native clamp input");
	record_game_command(110,0,false);native_axis(8,axis);
	require(apply_camera(origin, axis, 110, story_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::free, 77, 0}) && close(heading(axis),95),
		"real bridge freely composes HMD yaw over scripted body heading");
	story_angles[1]=-70;story_delta[1]=12;
	require(apply_game_view(story_angles,story_delta) && close(story_angles[1]+story_delta[1],95),
		"script exit restores real native heading including delta angles");
	record_game_command(120,0,true);float released[]{0,95,0};native_axis(95,axis);
	require(apply_camera(origin,axis,120,released) && close(heading(axis),95),
		"real bridge exit does not snap or double-count head yaw");
	publish_tracking_pose({{0,1.6f,0},tracking_yaw(80)});
	require(apply_game_view(story_angles,story_delta) && close(story_angles[1]+story_delta[1],115),
		"free gameplay head input resumes after scripted camera handoff");
	reset_game_view();native_axis(-30,axis);float respawn[]{0,-30,0};
	require(apply_camera(origin,axis,120,respawn) && close(get_status().base_yaw_degrees,-30),
		"same-time checkpoint uses native spawn heading without the old HMD contribution");
	story_angles[1]=-30;story_delta[1]=0;
	require(apply_game_view(story_angles,story_delta),"checkpoint history reset retains available head tracking");
	record_game_command(120,0,true);native_axis(story_angles[1],axis);
		require(apply_camera(origin,axis,120,story_angles) && close(get_status().base_yaw_degrees,-30),
		"saved time and pointer reuse establish the new native world heading");
	reset();configure_target(true);set_enabled(true);publish_tracking_pose({{0,1.6f,0},identity});
	publish_tracking_pose({{.4f,1.6f,-.2f},tracking_yaw(75)});
	float locked_angles[]{25,50,15};auto scripted_axis=full_axis(25,50,15);
	std::memcpy(axis,scripted_axis.data(),sizeof(axis));origin[0]=100;origin[1]=200;origin[2]=300;
	require(apply_camera(origin, axis, 900, locked_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::locked, 123, 0}),"native camera mode accepts a valid scripted view");
	require(origin[0]==100 && origin[1]==200 && origin[2]==300 && std::memcmp(axis,scripted_axis.data(),sizeof(axis))==0,
		"locked camera preserves authored translation pitch yaw and roll despite HMD movement");
	require(!get_spatial_frame(spatial),"native camera mode cannot publish stale hand interaction coordinates");
	require(!apply_game_view(locked_angles,delta,123),"locked camera also suspends HMD command-angle injection");
	float unlocked[]{0,50,0};const float no_delta[]{0,0,0};
	require(apply_game_view(unlocked,no_delta) && close(unlocked[1],50),"native camera exit retains last authored yaw");
	record_game_command(920,0,true);native_axis(50,axis);
	require(apply_camera(origin,axis,920,unlocked) && close(heading(axis),50),"tracked camera resumes without double head yaw");
	reset();configure_target(true);set_enabled(true);publish_tracking_pose({{0,1.6f,0},identity});
	const float cx=std::cos(.3f),sx=std::sin(.3f),cz=std::cos(.2f),sz=std::sin(.2f);
	const matrix3 tilted{{{cz,-sz*cx,sz*sx},{sz,cz*cx,-cz*sx},{0,sx,cx}}};
	publish_tracking_pose({{.1f,1.65f,0},tilted});
	float breach_angles[]{35,50,27};auto breach_axis=full_axis(35,50,27);
	std::memcpy(axis,breach_axis.data(),sizeof(axis));origin[0]=100;origin[1]=200;origin[2]=300;
	require(apply_camera(origin, axis, 1000, breach_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::yaw, 201, 0}),"breach yaw policy accepts authored camera with tracked tilt");
	matrix3 expected{};std::memcpy(expected.data(),axis,sizeof(axis));
	const auto tilted_status=get_status();const auto world_heading=heading(axis);
	require(std::abs(tilted_status.output_pitch_degrees)>1 && std::abs(tilted_status.output_horizon_roll_degrees)>1,
		"headset still controls pitch and roll during a breach");
	require(get_spatial_frame(spatial),"yaw-only breach camera publishes fresh gun interaction coordinates");
	breach_angles[0]=-75;breach_angles[2]=110;breach_axis=full_axis(-75,50,110);
	std::memcpy(axis,breach_axis.data(),sizeof(axis));origin[0]=100;origin[1]=200;origin[2]=300;
	require(apply_camera(origin, axis, 1050, breach_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::yaw, 201, 0}),"breach ignores changing native pitch and roll");
	for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)require(close(axis[i][j],expected[i][j]),"native tilt cannot contaminate tracked orientation");
	breach_angles[1]=80;breach_axis=full_axis(-75,80,110);std::memcpy(axis,breach_axis.data(),sizeof(axis));
	require(apply_camera(origin, axis, 1100, breach_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::yaw, 201, 0}) &&
		close(std::remainder(heading(axis)-world_heading,360.f),30),"native breach yaw continues to animate instead of freezing at entry");
	require(close(get_status().output_pitch_degrees,tilted_status.output_pitch_degrees) &&
		close(get_status().output_horizon_roll_degrees,tilted_status.output_horizon_roll_degrees),"yaw animation retains physical pitch and roll");
	const float exit_heading=heading(axis);float exit_angles[]{0,-90,0};
	require(!apply_game_view(exit_angles,no_delta,201),"linked breach cannot feed tracked yaw back into native rig");
	require(apply_game_view(exit_angles,no_delta) && close(exit_angles[1],exit_heading),"breach exit restores rendered yaw with tracked look included once");
	record_game_command(1150,0,true);native_axis(exit_angles[1],axis);
	require(apply_camera(origin,axis,1150,exit_angles) && close(heading(axis),exit_heading),"breach combat handoff does not double head yaw");
	reset();configure_target(true);set_enabled(true);publish_tracking_pose({{0,1.6f,0},identity});
	publish_tracking_pose({{.4f,1.6f,0},identity});float anchor_angles[]{0,30,0};native_axis(30,axis);
	origin[0]=100;origin[1]=200;origin[2]=300;
	require(apply_camera(origin, axis, 1300, anchor_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::yaw, 301, 501}) &&
		origin[0]==100 && origin[1]==200 && origin[2]==300,"real bridge anchors a displaced physical head at script entry");
	const tracking_pose moved{{.6f,1.6f,0},identity};publish_tracking_pose(moved);native_axis(30,axis);
	origin[0]=100;origin[1]=200;origin[2]=300;
	require(apply_camera(origin, axis, 1350, anchor_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::yaw, 301, 501}),"scaled cinematic translation composes with existing angle policy");
	require(close(std::hypot(origin[0]-100,origin[1]-200),.02f*get_status().world_scale),"script camera translates at ten percent of physical movement");
	world_pose mapped;
	require(get_spatial_frame(spatial) && tracking_to_world(spatial,moved,mapped) && close(mapped.position[0],origin[0]) && close(mapped.position[1],origin[1]),
		"hand tracking frame remains aligned to the scaled head camera");
	reset();configure_target(true);set_enabled(true);publish_tracking_pose({{0,1.6f,0},identity});
	publish_tracking_pose({{0,1.6f,0},tracking_yaw(75)});float seeded_angles[]{0,50,0};native_axis(50,axis);
	require(apply_camera(origin, axis, 1500, seeded_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::aligned, 401, 601}) && close(heading(axis),50),
		"free-look scene initially faces native camera regardless of previous physical heading");
	publish_tracking_pose({{0,1.6f,0},tracking_yaw(95)});seeded_angles[1]=160;native_axis(160,axis);
	require(apply_camera(origin, axis, 1550, seeded_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::aligned, 401, 601}) && close(heading(axis),70),
		"free-look seed is applied once rather than pulling subsequent head turns back");
	seeded_angles[1]=-90;native_axis(-90,axis);
	require(apply_camera(origin, axis, 1600, seeded_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::aligned, 401, 602}) && close(heading(axis),-90),
		"new linked body seeds its own heading even when sequence epoch is unchanged");
	publish_tracking_pose({{0,1.6f,0},tilted});
	seeded_angles[0]=-70;seeded_angles[1]=135;seeded_angles[2]=95;
	auto cinematic_axis=full_axis(-70,135,95);std::memcpy(axis,cinematic_axis.data(),sizeof(axis));
	require(apply_camera(origin, axis, 1650, seeded_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::aligned, 401, 602}),
		"slide and knockdown free-look accepts a fully rotating authored camera");
	matrix3 free_expected{};std::memcpy(free_expected.data(),axis,sizeof(axis));const auto free_status=get_status();
	require(std::abs(free_status.output_pitch_degrees)>1 && std::abs(free_status.output_horizon_roll_degrees)>1,
		"free-look cinematic still follows physical headset pitch and roll");
	seeded_angles[0]=60;seeded_angles[1]=-35;seeded_angles[2]=-120;
	cinematic_axis=full_axis(60,-35,-120);std::memcpy(axis,cinematic_axis.data(),sizeof(axis));
	require(apply_camera(origin, axis, 1700, seeded_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::aligned, 401, 602}),
		"changing native view clamps do not terminate linked free look");
	for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)require(close(axis[i][j],free_expected[i][j]),
		"authored pitch yaw and roll cannot pull the player's view during a free-look animation");
	const auto free_heading=heading(axis);float free_exit[]{0,120,0};
	require(apply_game_view(free_exit,no_delta) && close(free_exit[1],free_heading),
		"unlink hands the rendered free-look heading back to ordinary controls");
	record_game_command(1750,0,true);native_axis(free_exit[1],axis);
	require(apply_camera(origin,axis,1750,free_exit) && close(heading(axis),free_heading),
		"ordinary camera resumes without applying cinematic head yaw twice");
	{
		// Cliffhanger keeps one free tracking heading while its native body
		// changes from ledge to climb, jump/slide/hang and final ascent.
		reset();configure_target(true);set_enabled(true);set_world_scale(40);
		publish_tracking_pose({{0,1.6f,0},identity});
		float native_reference[]{0,35,0};
		require(apply_game_view(native_reference,no_delta),"establish Cliffhanger approach heading");
		record_game_command(2000,0,true);native_axis(35,axis);
		require(apply_camera(origin,axis,2000,native_reference),"render approach before the scripted clamp");
		publish_tracking_pose({{0,1.6f,0},tilted});
		float constrained[]{70,-120,100};
		require(!apply_game_view(constrained,no_delta,701) && constrained[0]==70 && constrained[1]==-120 && constrained[2]==100,
			"free observation leaves native angle and movement references untouched");
		auto native_clip=full_axis(70,-120,100);std::memcpy(axis,native_clip.data(),sizeof(axis));
		origin[0]=100;origin[1]=200;origin[2]=300;
		require(apply_camera(origin, axis, 2050, constrained, vr::game_view::camera_request{vr::game_view::camera_profiles::free, 701, 801}),
			"early Cliffhanger camera composes full HMD rotation independently of the animation");
		matrix3 fixed_head{};std::memcpy(fixed_head.data(),axis,sizeof(axis));
		require(close(origin[0],100) && close(origin[1],200) && close(origin[2],300),"linked entry anchors only physical head displacement");
		constrained[0]=-65;constrained[1]=160;constrained[2]=-110;
		native_clip=full_axis(-65,160,-110);std::memcpy(axis,native_clip.data(),sizeof(axis));
		origin[0]=140;origin[1]=180;origin[2]=380;
		require(apply_camera(origin, axis, 2100, constrained, vr::game_view::camera_request{vr::game_view::camera_profiles::free, 701, 801}),"native animation advances with a stationary physical head");
		for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)require(close(axis[i][j],fixed_head[i][j]),"changing every authored camera angle cannot clamp or rotate the HMD");
		require(close(origin[0],140) && close(origin[1],180) && close(origin[2],380),"authored climb/jump translation retains its full amplitude");
		publish_tracking_pose({{.2f,1.6f,0},tilted});
		std::memcpy(axis,native_clip.data(),sizeof(axis));origin[0]=140;origin[1]=180;origin[2]=380;
		require(apply_camera(origin, axis, 2150, constrained, vr::game_view::camera_request{vr::game_view::camera_profiles::free, 701, 801}) &&
			close(std::hypot(std::hypot(origin[0]-140,origin[1]-180),origin[2]-380),.02f*40),
			"only added physical head motion is reduced to ten percent while the body moves");
		// A new body reanchors positional comfort, never the viewing direction.
		constrained[1]=-30;native_clip=full_axis(-65,-30,-110);std::memcpy(axis,native_clip.data(),sizeof(axis));
		origin[0]=140;origin[1]=180;origin[2]=380;
		require(apply_camera(origin, axis, 2200, constrained, vr::game_view::camera_request{vr::game_view::camera_profiles::free, 701, 802}),"replacement climbing body is admitted without reseeding its camera yaw");
		for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)require(close(axis[i][j],fixed_head[i][j]),"rig handoff preserves unrestricted world viewing direction");
		const auto before_recenter=heading(axis);
		request_recenter();publish_tracking_pose({{.2f,1.6f,0},tilted});
		std::memcpy(axis,native_clip.data(),sizeof(axis));origin[0]=140;origin[1]=180;origin[2]=380;
		require(apply_camera(origin, axis, 2250, constrained, vr::game_view::camera_request{vr::game_view::camera_profiles::free, 701, 802}) && close(heading(axis),before_recenter),
			"recenter during climbing preserves world heading and does not borrow the native clamp");
		const auto exit_heading=heading(axis);float exit_command[]{0,0,0};
		require(apply_game_view(exit_command,no_delta) && close(exit_command[1],exit_heading),"native unlink hands free-look heading back to normal input");
		record_game_command(2300,0,true);native_axis(exit_command[1],axis);
		require(apply_camera(origin,axis,2300,exit_command) && close(heading(axis),exit_heading),"early camera exit does not double HMD yaw");
	}
	{
		reset();configure_target(true);set_enabled(true);set_world_scale(40);
		publish_tracking_pose({{0,1.6f,0},identity});float intro_angles[]{0,35,0};
		require(apply_game_view(intro_angles,no_delta),"establish helicopter approach reference");
		record_game_command(2400,0,true);native_axis(intro_angles[1],axis);
		require(apply_camera(origin,axis,2400,intro_angles),"render pre-helicopter free head reference");
		publish_tracking_pose({{0,1.6f,0},tilted});
		vr::game_view::scripted_rotation_reference authored{901,full_axis(5,100,15)};
		float clamp[]{70,-90,-50};auto clamp_axis=full_axis(70,-90,-50);std::memcpy(axis,clamp_axis.data(),sizeof(axis));
		origin[0]=100;origin[1]=200;origin[2]=300;
		require(apply_camera(origin, axis, 2450, clamp, vr::game_view::camera_request{vr::game_view::camera_profiles::authored, 801, 901}, &authored),"helicopter consumes an independent native camera-tag reference");
		const auto before=heading(axis);const auto initial=get_status();
		authored.axis=full_axis(-35,110,-60);clamp[0]=-60;clamp[1]=140;clamp[2]=80;
		clamp_axis=full_axis(-60,140,80);std::memcpy(axis,clamp_axis.data(),sizeof(axis));origin[0]=120;origin[1]=210;origin[2]=330;
		require(apply_camera(origin, axis, 2500, clamp, vr::game_view::camera_request{vr::game_view::camera_profiles::authored, 801, 901}, &authored) &&
			close(std::remainder(heading(axis)-before,360.f),10),"helicopter yaw moves the view by its actual ten-degree turn, not the changed clamp");
		require(close(get_status().output_pitch_degrees,initial.output_pitch_degrees) && close(get_status().output_horizon_roll_degrees,initial.output_horizon_roll_degrees),
			"helicopter pitch and bank do not override physical headset pitch or roll");
		require(close(origin[0],120) && close(origin[1],210) && close(origin[2],330),"authored rotation leaves native helicopter translation at full amplitude");
		matrix3 transported{};std::memcpy(transported.data(),axis,sizeof(axis));
		clamp[1]=-40;clamp_axis=full_axis(-60,-40,80);std::memcpy(axis,clamp_axis.data(),sizeof(axis));
		require(apply_camera(origin, axis, 2550, clamp, vr::game_view::camera_request{vr::game_view::camera_profiles::authored, 801, 901}, &authored),"camera remains valid while only native view restrictions change");
		for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)require(close(axis[i][j],transported[i][j]),"changing a clamp cannot become authored camera movement");
		const auto transported_base=get_status().base_yaw_degrees;
		publish_tracking_pose({{0,1.6f,0},tracking_yaw(50)});std::memcpy(axis,clamp_axis.data(),sizeof(axis));
		require(apply_camera(origin, axis, 2600, clamp, vr::game_view::camera_request{vr::game_view::camera_profiles::authored, 801, 901}, &authored) &&
			close(get_status().base_yaw_degrees,transported_base),"physical head turning cannot alter the authored rotation reference");
		const auto before_center=heading(axis);request_recenter();publish_tracking_pose({{0,1.6f,0},tracking_yaw(50)});
		authored.axis=full_axis(30,115,45);std::memcpy(axis,clamp_axis.data(),sizeof(axis));
		require(apply_camera(origin, axis, 2650, clamp, vr::game_view::camera_request{vr::game_view::camera_profiles::authored, 801, 901}, &authored) &&
			close(std::remainder(heading(axis)-before_center,360.f),5),"recenter retains free look and the helicopter's simultaneous five-degree turn");
		const auto before_handoff=heading(axis);authored.source=902;authored.axis=full_axis(0,-130,0);std::memcpy(axis,clamp_axis.data(),sizeof(axis));
		require(apply_camera(origin, axis, 2700, clamp, vr::game_view::camera_request{vr::game_view::camera_profiles::authored, 801, 902}, &authored) && close(heading(axis),before_handoff),
			"legacy-to-remastered camera handoff does not apply the rig's different initial yaw");
		authored.axis[0][0]=std::numeric_limits<float>::quiet_NaN();std::memcpy(axis,clamp_axis.data(),sizeof(axis));
		require(apply_camera(origin, axis, 2750, clamp, vr::game_view::camera_request{vr::game_view::camera_profiles::authored, 801, 902}, &authored) && close(heading(axis),before_handoff),
			"unavailable authored pose never falls back to the constrained native view angle");
		float handback[]{0,0,0};require(apply_game_view(handback,no_delta) && close(handback[1],before_handoff),"helicopter exit returns the accumulated view to native commands once");
		record_game_command(2800,0,true);native_axis(handback[1],axis);
		require(apply_camera(origin,axis,2800,handback) && close(heading(axis),before_handoff),"helicopter handback does not double authored or physical yaw");
	}
	reset();configure_target(true);set_enabled(true);set_world_scale(40);
	publish_tracking_pose({{0,1.6f,0},identity});publish_tracking_pose({{0,1.6f,0},tracking_yaw(30)});
	float scope_angles[]{12,40,0};
	const std::uint64_t scope_epoch=(1ull<<63)|701;
	for(const auto& pose:{tracking_pose{{0,1.6f,0},tracking_yaw(30)},tracking_pose{{.2f,1.8f,.1f},tilted}})
	{
		publish_tracking_pose(pose);
		const auto before=std::array{scope_angles[0],scope_angles[1],scope_angles[2]};
		require(!apply_game_view(scope_angles,no_delta,scope_epoch) && std::equal(before.begin(),before.end(),scope_angles),
			"head rotation cannot change a fixed-scope native command");
		const auto authored=full_axis(-scope_angles[0],scope_angles[1],0);std::memcpy(axis,authored.data(),sizeof(axis));
		origin[0]=100;origin[1]=200;origin[2]=300;
		require(apply_camera(origin, axis, 1800, scope_angles, vr::game_view::camera_request{vr::game_view::camera_profiles::locked, scope_epoch, 0}) &&
			std::memcmp(axis,authored.data(),sizeof(axis))==0 && origin[0]==100 && origin[1]==200 && origin[2]==300,
			"head rotation and translation leave the native thermal camera bit-identical");
		tracking_reference reference;vector3 local_hand{};
		require(get_tracking_reference(reference) && reference.generation==get_status().recenter_count &&
			tracking_position(reference,{.1f,1.5f,-.5f},local_hand),"native-camera ownership still exposes a read-only controller tracking reference");
		require(close(local_hand[0],.5f) && close(local_hand[1],-.1f) && close(local_hand[2],-.1f),
			"raw hand position stays in the recenter frame and is unaffected by current head rotation or native scope aim");
		scope_angles[0]-=3;scope_angles[1]+=7; // Right-stick/native aim still changes the image camera.
	}
	float scope_exit[]{0,scope_angles[1]-7,0};
	require(apply_game_view(scope_exit,no_delta) && close(scope_exit[1],scope_angles[1]-7),
		"scope exit restores ordinary head control without accumulated mounted head turns");
	// Identical physical orientations must produce identical views regardless
	// of the Euler branch reached while the headset was off or upside down.
	for(const auto policy:{vr::game_view::camera_profiles::gameplay,vr::game_view::camera_profiles::free,
		vr::game_view::camera_profiles::locked,vr::game_view::camera_profiles::yaw,
		vr::game_view::camera_profiles::aligned,vr::game_view::camera_profiles::vehicle,
		vr::game_view::camera_profiles::authored})
	{
		std::array<matrix3,3> canonical{};
		for(bool alternate:{false,true})
		{
			reset();configure_target(true);set_enabled(true);set_world_scale(40);
			publish_tracking_pose({{0,1.6f,0},identity});
			float normal[]{0,35,0};require(apply_game_view(normal,no_delta),"resume fixture establishes native input");
			record_game_command(100,0,true);native_axis(normal[1],axis);origin[0]=origin[1]=origin[2]=0;
			require(apply_camera(origin,axis,100,normal),"resume fixture establishes camera history");
			if(alternate)
			{
				for(int p=0;p<=162;p+=3)publish_tracking_pose({{0,1.6f,0},tracking_from_h2(full_axis(float(p),0,0))});
				for(int step=1;step<=120;++step)publish_tracking_pose({{0,1.6f,0},tracking_from_h2(full_axis(162,73.f*step/120,-177.f*step/120))});
			}
			const auto pose=tracking_from_h2(full_axis(173.845947f,-110.583710f,172.956070f));
			publish_tracking_pose({{0,1.6f,0},pose});
			require(apply_game_view(normal,no_delta),"captured upright pose supplies native input on either Euler branch");
			record_game_command(200,0,true);native_axis(normal[1],axis);
			require(apply_camera(origin,axis,200,normal),"captured pose renders before scene takeover");
			const bool scripted=policy!=vr::game_view::camera_profiles::gameplay;
			const std::uint64_t epoch=scripted?600:0;
			float authored_angles[]{0,35,0};vr::game_view::scripted_rotation_reference authored{700,full_axis(0,35,0)};
			for(unsigned stage=0;stage<3;++stage)
			{
				const int time=300+int(stage)*100;
				if(stage==1){invalidate_pose();publish_tracking_pose({{0,1.6f,0},pose});}
				if(stage==2){request_recenter();publish_tracking_pose({{0,1.6f,0},pose});}
				if(!scripted)require(apply_game_view(normal,no_delta),"ordinary camera resumes with valid native input");
				record_game_command(time,0,!scripted);
				auto* input=scripted?authored_angles:normal;native_axis(input[1],axis);
				require(apply_camera(origin, axis, time, input, vr::game_view::camera_request{policy, epoch, scripted?700ull:0ull}, &authored),"camera policy admits the resume fixture");
				matrix3 actual;std::memcpy(actual.data(),axis,sizeof(axis));
				if(!alternate)canonical[stage]=actual;
				else for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
					require(std::abs(actual[row][col]-canonical[stage][row][col])<.003f,"all camera policies reject a 180-degree Euler-branch view reversal");
				if(stage)for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
						require(std::abs(actual[row][col]-canonical[0][row][col])<.003f,std::format("resume view mismatch policy={} alternate={} stage={} cell={}/{} before={} after={}",
							vr::game_view::name(policy),alternate,stage,row,col,canonical[0][row][col],actual[row][col]));
			}
			if(scripted)
			{
				const auto previous=heading(axis);float exit[]{0,0,0};
				require(apply_game_view(exit,no_delta),"all scripted cameras hand control back after recovery");
				record_game_command(700,0,true);native_axis(exit[1],axis);
				require(apply_camera(origin,axis,700,exit),"resumed scripted camera exit renders");
				require(std::abs(std::remainder(heading(axis)-previous,360.f))<2.f,"camera exit cannot introduce a backward-facing half turn");
			}
		}
	}
	{
		reset();configure_target(true);set_enabled(true);set_world_scale(40);
		publish_tracking_pose({{0,1.6f,0},identity});
		auto policy=vr::game_view::camera_profiles::authored_full;policy.translation=vr::game_view::head_translation::fixed;
		const vr::game_view::camera_request shot{policy,910,911,910};
		vr::game_view::scripted_rotation_reference authored{911,full_axis(20,45,30)};
		float stale_view[]{0,180,0};native_axis(180,axis);origin[0]=100;origin[1]=200;origin[2]=300;
		require(apply_camera(origin,axis,9000,stale_view,shot,&authored),"full authored rope camera enters through the real bridge");
		publish_tracking_pose({{.3f,1.8f,.2f},tilted});
		native_axis(180,axis);origin[0]=120;origin[1]=210;origin[2]=340;
		require(apply_camera(origin,axis,9010,stale_view,shot,&authored) && close(origin[0],120) && close(origin[1],210) && close(origin[2],340),
			"rope props retain exact native camera translation despite physical leaning");
		const auto reference=get_status().recenter_count;
		request_recenter(true);publish_tracking_pose({{.3f,1.8f,.2f},tilted});
		native_axis(180,axis);origin[0]=120;origin[1]=210;origin[2]=340;
		require(get_status().recenter_count==reference+1 && apply_camera(origin,axis,9020,stale_view,shot,&authored),
			"scripted-load recenter resets tracking and prior camera ownership atomically");
		for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
			require(close(axis[row][col],authored.axis[row][col]),"scripted-load reset cannot retain the previous backward free-look anchor");
	}
	{
		reset();configure_target(true);set_enabled(true);set_world_scale(40);
		publish_tracking_pose({{0,1.6f,0},identity});
		auto policy=vr::game_view::camera_profiles::authored_yaw_roll;policy.translation=vr::game_view::head_translation::fixed;
		const vr::game_view::camera_request shot{policy,920,921,920};
		vr::game_view::scripted_rotation_reference authored{921,full_axis(70,45,30)};
		const auto expected=full_axis(0,45,30);float native_view[]{70,180,80};
		for(int stage=0;stage<2;++stage)
		{
			authored.axis=full_axis(stage?-40.f:70.f,45,30);
			native_axis(180,axis);origin[0]=100;origin[1]=200;origin[2]=300;
			require(apply_camera(origin,axis,9100+stage*10,native_view,shot,&authored),"yaw/roll camera composes through the real head bridge");
			for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
				require(close(axis[row][col],expected[row][col]),"native pitch is absent both on entry and during later animation");
			require(close(origin[0],100)&&close(origin[1],200)&&close(origin[2],300),"pitch exclusion retains exact native rope position");
		}
	}
	{
		reset();configure_target(true);set_enabled(true);set_world_scale(40);
		publish_tracking_pose({{0,1.6f,0},identity});
		publish_tracking_pose({{0,1.6f,0},tracking_yaw(90)});
		float remote_angles[]{60,25,0};const float remote_delta[]{0,0,0};
		const vr::game_view::camera_request request{vr::game_view::camera_profiles::remote_control,42};
		require(apply_game_view(remote_angles,remote_delta,42,true) && close(remote_angles[0],60) && close(remote_angles[1],25),
			"remote input bridge seeds without changing native initial aim");
		publish_tracking_pose({{0,1.6f,0},tracking_yaw(120)});
		require(apply_game_view(remote_angles,remote_delta,42,true) && close(remote_angles[1],55),
			"remote input bridge passes relative head yaw through native command angles");
		record_game_command(9200,0,false);
		const auto expected=full_axis(-60,55,0);std::memcpy(axis,expected.data(),sizeof(axis));
		origin[0]=100;origin[1]=200;origin[2]=300;
		require(apply_camera(origin,axis,9200,remote_angles,request) && close(origin[2],300),"remote render bridge retains the native camera position");
		for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
			require(close(axis[row][col],expected[row][col]),"native UAV pitch survives and consumed head yaw is not doubled");
		float restored[]{0,11,0};
		require(apply_game_view(restored,remote_delta) && close(restored[1],11),
			"UAV exit keeps native restored player facing instead of inheriting the missile heading");
	}
	reset();
}
