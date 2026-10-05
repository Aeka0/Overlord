#pragma once
#include "component/vr/gameplay/ladder_geometry.hpp"
#include "component/vr/gameplay/climb_collision.hpp"
#include "component/vr/gameplay/hand_interaction/pose_plan.hpp"
#include "component/vr/gameplay/bar_hand_pose.hpp"
#include "ladder_training_geometry.hpp"

namespace ladder_tests
{
    template<class Check>void run(Check check)
    {
        using namespace vr::gameplay::ladders;
        const auto near=[](vec a,vec b){return length(sub(a,b))<.001f;};
        placement transformed{{10,20,30},{{{0,1,0},{-1,0,0},{0,0,1}}},.7f};
        check(near(transformed.local(transformed.world({2,4,6})),{2,4,6}),"scaled rotated oilrig rung round-trip uses model coordinates");
        check(horizontal_edge({0,-12,0},{0,12,0}) && !horizontal_edge({0,0,0},{0,0,24}),"visual horizontal rungs admitted, vertical rails excluded");
        check(!horizontal_edge({NAN,0,0},{0,24,0}),"malformed visual geometry rejected");
        const auto trainer=repeated_rungs(trainer_edges);
        check(trainer.size()==9,"captured trainer mesh resolves to its nine main rungs");
        check(!trainer.empty() && trainer.back().a[2]<-8 && std::all_of(trainer.begin(),trainer.end(),[](rung r){return r.a[0]<-11;}),
            "forward top rail is excluded from grasping and highest-rung selection");
        camera_translation camera;
        (void)camera.sample({},1,0,40);
        const auto eased=camera.sample({0,0,1},1,.011,40);
        check(eased[2]>0 && eased[2]<1,"rendered body translation eases a discrete accepted step without overshoot");
        check(near(camera.sample({0,0,1},1,.011,40),eased),"repeated centre-eye time cannot advance camera smoothing twice");
        const auto later=camera.sample({0,0,1},1,.022,40);
        check(later[2]>eased[2] && later[2]<1,"camera moves between simulation updates without a second physics move");
        check(near(camera.sample({4,5,6},2,.023,40),{4,5,6}),"reference change resets the camera instead of blending between spaces");
        const vec confirmed{1,2,3};
        check(near(preview_pull(confirmed,{vec{0,0,-.08f},vec{0,0,-.04f}},3,{{0,0,1},-.1f,.1f,true}),{1,2,3.06f}),
            "render camera consumes new hand motion between server updates without doubling two-hand travel");
        check(near(preview_pull(confirmed,{vec{0,0,.3f},vec{}},1,{{0,0,1},-.01f,.1f,true}),{1,2,2.99f}),
            "late camera descent cannot pass the server-tested floor limit");
        check(near(preview_pull(confirmed,{vec{0,0,-.3f},vec{}},1,{{0,0,1},-.1f,0,true}),confirmed),
            "late camera ascent cannot pass the server-tested ceiling");
        check(near(preview_pull({1,2,3.08f},{},1,{{0,0,1},-.1f,.1f,true}),
            preview_pull(confirmed,{vec{0,0,-.08f},vec{}},1,{{0,0,1},-.1f,.1f,true})),
            "server acknowledgement rebases the preview rather than applying the same pull twice");
        check(top_pull_meters>0 && top_pull_meters<=.1f,"top rung requires a short deliberate extra pull");
        const auto diagonal=unit(vec{1,1,0});
        check(near(preview_pull({}, {vec{-.1f,-.1f,-1},vec{}},1,{diagonal,-.1f,.05f,true}),scale(diagonal,.05f)),
            "lateral preview remains inside the exact swept segment and excludes untested orthogonal travel");
        vr::gameplay::hands::bar_grip::binding bar{{0,0,0,1},{2,1,1},true};
        for(unsigned h=0;h<2;++h)
        {
            const auto wrist=vr::gameplay::hands::bar_grip::on_bar(bar,{10,20,30},{1,0,0},h);
            check(near(add(wrist.position,rotate(wrist.rotation,bar.centre_in_wrist)),{10,20,30}),
                "native posed palm centre coincides with rung centre for either hand");
            const auto basis=from_axis({{{0,1,0},{-1,0,0},{0,0,1}}});
            const anchor tracked{wrist.position,normalize(multiply(wrist.rotation,conjugate(basis)))};
            check(near(bar_grip::centre(bar,tracked,basis),{10,20,30}),
                "grasp admission reconstructs the displayed palm including the native wrist basis");
            check(std::abs(dot(rotate(wrist.rotation,{0,1,0}),{0,0,1}))<.001f,
                "knuckle line follows the horizontal rung rather than standing vertically");
        }
        check(near(closest({{0,-12,3},{0,12,3}},{0,4,3}),{0,4,3}),"grasp remains at the hand's lateral point, not the rung centre");
        pull_solver pull;pull.attach(0,{0,0,1},{1,0,0},{0,0,0,1});pull.attach(1,{0,0,1},{1,0,0},{0,0,0,1});
        std::array<vr::gameplay::free_climb::hand,2> hands{};
        for(auto& h:hands)h={{0,0,0},{0,0,1},{0,0,0,1},true,true,{0,0,0}};
        (void)pull.update({},1,.016f,hands);
        for(auto& h:hands)h.motion[2]=-.1f;
        check(near(pull.update({},1,.016f,hands),{0,0,.1f}),"two grips average physical pull instead of doubling movement");
        hands[0].motion[2]=-.3f;
        const auto mixed=pull.update({0,0,.1f},1,.016f,hands);
        pull.release(1);pull.topology_changed(mixed);
        check(near(pull.update(mixed,1,.016f,hands),mixed),"releasing one hand cannot expose its partner's unaveraged historical goal as a body jump");
        pull.attach(1,{0,0,1},{1,0,0},{0,0,0,1});pull.topology_changed(mixed);
        check(near(pull.update(mixed,1,.016f,hands),mixed),"adding a second grip cannot pull the body backwards toward a new baseline");
        pull.release(0);check(pull.held()==2,"one released grip retains the other support");
        pull.release(1);check(pull.held()==0,"last Grip release has no ice-pick extraction threshold");
        pull.attach(0,{0,0,1},{1,0,0},{0,0,0,1});pull.rebase();hands[0].motion={};(void)pull.update({},2,.016f,hands);
        hands[0].motion[2]=.2f;const auto wanted=pull.update({},2,.016f,hands);
        check(wanted[2]<-.19f,"raising attached controller requests descent");
        hands[0].motion=add(hands[0].motion,vec{.1f,-.05f,0});
        const auto lateral=pull.update({},2,.016f,hands);
        check(std::abs(lateral[0]+.1f)<.001f && std::abs(lateral[1]-.05f)<.001f,
            "attached hand motion retains both horizontal axes for lateral climbing");
        const auto floor=[](vec a,vec b){vr::gameplay::free_climb::sweep_hit h;if(b[2]<0){h.fraction=a[2]/(a[2]-b[2]);h.normal={0,0,1};}return h;};
        const auto stopped=vr::gameplay::free_climb::slide({0,0,.05f},{0,0,-.5f},.001f,floor);
        check(stopped.blocked && stopped.position[2]>=0,"downward body sweep stops above the floor instead of penetrating it");
        pull.obstructed(stopped.position);
        check(near(pull.update(stopped.position,2,.016f,hands),stopped.position),"floor block removes accumulated downward pull before release or regrasp");
        exit_route route{{0,0,1},{1,0,1},true};
        check(clear_route({},route,[](vec,vec){return vr::gameplay::free_climb::sweep_hit{};}),"clear lift and forward path admit top exit");
        check(!clear_route({},route,[](vec a,vec b){return vr::gameplay::free_climb::sweep_hit{b[0]>a[0]?.2f:1.f,{1,0,0}};}),"wall on forward top path prevents exit");
        check(!clear_route({},route,[](vec,vec){return vr::gameplay::free_climb::sweep_hit{0,{},true,true};}),"embedded body cannot exit through geometry");
        check(!clear_route({},route,[](vec,vec){return vr::gameplay::free_climb::sweep_hit{NAN};}),"invalid collision result cannot authorize top-out");
        namespace hi=vr::gameplay::hand_interaction;
        hi::session session{1,1,1,1,vr::hand::left,{{hi::domain::ladder,{42,1},0,0},hi::role::part,hi::button::grip,hi::recipe::single,hi::capability::action}};
        const auto plan=hi::compose_pose(vr::hand::left,{&session,1});
        check(plan.driver.provider==hi::domain::ladder && !plan.melee,"ladder grip owns hand pose and excludes empty-fist melee");
        hi::arbiter arbiter;arbiter.begin(1,1);
        arbiter.offer({vr::hand::left,session.held,1,5,0,1,true,false});
        arbiter.offer({vr::hand::right,session.held,1,5,0,1,true,false});
        arbiter.resolve([](const auto&){return true;});
        check(arbiter.decisions()[0].result==hi::rejection::none && arbiter.decisions()[1].result==hi::rejection::none,
            "both hands may grasp different points of the same physical rung");
    }
}
