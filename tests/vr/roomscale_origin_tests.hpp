#pragma once
#include "component/vr/head_pose_bridge.hpp"
#include "component/vr/roomscale_origin.hpp"
#include "test_support.hpp"

inline void expect_roomscale_turn_origin()
{
	using namespace vr::head_pose_bridge;
	using vr::tests::require;
	constexpr matrix3 identity{{{1,0,0},{0,1,0},{0,0,1}}};
	const auto yaw=[](float degrees) {
		const float r=degrees*vr::pose_filter::radians,c=std::cos(r),s=std::sin(r);
		return matrix3{{{c,s,0},{-s,c,0},{0,0,1}}};
	};
	const auto world=[](const matrix3& basis,vector3 local) {
		vector3 out{};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)out[j]+=local[i]*basis[i][j];return out;
	};
	const auto close=[](vector3 a,vector3 b) {return vr::pose_filter::length(vr::pose_filter::sub(a,b))<.002f;};
	reset();configure_target(true);set_enabled(true);set_world_scale(40);
	publish_tracking_pose({{0,1.65f,0},identity});
	publish_tracking_pose({{-.25f,1.65f,-1},identity});
	const std::array<tracking_pose,2> controllers{{{{-.35f,1.3f,-1.4f},identity},{{.1f,1.25f,-1.3f},identity}}};
	const float delta[3]{};
	const auto render=[&](int time,float native_yaw,vector3 native_origin=vector3{100,200,300}) {
		float view_axis[3][3]{};const auto basis=yaw(native_yaw);std::memcpy(view_axis,basis.data(),sizeof(view_axis));
		float angles[3]{0,native_yaw,0};
		require(apply_camera(native_origin.data(),view_axis,time,angles),"roomscale turn reaches the real camera bridge");
		spatial_frame frame;require(get_spatial_frame(frame),"roomscale turn publishes one shared spatial frame");return frame;
	};
	vector3 initial_head{},initial_body{};std::array<vector3,2> initial_hands{};
	int time=100;
	for(float angle:{35.f,65.f,125.f,-175.f,125.f})
	{
		float command[3]{0,angle,0};require(apply_game_view(command,delta),"off-centre physical head enters native turn command");
		record_game_command(time);
		const auto frame=render(time,angle);
		if(time==100){initial_head=frame.head_position;initial_body=vr::pose_filter::sub(frame.body.position,initial_head);}
		require(close(frame.head_position,initial_head),"accepted smooth/snap yaw does not orbit the head around the recenter point");
		const auto relative_basis=yaw(angle-35.f);
		for(unsigned hand=0;hand<2;++hand)
		{
			world_pose pose;require(tracking_to_world(frame,controllers[hand],pose),"either controller shares the corrected origin");
			const auto relative=vr::pose_filter::sub(pose.position,frame.head_position);
			if(time==100)initial_hands[hand]=relative;
			require(close(relative,world(relative_basis,initial_hands[hand])),"turn rotates both hands rigidly about the head");
		}
		require(frame.body.valid && close(vr::pose_filter::sub(frame.body.position,frame.head_position),world(relative_basis,initial_body)),
			"body equipment uses the same turn pivot without filter lag");
		require(close(render(time,angle).head_position,initial_head),"repeated camera passes cannot integrate the turn again");
		++time;
	}
	// A native prediction may visit an input command that skipped rendering.
	// Its reconstruction must not replace the most recent forward origin.
	float skipped[3]{0,125,0};require(apply_game_view(skipped,delta),"unrendered command retains valid tracking");
	record_game_command(105);record_game_command(106);
	require(close(render(106,125).head_position,initial_head) && close(render(105,130).head_position,initial_head),
		"a known previously unrendered prediction preserves the current head pivot");
	require(close(render(106,125).head_position,initial_head),"older reconstruction cannot become the forward origin baseline");
	// Walking after a turn follows the new virtual heading. Rendering an old
	// prediction and then the current one must not consume that motion twice.
	publish_tracking_pose({{-.25f,1.65f,-1.2f},identity});
	const auto moved=vr::pose_filter::add(initial_head,world(yaw(125),{8,0,0}));
	require(close(render(106,125).head_position,moved),"physical forward motion after turning uses the new world basis");
	require(close(render(105,130).head_position,vr::pose_filter::add(initial_head,world(yaw(130),{8,0,0}))),
		"reconstructed old prediction keeps its own basis during later physical movement");
	require(close(render(100,35).head_position,vr::pose_filter::add(initial_head,world(yaw(35),{8,0,0}))),
		"older prediction reuses its own captured turn origin");
	require(close(render(106,125).head_position,moved),"prediction replay cannot accumulate origin displacement");
	float requested[3]{0,170,0};require(apply_game_view(requested,delta),"native-constrained turn fixture packs requested input");
	record_game_command(107);
	const vector3 locomotion{6,-4,2};
	const auto translated=render(107,130,vr::pose_filter::add(vector3{100,200,300},locomotion));
	require(close(translated.head_position,vr::pose_filter::add(moved,locomotion)),
		"pivot compensation follows native accepted yaw while retaining native locomotion");
	const auto generation=translated.generation;
	request_recenter();publish_tracking_pose({{-.25f,1.65f,-1.2f},identity});
	float recentered[3]{0,130,0};require(apply_game_view(recentered,delta),"recenter restores native command ownership");record_game_command(108);
	const auto centered=render(108,130);
	require(centered.generation>generation && close(centered.head_position,{100,200,300}),"recenter discards the previous pivot offset exactly once");
	const vr::game_view::camera_request scripted{vr::game_view::camera_profiles::vehicle,77,88};
	float story_origin[3]{500,600,700},story_axis[3][3]{};const auto story_basis=yaw(20);std::memcpy(story_axis,story_basis.data(),sizeof(story_axis));
	float story_angles[3]{0,20,0};
	require(apply_camera(story_origin,story_axis,109,story_angles,scripted) && close({story_origin[0],story_origin[1],story_origin[2]},{500,600,700}),
		"authored vehicle origin does not inherit ordinary turn compensation");
	reset();
	vr::game_view::roomscale_origin history;vector3 offset{};
	for(int command=0;command<70;++command)
	{
		history.record(command);require(history.offset(command,1,yaw(float(command)),{1,0,0},offset),"turn history stays bounded during continuous input");
	}
	const auto retained=offset;
	require(!history.offset(0,1,yaw(0),{1,0,0},offset) && offset==retained,"evicted prediction cannot poison the live turn origin");
	history.record(10);
	require(history.offset(10,1,yaw(90),{1,0,0},offset) && offset==vector3{},"command timeline rollback clears stale map compensation");
}
