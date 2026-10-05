#pragma once
#include "ladder_geometry.hpp"
#include "hand_interaction/object_identity.hpp"
#include <optional>
#include <string>

namespace vr::gameplay::ladders::scene
{
    struct contact
    {
        hand_interaction::object_identity object{};unsigned edge{};
        vec point{},normal{},local{},tangent{};float distance{};
        explicit operator bool()const noexcept{return object.value!=0;}
    };
    void reset()noexcept;
    void prepare(vec player,float units);
    contact nearest(vec wrist,vec head,float units);
    bool refresh(contact&,float units);
    float top(const contact&,float units);
    std::string status();
}
