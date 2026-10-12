-- The campaign menu is built after the active player profile has been loaded.
-- Do not apply during HKS initialization or a gameplay VM restart.
if Engine.InFrontend() then
    LUI.onmenuopen("main_campaign", function()
        game:applycampaignunlock(function()
            Engine.ExecNow("profile_menuDvarsSetup")
            if Engine.GetDvarBool("profileMenuOption_hasUnlockedAll_SP") == nil then
                error("Campaign profile is unavailable")
            end
            Engine.SetDvarBool("profileMenuOption_hasUnlockedAll_SP", true)
            Engine.ExecNow("profile_menuDvarsFinish")
            Engine.ExecNow("updategamerprofile")
            -- Reload the profile-backed menu values rather than trusting the set.
            Engine.ExecNow("profile_menuDvarsSetup")
            if Engine.GetDvarBool("profileMenuOption_hasUnlockedAll_SP") ~= true then
                error("Campaign profile did not accept the unlock request")
            end
        end)
    end)
end
