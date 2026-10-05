#include <std_include.hpp>
#include "weapon_inventory_snapshot.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_carry.hpp"
#include "physical_reload_runtime.hpp"
#include "cylinder_runtime.hpp"
#include "tube_runtime.hpp"
#include "break_action_runtime.hpp"
#include "underbarrel_runtime.hpp"
#include "launcher_runtime.hpp"
#include "weapon_registry.hpp"
#include "reload_item_runtime.hpp"
#include "component/scheduler_context.hpp"
#include "game/game.hpp"

namespace vr::gameplay::weapons::inventory_snapshot
{
    namespace
    {
        std::uint32_t token(std::string_view name)
        {
            for(std::uint32_t i=1;i<512;++i)
                if(game::weapon_defs[i] && game::weapon_defs[i]->szInternalName && name==game::weapon_defs[i]->szInternalName)return i;
            return 0;
        }
        template<class T> const T* recipe(const weapon& w,const T* profile::*member)
        {
            for(const auto& p:registered_profiles)
                if(const auto* value=p.value->*member;value && value->id==w.recipe)return value;
            return nullptr;
        }
        template<class T> T bind(T s,weapon_identity id,int reserve)
        {s.weapon=id.weapon;s.instance_generation=id.generation;s.revision=1;s.reserve=reserve;s.held_rounds=0;s.loader_hand=hand::none;return s;}
        mechanics::state bind(mechanics::state s,weapon_identity id,int reserve)
        {s.weapon=id.weapon;s.instance_generation=id.generation;s.revision=1;s.reserve_rounds=reserve;s.held_rounds=0;s.magazine_hand=hand::none;return s;}
        bool feed_valid(const weapon& w,std::uint32_t t)
        {
            if(!t)return false;
            const auto capacity=game::weapon_defs[t]->clipSize;const weapon_identity id{t,1};
            switch(w.kind)
            {
            case feed::native:return w.recipe.empty();
            case feed::magazine:
                if(const auto* p=recipe(w,&profile::reload);p && p->matches_native(w.name,capacity))
                {const auto s=bind(w.magazine,id,w.reserve);return mechanics::valid(p->ammunition,s) && mechanics::native_ammo(s).loaded==w.loaded;}break;
            case feed::cylinder:
                if(const auto* p=recipe(w,&profile::cylinder);p && p->matches_native(w.name,capacity))
                {const auto s=bind(w.revolver,id,w.reserve);return cylinder::valid(p->ammunition,s) && s.live==w.loaded;}break;
            case feed::tube:
                if(const auto* p=recipe(w,&profile::tube);p && p->matches_native(w.name,capacity))
                {const auto s=bind(w.shotgun,id,w.reserve);return tube::valid(p->ammunition,s) && tube::native_ammo(s).loaded==w.loaded;}break;
            case feed::hinged:
                if(const auto* p=recipe(w,&profile::break_open);p && p->matches_native(w.name,capacity))
                {const auto s=bind(w.hinged,id,w.reserve);return break_action::valid(p->ammunition,s) && break_action::native_ammo(s).loaded==w.loaded;}break;
            case feed::launcher:
                if(const auto* p=recipe(w,&profile::launcher);p && p->matches(w.name))
                    return w.loaded<=1 && (!w.launcher_spent || (p->loading==launcher_loading::disposable && w.loaded==0));break;
            }
            return false;
        }
    }
    bool settle()
    {
        if(!scheduler::is_executing(scheduler::pipeline::server))return false;
        const auto native=native_carry::observe();if(!native.valid)return false;
        for(std::size_t i=0;i<native.count;++i)
        {
            const auto id=native.owned[i].id;
            physical_reload::presentation m;cylinder::presentation c;tube::presentation t;break_action::presentation b;
            if(!launcher::prepare_transfer(id) || !underbarrel::prepare_transfer(id) || !physical_reload::prepare_transfer(id,m) ||
                !cylinder::prepare_transfer(id,c) || !tube::prepare_transfer(id,t) || !break_action::prepare_transfer(id,b))return false;
        }
        return reload_items::settle_inventory();
    }
    bool capture(snapshot& out)
    {
        if(!scheduler::is_executing(scheduler::pipeline::server))return false;
        const auto native=native_carry::observe();if(!native.valid)return false;
        if(!settle())return false;
        const auto layout=carry::capture_inventory({native.owned.data(),native.count},native.selected);
        snapshot next;std::array<weapon_identity,carry::inventory::capacity> ids{};
        auto selected=carry::current_hold().id();if(!layout.find(selected))selected=native_ammunition::projected_identity(native.selected);
        for(const auto& item:layout.instances())if(item.id)
        {
            if(next.count==int(next.weapons.size()))return false;
            auto& w=next.weapons[next.count];ids[next.count]=item.id;
            if(item.at==carry::location::held && item.id==selected)next.selected=next.count;
            const auto* def=game::weapon_defs[item.id.weapon];if(!def || !def->szInternalName)return false;
            w.name=def->szInternalName;w.location=item.at;w.rear=item.owner.rear;w.support=item.owner.support;w.pose=item.owner.pose_rear;
            w.attachment=item.owner.attachment;
            if(!valid_hand(w.pose))w.pose=hand::right;
            if(item.at==carry::location::overflow)
            {w.overflow=1;for(const auto& other:layout.instances())if(other.at==carry::location::overflow && other.overflow_order<item.overflow_order)++w.overflow;}
            const auto m=physical_reload::current(item.id);const auto c=cylinder::current(item.id);
            const auto t=tube::current(item.id);const auto b=break_action::current(item.id);
            if(m.active && !m.fault && m.definition){w.kind=feed::magazine;w.recipe=m.definition->id;w.magazine=m.ammo;}
            else if(c.active && !c.fault && c.definition){w.kind=feed::cylinder;w.recipe=c.definition->id;w.revolver=c.ammo;}
            else if(t.active && !t.fault && t.definition){w.kind=feed::tube;w.recipe=t.definition->id;w.shotgun=t.ammo;w.tube_travel=t.travel;w.lever_open=t.lever.open;w.lever_grasped=t.lever.grasped;}
            else if(b.active && !b.fault && b.definition){w.kind=feed::hinged;w.recipe=b.definition->id;w.hinged=b.ammo;}
            else if(const auto l=launcher::current(item.id);l.active && !l.fault && l.definition)
            {w.kind=feed::launcher;w.recipe=l.definition->id;w.launcher_spent=l.spent;}
            const auto module=underbarrel::current(item.id);
            if(module.active && !module.fault)
            {w.has_module=true;w.module=module.ammo;w.module_travel=module.travel;if(w.module.held || valid_hand(w.module.loader))return false;}
            else
            {
                const auto binding=underbarrel::native::resolve(item.id);
                if(binding)
                {
                    const auto observed=underbarrel::native::observe(binding);if(!observed.valid)return false;
                    w.module=underbarrel::import_native(binding.id,observed.ammo.loaded,observed.ammo.reserve);
                    if(!underbarrel::valid(w.module))return false;w.has_module=true;
                }
            }
            ++next.count;
        }
        // Every escrow was settled before reading shared pools; two copies of
        // one caliber cannot capture different pre/post-refund reserve values.
        for(int i=0;i<next.count;++i)
        {
            const auto ammo=native_ammunition::observe_carried(native.player,ids[i]);
            const auto ledger=native_ammunition::instances();const auto* entry=ledger.find(ids[i]);
            if(!ammo.valid && (!entry || entry->key))return false;
            next.weapons[i].loaded=ammo.valid?ammo.loaded:0;next.weapons[i].reserve=ammo.valid?ammo.reserve:0;
            if(next.weapons[i].has_module)
            {
                const auto observed=underbarrel::native::observe(underbarrel::native::resolve(ids[i]));
                if(!observed.valid || observed.ammo.loaded!=next.weapons[i].module.loaded)return false;
                next.weapons[i].module.reserve=observed.ammo.reserve;
            }
        }
        if(!validate(next))return false;out=std::move(next);return true;
    }
    bool validate(const snapshot& s)
    {
        if(!shape_valid(s))return false;
        for(int i=0;i<s.count;++i)
        {
            const auto& w=s.weapons[i];const auto t=token(w.name);if(!feed_valid(w,t))return false;
            if(w.has_module)
            {
                auto m=w.module;m.id.host={t,1};m.id.definition=t==1?2:1;m.revision=1;m.held=0;m.loader=hand::none;
                if(!underbarrel::valid(m))return false;
            }
        }
        return true;
    }
    bool restore(const snapshot& s)
    {
        if(!scheduler::is_executing(scheduler::pipeline::server) || !validate(s))return false;
        const auto native=native_carry::observe();if(!native.valid)return false;
        std::array<native_ammunition::restored_clip,carry::inventory::capacity> clips{};
        std::array<weapon_identity,carry::inventory::capacity> ids{};
        std::array<carry::instance,carry::inventory::capacity> entries{};
        for(int i=0;i<s.count;++i)
        {
            const auto& w=s.weapons[i];const auto t=token(w.name);clips[i]={t,w.loaded,w.reserve};
            const auto found=std::find_if(native.owned.begin(),native.owned.begin()+native.count,[&](const auto& v){return v.id.weapon==t;});
            if(found==native.owned.begin()+native.count)return false;
            auto& e=entries[i];e.id={t,std::uint64_t(i+1)};e.policy=found->policy;e.at=w.location;e.overflow_order=w.overflow;
            e.owner.weapon=t;e.owner.instance_generation=e.id.generation;e.owner.rear=w.rear;e.owner.support=w.support;e.owner.pose_rear=w.pose;
            e.owner.attachment=w.attachment;
        }
        carry::inventory preflight;if(!preflight.restore({entries.data(),std::size_t(s.count)}))return false;
        if(!native_ammunition::restore_clips({clips.data(),std::size_t(s.count)},{ids.data(),std::size_t(s.count)}))return false;
        for(int i=0;i<s.count;++i){entries[i].id=ids[i];entries[i].owner.instance_generation=ids[i].generation;}
        if(!carry::restore_inventory({entries.data(),std::size_t(s.count)},s.selected<0?weapon_identity{}:ids[s.selected]))return false;
        // Establish each lifecycle's current player/timeline before installing
        // restored feeds. Hand/trigger leases deliberately require fresh input.
        physical_reload::update_lifecycle(true);cylinder::update_lifecycle(true);tube::update_lifecycle(true);
        break_action::update_lifecycle(true);launcher::update_lifecycle(true);
        bool ok=true;
        for(int i=0;i<s.count;++i)
        {
            const auto& w=s.weapons[i];auto owner=entries[i].owner;owner.revision=owner.rear_revision=1;
            if(const auto held=carry::held(ids[i]);held.id())owner=held;
            switch(w.kind)
            {
            case feed::magazine:{physical_reload::presentation p;p.active=true;p.owner=owner;p.definition=recipe(w,&profile::reload);p.ammo=bind(w.magazine,ids[i],w.reserve);ok=physical_reload::restore_transfer(p)&&ok;break;}
            case feed::cylinder:{cylinder::presentation p;p.active=true;p.owner=owner;p.definition=recipe(w,&profile::cylinder);p.ammo=bind(w.revolver,ids[i],w.reserve);ok=cylinder::restore_transfer(p)&&ok;break;}
            case feed::tube:{tube::presentation p;p.active=true;p.owner=owner;p.definition=recipe(w,&profile::tube);p.ammo=bind(w.shotgun,ids[i],w.reserve);p.travel=w.tube_travel;p.lever.open=w.lever_open;p.lever.grasped=w.lever_grasped;ok=tube::restore_transfer(p)&&ok;break;}
            case feed::hinged:{break_action::presentation p;p.active=true;p.owner=owner;p.definition=recipe(w,&profile::break_open);p.ammo=bind(w.hinged,ids[i],w.reserve);ok=break_action::restore_transfer(p)&&ok;break;}
            case feed::launcher:{launcher::presentation p;p.active=true;p.owner=owner;p.definition=recipe(w,&profile::launcher);p.loaded=w.loaded;p.spent=w.launcher_spent;ok=launcher::restore_transfer(p)&&ok;break;}
            default:break;
            }
            if(w.has_module)
            {
                const auto module=underbarrel::native::resolve(ids[i]);
                if(!module || module.id.type!=w.module.id.type){ok=false;continue;}
                underbarrel::presentation p;p.active=true;p.owner=owner;p.ammo=w.module;p.ammo.id=module.id;
                p.ammo.revision=1;p.ammo.held=0;p.ammo.loader=hand::none;p.travel=w.module_travel;
                ok=underbarrel::restore_transfer(p)&&ok;
            }
        }
        return ok;
    }
}
