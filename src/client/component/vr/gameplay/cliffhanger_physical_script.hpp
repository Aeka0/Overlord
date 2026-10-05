#pragma once
#include <string_view>

namespace vr::gameplay::cliffhanger_physical
{
    inline constexpr std::string_view script_name="scripts/vr_cliffhanger_physical";
    // Only the two native climb loops are redirected. Native assets, entry,
    // first-wall exit, jump/slide/hang, failure and final exit remain native.
    inline constexpr std::string_view script_source=R"GSC(
climb(origin, side, sequence)
{
    level.player endon("death");
    level.player allowcrouch(0);
    level.player allowprone(0);
    level.player _meth_830F(0);
    common_scripts\utility::flag_set("player_starts_climbing");
    body = level._id_C374;
    // Declare checkpoint fields in compiled GSC so canonical field IDs are
    // registered consistently before either native save loading or C++ access.
    body.vr_climb_entry = undefined;
    body.vr_climb_handoff = 0;
    if (!vrphysicalclimbprepare(body))
        return 0;
    shared = spawnstruct();
    shared._id_B04C = side;
    shared._id_BCDC = [];
    foreach (hand in ["left", "right"])
    {
        receiver = spawnstruct();
        receiver.player = level.player;
        receiver._id_B375 = body;
        receiver._id_B60A = hand;
        receiver._id_B9D8 = "j";
        if (hand == "left")
            receiver._id_CAA4 = _id_BB6C::_id_D1F4;
        else
        {
            receiver._id_CAA4 = _id_BB6C::_id_B833;
            receiver._id_B9D8 = "k";
        }
        receiver._id_D509 = shared;
        receiver.anims = _id_CAF3::_id_C0DA([], "up", hand);
        shared._id_BCDC[hand] = receiver;
    }
    level._id_B05B = shared;
    level.player._id_B7DD = shared._id_BCDC[side];
    body._id_B60A = side;
    final = common_scripts\utility::flag("final_climb");
    // _id_B33F plays sequence[0] and enters sequence[1] before its first
    // _id_AEAA input poll, in both ascents. Keep every authored stab/notetrack
    // in that entry; controller ownership starts at the same input boundary.
    origin thread maps\_anim::anim_single_solo(body, sequence[0]);
    body animscripts\shared::donotetracks("single anim", ::entry_note);
    rest_key = sequence[1];
    origin maps\_anim::anim_first_frame_solo(body, rest_key);
    // The native helper establishes this idle's authored root and angles.
    // Wait for its first frame before publishing controller ownership.
    wait 0.05;
    rest = body maps\_utility::getanim(rest_key);
    body notsolid();
    body_start = body.origin;
    carrier_start = level.player.origin;
    carrier = spawn("script_model", carrier_start);
    carrier setmodel("tag_origin");
    carrier.animname = "vr_climb_carrier";
    carrier.vr_climb_save = undefined;
    carrier hide();
    carrier notsolid();
    closing_key = sequence[sequence.size-1];
    closing = body maps\_utility::getanim(closing_key);
    finish = carrier_start + getstartorigin(origin.origin, origin.angles, closing) - body_start;
    while (!vrphysicalclimbentryready())
        wait 0.05;
    level.player unlink();
    level.player playerlinktodelta(carrier, "tag_origin", 1, 0, 0, 0, 0, 1);
    level.player playersetgroundreferenceent(carrier);
    if (!vrphysicalclimbbegin(body, carrier, final, side, finish))
    {
        level.player playersetgroundreferenceent(undefined);
        level.player unlink();
        level.player playerlinktodelta(body, "tag_player", 1, 0, 0, 0, 0, 1);
        carrier delete();
        vrphysicalclimbend();
        return 0;
    }
    body hide();
    status = 0;
    milestone = 0;
    while (status == 0)
    {
        status = vrphysicalclimbstate(body, carrier, final, carrier_start, finish);
        reached = vrphysicalclimbmilestone();
        if (!final && (reached & 1) && !(milestone & 1))
        {
            common_scripts\utility::flag_set("player_begins_to_climb");
        }
        if (!final && (reached & 2) && !(milestone & 2) && !common_scripts\utility::flag("price_climb_continues"))
        {
            common_scripts\utility::flag_set("price_climb_continues");
            level notify("fourth_swing");
        }
        if (!final && (reached & 4) && !(milestone & 4))
            common_scripts\utility::flag_set("player_climbed_3_steps");
        milestone = reached;
        wait 0.05;
    }
    // Only the handoff uses the authored body again. The free-climb proxy
    // carries the player independently of its animation and shoulder geometry.
    body.origin = body_start + carrier.origin - carrier_start;
    level.player playersetgroundreferenceent(undefined);
    level.player unlink();
    level.player playerlinktodelta(body, "tag_player", 1, 0, 0, 0, 0, 1);
    body show();
    carrier delete();
    if (status < 0)
    {
        receiver = shared._id_BCDC[vrphysicalclimbfallside()];
        result = _id_BB6C::_id_B842(receiver, rest_key, origin);
        _id_BB6C::_id_AB02();
        vrphysicalclimbend();
        return result;
    }
    if (!final)
        common_scripts\utility::flag_set("force_single_ice_crack");
    origin thread maps\_anim::anim_single_solo(body, closing_key);
    body thread animscripts\shared::donotetracks("single anim", _id_BB6C::_id_B050);
    if (!final && !common_scripts\utility::flag("player_preps_for_jump"))
    {
        jump = getent("climb_jump_org", "targetname");
        body waittillmatch("single anim", "spawn_soap");
        thread _id_BB6C::_id_C7AE(jump);
    }
    body waittillmatch("single anim", "end");
    if (!final)
        common_scripts\utility::flag_clear("force_single_ice_crack");
    common_scripts\utility::flag_set("finished_climbing");
    level notify("player_shimmy_stop");
    body notify("stop_crack");
    level.player playersetgroundreferenceent(undefined);
    wait 0.05;
    level.player unlink();
    body delete();
    level.player allowfire(1);
    level.player allowcrouch(1);
    level.player allowprone(1);
    level.player _meth_830F(1);
    common_scripts\utility::flag_clear("climbing_dof");
    _id_BB6C::_id_AB02();
    vrphysicalclimbend();
    return 1;
}
entry_right() { return vrphysicalclimbentryready(); }
entry_left() { return vrphysicalclimbentryready(); }
entry_failure() { return 0; }
entry_note(note)
{
    // Keep the native ray and callback at the actual stab event. Only insert
    // an observation before its original impact/settle callback.
    if (note == "left_stab")
    {
        level._id_C374._id_B60A = "left";
        note = "stab";
    }
    else if (note == "right_stab")
    {
        level._id_C374._id_B60A = "right";
        note = "stab";
    }
    if (note == "stab")
        _id_BB6C::_id_BA39(::entry_hit);
    else
        _id_BB6C::_id_B050(note);
}
entry_hit(point, normal)
{
    vrphysicalclimbseed(level._id_C374, level._id_C374._id_B60A, point, normal);
    _id_BB6C::_id_CDFC(point, normal);
}
)GSC";
}
