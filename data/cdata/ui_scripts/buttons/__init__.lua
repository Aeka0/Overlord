LUI.addmenubutton("main_campaign", {
	index = 6,
	text = "@MENU_MODS",
	description = Engine.Localize("@MENU_MODS_DESC"),
	callback = function()
		LUI.FlowManager.RequestAddMenu(nil, "mods_menu")
	end
})

LUI.addmenubutton("main_campaign", {
	index = 6,
	text = "@LUA_MENU_ACHIEVEMENTS",
	description = Engine.Localize("@LUA_MENU_ACHIEVEMENTS_DESC"),
	callback = function()
		LUI.FlowManager.RequestAddMenu(nil, "achievements_menu")
	end
})

local maincampaign = LUI.MenuBuilder.m_types_build["main_campaign"]
LUI.MenuBuilder.m_types_build["main_campaign"] = function(...)
    local initlist = LUI.Options.InitScrollingList
    LUI.Options.InitScrollingList = function(list) 
    	initlist(list, nil ,{
            rows = 10
        })
    end

    local ok, menu = pcall(maincampaign, ...)
    LUI.Options.InitScrollingList = initlist
    if not ok then
        -- A failed native menu build must not leave this temporary wrapper on
        -- the shared function, stacking it again on the next open attempt.
        error(menu, 0)
    end
    return menu
end
