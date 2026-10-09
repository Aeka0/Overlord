#include "std_include.hpp"
#include "component/vr/pose_filter.hpp"
#include "component/vr/stabilization.hpp"
#include "component/vr/controller_input.hpp"
#include "component/vr/gameplay/hands/position_offset.hpp"
#include "component/vr/desktop_stabilization.hpp"
#include <limits>

namespace
{
	using namespace vr::pose_filter;
	void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
	bool close_float(float a,float b,float tolerance=1e-5f){return std::abs(a-b)<tolerance;}
	bool same(const pose& a,const pose& b,float tolerance=1e-5f)
	{
		if(length(sub(a.position,b.position))>tolerance)return false;
		for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)if(!close_float(a.orientation[i][j],b.orientation[i][j],tolerance))return false;
		return true;
	}
	pose yaw(float degrees,vec position={}) {const float angle=degrees*radians*.5f;return {position,rotation({0,std::sin(angle),0,std::cos(angle)})};}
	void filters()
	{
		const auto begin=clock::now();
		for(float degrees:{0.f,90.f,179.9f,-179.9f,180.f,-90.f})
		{const auto p=yaw(degrees);require(same(p,{{},rotation(quaternion(p.orientation))}),"matrix/quaternion rotation roundtrip");}
		const quat q=quaternion(yaw(179).orientation);quat negative=q;for(auto& x:negative)x=-x;
		require(angle(q,slerp(q,negative,.5f))<.001f,"quaternion sign must not cause a spin");
		for(int hz:{72,90,120,144})
		{
			filter f;float raw_energy=0,filtered_energy=0;
			for(unsigned i=0;i<500;++i)
			{
				pose raw;raw.position[0]=i%2?.001f:-.001f;
				const auto out=f.update(raw,i+1,1,begin+std::chrono::microseconds(i*1000000/hz),80,hand);
				if(i>20){raw_energy+=raw.position[0]*raw.position[0];filtered_energy+=out.position[0]*out.position[0];}
			}
			require(filtered_energy<raw_energy*.7f,"stationary hand noise must attenuate at common headset rates");
		}
		filter f;const auto zero=yaw(0);
		f.update(zero,1,1,begin,100,head);
		const auto moving=yaw(20,{.05f,0,0});
		const auto out=f.update(moving,2,1,begin+std::chrono::milliseconds(11),100,head);
		require(length(sub(out.position,moving.position))<=.00201f && angle(quaternion(out.orientation),quaternion(moving.orientation))<=.25f*radians+.001f,
			"head correction must stay within position/angle caps during quick movement");
		require(same(out,f.update(moving,2,1,begin+std::chrono::milliseconds(20),100,head),0.000001f),"duplicate sequence cannot integrate again");
		require(same(zero,f.update(zero,3,1,begin+std::chrono::milliseconds(22),0,head),0.000001f),"disabled filter is exact bypass");
		require(same(moving,f.update(moving,4,1,begin+std::chrono::milliseconds(33),100,head)),"enable seeds instead of pulling stale history");
		require(same(zero,f.update(zero,5,2,begin+std::chrono::milliseconds(44),100,head)),"recenter resets");
		require(same(moving,f.update(moving,6,2,begin+std::chrono::seconds(1),100,head)),"long sample gap resets");
		require(same(zero,f.update(zero,7,2,begin,100,head)),"time reversal resets");
		pose invalid;invalid.position[0]=std::numeric_limits<float>::quiet_NaN();
		f.update(invalid,8,2,begin,100,head);
		require(same(moving,f.update(moving,9,2,begin+std::chrono::milliseconds(10),100,head)),"invalid input cannot poison later samples");
		filter wrapped;wrapped.update(yaw(179),1,1,begin,100,hand);
		const auto across=wrapped.update(yaw(-179),2,1,begin+std::chrono::milliseconds(11),100,hand);
		require(angle(quaternion(across.orientation),quaternion(yaw(180).orientation))<3*radians,"crossing +/-180 uses the short arc");
	}
	vr::stabilization::configuration controls;
	vr::stabilization::configuration read_controls() noexcept {return controls;}
	void hands()
	{
		using namespace vr;
		stabilization::settings_provider.store(read_controls);controls.hand={true,100};
		controller_input::invalidate();controller_input::set_gameplay_active(true);
		controller_input::frame input{};input.focused=true;input.reference_generation=77;input.sequence=1;input.sampled_at=clock::now();
		const pose offset=yaw(10,{.1f,.02f,0});
		for(unsigned h=0;h<2;++h){input.grip[h]={true,{{},identity}};input.aim[h]={true,{offset.position,offset.orientation}};}
		controller_input::publish(input);
		const auto original=controller_input::latest();
		input.sequence=2;input.sampled_at+=std::chrono::milliseconds(11);input.trigger[0]={true,true,1,1,0};
		const auto grip=yaw(.2f,{.002f,0,0});const auto aim=compose(grip,offset);
		for(unsigned h=0;h<2;++h){input.grip[h].tracking={grip.position,grip.orientation};input.aim[h].tracking={aim.position,aim.orientation};}
		controller_input::publish(input);const auto result=controller_input::latest();
		const pose filtered{result.grip[0].tracking.position_meters,result.grip[0].tracking.orientation};
		const pose filtered_aim{result.aim[0].tracking.position_meters,result.aim[0].tracking.orientation};
		require(same(compose(inverse(filtered),filtered_aim),offset),"grip/aim rigid relation must survive smoothing");
		require(filtered.position[0]<grip.position[0],"production publication actually filters the hand");
		require(result.trigger[0].presses==1 && result.trigger[0].down && result.sequence==input.sequence,"pose filter must not change buttons or sequence");
		require(result.runtime_grip[0].tracking.position_meters==grip.position && result.runtime_aim[0].tracking.position_meters==aim.position,"raw diagnostic witnesses preserved");
		// Calibration is part of the same filtered rigid grip pose. An unfiltered
		// diagnostic pose must not inject a second angular or translational lag.
		for(int h=0;h<2;++h)
		for(bool head_filter:{false,true})
		{
			head_pose_bridge::spatial_frame body{};body.reference={{},identity};
			body.world_yaw_axis=identity;body.units_per_meter=100;
			body.head_stabilized=head_filter;body.tracking_correction=yaw(12,{.01f,0,-.02f});
			using gameplay::hands::position_offsets;using gameplay::hands::anchor;using gameplay::hands::tracked_wrist;
			const position_offsets alignment{.08f,-.06f,.02f};
			const position_offsets pivot{};
			anchor first_aligned{},first_physical{},next_aligned{},next_physical{};
			require(tracked_wrist(original,body,{},h,alignment,first_aligned) && tracked_wrist(original,body,{},h,pivot,first_physical) &&
				tracked_wrist(result,body,{},h,alignment,next_aligned) && tracked_wrist(result,body,{},h,pivot,next_physical),
				"published poses reach the shared wrist adapter with either stabilization root");
			head_pose_bridge::world_pose mapped_grip;
			require(head_pose_bridge::tracking_to_world(body,result.grip[h].tracking,mapped_grip),"filtered grip maps through the selected head root");
			gameplay::hands::vec expected;
			require(gameplay::hands::offset_wrist({},mapped_grip.axis,body.units_per_meter,h,alignment,expected) &&
				length(sub(sub(next_aligned.position,next_physical.position),expected))<.00002f,
				"calibration follows the filtered grip with no raw-pose mixing");
			auto missing=result;missing.runtime_grip[h].valid=false;const auto retained=next_aligned;
			require(tracked_wrist(missing,body,{},h,alignment,next_aligned) && next_aligned.position==retained.position,
				"visual wrist does not depend on an unused raw diagnostic witness");
			require(!tracked_wrist(missing,body,{},h,alignment,next_aligned,true) && next_aligned.position==retained.position &&
				next_aligned.rotation==retained.rotation,"raw mechanical wrist rejects its missing grip transactionally");
		}
		controller_input::publish(input);require(controller_input::latest().grip[0].tracking.position_meters==result.grip[0].tracking.position_meters,"producer duplicate does not advance hand filter");
		controls.hand.enabled=false;++input.sequence;input.sampled_at+=std::chrono::milliseconds(11);controller_input::publish(input);
		require(controller_input::latest().grip[0].tracking.position_meters==input.grip[0].tracking.position_meters,"disabled publication passes through");
		require(controller_input::latest().continuity_generation!=result.continuity_generation && controller_input::latest().trigger[0].presses==1,
			"toggle resets movement history without synthesizing button events");
		controls.hand.enabled=true;controller_input::set_gameplay_active(false);controller_input::set_gameplay_active(true);
		++input.sequence;input.sampled_at+=std::chrono::milliseconds(11);controller_input::publish(input);
		require(controller_input::latest().grip[0].tracking.position_meters==input.grip[0].tracking.position_meters,"pause/resume must seed, not trail");
	}
	void tracking_root()
	{
		// The same virtual root must map camera and controllers. Test the bridge
		// with simultaneous physical translation + rotation, not just identity.
		using namespace vr;
		const auto raw=yaw(23,{.3f,1.7f,-.2f}),filtered=yaw(22.8f,{.299f,1.7f,-.2f});
		const auto root=correction(raw,filtered);
		require(same(compose(root,raw),filtered),"virtual root reproduces desired head pose");
		head_pose_bridge::spatial_frame frame{};frame.reference={{},identity};frame.world_yaw_axis=identity;frame.units_per_meter=1;
		frame.tracking_correction=root;frame.head_stabilized=true;
		const auto controller=yaw(-12,{.6f,1.2f,-.7f});
		head_pose_bridge::world_pose actual{},expected{};
		require(head_pose_bridge::tracking_to_world(frame,{controller.position,controller.orientation},actual),"bridge accepts stabilized frame");
		frame.head_stabilized=false;const auto corrected=compose(root,controller);
		require(head_pose_bridge::tracking_to_world(frame,{corrected.position,corrected.orientation},expected),"bridge reference conversion");
		require(same({actual.position,actual.axis},{expected.position,expected.axis}),"hand/world bridge uses the same root as the camera");
		head_pose_bridge::reset();head_pose_bridge::configure_target(true);head_pose_bridge::set_enabled(true);
		head_pose_bridge::set_world_scale(40);controls.head={true,100};stabilization::set_gameplay(true);
		const auto begin=clock::now();
		head_pose_bridge::publish_tracking_pose({{0,1.7f,0},identity},1,begin);
		const auto turned_head=yaw(.2f,{.001f,1.7f,0});
		head_pose_bridge::publish_tracking_pose({turned_head.position,turned_head.orientation},2,begin+std::chrono::milliseconds(11));
		float origin[3]{100,200,300};float axes[3][3]{{1,0,0},{0,1,0},{0,0,1}};
		require(head_pose_bridge::apply_camera(origin,axes) && head_pose_bridge::get_spatial_frame(frame) && frame.head_stabilized,"head publication reaches applied camera frame");
		head_pose_bridge::world_pose matched_head;
		require(head_pose_bridge::tracking_to_world(frame,{turned_head.position,turned_head.orientation},matched_head),"raw head can be transformed through the shared virtual root");
		require(length(sub(matched_head.position,frame.head_position))<.0001f,"rendered camera and corrected tracking coordinates must coincide");
		for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)require(close_float(matched_head.axis[r][c],axes[r][c]),"camera and corrected tracking orientations must coincide");
		const auto retained=frame;
		head_pose_bridge::publish_tracking_pose({{.002f,1.7f,0},identity},3,begin+std::chrono::milliseconds(22));
		require(head_pose_bridge::get_spatial_frame(frame) && frame.tracking_correction.position==retained.tracking_correction.position,"new raw sample cannot mutate an already captured scene root");
		controls.head.enabled=false;
		head_pose_bridge::publish_tracking_pose({{.002f,1.7f,0},identity},4,begin+std::chrono::milliseconds(33));
		float next_origin[3]{100,200,300};float next_axes[3][3]{{1,0,0},{0,1,0},{0,0,1}};
		require(head_pose_bridge::apply_camera(next_origin,next_axes) && head_pose_bridge::get_spatial_frame(frame) && !frame.head_stabilized,"disabled head restores original coordinate path");
		head_pose_bridge::reset();
	}
	void desktop_math()
	{
		using namespace vr;
		const engine_stereo_bridge::eye_projection projection{-1.15f,.93f,-.8f,1.2f};
		for(const auto size:{std::array<float,2>{1920,1080},{900,1600},{3440,1440}})
		{
			const auto base=desktop_mirror::project(projection,2528,2704,size[0],size[1],95);
			for(unsigned axis=0;axis<3;++axis)for(float degrees:{0.f,2.f,15.f,90.f,179.f})
			{
				quat turn{0,0,0,std::cos(degrees*radians*.5f)};turn[axis]=std::sin(degrees*radians*.5f);
				const auto target=transpose(rotation(turn));
				const auto view=desktop_mirror::stabilize(projection,base,2528,2704,identity,target);
				require(view.valid && desktop_mirror::contained(view.uv_transform,{.5f/2528,.5f/2704,1-.5f/2528,1-.5f/2704}),"all output corners stay forward and in source");
				require(close_float(std::tan(view.horizontal_fov*radians*.5f),std::tan(base.horizontal_fov*radians*.5f)*.85f),"stabilization reserves fixed margin without FOV breathing");
				if(view.correction_fraction==1 && degrees<3)
				{
					const auto centre=rotate(view.uv_transform,{.5f,.5f,1});
					const auto forward=target[0]; // In source F/L/U coordinates (source axes = identity).
					const float expected_u=(-forward[1]/forward[0]-projection.tan_left)/(projection.tan_right-projection.tan_left);
					const float expected_v=(projection.tan_up-forward[2]/forward[0])/(projection.tan_up-projection.tan_down);
					require(close_float(centre[0]/centre[2],expected_u) && close_float(centre[1]/centre[2],expected_v),"three-axis reprojection must point along the target camera, not its inverse");
				}
				const auto guide=desktop_mirror::stabilize(projection,base,2528,2704,identity,target,base);
				require(guide.valid && desktop_mirror::contained(guide.uv_transform,base),"recording guide decoration never enters desktop sample");
			}
		}
	}
}
int main()
{
	try{filters();hands();tracking_root();desktop_math();std::cout<<"PASS: pose filters, controller publication, virtual tracking root and desktop reprojection\n";return 0;}
	catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
