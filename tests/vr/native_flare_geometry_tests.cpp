#include "component/vr/native_flare_geometry.hpp"
#include "component/vr/native_flare_projection.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>

int main()
{
	using namespace vr::native_flare_geometry;
	int failures{};
	const auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
	const auto close = [](double a, double b) { return std::abs(a-b) < .004; };
	const auto project = [](const vector& point, const std::array<float,64>& m, unsigned offset) {
		std::array<double,4> result{};
		for (unsigned c=0;c<4;++c) { result[c]=m[offset+12+c]; for(unsigned r=0;r<3;++r) result[c]+=double(point[r])*m[offset+r*4+c]; }
		return result;
	};
	for (const vector origin : {vector{0,0,0},vector{-15000,24000,-16000},vector{75,-30,40}})
	for (const auto axis : {std::array<vector,3>{{{0,0,1},{-1,0,0},{0,-1,0}}},
		std::array<vector,3>{{{1,0,0},{0,0,1},{0,-1,0}}},std::array<vector,3>{{{0,1,0},{-1,0,0},{0,0,1}}}})
	{
		camera c{origin,axis,.6f,.48f};
		vector light{};
		for(unsigned j=0;j<3;++j) light[j]=origin[j]+250*axis[0][j]-20*axis[1][j]+15*axis[2][j];
		quad q{};
		const std::array<std::array<float,2>,4> corners{{{-.04f,-.03f},{.04f,-.03f},{.04f,.03f},{-.04f,.03f}}};
		for(unsigned i=0;i<4;++i)
		{
			q[i].position={20/(250*c.tan_x)+corners[i][0],15/(250*c.tan_y)+corners[i][1],1};
			for(unsigned j=0;j<36;++j) q[i].attributes[j]=std::byte((i*36+j)%256);
		}
		const auto native=q;
		check(to_world(c,light,q),"valid source lifts to world");
		for(unsigned j=0;j<3;++j)
			check(close((double(q[0].position[j])+q[1].position[j]+q[2].position[j]+q[3].position[j])/4,light[j]),"source center at translated/rotated camera");
		for(unsigned i=0;i<4;++i) check(q[i].attributes==native[i].attributes,"all non-position bytes unchanged");
		for(unsigned i=0;i<4;++i)
		{
			vector delta{}; for(unsigned j=0;j<3;++j) delta[j]=q[i].position[j]-origin[j];
			const auto depth=dot(delta,axis[0]);
			check(close(-dot(delta,axis[1])/(depth*c.tan_x),native[i].position[0]) &&
				close(dot(delta,axis[2])/(depth*c.tan_y),native[i].position[1]),"native silhouette round trip");
		}
	}
	for(float depth : {40.f,250.f,10000.f})
	{
		std::array<double,2> centered_x{};
		for(unsigned eye=0;eye<2;++eye)
		{
			const vector origin{-15000 + (eye ? 1.25f : -1.25f),24000,-16000};
			std::array<float,64> relative{};
			for(unsigned i=0;i<4;++i) relative[i*5]=1;
			relative[16]=.9f; relative[21]=.84f; relative[24]=eye ? -.24f : .24f;
			relative[25]=.19f; relative[27]=1; relative[30]=4;
			std::copy_n(relative.begin()+16,16,relative.begin()+32);
			relative[48]=1/.9f; relative[53]=1/.84f;
			relative[59]=.25f; relative[60]=-relative[24]/.9f;
			relative[61]=-.19f/.84f; relative[62]=1;
			std::array<float,64> full{};
			check(absolute_matrices(relative,origin,full),"absolute projection constructed");
			const vector light{-14990,24012,-16000+depth};
			vector delta{}; for(unsigned j=0;j<3;++j) delta[j]=light[j]-origin[j];
			const auto expected=project(delta,relative,32), actual=project(light,full,32);
			for(unsigned j=0;j<4;++j) check(close(expected[j],actual[j]),"origin applied once");
			centered_x[eye]=actual[0]/actual[3]-relative[24];
			std::array<double,4> restored{};
			for(unsigned col=0;col<4;++col) for(unsigned row=0;row<4;++row) restored[col]+=actual[row]*full[48+row*4+col];
			for(unsigned j=0;j<3;++j) check(std::abs(restored[j]/restored[3]-light[j])<.02,"inverse VP coherent");
		}
		check(close(centered_x[0]-centered_x[1],.9*2.5/depth),"disparity scales with depth independently of optical-center shift");
	}
	{
		camera c{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}},.6f,.48f};
		quad q{}; for(auto& v:q) v.position={.2f,.3f,1};
		const auto unchanged=q;
		check(!to_world(c,{-1,0,0},q) && std::memcmp(&q,&unchanged,sizeof(q))==0,"behind-eye failure atomic");
		c.tan_x=0; check(!to_world(c,{10,0,0},q),"zero FOV rejected");
		c.tan_x=std::numeric_limits<float>::quiet_NaN(); check(!to_world(c,{10,0,0},q),"NaN FOV rejected");
		c.tan_x=.6f; c.axis[1]=c.axis[0]; check(!to_world(c,{10,0,0},q),"degenerate basis rejected");
		c.axis[1]={0,1,0}; q[3].position[0]=std::numeric_limits<float>::infinity();
		const auto bad=q; check(!to_world(c,{10,0,0},q) && std::memcmp(&q,&bad,sizeof(q))==0,"late vertex failure atomic");
		std::array<float,64> input{}, output{}; output.fill(7); const auto old=output;
		input[40]=std::numeric_limits<float>::quiet_NaN();
		check(!absolute_matrices(input,{0,0,0},output) && output==old,"bad matrix preserves output");
	}
	{
		std::array<std::byte,0x3400> state{}; state.fill(std::byte{0x31});
		std::array<float,64> saved{}, changed{}; saved.fill(1); changed.fill(2);
		std::memcpy(state.data()+0x2BF0,saved.data(),sizeof(saved));
		state[0x3340]=std::byte{0x15};
		const auto before=state;
		const auto version=[&](std::size_t offset) { std::uint16_t v{}; std::memcpy(&v,state.data()+offset,2); return v; };
		{
			vr::native_flare::projection_scope scope(state.data(),saved,changed,0x15);
			check(std::memcmp(state.data()+0x2BF0,changed.data(),sizeof(changed))==0 && state[0x3340]==std::byte{0x14},"scoped eye matrix and depth-hack override");
			check(version(0x31EC)==0x3132 && version(0x31FA)==0x3131,"VP invalidated, WORLD0 stamp preserved");
			// Model a native lazy matrix rebuild and another flag update.
			state[0x200]=std::byte{0x55}; state[0x3340]=std::byte{0x34};
		}
		check(std::memcmp(state.data()+0x2BF0,saved.data(),sizeof(saved))==0,"original matrices restored");
		check(state[0x3340]==std::byte{0x35} && state[0x200]==std::byte{0x55},"only owned depth bit restored, cache not rewound");
		check(version(0x31EC)==0x3133 && version(0x31FA)==0x3131,"restoration advances versions instead of reusing stale GPU stamps");
		for(std::size_t i=0;i<state.size();++i)
		{
			bool owned=(i>=0x2BF0 && i<0x2CF0) || i==0x200 || i==0x3340;
			for(auto offset:{0x31E8u,0x31EAu,0x31ECu,0x31F6u,0x31FCu,0x31FEu}) owned=owned || i==offset || i==offset+1;
			if(!owned) check(state[i]==before[i],"unrelated source bytes unchanged");
		}
	}
	std::cout << "native flare geometry failures=" << failures << '\n';
	return failures ? 1 : 0;
}
