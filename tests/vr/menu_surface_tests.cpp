#include "component/vr/menu_surface.hpp"
#include "component/native_ui_pointer.hpp"
#include "component/vr/native_menu_stack.hpp"
#include "component/vr/ui_canvas.hpp"
#include "component/vr/menu_acceptance.hpp"
#include "component/vr/movie_presentation.hpp"
#include "component/vr/hud_prompts.hpp"
#include "component/vr/menu_input_trace.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace vr::menu_surface;
	void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
	bool near(float a,float b){return std::abs(a-b)<.0001f;}
	void acceptance_tests()
	{
		require(movie_theater(true,true,false,true,true,1,true,false),"in-game native briefing popup failed to enter movie theater");
		require(!movie_theater(true,true,true,false,true,1,false,true),"ordinary frontend background stole movie routing");
		require(!movie_theater(true,false,false,true,true,1,true,true),"ended briefing retained movie route");
		require(movie_theater(true,true,false,false,false,0,false,false),"pre-scene loading video lost theater presentation");
		require(!movie_theater(true,true,false,true,true,1,false,false),"Gulag world video was mistaken for fullscreen playback");
		require(!movie_theater(true,true,false,true,false,0,true,false),"stale briefing ownership retained theater over a world video");
		require(movie_theater(true,true,false,true,false,0,false,true),"native fullscreen cinematic lost presentation over an initialized map");
		require(!movie_theater(true,false,false,true,false,0,false,true),"fullscreen configuration alone invented active video");
		require(!native_video_playing(false,true,true,2,1)&&!native_video_playing(false,false,true,2,1),
			"pending native stop/start was treated as active playback");
		require(native_video_playing(true,false,true,2,1)&&native_video_playing(false,true,true,2,2)&&
			native_video_playing(false,true,false,2,1),"native started/named playback predicates changed");
		require(accepts_without_pointer(true,true,1)&&accepts_without_pointer(false,true,0)&&
			!accepts_without_pointer(false,true,1)&&!accepts_without_pointer(false,false,0),
			"background movies stole menu rays or movies/entry lost confirmation");
		using namespace vr::controller_input;acceptance accept;frame f;
		f.sequence=1;f.focused=true;f.reference_generation=1;f.continuity_generation=1;f.sampled_at=clock::now();
		for(auto& b:f.trigger){b.active=true;b.generation=1;}
		const auto sample=[&](std::uint64_t context=1){++f.sequence;return accept.held(f,context,true,f.sampled_at);};
		f.trigger[1].down=true;require(!sample(),"trigger held through entry skipped immediately");
		f.trigger[1].down=false;require(!sample(),"neutral accepted");
		f.trigger[1].down=true;require(sample(),"fresh trigger cannot confirm without pose/ray");
		f.sampled_at+=std::chrono::milliseconds(1600);require(sample(),"native skip hold was reduced to a tap");
		f.trigger[0].down=true;require(sample(),"second trigger interrupted native hold");
		f.trigger[1].down=false;require(sample(),"one hand released other hand's hold");
		require(!sample(2),"held trigger leaked into next screen");
		f.trigger[0].down=false;require(!sample(2),"release accepted next screen");
		f.trigger[0].down=true;require(sample(2),"left trigger cannot confirm");
		f.focused=false;require(!sample(2),"focus loss retained native Enter");f.focused=true;
		require(!sample(2),"wake replayed held trigger");
		f.trigger[0].down=false;sample(2);f.trigger[0].down=true;require(sample(2),"wake did not rearm after neutral");
		require(!accept.held(f,2,true,f.sampled_at+std::chrono::milliseconds(151)),"stale sampling retained native Enter");
		using namespace vr::hud_prompts;using game_text::locale;
		require(replace(source::lui,"@PLATFORM_HOLD_TO_SKIP_KEYBOARD",{true,{}},locale::english)=="Hold ^3Trigger^7 or ^3Enter^7 to skip","native briefing key not translated");
		require(!replace(source::lui,"PLATFORM_HOLD_TO_SKIP_KEYBOARD",{false,{}},locale::english)&&
			!replace(source::lui,"MENU_SP_OFFENSIVE_SKIP_NOW",{true,{}},locale::english),"VR text override escaped its native scope");
	}
	void input_trace_tests()
	{
		using namespace vr::native_menu;input_trace trace;input_sample row;
		for(std::uint64_t t=1;t<120000;++t){row.tick=t;trace.record(row,true,true);}
		require(trace.startup.count==301&&trace.recovery.count==0,"startup trace was unbounded or restarted during normal input");
		row.tick=120000;trace.record(row,true,false);row.tick=120501;trace.record(row,true,true);
		require(trace.recovery.start==120501&&trace.recovery.count==1&&trace.startup.count==301,"wake failed to preserve startup separately");
		row.tick=121000;trace.record(row,true,false);row.tick=121600;trace.record(row,true,true);
		require(trace.recovery.start==120501,"wake jitter erased the recovery window");
	}
	void native_pointer_tests()
	{
		input::native_ui_pointer pointer;
		auto event=pointer.native_position(900,500);require(!event.activity&&event.forward,"first native sample is a baseline");
		event=pointer.native_position(900,500);require(!event.activity&&event.forward,"flat native polling was suppressed");
		pointer.acquire();
		vr::controller_input::frame f;f.focused=true;f.sequence=1;f.continuity_generation=1;f.sampled_at=vr::controller_input::clock::now();
		f.trigger[1].active=true;f.trigger[1].generation=1;navigation nav;
		nav.consume(f,1,true,1,true,f.sampled_at);
		for(unsigned i=0;i<2700;++i)
		{
			// Reproduce thirty seconds of 90-Hz native polling from the trace.
			event=pointer.native_position(900,500);
			require(!event.activity&&!event.forward,"stationary native poll stole VR ownership or overwrote the ray");
			++f.sequence;f.sampled_at+=std::chrono::milliseconds(11);
			f.trigger[1].down=i==30;
			if(i==30)++f.trigger[1].presses;
			const auto buttons=nav.consume(f,1,true,1,true,f.sampled_at);
			require(buttons.click==(i==30),"passive native polling blocked or repeated trigger selection");
		}
		event=pointer.native_position(901,500);require(event.activity&&event.forward,"real desktop motion did not reclaim pointer");
		event=pointer.native_position(901,500);require(!event.activity&&event.forward,"stationary desktop polling became new intent");
		pointer.acquire();event=pointer.native_position(901,500);require(!event.activity&&!event.forward,"VR reacquisition changed the independent physical baseline");
		pointer.release();event=pointer.native_position(901,500);require(!event.activity&&event.forward,"UI exit/focus loss left native input suppressed");
		pointer.acquire();const auto desktop=pointer.native_button();
		require(desktop.restore&&desktop.x==901&&desktop.y==500,"stationary physical click/wheel used the last VR ray instead of desktop coordinates");
		require(!pointer.native_button().restore&&pointer.native_position(901,500).forward,"physical click did not retire VR ownership");
		input::native_ui_pointer early;early.acquire();event=early.native_position(-2000000000,2000000000);
		require(!event.activity&&!event.forward,"first native sample after VR entry stole ownership");
		event=early.native_position(2000000000,-2000000000);require(event.activity&&event.forward,"extreme desktop coordinates overflowed intent detection");
	}
	void geometry_tests()
	{
		vr::head_pose_bridge::tracking_pose head{{1,1.6f,2},{{{1,0,0},{0,1,0},{0,0,1}}}};
		const auto a=anchored(head);require(a.valid,"horizontal anchor rejected");
		for(bool theater:{false,true})for(float aspect:{.5f,1.f,16.f/9,32.f/9})
		{
			const auto g=layout(a,theater,aspect,0);
			require(near(g.distance,theater?3.f:.5f),"fixed active distance");
			require(near(g.width/g.height,aspect),"native aspect changed");
			if(!theater)require(near(g.width,.9f),"in-game canvas is too small for native LUI");
			if(theater)require(near(g.width/g.radius,pi*.5f),"theater angular span changed");
			const auto parent=layout(a,theater,aspect,1);
			require(near(parent.width,g.width)&&near(parent.distance-g.distance,.25f),"ancestor motion changed physical size");
		}
		const auto invalid=std::numeric_limits<float>::quiet_NaN();
		auto vertical=head;vertical.orientation={{{1,0,0},{0,0,-1},{0,1,0}}};
		require(anchored(vertical).valid&&near(anchored(vertical).forward[2],-1),"vertical-pitch heading lost");
		vertical.position_meters[0]=invalid;require(!anchored(vertical).valid,"NaN head pose admitted");
		require(layout(a,false,0,0).width==0,"zero canvas admitted");
		require(layout(a,false,1,maximum_menus).width==0,"unbounded stack admitted");
		float parent=2;
		for(int cycle=0;cycle<1000;++cycle)
		{
			for(int step=0;step<20;++step)parent=approach_depth(parent,2.25f,.01f);
			require(near(parent,2.25f),"parent did not retreat");
			parent=approach_depth(parent,2.5f,.02f); // Interrupted by a pop before completion.
			for(int step=0;step<30;++step)parent=approach_depth(parent,2,.01f);
			require(near(parent,2),"stack depth accumulated drift");
		}
	}
	void input_tests()
	{
		using namespace vr::controller_input;
		navigation nav;frame f;const auto start=clock::now();f.focused=true;f.continuity_generation=1;
		f.move_active=true;f.trigger[1].active=f.primary[1].active=f.secondary[1].active=true;
		auto step=[&](int ms,std::uint64_t target=1,bool allowed=true,bool hit=true)
		{++f.sequence;f.sampled_at=start+std::chrono::milliseconds(ms);return nav.consume(f,target,allowed,1,hit,f.sampled_at);};
		f.trigger[1].down=true;f.trigger[1].presses=1;
		require(!step(0).click&&!step(10).click,"held trigger activated opening menu");
		f.trigger[1].down=false;step(20);++f.trigger[1].presses;f.trigger[1].down=true;
		require(step(30).click&&step(40).click_held,"valid click/drag missing");
		require(!step(50,2).click,"press leaked into new confirmation");
		f.trigger[1].down=false;step(60,2);++f.trigger[1].presses;
		require(step(70,2).click,"tap between samples lost");
		++f.trigger[1].presses;require(!step(80,2,true,false).click,"ray miss clicked ancestor");
		f.move={0,1};require(step(90,2).vertical==1,"stick initial navigation missing");
		require(step(200,2).vertical==0,"stick repeated without delay");
		require(step(450,2).vertical==1,"stick repeat missing");
		require(!step(460,3).vertical,"held stick leaked into child menu");
		f.move={0,0};step(470,3);f.move={-1,0};require(step(480,3).horizontal==-1,"horizontal navigation missing");
		step(490,3,false);require(!step(500,3).horizontal,"focus recovery failed neutral arming");
		f.move={0,0};step(510,3);++f.secondary[1].presses;
		require(step(520,3).back,"native Back tap missing");
		++f.continuity_generation;f.primary[1].down=true;++f.primary[1].presses;
		require(!step(530,3).confirm,"tracking discontinuity replayed confirm");
		f.primary[1].down=false;step(540,3);++f.primary[1].presses;
		require(step(550,3).confirm,"fresh confirm after recovery missing");
		++f.trigger[1].generation;f.trigger[1].down=true;++f.trigger[1].presses;
		require(!step(560,3).click,"binding generation replayed held trigger");
		f.trigger[1].down=false;step(570,3);++f.trigger[1].presses;require(step(580,3).click,"trigger did not rearm after binding change");
		require(!nav.consume(f,3,true,1,true,f.sampled_at+std::chrono::seconds(1)).confirm,"stale frame admitted");
	}
	void menu_button_tests()
	{
		using namespace vr::controller_input;
		menu_button button;frame f;f.focused=true;f.menu_recenter.active=true;f.menu_recenter.generation=1;
		const auto start=clock::now();
		auto sample=[&](int ms,std::uint64_t target=1,bool allowed=true)
		{++f.sequence;f.sampled_at=start+std::chrono::milliseconds(ms);return button.consume(f,target,allowed,f.sampled_at);};
		require(sample(0)==menu_action::none,"initial neutral sample toggled menu");
		f.menu_recenter.down=true;++f.menu_recenter.presses;
		require(sample(10)==menu_action::none,"tap fired before release");
		f.menu_recenter.down=false;require(sample(100)==menu_action::toggle,"short release did not pause");
		f.menu_recenter.down=true;++f.menu_recenter.presses;
		require(sample(110)==menu_action::none,"long press initially paused");
		for(int t=210;t<1110;t+=100)require(sample(t)==menu_action::none,"long gesture fired early");
		require(sample(1110)==menu_action::recenter,"one-second hold did not recenter");
		require(sample(1210)==menu_action::none,"held button repeated recenter");
		f.menu_recenter.down=false;require(sample(1220)==menu_action::none,"long release toggled pause");
		++f.menu_recenter.presses;require(sample(1230)==menu_action::toggle,"coalesced short tap was lost");
		f.menu_recenter.down=true;++f.menu_recenter.presses;sample(1240);
		require(sample(1250,2)==menu_action::none,"context change replayed hold");
		f.menu_recenter.down=false;require(sample(1260,2)==menu_action::none,"context change released into pause");
		f.menu_recenter.down=true;++f.menu_recenter.presses;sample(1270,2);
		require(sample(1280,2,false)==menu_action::none,"focus loss triggered action");
		require(sample(1290,2)==menu_action::none,"focus return accepted held button");
		f.menu_recenter.down=false;require(sample(1300,2)==menu_action::none,"focus return release paused");
		f.menu_recenter.down=true;++f.menu_recenter.presses;sample(1310,2);
		++f.menu_recenter.generation;require(sample(1320,2)==menu_action::none,"binding change replayed action");
		f.menu_recenter.down=false;require(sample(1330,2)==menu_action::none,"binding change release paused");
		f.menu_recenter.down=true;++f.menu_recenter.presses;sample(1340,2);
		require(sample(2000,2)==menu_action::none,"sampling hitch became a long press");
		f.menu_recenter.down=false;require(sample(2010,2)==menu_action::none,"hitch release paused");
	}
	void ownership_and_stack_tests()
	{
		using namespace vr::controller_input;
		pointer_owner owner;frame f;f.focused=true;f.reference_generation=f.continuity_generation=1;
		for(unsigned h=0;h<2;++h){f.runtime_aim[h].valid=true;f.trigger[h].active=true;f.trigger[h].generation=1;}
		auto step=[&](bool allowed=true){++f.sequence;f.sampled_at=clock::now();owner.update(f,allowed,f.sampled_at);return owner.hand();};
		require(step()==1,"pointer must default to right hand");
		for(int i=0;i<30;++i)require(step()==1,"idle hand switched without a trigger");
		f.runtime_aim[1].valid=false;require(step()==1,"tracking loss transferred ownership");f.runtime_aim[1].valid=true;step();
		f.trigger[0].down=true;++f.trigger[0].presses;require(step()==0,"left deliberate trigger did not acquire pointer");
		for(int i=0;i<10;++i)require(step()==0,"held trigger ping-ponged ownership");
		f.trigger[0].down=false;step();f.trigger[1].down=true;++f.trigger[1].presses;
		require(step()==1,"right deliberate trigger did not reacquire pointer");
		step(false);f.trigger[0].down=true;++f.trigger[0].presses;require(step()==1,"focus return transferred to a held trigger");
		f.trigger[0].down=f.trigger[1].down=false;step();
		for(auto& t:f.trigger){t.down=true;++t.presses;}require(step()==1,"simultaneous triggers changed owner");
		owner.reset();require(owner.hand()==1,"runtime restart did not reset default hand");
		using namespace vr::native_menu::stack;
		const std::array roles{classify(false),classify(false),classify(false),classify(true)};
		require(first_visible(std::span(roles).first(1),4)==0,"entry page itself disappeared");
		require(first_visible(std::span(roles).first(2),4)==1,"closed entry page leaked behind campaign menu");
		require(first_visible(roles,4)==2,"closed page history remained behind its replacement");
		require(first_visible(roles,2)==2,"visible menu capacity exceeded");
		const std::array pause{classify(false),classify(false),classify(true)};
		require(first_visible(pause,4)==1,"page replacement did not start a new presentation group");
		const std::array popups{classify(false),classify(true),classify(true)};
		require(first_visible(popups,4)==0,"nested popups lost their visible parents");
	}
	void canvas_tests()
	{
		for(const auto size:{std::array{1639u,1351u},std::array{1280u,720u},std::array{900u,1600u},std::array{3440u,1440u},std::array{3840u,2160u}})
		{
			const auto map=vr::ui_canvas::fit(size[0],size[1]);
			require(map.valid()&&map.target_width==1920&&map.target_height==1080,"VR canvas dimensions followed native viewport");
			require(near(map.width/map.height,float(size[0])/size[1]),"native menu aspect was stretched");
			for(float x:{0.f,.25f,.5f,.9f,1.f})for(float y:{0.f,.25f,.5f,.9f,1.f})
			{
				float u=(map.x+x*map.width)/map.target_width,v=(map.y+y*map.height)/map.target_height;
				require(map.source_uv(u,v)&&near(u,x)&&near(v,y),"VR hit did not round-trip to native coordinates");
			}
			float u=map.x>1?map.x*.5f/map.target_width:.5f,v=map.y>1?map.y*.5f/map.target_height:.5f;
			if(map.x>1||map.y>1)require(!map.source_uv(u,v),"transparent canvas padding accepted a click");
		}
		require(!vr::ui_canvas::fit(0,1080).valid()&&!vr::ui_canvas::fit(999999,1080).valid(),"invalid canvas extent admitted");
	}
}
int main()
{
	try{geometry_tests();input_tests();menu_button_tests();ownership_and_stack_tests();canvas_tests();acceptance_tests();input_trace_tests();native_pointer_tests();std::cout<<"vr-menu-surface-tests: PASS\n";return 0;}
	catch(const std::exception& e){std::cerr<<"vr-menu-surface-tests: FAIL: "<<e.what()<<'\n';return 1;}
}
