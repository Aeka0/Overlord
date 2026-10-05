#include "component/vr/gameplay/weapon_inventory_snapshot.hpp"
#include "component/vr/gameplay/knife_profile.hpp"
#include "component/vr/gameplay/weapon_clip_projection.hpp"
#include "component/vr/gameplay/official_cheats_policy.hpp"
#include "component/vr/gameplay/launcher_cheats_policy.hpp"
#include "component/vr/gameplay/special_melee.hpp"
#include <any>
#include <iostream>
#include <map>

namespace w=vr::gameplay::weapons;
namespace snap=w::inventory_snapshot;
using vr::hand;
struct archive
{
    std::map<std::string,std::any> data;bool loading{};
    template<class T> void operator()(const char* key,T& value)
    {
        if constexpr(std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T,std::string>)
        {if(loading)value=std::any_cast<T>(data.at(key));else data[key]=value;}
        else {archive child;if(loading)child=std::any_cast<archive>(data.at(key));child.loading=loading;snap::fields(child,value);if(!loading)data[key]=child;}
    }
};
int main()
{
    int failures{};const auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<"FAIL: "<<why<<'\n';}};
    {
        namespace l=vr::gameplay::cheats::launcher;
        const unsigned native=0x80|l::notarget;
        auto mode=l::apply_flags(native,0,0,false);
        check(mode.flags==native && !mode.owned,"disabled defaults preserve all native flags");
        mode=l::apply_flags(mode.flags,mode.owned,1,true);
        check(mode.flags==(native|l::demigod) && mode.owned==l::demigod,"demigod owns only new bits, not native notarget");
        const auto repeated=l::apply_flags(mode.flags,mode.owned,1,true);
        check(repeated.flags==mode.flags && repeated.owned==mode.owned,"repeated frames never toggle enabled cheats off");
        mode=l::apply_flags(mode.flags,mode.owned,2,false);
        check(mode.flags==(native|l::god) && mode.owned==l::god,"switching to god removes launcher demigod and preserves native state");
        mode=l::apply_flags(mode.flags,mode.owned,0,false);
        check(mode.flags==native && !mode.owned,"saved ownership permits disabling after checkpoint restoration");
        mode=l::apply_flags(0,0,2,true);
        mode=l::apply_flags(mode.flags,mode.owned,0,false);
        check(!mode.flags && !mode.owned,"both launcher protections fully disable");
    }
    {
        namespace c=vr::gameplay::cheats;
        check(c::green_beret_weapon("cliffhanger")=="h2_cheatpickaxe" && c::green_beret_weapon("gulag")=="h2_cheatcommandoknife",
            "native Cliffhanger pickaxe choice is distinct from the ordinary bayonet");
        check(c::body_weapon_definition("h2_cheatpickaxe") && c::body_weapon_definition("h2_cheatcommandoknife") &&
            !c::body_weapon_definition("ice_picker") && !c::body_weapon_definition("ice_picker_bigjump"),
            "cheat body equipment never takes ownership of the scripted climbing weapons");
        check(!w::special_melee::uses_ammunition(1,"h2_cheatpickaxe") && !w::special_melee::supported("h2_cheatpickaxe"),
            "pickaxe has no ammunition and is not admitted as a generic carried blade");
    }
    for(auto type:{w::mechanics::feed_type::closed_bolt,w::mechanics::feed_type::open_bolt,w::mechanics::feed_type::manual_bolt})
    {
        w::mechanics::rules r{5};r.feed=type;
        w::mechanics::state s{1,1,1,false,false,0,20};check(w::mechanics::from_native_automatic(r,1,s),"one-round feed import");
        const auto before=w::mechanics::native_ammo(s);
        for(int n=0;n<8;++n)
        {
            w::mechanics::request q{w::mechanics::operation::accepted_shot,1,1,s.revision,hand::right,hand::right};q.sustained=true;
            const auto tx=w::mechanics::plan(r,s,q);check(bool(tx)&&tx.before==before&&tx.after==before&&!tx.rounds_spent,"sustained magazine shot conserves clip and reserve");
            if(!tx)break;s=tx.next;check(w::mechanics::ready(r,s),"manual/automatic feed remains ready after sustained shot");
            check(!w::mechanics::plan(r,s,q),"same shot revision cannot replay");
        }
        w::mechanics::request q{w::mechanics::operation::accepted_shot,1,1,s.revision,hand::right,hand::right};
        const auto normal=w::mechanics::plan(r,s,q);check(bool(normal)&&normal.rounds_spent==1&&normal.after.loaded==0,"disabling sustain resumes normal debit");
    }
    {
        w::cylinder::rules r{6};auto s=w::cylinder::import_native(r,2,1,{1,0});
        w::cylinder::request q{w::cylinder::operation::accepted_shot,2,1,s.revision,hand::right,hand::right};q.sustained=true;
        auto tx=w::cylinder::plan(r,s,q);check(bool(tx)&&tx.next.live==1&&tx.next.spent==5&&!tx.rounds_spent,"sustained cylinder retains live round and existing cases");
    }
    for(auto drive:{w::tube::action_drive::automatic,w::tube::action_drive::pump,w::tube::action_drive::lever})
    {
        w::tube::rules r{4,drive};w::tube::state s{3,1,1,0,0,0,true};
        w::tube::request q{w::tube::operation::accepted_shot,3,1,1,hand::right,hand::right};q.sustained=true;
        const auto tx=w::tube::plan(r,s,q);check(bool(tx)&&tx.next.chamber&&!tx.next.spent_case&&w::tube::ready(r,tx.next),"sustained pump/lever stays chambered without cycling");
        s.chamber=false;check(!w::tube::plan(r,s,q),"sustain cannot fire an initially empty chamber");
    }
    {
        w::break_action::rules r{2};auto s=w::break_action::import_native(r,4,1,{1,0});
        w::break_action::request q{w::break_action::operation::accepted_shot,4,1,1,hand::right,hand::right};q.sustained=true;
        const auto tx=w::break_action::plan(r,s,q);check(bool(tx)&&tx.next.live==s.live&&tx.next.spent==s.spent,"sustained hinged shot preserves chamber masks");
    }
    for(auto kind:{w::underbarrel::kind::m203,w::underbarrel::kind::gp25,w::underbarrel::kind::shotgun})
    {
        auto s=w::underbarrel::import_native({{5,1},6,kind},1,0);
        for(int i=0;i<4;++i)
        {const auto tx=w::underbarrel::plan(s,{w::underbarrel::operation::shot,s.id,s.revision,hand::right,hand::left,true,false,true});
            check(bool(tx)&&tx.after==tx.before&&!tx.spent&&w::underbarrel::ready(tx.next),"sustained secondary shots retain loaded readiness");if(!tx)break;s=tx.next;}
    }
    {
        snap::snapshot s;check(snap::shape_valid(s),"empty inventory is a valid saved state");s.count=4;
        for(int i=0;i<s.count;++i){auto& v=s.weapons[i];v.name="same_weapon";v.location=i<2?w::carry::location::held:w::carry::location::overflow;v.rear=i<2?hand(i):hand::none;v.overflow=i<2?0:i-1;v.loaded=i+1;}
        s.selected=1;check(snap::shape_valid(s),"duplicates, dual hands and overflow survive shape validation");
        auto invalid=s;invalid.weapons[1].rear=hand::left;check(!snap::shape_valid(invalid),"duplicate hand ownership rejected");
        invalid=s;invalid.weapons[3].location=w::carry::location::right_waist;invalid.weapons[2].location=w::carry::location::right_waist;check(!snap::shape_valid(invalid),"duplicate occupied slots rejected");
        snap::weapon original=s.weapons[2];original.kind=snap::feed::magazine;original.magazine={7,99,101,true,false,3,44};
        original.magazine.bolt={1,.4f,false,true,true,false};original.has_module=true;original.module.id.type=w::underbarrel::kind::m203;original.module.open=true;
        archive a;snap::fields(a,original);a.loading=true;snap::weapon restored;snap::fields(a,restored);
        check(restored.name==original.name&&restored.location==original.location&&restored.overflow==original.overflow&&restored.magazine.bolt.feeding&&restored.module.open,"stable feed and topology fields round-trip");
        check(!restored.magazine.weapon&&!restored.magazine.instance_generation&&!restored.magazine.revision&&!restored.module.id.host,"runtime identities are not persisted");
        std::array<w::carry::instance,3> entries{};
        for(unsigned i=0;i<entries.size();++i){auto& e=entries[i];e.id={9,i+1};e.at=i==0?w::carry::location::right_waist:w::carry::location::overflow;e.policy.waist=true;e.overflow_order=i;e.owner.pose_rear=hand::right;}
        w::carry::inventory inventory;check(inventory.restore(entries)&&!inventory.in_hand(hand::left)&&!inventory.in_hand(hand::right)&&!inventory.in_slot(w::carry::location::left_waist),"explicit restore preserves empty hands and empty holsters");
        check(inventory.next_back()&&inventory.next_back()->id==entries[1].id,"overflow draw order retained");
    }
    {
        namespace projection=w::native_ammunition::projection;namespace storage=w::native_ammunition::storage;
        w::clip_ledger clips;std::array<std::byte,storage::extent> bytes{};bytes[0]=std::byte{0x5a};
        std::array<projection::restored_clip,2> saved{{{9,3,50,11,21,9},{9,7,50,11,21,9}}};
        std::array<w::weapon_identity,2> ids{};
        check(projection::restore(clips,bytes,saved,ids)&&ids[0]!=ids[1]&&clips.count(9)==2,"restore creates independent same-model instances");
        check(clips.find(ids[0])->loaded==3&&clips.find(ids[1])->loaded==7&&storage::observe(bytes,11,21).reserve.count==50,"independent clips share one restored reserve pool");
        check(projection::commit(clips,bytes,ids[1],11,21,7,50,7,vr::gameplay::cheats::launcher::reserve_rounds) &&
            clips.find(ids[0])->loaded==3 && clips.find(ids[1])->loaded==7 && storage::observe(bytes,11,21).reserve.count==500,
            "500 reserve grant preserves projected and nonprojected duplicate clips");
        check(projection::select(clips,bytes,ids[1],11,21)&&storage::observe(bytes,11,21).clip.count==7,"restored secondary instance projects its own clip");
        const auto before=bytes;const auto prior=ids;saved[1].reserve=51;
        check(!projection::restore(clips,bytes,saved,ids)&&before==bytes&&clips.find(prior[0])&&clips.find(prior[1]),"contradictory shared reserves reject before any native/ledger mutation");
        check(bytes[0]==std::byte{0x5a},"restore never changes non-ammunition player fields");
    }
    {
        namespace kp=vr::gameplay::equipment::knife_profile;using namespace vr::gameplay::hands;using namespace pose_math;
        for(bool right:{false,true})for(auto pose:{vr::gameplay::equipment::knife_hand_pose::grip,vr::gameplay::equipment::knife_hand_pose::magazine,vr::gameplay::equipment::knife_hand_pose::slide})
        for(auto grip:{vr::gameplay::equipment::knife_grip::forward,vr::gameplay::equipment::knife_grip::reverse})
        {
            const auto mirror=normalize(quat{.1f,.2f,.3f,.9f});const auto common=kp::attachment(grip,mirror,right,pose),bayonet=kp::attachment(grip,mirror,right,pose,true);
            check(length(sub(compose(common,{kp::grip_center,{0,0,0,1}}).position,compose(bayonet,{kp::bayonet_grip_center,{0,0,0,1}}).position))<.0001f,"bayonet keeps common handle contact in both hands and all co-grasps");
            check(dot(rotate(common.rotation,{1,0,0}),rotate(bayonet.rotation,{0,0,1}))>.999f,"bayonet blade direction follows forward/reverse selection");
        }
    }
    {
        w::carry::inventory inventory;const w::carry::owned_instance initial{{7,1},{true,true}};
        check(inventory.reconcile_instances({&initial,1})&&inventory.equip(initial.id,hand::right)&&inventory.support(initial.id,hand::left),"dual grip carry fixture");
        const auto no_drop=[](const auto&){return false;};
        const auto support=inventory.release(initial.id,1,w::carry::location::right_waist,true,no_drop,true,false);
        check(support.action==w::carry::outcome::support_released,"Green Beret still permits a support-hand release near the holster");
        const auto refused=inventory.release(initial.id,2,w::carry::location::right_waist,true,no_drop,true,false);
        check(refused.action==w::carry::outcome::retained&&inventory.in_hand(hand::right),"final hand release cannot stow under Green Beret");
        const auto stored=inventory.release(initial.id,2,w::carry::location::right_waist,true,no_drop);
        check(stored.action==w::carry::outcome::stowed&&inventory.draw(w::carry::location::right_waist,hand::left).action==w::carry::outcome::drawn,"storage can resume and existing stored weapon can still be drawn");
    }
    std::cout<<"official cheat failures="<<failures<<'\n';return failures?1:0;
}
