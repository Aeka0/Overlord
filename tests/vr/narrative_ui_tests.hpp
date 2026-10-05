#pragma once
#include "component/vr/narrative_ui.hpp"
#include "component/vr/native_waypoints.hpp"
#include "component/vr/remote_hud_policy.hpp"

namespace narrative_ui_tests
{
	// Live ending checkpoint retry, 2026-09-13: native StretchPic black overlay.
	// The process-specific material pointer and unused padding are zeroed.
	inline std::array<std::uint8_t, 56> ending_fade()
	{
		return {{0x38,0,10,0, 0,0,0,0, 0,0,0,0,0,0,0,0,
			0,0,0,0, 0,0,0,0, 0,0xc0,0xcc,0x44, 0,0x60,0xa3,0x44,
			0,0,0,0, 0,0,0,0, 0,0,0x80,0x3f, 0,0,0x80,0x3f,
			0xff,0xff,0xff,0xff, 0,0,0,0}};
	}
	template<class Check> void run(Check& check)
	{
		using namespace vr::narrative_ui;
		{
			vr::spatial_panel::projected_quad left{},right{};
			check(vr::remote_hud::project({1,.75f},{-1.2f,.8f,-.9f,.9f},left) &&
				vr::remote_hud::project({1,.75f},{-.8f,1.2f,-.9f,.9f},right) &&
				std::abs((left[0][0]+left[1][0])*.5f-.2f)<.0001f &&
				std::abs((right[0][0]+right[1][0])*.5f+.2f)<.0001f,
				"native UAV canvas center follows each asymmetric optical projection instead of centering the crosshair in each eye texture");
			check(!vr::remote_hud::project({0,.75f},{-1,1,-1,1},left) &&
				!vr::remote_hud::project({1,.75f},{1,-1,-1,1},left),"invalid UAV framing fails closed");
			check(vr::remote_hud::material("remotemissile_infantry_target_colorblind") &&
				vr::remote_hud::material("h2_overlays_predator_graded_bar_side") && !vr::remote_hud::material("hud_javelin_bg"),
				"UAV target and instrument capture includes native accessibility materials without taking other weapon displays");
			check(!vr::remote_hud::material("h1_ac130_screen_overlay") &&
				vr::remote_hud::target_material("remotemissile_infantry_target") &&
				!vr::remote_hud::instrument_material("remotemissile_infantry_target"),
				"remote vignette is omitted and world target boxes cannot enter the enlarged instrument layer");
			const auto large=vr::remote_hud::instrument_tangents(1920,1080);
			check(large[0]>.83f && std::abs(large[0]/large[1]-1920.f/1080.f)<.0001f,
				"remote instruments use a readable zoom-independent canvas without stretching their aspect");
			check(vr::remote_hud::project({.25f,.14f},{-1,1,-1,1},left) &&
				vr::remote_hud::project(large,{-1,1,-1,1},right) && right[1][0]-right[0][0]>3*(left[1][0]-left[0][0]),
				"narrow native missile projection keeps target placement while instruments expand independently");
		}
		{
			// Captured tutorial borders/text were in a 1639x1351 canvas. The
			// flagged blur alone was scaled again to x=316..2212, y=620..749.
			const backdrop source{{205.f,310.f,1434.25f,374.02344f},.9f};
			const auto aligned=normalize_backdrop(source,1639,1351);
			check(valid_backdrop(source) && std::abs(aligned.bounds[0]-205.f/1639)<.00001f &&
				std::abs(aligned.bounds[1]-310.f/1351)<.00001f && aligned.alpha==.9f,
				"hint backdrop uses text-space geometry before native blur rescales it");
			check(675.f/1639>aligned.bounds[0] && 356.f/1351>aligned.bounds[1] && 356.f/1351<aligned.bounds[3],
				"captured tutorial text lies inside its correctly placed background");
			auto invalid=source;invalid.pixels[2]=invalid.pixels[0];
			check(!valid_backdrop(invalid) && normalize_backdrop(source,0,1351).alpha==0,"empty rectangles and missing canvas fail safely");
			invalid=source;invalid.alpha=std::numeric_limits<float>::quiet_NaN();
			check(!valid_backdrop(invalid),"nonfinite backdrop alpha is rejected");
			invalid=source;invalid.pixels[0]=40000;
			check(!valid_backdrop(invalid),"unbounded backdrop positions are rejected");
			vr::native_waypoints::group owned;owned.begin=1000;owned.end=1200;owned.narrative=true;owned.hint_backdrop=source;
			const std::array owned_groups{owned};
			check(vr::native_waypoints::owns_hint_backdrop(owned_groups,1008,104) &&
				!vr::native_waypoints::owns_hint_backdrop(owned_groups,1190,104) &&
				!vr::native_waypoints::owns_hint_backdrop(owned_groups,std::numeric_limits<std::uintptr_t>::max()-2,104),
				"only a complete command within validated backdrop ownership can replace a flat blur");
			owned.hint_backdrop.reset();
			check(!vr::native_waypoints::owns_hint_backdrop(std::array{owned},1008,104),
				"ordinary script text ownership cannot suppress unrelated blur effects");
			check(hint_blur_material("h1_hud_tutorial_blur") && hint_border_material("h1_hud_tutorial_border") &&
				!hint_blur_material("h1_hud_weapwidget_blur") && !hint_border_material("white"),
				"backdrop routing is limited to native tutorial materials");
			check(subtractive_script_ink("h1_hud_tutorial_border",true)&&
				!subtractive_script_ink("h1_hud_tutorial_border",false)&&
				subtractive_script_ink("h2_hud_ssdd_results_line",true)&&subtractive_script_ink("h1_hud_fng_results_border",true)&&
				hint_blur_material("h2_hud_ssdd_results_blur"),
				"captured tutorial and result backing share explicit native material ownership");
		}
		const std::vector<std::string> labels{"Download progress", " files", "\xe5\x89\xa9\xe4\xbd\x99\xe6\x97\xb6\xe9\x97\xb4", "\xe5\x88\x86\xe9\x92\x9f"};
		const std::vector<std::string> native_formats{"&&1Mb/s","&&1/2067","\x26\x26\x31\xe5\x88\x86","\x26\x26\x31\xe7\xa7\x92"};
		check(progress_text("0.85Mb/s",native_formats) && progress_text("800/2067",native_formats) && progress_text("\x32\xe5\x88\x86",native_formats),"captured DSM numeric replacement templates retain speed count and time on progress plane");
		check(!progress_text("Mb/s",native_formats) && !progress_text("badMb/s",native_formats),"numeric label templates require an actual numeric value");
		check(progress_text("^3Download progress^7",labels) && progress_text("280 files",labels) && progress_text("\x32\xe5\x88\x86\xe9\x92\x9f",labels),"native progress labels support colored English and UTF-8 numeric values");
		check(!progress_text("We need more files",labels) && !progress_text("Download progress is slow",labels),"dialogue mentioning a download is not classified as progress HUD");
		check(layout(channel::progress).distance_meters<layout(channel::story).distance_meters && layout(channel::progress).lower_fraction>0,"mission progress has a lower and nearer stereo plane");
		check(layout(channel::announcement).distance_meters<layout(channel::story).distance_meters &&
			layout(channel::announcement).distance_meters>layout(channel::progress).distance_meters &&
			layout(channel::announcement).lower_fraction==layout(channel::story).lower_fraction,
			"opening announcements move nearer without shifting the native subtitle/title layout");
		vr::native_waypoints::group tagged;tagged.begin=1000;tagged.end=1800;tagged.narrative=tagged.progress=true;
		const std::array groups{tagged};
		check(vr::native_waypoints::owner(groups,1000,240)==vr::native_waypoints::text_owner::progress &&
			vr::native_waypoints::owner(groups,1240,240)==vr::native_waypoints::text_owner::progress,"native label and separate dynamic value inherit the same element depth");
		check(vr::native_waypoints::owner(groups,1790,20)==vr::native_waypoints::text_owner::none &&
			vr::native_waypoints::owner(groups,std::numeric_limits<std::uintptr_t>::max()-2,240)==vr::native_waypoints::text_owner::none,"neighboring commands and overflow cannot inherit script HUD ownership");
		std::array<std::uint8_t, 256> text{};
		text[2] = 20;
		auto flags = [&](std::uint32_t value) { std::memcpy(text.data()+56, &value, 4); };
		flags(0x10);
		check(!text_command(text.data(), text.size()), "observed Chinese pickup hint is not a subtitle");
		flags(0);
		check(!text_command(text.data(),text.size()) && text_command(text.data(),text.size(),true),
			"captured death reason and hint have zero flags and require exact native producer ownership");
		flags(0x2014);
		check(!text_command(text.data(), text.size()), "observed native ammo text is not narrative UI");
		flags(0x1c);
		check(!text_command(text.data(),text.size()) && text_command(text.data(),text.size(),true),
			"flat rappel hint style requires exact authored narrative range ownership");
		text[3]=64;
		check(!text_command(text.data(),text.size(),true),"owned hint still excludes flagged blur pass");
		text[3]=0;
		flags(0x400);
		check(!text_command(text.data(), text.size()), "frontend subtitle style is not its backend marker");
		flags(0x110);
		check(text_command(text.data(), text.size()), "native subtitle marker retains glow flag");
		check(!announcement_command(text.data(),text.size()),"dialogue subtitles stay on their original plane");
		flags(0x1d0);
		check(!announcement_command(text.data(),text.size()),"subtitle ownership wins over simultaneous typewriter styling");
		flags(0xd0);
		check(text_command(text.data(), text.size()), "native typewriter marker retains glow flag");
		check(announcement_command(text.data(),text.size()),"native mission-opening typewriter text selects the nearer plane");
		{
			using namespace vr::native_waypoints;
			group table;table.begin=1000;table.end=1800;table.narrative=table.panel=table.script_ink=true;
			const std::array table_groups{table};
			const auto source=owner(table_groups,1000,text.size());
			check(script_panel("trainer") && !script_panel("estate") && source==text_owner::panel &&
				!announcement(source,text.data(),text.size()),"trainer pulsing scores remain on the same plane as ordinary labels and time strings");
			check(announcement(text_owner::story,text.data(),text.size()) &&
				announcement(text_owner::none,text.data(),text.size()),"ordinary mission titles outside the script panel retain their announcement plane");
			check(script_ink_material("h2_hud_ssdd_results_line") && !script_ink_material("white") &&
				owns_script_ink(table_groups,1240,56) && !owns_script_ink(table_groups,1790,56) &&
				!owns_script_ink(table_groups,std::numeric_limits<std::uintptr_t>::max()-2,56),
				"native result separators are ink only inside their complete allocator-owned command range");
			table.narrative=false;
			check(!owns_script_ink(std::array{table},1240,56),"unowned LUI images cannot enter the result canvas");
		}
		flags(0x90);
		check(!announcement_command(text.data(),text.size()),"partial pulse flags cannot move ordinary HUD text");
		flags(0xd0);
		text[3] = 64;
		check(!text_command(text.data(), text.size()), "flagged blur pass is not narrative ink");
		check(!announcement_command(text.data(),text.size()),"announcement classification excludes flagged blur pass");
		text[3] = 0;
		check(!text_command(nullptr, 256) && !text_command(text.data(), 232) &&
			!text_command(text.data(), 65536), "bounded narrative header parsing");
		check(text_command(text.data(), 65535), "long UTF-8 command retains native 16-bit length capacity");
		float nan = std::numeric_limits<float>::quiet_NaN(); std::memcpy(text.data()+4, &nan, 4);
		check(!text_command(text.data(), 256), "nonfinite native text position rejected");
		check(black_material("black", 0xffffffff) && black_material("white", 0x80000000),
			"black image uses native white tint while white image requires black tint");
		check(!black_material("white", 0x80000001) && !black_material("black", 0xffffff) &&
			!black_material("vignette", 0xff000000), "colored translucent or textured UI is not a black fade");
		auto legacy = ending_fade();
		vr::native_hud_quad::quad decoded{};
		check(fade_quad_command(legacy.data(),legacy.size(),"white",decoded) &&
			fade_color("white",decoded.color)==vr::spatial_panel::vec4{1,1,1,1},
			"Second Sun white overlay retains white RGB as well as native coverage");
		check(!fade_quad_command(legacy.data(),legacy.size(),"vignette",decoded),
			"textured fullscreen HUD cannot be extended as a uniform fade");
		vr::spatial_panel::vec4 mixed{};
		append_fade(mixed,fade_color("white",0x80ffffff));
		append_fade(mixed,fade_color("black",0x80ffffff));
		check(std::abs(mixed[0]-(128.f/255.f)*(127.f/255.f))<.00001f &&
			std::abs(mixed[3]-(1-(127.f/255.f)*(127.f/255.f)))<.00001f,
			"overlapping black and white fades preserve native draw order");
		check(black_quad_command(legacy.data(), legacy.size(), "black", decoded) &&
			decoded.color == 0xffffffff && covers_viewport(decoded.vertices, 0,0,1638,1307),
			"observed legacy opening fade is admitted and covers its native viewport");
		legacy[51] = 206;
		std::memcpy(legacy.data()+52, &nan, sizeof(nan));
		check(black_quad_command(legacy.data(), legacy.size(), "black", decoded) &&
			decoded.color == 0xceffffff && covers_viewport(decoded.vertices, 0,0,1638,1307),
			"legacy fade reads alpha at 48 and ignores StretchPic padding at 52");
		legacy[2] = 12;
		check(!black_quad_command(legacy.data(), legacy.size(), "black", decoded),
			"rotated format must validate the angle where StretchPic has padding");
		legacy[2] = 10; legacy[3] = 64;
		check(!black_quad_command(legacy.data(), legacy.size(), "black", decoded), "flagged quad is not narrative ink");
		legacy[3] = 0; legacy[51] = 0;
		check(!black_quad_command(legacy.data(), legacy.size(), "black", decoded), "finished native fade is no longer selected");
		legacy[51] = 255;
		check(!black_quad_command(legacy.data(), 55, "black", decoded) &&
			!black_quad_command(nullptr, 56, "black", decoded), "truncated and absent native fades rejected");
		float bar_height = 100; std::memcpy(legacy.data()+28, &bar_height, sizeof(bar_height));
		check(black_quad_command(legacy.data(), legacy.size(), "black", decoded) &&
			!covers_viewport(decoded.vertices, 0,0,1638,1307), "legacy letterbox cannot become full-eye black");
		for (const unsigned op : {16u,17u})
		{
			std::array<std::uint8_t,104> raw{};
			const std::uint16_t length = op == 16 ? 72 : 104;
			std::memcpy(raw.data(), &length, sizeof(length)); raw[2] = static_cast<std::uint8_t>(op);
			const std::array<std::array<float,2>,4> corners{{{0,0},{1638,0},{1638,1307},{0,1307}}};
			for (unsigned i=0;i<4;++i) std::memcpy(raw.data()+16+i*(op == 16 ? 8 : 16), corners[i].data(), 8);
			const std::uint32_t tint = 0x8c000000; std::memcpy(raw.data()+length-8, &tint, sizeof(tint));
			check(black_quad_command(raw.data(), length, "white", decoded) && decoded.color == tint &&
				covers_viewport(decoded.vertices, 0,0,1638,1307), "XY and LUI fades retain their own vertex stride and color offset");
		}
		std::array<std::array<float, 2>, 4> vertices{{{0,0},{1920,0},{1920,1080},{0,1080}}};
		check(covers_viewport(vertices, 0,0,1920,1080), "native full-frame fade coverage");
		vertices = {{{-1,-1},{1921,-1},{1921,1081},{-1,1081}}};
		check(covers_viewport(vertices, 0,0,1920,1080), "native overscan fade still covers the full viewport");
		vertices[2][1] = vertices[3][1] = 100;
		check(!covers_viewport(vertices, 0,0,1920,1080), "letterbox bar cannot black the headset");
		vertices = {{{960,0},{1920,540},{960,1080},{0,540}}};
		check(!covers_viewport(vertices, 0,0,1920,1080), "diamond AABB is not full-field coverage");
		check(current(1000, 1250) && !current(1000, 1251) && !current(1000, 999) && !current(0, 0),
			"stalled or backwards timestamps cannot retain transient narrative UI");
	}
}
