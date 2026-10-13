-- The native subtitle preference belongs to the loaded player profile.
-- Apply once per process at the same profile-ready boundary as campaign actions.
if Engine.InFrontend() then
    LUI.onmenuopen("main_campaign", function()
        game:applysubtitles(function(enabled)
            local controller = Engine.GetFirstActiveController()
            if type(controller) ~= "number" or controller < 0 then
                error("No active controller for the subtitle profile")
            end
            if Engine.SubtitlesEnabled(controller) == enabled then
                return
            end
            Engine.SetSubtitlesEnabled(controller, enabled)
            if Engine.SubtitlesEnabled(controller) ~= enabled then
                error("Player profile did not accept the subtitle preference")
            end
            Engine.ExecNow("updategamerprofile", controller)
        end)
    end)
end
