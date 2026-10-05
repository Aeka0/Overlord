#pragma once
#include "component/d3d11.hpp"
#include <memory>
#include "directional_ui.hpp"
#include "narrative_ui.hpp"
#include "gameplay/weapon_identity.hpp"
#include "gameplay/weapon_hud_channels.hpp"
#include "ui_canvas.hpp"
#include "remote_hud_policy.hpp"

namespace vr::native_hud_capture
{
	struct frame
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		// Weapon widget's native blur mask, leased with this exact capture.
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> blur_mask;
		spatial_panel::vec4 blur_window{0,0,1,1};
		float blur_alpha{};
		std::uint64_t generation{}, sequence{}, timestamp{};
		std::uint64_t reference_generation{}, rear_revision{}, instance_generation{};
		std::uint64_t screen_scope_epoch{};
		std::uint64_t remote_camera_epoch{};
		std::uint64_t weapon_display_epoch{};
		std::uintptr_t context{};
		std::uint32_t width{}, height{}, weapon{};
		gameplay::weapon_hud::feed channel{};
		std::uint32_t definition{};
		gameplay::weapons::weapon_identity id() const noexcept { return {weapon,instance_generation}; }
		spatial_panel::vec4 fade{}; // narrative: native encoded premultiplied RGBA outside the text canvas
		std::array<narrative_ui::backdrop,spatial_panel::blur_region_capacity> backdrops{};
		unsigned backdrop_count{};
		bool hud_hidden{}; // Narrative texture's visibility policy; reject stale ink after a toggle.
		ui_canvas::mapping canvas; // Optional independent UI target -> original native canvas.
		std::shared_ptr<const frame> subtractive; // Same-stream native backing, before narrative ink.
	};
	inline void release_retired_subtractive(std::span<std::shared_ptr<frame>> slots) noexcept
	{
		// A pool-only frame has no consumer. Drop its companion before trying
		// to reuse the companion pool; otherwise two bounded pools pin each other.
		for(auto& slot:slots)if(slot&&slot.use_count()==1)slot->subtractive.reset();
	}
	// Published immutable capture lease. A producer never overwrites a texture
	// retained by a compositor/consumer. No native ammo or font rebuilding here.
	std::shared_ptr<const frame> latest(gameplay::weapons::weapon_identity weapon={},
		gameplay::weapon_hud::feed channel=gameplay::weapon_hud::feed::primary) noexcept;
	// Empty publication is meaningful: the previous subtitle/title/fade is gone.
	std::shared_ptr<const frame> latest_narrative(narrative_ui::channel channel=narrative_ui::channel::story) noexcept;
	using narrative_frames=std::array<std::shared_ptr<const frame>,static_cast<unsigned>(narrative_ui::channel::count)>;
	// Read text planes and their shared fade from the same completed publication.
	narrative_frames latest_narrative_layers() noexcept;
	std::shared_ptr<const frame> latest_damage() noexcept;
	using remote_frames=std::array<std::shared_ptr<const frame>,remote_hud::frame_count>; // target/instrument x scene/independent UI
	remote_frames latest_remote() noexcept;
	struct screen_scope_frame
	{
		std::shared_ptr<const frame> ink,shadow,flash;
	};
	// One completed native stream; ordered source-over / subtraction / source-over.
	screen_scope_frame latest_screen_scope() noexcept;
	struct indicators
	{
		std::shared_ptr<const frame> warnings, atlas;
		std::array<directional_ui::waypoint, directional_ui::waypoint_capacity> markers{};
		unsigned count{};
	};
	std::shared_ptr<const indicators> latest_indicators() noexcept;
	void set_requested(bool enabled) noexcept;
	struct counters
	{
		std::uint64_t dispatches{}, captures{}, draws{}, rejected{};
		std::uint64_t selections{}, selection_total_us{}, selection_last_us{};
	};
	// Approximate concurrent counters only; no resource readback or owner-thread work.
	counters get_counters() noexcept;
}
