#include <std_include.hpp>
#include "world_interaction.hpp"
#include "world_interaction_prompt.hpp"
#include "../hud_prompts.hpp"
#include "../native_waypoints.hpp"
#include <utils/native_memory.hpp>
#include "../engine_stereo_bridge.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::interaction
{
	namespace
	{
		std::atomic_uint64_t prompt_frames{},target_frames{},stale_frames{},prepared{},recorded{},rejected{};
		struct weapon_prompt {game::Material* icon{};int ratio{};std::string name,attachments;};
		template<class T> T asset_field(const void* asset,std::size_t offset)
		{
			T out{};
			if (!utils::native_memory::read_bytes(&out,static_cast<const std::byte*>(asset)+offset,sizeof(out))) return {};
			return out;
		}
		std::string asset_string(const char* source)
		{
			if (!source) return {};
			std::string out;out.reserve(64);
			for (unsigned i=0;i<256;++i)
			{
				char c{};
				if (!utils::native_memory::read_bytes(&c,source+i,1)) return {};
				if (!c) return out;
				out+=c;
			}
			return {}; // Do not pass unterminated keys or partial UTF-8 to native UI.
		}
		bool native_icon_layout()
		{
			// Current H2 cursor hint at 0x140386CBC/CF0 selects pickupIcon and
			// pickupRatio, otherwise hudIcon and hudRatio. The legacy WeaponDef
			// declaration has different offsets here (its pickupIcon is a sound).
			constexpr std::uint8_t pickup[]{0x48,0x39,0xb9,0xa0,0x04,0,0,0x74,0x2b,0x8b,0x91,0xc8,0x06,0,0};
			constexpr std::uint8_t hud[]{0x48,0x39,0xb9,0x98,0x04,0,0,0x74,0x3a,0x8b,0x91,0xc4,0x06,0,0};
			std::array<std::uint8_t,sizeof(pickup)> mask{};mask.fill(0xff);
			const bool valid=utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140386CBC),{pickup,mask.data(),sizeof(pickup)}) &&
				utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140386CF0),{hud,mask.data(),sizeof(hud)});
			if (!valid) console::error("[VR interaction] native pickup icon layout rejected\n");
			return valid;
		}
		weapon_prompt prompt_for(std::uint32_t token)
		{
			weapon_prompt out{};
			if (!token || token>=512) return out;
			const auto* definition=game::weapon_defs[token];if (!definition) return out;
			const auto key=asset_string(asset_field<const char*>(definition,8));
			if (!key.empty()) out.name=asset_string(game::UI_SafeTranslateString(key.c_str()));
			// A missing native name stays absent. The shared prompt composer owns
			// the generic item noun and resolves it with the sentence's locale.
			if (out.name==key) out.name.clear();
			const auto text=prompt_text::split(key,out.name,[](std::string_view candidate) {
				const std::string name(candidate);
				const auto* entry=game::DB_FindXAssetHeader(game::ASSET_TYPE_LOCALIZE_ENTRY,name.c_str(),0).localize;
				if (!entry || asset_string(asset_field<const char*>(entry,8))!=name) return std::string{};
				return asset_string(asset_field<const char*>(entry,0));
			});
			out.name=text.name;out.attachments=text.attachments;
			static const bool valid=native_icon_layout();if (!valid) return out;
			out.icon=asset_field<game::Material*>(definition,0x4a0);
			out.ratio=asset_field<int>(definition,out.icon ? 0x6c8 : 0x6c4);
			if (!out.icon) out.icon=asset_field<game::Material*>(definition,0x498);
			if (out.ratio<0 || out.ratio>=game::WEAPON_ICON_RATIO_COUNT) out.icon=nullptr;
			return out;
		}
		void remove_last_character(std::string& text)
		{
			if (text.empty()) return;
			auto end=text.size()-1;
			while (end && (static_cast<unsigned char>(text[end])&0xc0)==0x80) --end;
			text.resize(end);
		}
		void draw_prompts()
		{
			++prompt_frames;
			if (!game::CL_IsCgameInitialized() || !engine_stereo_bridge::is_active() || *game::keyCatchers) return;
			const auto language=game_text::current();
			if(!hud_prompts::owns_world_text(language))return; // Original cursor producer supplies the whole native instruction.
			const auto view=latest();const auto now=controller_input::clock::now();
			if (!view.targets[0] && !view.targets[1]) return;
			++target_frames;
			head_pose_bridge::spatial_frame body;
			if (!view.at.time_since_epoch().count() || now<view.at || now-view.at>150ms ||
				!head_pose_bridge::get_spatial_frame(body) || body.generation!=view.reference) {++stale_frames;return;}
			const auto* placement=game::ScrPlace_GetViewPlacement();if (!placement) return;
			// Reuse the native font cache at the frontend LUI boundary. Never retain a
			// zone-owned font pointer across level changes or create another atlas.
			// Native cursorHintDef uses SP_HudCarbon27: defaultBold, 27 px.
			auto* font=game::R_RegisterFont("fonts/defaultBold.otf",27);if (!font) return;
			for (int h=0;h<2;++h)
			{
				const auto& target=view.targets[h];
				if (!target || (h && target.key==view.targets[0].key)) continue;
				const auto weapon=prompt_for(target.weapon);
				const bool shared=view.targets[0] && view.targets[1] && view.targets[0].key==view.targets[1].key;
				auto item=weapon.name;
				game_text::runs runs;
				const auto specific=target.weapon?std::optional<game_text::key>{}:hud_prompts::world_message(target.hint);
				const auto message=specific?*specific:target.weapon ? game_text::key::interaction_pickup :
					target.prompt==prompt_kind::resupply ? game_text::key::interaction_resupply : game_text::key::interaction_use;
				auto attachments=weapon.attachments;
				// Spaces belong to the translated template, not to the argument
				// boundary. Native width drops trailing blanks, so account for them
				// explicitly while Chinese spans remain adjacent without extra gaps.
				const float gap=float(game::R_TextWidth("x x",3,font)-game::R_TextWidth("xx",2,font));
				if (!std::isfinite(gap) || gap<=0) continue;
				struct measured_run {std::string ink;float leading{},advance{};};
				std::vector<measured_run> measured;
					const auto measure=[&] {
						auto composed=hud_prompts::compose_current(message,language,{h,shared,item});
						if(!composed)return 0.f;
						runs=std::move(composed->parts);
						measured.clear();measured.resize(runs.size());
						float total{};
						for (unsigned i=0;i<runs.size();++i)
						{
							const auto& value=runs[i].value;auto& m=measured[i];
							const auto first=value.find_first_not_of(' '),last=value.find_last_not_of(' ');
							if(first==std::string::npos) {m.advance=float(value.size())*gap;total+=m.advance;continue;}
							m.ink=value.substr(first,last-first+1);m.leading=float(first)*gap;
							const auto ink_width=float(game::R_TextWidth(m.ink.c_str(),int(m.ink.size()),font));
							if(ink_width<0 || !std::isfinite(ink_width))return 0.f;
							m.advance=ink_width+float(first+value.size()-last-1)*gap;total+=m.advance;
					}
					return total;
				};
				auto width=measure();
				// Keep normal names intact. Bound pathological/localized names by
				// glyph width, trimming only at UTF-8 boundaries before adding dots.
				if (width>880)
				{
					auto name=item;
					do {remove_last_character(name);item=name+"...";width=measure();} while (width>880 && !name.empty());
				}
				if (width<=0 || width>880) continue;
				float attachment_width=attachments.empty() ? 0.f : float(game::R_TextWidth(attachments.c_str(),int(attachments.size()),font));
				if (!std::isfinite(attachment_width) || attachment_width<0) continue;
				if (attachment_width>880)
				{
					auto text=attachments;
					do {remove_last_character(text);attachments=text+"...";
						attachment_width=float(game::R_TextWidth(attachments.c_str(),int(attachments.size()),font));
					} while (attachment_width>880 && !text.empty());
				}
				const float scale=std::min(1.f,440.f/std::max(width,attachment_width));
				const float text_width=std::max(width,attachment_width)*scale;
				const float icon_width=weapon.icon ? 48.f*(1<<weapon.ratio) : 0.f;
				directional_ui::waypoint marker;
				marker.world=target.position;marker.world[2]+=.14f*view.units;
				std::copy_n(placement->realViewportSize,2,marker.viewport.begin());
				// This is a texture producer, not a flat-screen world projection.
				// A ground item can project below the native viewport even while it
				// is visible in the headset. Rasterize safely in the source viewport;
				// world_quad later anchors this ink to the actual item in each eye.
				marker.anchor={marker.viewport[0]*.5f,marker.viewport[1]*.5f};
				// Preserve the old label's appearance at 2 m, then keep its visual
				// size as the player approaches. The icon and both text lines share it.
				const float pixels_per_meter=std::max(text_width,icon_width)/(target.weapon ? .48f : .28f);
				if (!directional_ui::set_fixed_visual_size(marker,pixels_per_meter,2.f)) continue;
				const float x=marker.anchor[0]-text_width*.5f,y=marker.anchor[1]+28;
				const float padding=27.f*scale;
				const directional_ui::rectangle bounds{
					std::min(x-padding,marker.anchor[0]-icon_width*.5f),
					weapon.icon ? marker.anchor[1]-48 : y-padding*1.5f,
					std::max(x+text_width+padding,marker.anchor[0]+icon_width*.5f),
					y+padding*.5f+(attachments.empty() ? 0.f : 32.f*scale)};
				++prepared;
				const bool accepted=native_waypoints::draw_world_marker(marker,[&] {
					float white[]{1,1,1,1},yellow[]{1,.85f,0,1};
					if (weapon.icon) game::R_AddCmdDrawStretchPic(marker.anchor[0]-icon_width*.5f,marker.anchor[1]-48,icon_width,48,0,0,1,1,white,weapon.icon);
					auto pen=marker.anchor[0]-width*scale*.5f;
					for (unsigned i=0;i<runs.size();++i)
					{
						const auto& m=measured[i];
						if(!m.ink.empty())game::R_AddCmdDrawText(m.ink.c_str(),int(m.ink.size()),font,
							pen+m.leading*scale,y,scale,scale,0,runs[i].emphasized ? yellow : white,4);
						pen+=m.advance*scale;
					}
					if (!attachments.empty()) game::R_AddCmdDrawText(attachments.c_str(),int(attachments.size()),font,
						marker.anchor[0]-attachment_width*scale*.5f,y+32.f*scale,scale,scale,0,white,4);
				},bounds);
				if (accepted) ++recorded;else ++rejected;
			}
		}
	}
	std::string prompt_status()
	{
		return std::format("prompt_frames={} target_frames={} stale={} prepared={} recorded={} rejected={} source=safe_viewport world=item\n",
			prompt_frames.load(),target_frames.load(),stale_frames.load(),prepared.load(),recorded.load(),rejected.load());
	}
	class hud_component final:public component_interface
	{
		// R_EndFrame's renderer callbacks run after the captured HUD stream's
		// terminator. LUI runs while the frontend command list is still open.
		void post_unpack() override {scheduler::loop(draw_prompts,scheduler::pipeline::lui);}
	};
}
REGISTER_COMPONENT(vr::gameplay::interaction::hud_component)
