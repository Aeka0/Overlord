#pragma once
#include "component/vr/gameplay/free_climb.hpp"
#include "component/vr/gameplay/cliffhanger_physical.hpp"
#include "component/vr/gameplay/cliffhanger_surface.hpp"
#include "component/vr/gameplay/climb_checkpoint.hpp"
#include "component/vr/gameplay/climb_collision.hpp"
#include "component/vr/gameplay/climb_exit.hpp"

namespace free_climb_tests
{
    template<class Check>void run(Check check)
    {
        using namespace vr::gameplay::free_climb;
        check(reached_exit({.8f,.2f,2.f},{0,0,1.f}),"passing above and beside the authored exit cannot miss handoff");
        check(!reached_exit({0,0,0},{0,0,1.f}) && !reached_exit({2,0,2},{0,0,1.f}),
            "exit still requires ledge height and horizontal proximity");
        for(const auto name:{"ice","snow","rock","plaster"})
            check(vr::gameplay::cliffhanger_physical::native_climb_material(name),"native cliff climb material admitted");
        for(const auto name:{"none","water","glass","metal",""})
            check(!vr::gameplay::cliffhanger_physical::native_climb_material(name),"unreviewed surface remains non-climbable");
        const contact wall{{0,0,0},{1,0,0},true};
        const auto pair=[](vec a,vec b){return std::array<vr::gameplay::free_climb::hand,2>{{{a,a,{0,0,0,1},true,true,a},{b,b,{0,0,0,1},true,true,b}}};};
        check(fixed(true,true,true,false) && fixed(true,false,true,true) && !fixed(true,false,true,false),"Trigger OR Grip owns fixation");
        check(fixed(false,false,true,false),"missing button state preserves support");
        solver awaiting;
        auto entry_result=awaiting.update({0,0,1},1,.05f,pair({.1f,0,0},{0,0,0}),{});
        check(awaiting.held()==0 && !entry_result.falling && entry_result.goal==vec{0,0,1},
            "missing authored seed holds entry instead of inventing an anchor or replaying the native body");
        entry_result=awaiting.update({0,0,1},1,.05f,pair({.01f,0,0},{0,0,0}),{{wall,{}}});
        check(entry_result.attached==1 && awaiting.held()==1,"unseeded entry accepts the first real VR contact");
        solver c;c.seed(1,wall,{0,0,0,1});
        c.update({},1,.05f,pair({.1f,0,0},{0,0,0}),{});
        auto r=c.update({},1,.05f,pair({.1f,0,0},{0,0,-.2f}),{});
        check(std::abs(r.goal[2]-.2f)<.00001f,"hand motion defines an absolute body target without native arm reach");
        const auto limited=bounded_step({},r.goal,.05f);
        check(std::abs(limited[2]-.125f)<.00001f,"movement speed is limited separately from pull accounting");
        r=c.update(limited,1,.05f,pair({.1f,0,0},{0,0,-.2f}),{});
        check(std::abs(r.goal[2]-.2f)<.00001f && std::abs(bounded_step(limited,r.goal,.05f)[2]-.2f)<.00001f,
            "stationary hands finish unapplied pull instead of permanently losing climb distance");
        c.obstructed(limited);r=c.update(limited,1,.05f,pair({.1f,0,0},{0,0,-.2f}),{});
        check(r.goal==limited,"actual collision discards blocked tension instead of storing a launch impulse");
        auto release=pair({.1f,0,0},{0,0,-.2f});release[1].fixed=false;
        r=c.update(limited,1,.05f,release,{});check(!r.detached,"button release does not drop the tool");
        release[1].motion[0]=.015f;r=c.update(limited,1,.05f,release,{});
        check(!r.detached && std::abs(c.hands[1].withdrawal-.015f)<.00001f,"withdrawal has continuous visible progress");
        release[1].motion[0]=.03f;r=c.update(limited,1,.05f,release,{});
        check(r.detached==2 && r.falling,"short deliberate rear travel releases the last support");

        solver two;two.seed(0,wall,{0,0,0,1});two.seed(1,wall,{0,0,0,1});
        two.update({},1,.05f,pair({0,0,0},{0,0,0}),{});
        r=two.update({},1,.05f,pair({0,0,-.1f},{0,0,-.1f}),{});
        check(std::abs(r.goal[2]-.1f)<.00001f,"two supports do not double body displacement");
        two.rebase();r=two.update({0,0,.1f},2,.05f,pair({1,1,1},{1,1,1}),{});
        check(r.goal==vec{0,0,.1f} && two.held()==3,"recenter rebases raw motion without releasing world anchors");

        solver contact_test;contact_test.seed(1,wall,{0,0,0,1});
        contact_test.update({},1,.05f,pair({.1f,0,0},{0,0,0}),{});
        r=contact_test.update({},1,.05f,pair({.01f,0,0},{0,0,0}),{{wall,{}}});
        check(r.attached==1 && contact_test.held()==3,"native contact plus a held button acquires an arbitrary ice point");
        auto off=pair({.01f,0,0},{0,0,0});off[0].fixed=false;
        contact_test.update({},1,.05f,off,{});off[0].motion[0]=.05f;
        r=contact_test.update({},1,.05f,off,{});check(r.detached==1 && !r.falling,"other support survives a release");
        off[0]={vec{.01f,0,0},vec{.01f,0,0},{0,0,0,1},true,true,vec{-.04f,0,0}};
        r=contact_test.update({},1,.05f,off,{{wall,{}}});
        check(r.attached==1,"the same surface can be struck repeatedly without a route history");

        solver passive;auto a=pair({.1f,0,0},{0,0,0});passive.update({},1,.05f,a,{});
        a[0].tip={0,0,0};r=passive.update({},1,.05f,a,{{wall,{}}});
        check(!r.attached,"carrier travel alone cannot manufacture a physical swing");
        auto unpressed=pair({.01f,0,0},{0,0,0});unpressed[0].fixed=false;
        r=passive.update({},1,.05f,unpressed,{{wall,{}}});check(!r.attached,"unpressed contact is not fixation");
        auto jumped=pair({-2,0,0},{0,0,0});r=passive.update({},1,.05f,jumped,{{wall,{}}});
        check(!r.attached,"tracking teleport cannot become a long surface sweep and attach");
        solver push;push.seed(0,wall,{0,0,0,1});auto push_hand=pair({0,0,0},{0,0,0});push_hand[0].fixed=false;
        push.update({},1,.05f,push_hand,{});push_hand[0].motion={-.1f,0,0};
        r=push.update({},1,.05f,push_hand,{});
        check(r.goal[0]>.09f,"pushing a released planted hand into the surface moves the body outward");
        push_hand[0].motion={-.07f,0,0};r=push.update({},1,.05f,push_hand,{});
        check(r.detached==1,"withdrawal starts at the last contact position, not before an inward push");
        check(length(sub(outside_surface({-.2f,0,0},wall,.07f),vec{.07f,0,0}))<.00001f,"hand clearance uses actual wall normal");
        solver saved_climb;saved_climb.seed(0,{{12,-746,8},{1,0,0},true},{0,0,0,1});
        saved_climb.hands[0].goal={100,200,300};saved_climb.hands[0].withdrawal=.02f;
        saved_climb.hands[0].sampled=saved_climb.hands[0].released=true;
        const auto saved=checkpoint(saved_climb,40.f);solver restored;
        check(restore(restored,saved,80.f) && restored.held()==1 &&
            restored.hands[0].surface.point==vec{6,-373,4},"checkpoint preserves world support across scale/reference changes");
        auto fresh=pair({4,5,6},{7,8,9});fresh[0].fixed=false;
        const auto after_load=restored.update({2,3,4},99,.05f,fresh,{});
        check(!after_load.detached && !after_load.falling && after_load.goal==vec{2,3,4} && restored.hands[0].withdrawal==0,
            "load discards old pull debt and release gestures while retaining the ice support");
        auto corrupt=saved;corrupt[0].normal={0,0,0};
        check(!restore(restored,corrupt,40) && restored.held()==1,"corrupt checkpoint is rejected without replacing a valid state");
        volume compact;
        check(hanging_volume({{0,0,35},{15,15,35}},40,compact) && compact.half[0]<15 && compact.half[2]<35 &&
            std::abs(compact.midpoint[2]+compact.half[2]-70)<.0001f,"climbing tucks lower body while preserving native head clearance");
        check(!hanging_volume({{0,0,35},{15,15,-1}},40,compact),"invalid collision shape is never passed to the engine");
        const auto plane=[](vec n) {return [=](vec from,vec to) {
            const float a=dot(from,n),b=dot(to,n);
            if(a<-.0001f)return sweep_hit{0,n,true,true};
            if(b>=0)return sweep_hit{};
            return sweep_hit{std::clamp(a/(a-b),0.f,1.f),n,false,false};
        };};
        const auto side=slide({1,0,0},{-2,0,2},.001f,plane({1,0,0}));
        check(side.blocked && !side.stuck && side.position[0]>=0 && side.position[2]>1.99f,
            "wall contact retains upward motion instead of cancelling the entire pull");
        const auto overhang_normal=unit(vec{1,0,-1});
        const auto overhang=slide({.2f,0,0},{0,0,1},.001f,plane(overhang_normal));
        check(overhang.blocked && !overhang.stuck && overhang.position[0]>.2f && overhang.position[2]>.2f &&
            dot(overhang.position,overhang_normal)>=-.0001f,"overhang slides the body outward and upward without crossing the surface");
        const auto slope=slide(overhang.position,{0,0,1},.001f,plane(unit(vec{1,0,1})));
        check(!slope.blocked && slope.position[2]>overhang.position[2]+.99f,"motion can continue above the convex transition");
        const auto ceiling=slide({0,0,-.2f},{0,0,1},.001f,plane({0,0,-1}));
        check(ceiling.blocked && ceiling.position[2]<=0,"sliding never disables a real ceiling collision");
        const auto trapped=slide({-1,0,0},{1,0,1},.001f,plane({1,0,0}));
        check(trapped.stuck && trapped.position==vec{-1,0,0},"startsolid does not teleport through geometry");
        using namespace vr::gameplay::cliffhanger_physical;
        const presentation entry{phase::authored_entry,42},climbing{phase::climbing,42,43},exit{phase::authored_exit,42};
        check(entry.authored() && exit.authored() && climbing.active() && !climbing.authored(),"native presentation is limited to story handoffs");
    }
}
