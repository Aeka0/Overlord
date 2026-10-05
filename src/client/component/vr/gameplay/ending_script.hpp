#pragma once
#include <string_view>
namespace vr::gameplay::ending
{
    inline constexpr std::string_view script_name="scripts/vr_ending_interaction";
    // Redirect only native interaction boundaries; the surrounding
    // fight, blade extraction notetracks, kick and final throw remain authored.
    inline constexpr std::string_view script_source=R"GSC(
knife_blood(actor)
{
    // Preserve the original model switch. Only the emitter on the invisible
    // stabbed-player knife is omitted; real extraction keeps its notetrack FX.
    maps\af_chase_knife_fight_code::_id_C1D9();
    if (common_scripts\utility::flag("throw_knife_pulled_out"))
        playfxontag(common_scripts\utility::getfx("player_stabbed"), actor, "TAG_FX");
}
knife_grasp()
{
    level endon("missionfailed");
    level endon("do_museum_credits");
    level.player endon("death");
    body = maps\af_chase_knife_fight_code::_id_C6C5();
    knife = maps\af_chase_knife_fight_code::_id_CF10();
    stopfxontag(common_scripts\utility::getfx("player_stabbed"), knife, "TAG_FX");
    maps\af_chase_knife_fight_code::_id_C1BA();
    common_scripts\utility::flag_set("player_looks_at_knife");
    while (!vrendinggrasp(body, knife))
        wait 0.05;
    common_scripts\utility::flag_set("focused_on_knife");
    soundscripts\_snd::snd_message("aud_start_player_knife_pullout");
    level notify("player_used_knife");
    thread maps\_utility::_id_BA76();
}
knife_hint()
{
    // This quick hint bypassed the draw-time key replacement. Replace its
    // exact producer while retaining native layout,
    // blinking, cleanup and controller/keyboard variants.
    text = vrendingknifehint();
    thread maps\_utility::_id_BD5A(text, text, 90, 1, ["+activate", "+usereload"]);
    level.player thread maps\_utility::_id_BD6E("death");
}
pull(sequence)
{
    level endon("missionfailed");
    level endon("do_museum_credits");
    level.player endon("death");
    body = maps\af_chase_knife_fight_code::_id_C6C5();
    knife = maps\af_chase_knife_fight_code::_id_CF10();
    objects = [body, knife];
    foreach (actor in objects)
        actor _meth_83D4(actor maps\_utility::getanim(sequence), 0);
    progress = body getanimtime(body maps\_utility::getanim(sequence));
    vrendingpullbegin(body, knife);
    while (progress < 0.95)
    {
        amount = vrendingpullwork(body, knife);
        if (amount > 0)
        {
            progress = clamp(progress + amount, 0, 0.95);
            foreach (actor in objects)
                actor setanimtime(actor maps\_utility::getanim(sequence), progress);
            if (isdefined(self.set_pull_weight))
                level.additive_pull_weight = progress;
        }
        wait 0.05;
    }
    vrendingpullend();
    level notify("new_hurt");
    maps\af_chase_knife_fight_code::_id_B3EA(1);
    foreach (actor in objects)
        actor setanim(actor maps\_utility::getanim(sequence), 1, 0, 0.06);
    if (sequence == "knifepull_pull_02")
        thread maps\_utility::_id_BA76();
}
throw_wait()
{
    level endon("missionfailed");
    level endon("do_museum_credits");
    level.player endon("death");
    level endon("player_throws_knife");
    body = maps\af_chase_knife_fight_code::_id_C6C5();
    knife = maps\af_chase_knife_fight_code::_id_CF10();
    while (!vrendingthrowprepare(body, knife))
        wait 0.05;
    body attach("weapon_commando_knife_bloody", "tag_weapon_right");
    body.vr_ending_knife_attached = 1;
    thread throw_prop_cleanup(body);
    while (!vrendingthrow(body, knife, level._id_B416))
        wait 0.05;
    // End the independent preparation before the native thread takes the blade.
    throw_prop_release(body);
    vrendingthrowend();
    common_scripts\utility::flag_set("player_throws_knife");
}
throw_prop_release(body)
{
    if (isdefined(body) && isdefined(body.vr_ending_knife_attached) && body.vr_ending_knife_attached)
    {
        body detach("weapon_commando_knife_bloody", "tag_weapon_right");
        body.vr_ending_knife_attached = 0;
    }
}
throw_prop_cleanup(body)
{
    // A failure ends throw_wait before its normal detach. Keep cleanup in a
    // separate native thread, and make it idempotent for the successful path.
    while (isdefined(body) && isalive(level.player) && !common_scripts\utility::flag("missionfailed") &&
        !common_scripts\utility::flag("player_throws_knife") && !common_scripts\utility::flag("do_museum_credits"))
        wait 0.05;
    throw_prop_release(body);
    vrendingthrowend();
}
gaze(body)
{
    level endon("player_throws_knife");
    level endon("missionfailed");
    level endon("do_museum_credits");
    for (;;)
    {
        if (vrendinggaze(level._id_B416))
            common_scripts\utility::flag_set("player_aims_knife_at_shepherd");
        else
            common_scripts\utility::flag_clear("player_aims_knife_at_shepherd");
        wait 0.05;
    }
}
crawl()
{
    level endon("missionfailed");
    level endon("do_museum_credits");
    level.player endon("death");
    common_scripts\utility::flag_set("crawl_gameplay_started");
    thread maps\af_chase_knife_fight::_id_CE81("crawl_gameplay_started");
    savegame("crawl", &"AUTOSAVE_LEVELSTART", "shot", 1);
    maps\af_chase_knife_fight_code::_id_AA80();
    level._id_C381.origin = (40, 0, 0);
    origin = common_scripts\utility::getstruct("end_scene_org_02", "targetname");
    body = maps\af_chase_knife_fight_code::_id_C6C5();
    if (!maps\_utility::is_default_start())
    {
        origin maps\_anim::anim_first_frame_solo(body, "gun_crawl_00_idle");
        wait 0.05;
        maps\af_chase_knife_fight_code::_id_B671();
    }
    thread maps\af_chase_knife_fight_code::_id_C8F9();
    level notify("stop_heart");
    start = level.player.origin;
    carrier = spawn("script_model", start);
    carrier setmodel("tag_origin");
    carrier.animname = "vr_ending_carrier";
    carrier.angles = body gettagangles("tag_player");
    carrier hide();
    carrier notsolid();
    level.player playersetgroundreferenceent(undefined);
    level.player unlink();
    level.player playerlinktodelta(carrier, "tag_origin", 1, 0, 0, 0, 0, 1);
    level.player playersetgroundreferenceent(carrier);
    body hide();
    // Link-to-delta preserves a native eye offset. Observe it only after the
    // link has advanced, then use that same frame for floor/arrival/collision.
    wait 0.05;
    // Only the hidden body changes animation now. Keeping the camera on its
    // previous endpoint avoids an idle-pose cut before the smooth floor settle.
    origin maps\_anim::anim_first_frame_solo(body, "gun_crawl_00_idle");
    wait 0.05;
    finish = start + getstartorigin(origin.origin, origin.angles, body maps\_utility::getanim("gun_crawl_06")) - body.origin;
    direction = vectornormalize((finish[0] - start[0], finish[1] - start[1], 0));
    milestones = [];
    for (step = 0; step < 6; step++)
        milestones[step] = vectordot(getstartorigin(origin.origin, origin.angles, body maps\_utility::getanim("gun_crawl_0" + step)) - body.origin, direction);
    carrier.vr_ending_start = start;
    carrier.vr_ending_finish = finish;
    carrier.vr_ending_body = body;
    step = 0;
    done = 0;
    while (!done || step < 6)
    {
        done = vrendingcrawl(body, carrier, start, finish);
        travelled = vectordot(carrier.origin - start, direction);
        // Keep native background choreography, audio and the six lighting/FOV
        // notifications at their original animation root positions. The hidden
        // player body never drives locomotion during this interval.
        if (step < 6 && (done || travelled >= milestones[step]))
        {
            common_scripts\utility::flag_set("crawl_gameplay_player_input");
            sound = "sand_crawl_right";
            if (step % 2)
                sound = "sand_crawl_left";
            level.player playsound(sound);
            if (step == 1)
                thread maps\af_chase_knife_fight_code::_id_B37D();
            if (step == 2)
                thread maps\af_chase_knife_fight_code::_id_C25B();
            if (step == 3)
                thread maps\af_chase_knife_fight_code::_id_B404();
            step++;
        }
        wait 0.05;
    }
    // Rejoin the exact native endpoint; the next native logic function owns
    // Shepherd's kick. Never advance a guessed animation index or route step.
    origin maps\_anim::anim_first_frame_solo(body, "gun_crawl_06");
    wait 0.05;
    level.player playersetgroundreferenceent(undefined);
    level.player unlink();
    level.player playerlinktodelta(body, "tag_player", 1, 0, 0, 0, 0, 1);
    body show();
    carrier delete();
    vrendingcrawlend();
    common_scripts\utility::flag_set("crawl_gameplay_complete");
}
)GSC";
}
