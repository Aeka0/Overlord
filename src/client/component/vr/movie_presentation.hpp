#pragma once

namespace vr::menu_surface
{
	// Engine.IsVideoPlaying uses R_Cinematic_IsStarted OR the named-video
	// predicate. An unacknowledged stop/replacement invalidates the old name;
	// the pending request itself is not proof of active playback.
	inline bool native_video_playing(bool started,bool named,bool pending,
		unsigned requested,unsigned acknowledged) noexcept
	{
		return started || (named && !(pending && requested!=acknowledged));
	}
	inline bool movie_theater(bool enabled,bool video,bool frontend,bool scene,
		bool fresh_menu,unsigned menus,bool briefing,bool native_fullscreen) noexcept
	{
		if(!enabled||!video)return false;
		// Native briefing can be a modal popup while the old map is initialized.
		// Its explicit owner wins over both scene availability and menu count.
		if(fresh_menu&&briefing)return true;
		if(frontend&&fresh_menu&&menus)return false;
		// A scene may play a cinematic as a world material (Gulag monitors).
		// Native CG's fullscreen draw gate, not the previous theater state,
		// decides whether that playback owns the screen once a scene exists.
		return !scene||native_fullscreen;
	}
}
