init()
{
    // Own only these five mixes. Also retire leftovers on a new script VM.
    release();
    level.killfeed_duck_running = false;
    level.killfeed_duck_until = 0;
}

trigger()
{
    level.killfeed_duck_until = gettime() + 150;
    if ( isdefined( level.killfeed_duck_running ) && level.killfeed_duck_running )
        return 49;

    level.killfeed_duck_running = true;
    // Arm cleanup before submission: a partial activation must also expire.
    level thread recover_duck();
    return activate();
}

recover_duck()
{
    while ( gettime() < level.killfeed_duck_until )
        wait 0.05;

    release();
    level.killfeed_duck_running = false;
}

activate()
{
    volmods = [];
    // Preserve the supplied mix amounts; never compound separate activations.
    volmods = add_volmod( volmods, "wpn_foley_plr", 0.55 );
    volmods = add_volmod( volmods, "wpn_foley_plr_beltfed", 0.55 );
    volmods = add_volmod( volmods, "wpn_plr_foley_h2", 0.55 );
    volmods = add_volmod( volmods, "wpn_plr", 0.45 );
    volmods = add_volmod( volmods, "wpn_plr_special", 0.45 );
    volmods = add_volmod( volmods, "iw4_wpn_plr_shot", 0.45 );
    volmods = add_volmod( volmods, "wpn_plr_shot_first", 0.45 );
    volmods = add_volmod( volmods, "wpn_plr_shot_special", 0.45 );
    volmods = add_volmod( volmods, "wpn_plr_shot", 0.45 );
    volmods = add_volmod( volmods, "wpn_plr_punch", 0.45 );
    volmods = add_volmod( volmods, "wpn_plr_mech", 0.55 );
    volmods = add_volmod( volmods, "wpn_plr_sub", 0.45 );
    volmods = add_volmod( volmods, "wpn_plr_lfe", 0.45 );
    volmods = add_volmod( volmods, "wpn_plr_tail", 0.5 );
    volmods = add_volmod( volmods, "wpn_plr_tail_int_med", 0.5 );
    volmods = add_volmod( volmods, "wpn_plr_tail_int_sml", 0.5 );
    volmods = add_volmod( volmods, "wpn_plr_tail_semi", 0.5 );
    volmods = add_volmod( volmods, "wpn_foley_npc", 0.65 );
    volmods = add_volmod( volmods, "wpn_npc", 0.6 );
    volmods = add_volmod( volmods, "wpn_npc_special", 0.6 );
    volmods = add_volmod( volmods, "mp_wpn_npc", 0.6 );
    volmods = add_volmod( volmods, "wpn_projectile", 0.6 );
    volmods = add_volmod( volmods, "iw4_wpn_npc_shot", 0.6 );
    volmods = add_volmod( volmods, "wpn_npc_shot", 0.6 );
    volmods = add_volmod( volmods, "wpn_npc_mech", 0.65 );
    volmods = add_volmod( volmods, "wpn_npc_sub", 0.6 );
    volmods = add_volmod( volmods, "wpn_npc_tail", 0.6 );
    volmods = add_volmod( volmods, "wpn_npc_dist", 0.6 );
    volmods = add_volmod( volmods, "h2_wpn_npc_shot_close", 0.6 );
    volmods = add_volmod( volmods, "h2_wpn_npc_shot_mid", 0.6 );
    volmods = add_volmod( volmods, "h2_wpn_npc_shot_far", 0.6 );
    volmods = add_volmod( volmods, "h2_wpn_npc_shot_tail", 0.6 );
    volmods = add_volmod( volmods, "h2_wpn_npc_shot_first", 0.6 );
    volmods = add_volmod( volmods, "destruct", 0.65 );
    volmods = add_volmod( volmods, "explosion", 0.6 );
    volmods = add_volmod( volmods, "explosion_grenade", 0.6 );
    volmods = add_volmod( volmods, "explosion_flashbang", 0.6 );
    volmods = add_volmod( volmods, "explosion_rocket", 0.6 );
    volmods = add_volmod( volmods, "explosion_car", 0.6 );
    volmods = add_volmod( volmods, "explosion_debris", 0.65 );
    volmods = add_volmod( volmods, "bullet_impact_geo", 0.65 );
    volmods = add_volmod( volmods, "bullet_impact_geo_metal", 0.65 );
    volmods = add_volmod( volmods, "bullet_impact_plr", 0.65 );
    volmods = add_volmod( volmods, "bullet_impact_npc", 0.65 );
    volmods = add_volmod( volmods, "mp_bullet_impact_geo", 0.65 );
    volmods = add_volmod( volmods, "mp_bullet_impact_plr", 0.65 );
    volmods = add_volmod( volmods, "mp_bullet_impact_npc", 0.65 );
    volmods = add_volmod( volmods, "bullet_whizby", 0.65 );
    volmods = add_volmod( volmods, "mp_bullet_whizby", 0.65 );

    return submit_volmods( volmods );
}

submix_names()
{
    // Fixed script strings identify exactly the same mixes at add and clear.
    names = [];
    names[0] = "killfeed_feedback_duck_0";
    names[1] = "killfeed_feedback_duck_1";
    names[2] = "killfeed_feedback_duck_2";
    names[3] = "killfeed_feedback_duck_3";
    names[4] = "killfeed_feedback_duck_4";
    return names;
}

release()
{
    names = submix_names();
    for ( index = 0; index < names.size; index++ )
        clearsubmix( names[index], 0.18 );
}

submit_volmods( volmods )
{
    names = submix_names();
    // H2 accepts ten name/value pairs per call. Keep all batches in our slots.
    if ( volmods.size == 0 || volmods.size % 2 != 0 || volmods.size > names.size * 20 )
        return 0;

    release();
    batch = [];
    batch_index = 0;
    for ( index = 0; index < volmods.size; index += 2 )
    {
        batch = add_volmod( batch, volmods[index], volmods[index + 1] );
        if ( batch.size == 20 || index + 2 == volmods.size )
        {
            addsubmix( names[batch_index], 0.01, 1.0, batch );
            batch_index++;
            batch = [];
        }
    }
    return volmods.size / 2;
}

add_volmod( volmods, name, amount )
{
    // GSC arrays use copy-on-write; return the modified array to the caller.
    volmods[volmods.size] = name;
    volmods[volmods.size] = amount;
    return volmods;
}
