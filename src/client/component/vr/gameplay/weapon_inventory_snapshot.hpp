#pragma once
#include "weapon_carry.hpp"
#include "detachable_magazine.hpp"
#include "cylinder_feed.hpp"
#include "tube_feed.hpp"
#include "break_action_feed.hpp"
#include "underbarrel_feed.hpp"
#include <string>

namespace vr::gameplay::weapons::inventory_snapshot
{
    enum class feed { native, magazine, cylinder, tube, hinged, launcher };
    struct weapon
    {
        std::string name,recipe;
        carry::location location{carry::location::absent};
        hand rear{hand::none},support{hand::none},pose{hand::right};
        control_attachment attachment{};
        int overflow{},loaded{},reserve{};
        feed kind{};
        mechanics::state magazine{};
        cylinder::state revolver{};
        tube::state shotgun{};
        float tube_travel{},lever_open{};bool lever_grasped{};
        break_action::state hinged{};
        bool launcher_spent{},has_module{};
        underbarrel::state module{};
        float module_travel{};
    };
    struct snapshot
    {
        static constexpr int schema=1;
        std::array<weapon,carry::inventory::capacity> weapons{};
        int count{},selected{-1}; // -1 explicitly preserves empty selection.
    };
    inline bool shape_valid(const snapshot& s) noexcept
    {
        if(s.count<0 || s.count>int(s.weapons.size()) || s.selected<-1 || s.selected>=s.count)return false;
        unsigned hands{};std::array<bool,3> slots{};
        for(int i=0;i<s.count;++i)
        {
            const auto& w=s.weapons[i];
            if(w.name.empty() || w.name.size()>=64 || w.recipe.size()>=96 ||
                w.loaded<0 || w.loaded>1001 || w.reserve<0 || w.reserve>1000000 ||
                w.kind<feed::native || w.kind>feed::launcher ||
                w.location<=carry::location::absent || w.location>carry::location::abdominal ||
                w.overflow<0 || w.overflow>int(s.weapons.size()) || !valid_hand(w.pose))return false;
            if(w.attachment!=control_attachment::fixed && w.attachment!=control_attachment::moving)return false;
            if(w.kind==feed::tube && (!std::isfinite(w.tube_travel) || w.tube_travel<0 || w.tube_travel>1 ||
                !std::isfinite(w.lever_open) || w.lever_open<0 || w.lever_open>1))return false;
            if((w.rear!=hand::none && !valid_hand(w.rear)) || (w.support!=hand::none && !valid_hand(w.support)))return false;
            if(w.location==carry::location::held)
            {
                if(w.rear==hand::none && w.support==hand::none)return false;
                if(valid_hand(w.rear) && w.rear==w.support)return false;
                for(auto h:{w.rear,w.support})if(valid_hand(h))
                {const auto bit=1u<<unsigned(h);if(hands&bit)return false;hands|=bit;}
            }
            else if(w.rear!=hand::none || w.support!=hand::none)return false;
            if(carry::ordinary_slot(w.location))
            {const auto n=int(w.location)-int(carry::location::left_waist);if(slots[n])return false;slots[n]=true;}
            if(w.location==carry::location::overflow && !w.overflow)return false;
            if(w.has_module && (!std::isfinite(w.module_travel) || w.module_travel<0 || w.module_travel>1))return false;
        }
        return s.selected<0 || s.weapons[s.selected].location==carry::location::held;
    }
    // Persist only stable logical fields. Runtime pointers, timestamps, input
    // leases, ammo-key tokens and instance generations are rebound after loading.
    template<class A> void fields(A& a,mechanics::state& s)
    {
        a("inserted",s.magazine_inserted);a("chamber",s.chamber_loaded);a("rounds",s.magazine_rounds);a("action",s.action);
        a("lift",s.bolt.lift);a("travel",s.bolt.travel);a("case",s.bolt.spent_case);a("cocked",s.bolt.cocked);
        a("feeding",s.bolt.feeding);a("feed_armed",s.bolt.feed_armed);a("returned",s.bolt.returned);
        a("cover",s.belt.cover);a("laid",s.belt.laid);a("bridge",s.belt.bridge);
    }
    template<class A> void fields(A& a,cylinder::state& s)
    {a("live",s.live);a("spent",s.spent);a("phase",s.phase);}
    template<class A> void fields(A& a,tube::state& s)
    {a("stored",s.stored);a("chamber",s.chamber);a("phase",s.phase);a("case",s.spent_case);a("index",s.drum_index);}
    template<class A> void fields(A& a,break_action::state& s)
    {a("live",s.live);a("spent",s.spent);a("phase",s.phase);a("hinge",s.hinge);}
    template<class A> void fields(A& a,underbarrel::state& s)
    {a("type",s.id.type);a("loaded",s.loaded);a("reserve",s.reserve);a("chamber",s.chamber);a("spent",s.spent);a("open",s.open);}
    template<class A> void fields(A& a,weapon& w)
    {
        a("name",w.name);a("recipe",w.recipe);a("location",w.location);a("rear",w.rear);a("support",w.support);a("pose",w.pose);
        a("attachment",w.attachment);
        a("overflow",w.overflow);a("loaded",w.loaded);a("reserve",w.reserve);a("feed",w.kind);
        switch(w.kind)
        {
        case feed::magazine:a("magazine",w.magazine);break;
        case feed::cylinder:a("cylinder",w.revolver);break;
        case feed::tube:a("tube",w.shotgun);a("tube_travel",w.tube_travel);a("lever_open",w.lever_open);a("lever_grasped",w.lever_grasped);break;
        case feed::hinged:a("hinged",w.hinged);break;
        case feed::launcher:a("spent",w.launcher_spent);break;
        default:break;
        }
        a("has_module",w.has_module);if(w.has_module){a("module",w.module);a("module_travel",w.module_travel);}
    }
    bool capture(snapshot&);
    bool settle(); // Resolve all in-flight reload escrow before native ownership changes.
    bool validate(const snapshot&); // Read-only native asset/profile preflight before any take/give.
    bool restore(const snapshot&);
}
