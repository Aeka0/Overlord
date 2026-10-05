#pragma once
#include "component/vr/directional_ui.hpp"
#include <limits>

namespace directional_ui_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::directional_ui;
		rectangle crop{};
		check(marker_crop({253,100,747,210},crop) && crop.x==249 && crop.right==751,
			"font-measured full pickup prompt fits atlas with shadow padding");
		check(!marker_crop({0,0,505,100},crop),"atlas admission includes outer pixel padding");
		check(!marker_crop({0,0,100,std::numeric_limits<float>::quiet_NaN()},crop) &&
			!marker_crop({100,0,0,100},crop),"invalid measured marker bounds rejected");
		check(marker_crop({-.25f,-.75f,502.1f,246.1f},crop) && crop.right-crop.x==512 && crop.bottom-crop.y==256,
			"fractional marker bounds round outward at exact atlas capacity");
		check(!marker_crop({-.25f,-.75f,503.1f,247.1f},crop),"fractional crop exceeding atlas by one pixel rejected");
		const std::array<float,12> camera{0,0,0,1,0,0,0,1,0,0,0,1};
		waypoint marker{}; marker.world={1000,-250,100}; marker.viewport={1600,1000}; marker.tangent={1,.625f};
		marker.anchor={1000,420}; marker.crop={980,400,1020,440};
		quad world{};
		check(world_quad(marker,camera,40,world),"world waypoint projects using native camera tangent");
		vec3 center{};
		for (const auto& p:world) for (unsigned i=0;i<3;++i) center[i]+=p[i]*.25f;
		check(std::abs(center[0]-1000)<.001f && std::abs(center[1]+250)<.001f && std::abs(center[2]-100)<.001f,
			"native screen anchor is exactly the actual world point, not a subtitle panel position");
		matrix vp{0,0,0,1,-1,0,0,0,0,1,0,0,0,0,0,0};
		projected_quad left{},right{};
		check(project(world,{0,1.3f,0},vp,4.8f,left) && project(world,{0,-1.3f,0},vp,4.8f,right) &&
			left[0][0]/left[0][3]>right[0][0]/right[0][3],"waypoint binocular disparity follows target depth");
		const auto near_disparity=left[0][0]/left[0][3]-right[0][0]/right[0][3];
		marker.world={2000,-500,200}; world_quad(marker,camera,40,world);
		project(world,{0,1.3f,0},vp,4.8f,left); project(world,{0,-1.3f,0},vp,4.8f,right);
		check(std::abs(near_disparity-2*(left[0][0]/left[0][3]-right[0][0]/right[0][3]))<.00001f,
			"distant marker halves disparity without changing angular icon size");
		marker.pixels_per_meter=200;
		{
			// Source ink can be relocated inside the native viewport without
			// moving the world HUD, including items projected below that viewport.
			auto ground=marker;ground.world={40,0,-50};ground.anchor={819,1418};ground.viewport={1638,1307};
			ground.crop={719,1388,919,1448};quad before{},after{};
			check(world_quad(ground,camera,40,before),"ground prompt has a valid world plane despite offscreen native projection");
			ground.anchor[1]=653.5f;ground.crop.y-=764.5f;ground.crop.bottom-=764.5f;
			check(world_quad(ground,camera,40,after) && before==after,"safe source raster position preserves all world vertices");
		}
		world_quad(marker,camera,40,world);const auto physical_width=std::abs(world[1][1]-world[0][1]);
		marker.world={1000,-250,100};world_quad(marker,camera,40,world);
		check(std::abs(physical_width-8)<.001f && std::abs(std::abs(world[1][1]-world[0][1])-physical_width)<.001f,
			"world interaction text retains physical size while target distance changes");
		marker.pixels_per_meter=std::numeric_limits<float>::infinity();
		check(!world_quad(marker,camera,40,world),"invalid authored text scale rejected");marker.pixels_per_meter=0;
		{
			for (float viewport_width:{800.f,1600.f,3200.f}) for (float depth:{20.f,40.f,80.f,120.f})
			{
				auto prompt=marker;prompt.world={depth,0,0};prompt.viewport={viewport_width,viewport_width*.625f};
				prompt.anchor={viewport_width*.5f,prompt.viewport[1]*.5f};
				prompt.crop={prompt.anchor[0]-220,prompt.anchor[1]-48,prompt.anchor[0]+220,prompt.anchor[1]+70};
				check(set_fixed_visual_size(prompt,440.f/.48f,2.f) && world_quad(prompt,camera,40,world),
					"fixed visual pickup size uses shared world projection across distance and viewport changes");
				project(world,{0,1.3f,0},vp,4.8f,left);project(world,{0,-1.3f,0},vp,4.8f,right);
				const auto width=[](const projected_quad& q) {return std::abs(q[1][0]/q[1][3]-q[0][0]/q[0][3]);};
				check(std::abs(width(left)-.24f)<.00001f && std::abs(width(right)-.24f)<.00001f,
					"pickup icon/text angular width remains fixed in each eye from half a metre to three metres");
				check(std::abs((left[0][0]/left[0][3]-right[0][0]/right[0][3])*depth-2.6f)<.0001f,
					"fixed visual size still preserves target-depth binocular disparity");
			}
			check(!set_fixed_visual_size(marker,0,2) && !set_fixed_visual_size(marker,200,0) &&
				!set_fixed_visual_size(marker,200,std::numeric_limits<float>::infinity()),
				"invalid fixed visual calibration cannot create oversized or nonfinite quads");
		}
		marker.world={-1000,0,0}; check(!world_quad(marker,camera,40,world),"behind-head world marker never flips through eye plane");
		marker.clamped=true; marker.crop={1500,460,1540,500};
		check(world_quad(marker,camera,40,world) && world[0][0]==80 && world[0][1]<0,
			"native rear-target edge arrow remains on right of head-relative canvas");
		const std::array<float,12> turned{0,0,0,0,1,0,-1,0,0,0,0,1};
		check(world_quad(marker,turned,40,world) && world[0][1]==80 && world[0][0]>0,
			"edge canvas follows actual head direction after ninety-degree turn");
		marker.viewport[0]=0; check(!world_quad(marker,camera,40,world),"zero viewport cannot divide during transition");
		marker.viewport[0]=1600; marker.tangent[0]=std::numeric_limits<float>::quiet_NaN();
		check(!world_quad(marker,camera,40,world),"corrupt projection fails closed");
		std::array<std::byte,104> command{};
		const auto put=[&](unsigned offset,const auto& value) { std::memcpy(command.data()+offset,&value,sizeof(value)); };
		// Captured ACR pickup icon: StretchPic uses opcode 10 and 56 bytes.
		// +52 is unused padding; treating it as a rotation can reject the group.
		put(0,std::uint16_t{56});put(2,std::uint8_t{10});
		put(16,771.f);put(20,605.5f);put(24,96.f);put(28,48.f);
		put(52,std::numeric_limits<float>::quiet_NaN());rectangle icon{};
		check(quad_command(command.data(),56) && quad_bounds(command.data(),56,icon) &&
			icon.x==771 && icon.y==605.5f && icon.right==867 && icon.bottom==653.5f,
			"native pickup StretchPic command is accepted without interpreting padding as rotation");
		check(!quad_command(command.data(),55),"malformed StretchPic command size rejected");
		put(24,-96.f);
		check(quad_bounds(command.data(),56,icon) && icon.x==675 && icon.right==771,"mirrored native icon bounds stay ordered");
		put(0,std::uint16_t{56}); put(2,std::uint8_t{12});
		put(16,100.f); put(20,200.f); put(24,40.f); put(28,20.f); put(52,90.f);
		rectangle box{};
		check(quad_bounds(command.data(),56,box) && std::abs(box.x-110)<.001f && std::abs(box.y-190)<.001f &&
			std::abs(box.right-130)<.001f && std::abs(box.bottom-230)<.001f,"rotated rectangle bounds include rotated arrow corners");
		check(!quad_bounds(command.data(),72,box),"native opcode and size must both match");
		put(0,std::uint16_t{72}); put(2,std::uint8_t{16});
		for (unsigned i=0;i<4;++i) { put(16+i*8,(i&1) ? 40.f : 10.f); put(20+i*8,(i&2) ? 80.f : 20.f); }
		check(quad_bounds(command.data(),72,box) && box.x==10 && box.right==40 && box.y==20 && box.bottom==80,
			"legacy warning quad uses eight-byte XY stride");
		put(24,std::numeric_limits<float>::infinity()); check(!quad_bounds(command.data(),72,box),"nonfinite native vertices rejected");
		check(warning_material("hit_direction_stun") && warning_material("hud_grenadethrowback") &&
			!warning_material("hud_grenadepointer_unrelated") && !warning_material("white"),"indicator selection uses exact audited native materials");
	}
}
