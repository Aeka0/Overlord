-- Paused with the native MOTD service; retain these modules for later restoration.
if not motd.isenabled or not motd.isenabled() then return end

require("motd")
require("featured")
