#pragma once
#include <array>
#include <cstdint>

namespace vr::game_view
{
	// Portable camera contract: no map, engine addresses, VM or runtime APIs.
	enum class head_rotation { fixed, pitch_roll, limited, free, command_delta };
	enum class head_translation { fixed, attenuated, tracked };
	enum class script_rotation { ignore, override_view, additive };
	enum class script_axes { yaw, all, yaw_roll };
	enum class rotation_source { view, tag };
	enum class camera_entry { preserve, align };
	enum class camera_owner { player, script };
	enum class scripted_camera_tag { none, player, aim, origin };
	struct camera_limits
	{
		// Degrees relative to the physical pose on entry: pitch, yaw, roll.
		std::array<float,3> minimum{{-90,-180,-180}},maximum{{90,180,180}};
	};
	struct camera_policy
	{
		camera_owner owner{camera_owner::player};
		head_rotation head{head_rotation::free};
		head_translation translation{head_translation::tracked};
		script_rotation script{script_rotation::ignore};
		script_axes axes{script_axes::yaw};
		rotation_source source{rotation_source::view};
		camera_entry entry{camera_entry::preserve};
		camera_limits limits{};
		float translation_gain{.1f},translation_limit{.05f}; // metres
		constexpr bool owns_rotation() const noexcept {return owner==camera_owner::script;}
		constexpr bool needs_tag() const noexcept
		{return source==rotation_source::tag && (script!=script_rotation::ignore || entry==camera_entry::align);}
		bool operator==(const camera_policy& other) const noexcept
		{return owner==other.owner && head==other.head && translation==other.translation && script==other.script && axes==other.axes &&
			source==other.source && entry==other.entry && limits.minimum==other.limits.minimum && limits.maximum==other.limits.maximum &&
			translation_gain==other.translation_gain && translation_limit==other.translation_limit;}
		bool operator!=(const camera_policy& other) const noexcept {return !(*this==other);}
	};
	struct camera_request
	{
		camera_policy policy{};
		std::uint64_t epoch{},position_epoch{},entry_epoch{};
	};
	// Consume the first valid post-load observation, not a later story entry.
	// Missing tracking/VM data leaves the load pending; ordinary gameplay
	// consumes it without recentering so a later scene cannot inherit the reset.
	class load_recenter
	{
		std::uint64_t observed_{};
	public:
		bool consume(std::uint64_t generation,bool ready,bool scripted)noexcept
		{
			if(!generation || !ready || generation==observed_)return false;
			observed_=generation;return scripted;
		}
	};
	struct scripted_rotation_reference
	{
		std::uint64_t source{}; // Native linked-parent lifetime, not tracking generation.
		std::array<std::array<float,3>,3> axis{};
	};
	inline const char* name(scripted_camera_tag tag) noexcept
	{return tag==scripted_camera_tag::player?"tag_player":tag==scripted_camera_tag::aim?"tag_aim":tag==scripted_camera_tag::origin?"tag_origin":"none";}
	inline const char* name(head_rotation mode) noexcept
	{return mode==head_rotation::fixed?"fixed":mode==head_rotation::pitch_roll?"pitch_roll":mode==head_rotation::limited?"limited":mode==head_rotation::command_delta?"command_delta":"free";}
	inline const char* name(head_translation mode) noexcept
	{return mode==head_translation::fixed?"fixed":mode==head_translation::attenuated?"attenuated":"tracked";}
	inline const char* name(script_rotation mode) noexcept
	{return mode==script_rotation::override_view?"override":mode==script_rotation::additive?"additive":"ignore";}
	inline const char* name(script_axes axes) noexcept {return axes==script_axes::yaw?"yaw":axes==script_axes::yaw_roll?"yaw_roll":"all";}
	inline const char* name(rotation_source source) noexcept {return source==rotation_source::tag?"tag":"view";}
	inline const char* name(camera_entry entry) noexcept {return entry==camera_entry::align?"align":"preserve";}
	inline const char* name(const camera_policy& policy) noexcept {return name(policy.head);}
	// Scene adapters select compositions; transforms belong to camera_rig.
	namespace camera_profiles
	{
		// Entry alignment is independent of ongoing rotation ownership and
		// script motion. Tag is the default so cuts cannot inherit clamped or
		// HMD-composed native player angles from the previous shot.
		inline constexpr camera_policy align_on_entry(camera_policy policy,rotation_source source=rotation_source::tag) noexcept
		{policy.entry=camera_entry::align;policy.source=source;return policy;}
		inline constexpr camera_policy gameplay{};
		inline constexpr camera_policy free{camera_owner::script,head_rotation::free,head_translation::attenuated};
		inline constexpr camera_policy aligned{camera_owner::script,head_rotation::free,head_translation::attenuated,
			script_rotation::ignore,script_axes::yaw,rotation_source::view,camera_entry::align};
		inline constexpr camera_policy locked{camera_owner::script,head_rotation::fixed,head_translation::fixed,
			script_rotation::override_view,script_axes::all};
		inline constexpr camera_policy yaw{camera_owner::script,head_rotation::free,head_translation::attenuated,
			script_rotation::override_view,script_axes::yaw};
		inline constexpr camera_policy vehicle{camera_owner::script,head_rotation::free,head_translation::attenuated,
			script_rotation::additive,script_axes::yaw,rotation_source::view,camera_entry::align};
		inline constexpr camera_policy authored{camera_owner::script,head_rotation::free,head_translation::attenuated,
			script_rotation::additive,script_axes::yaw,rotation_source::tag};
		inline constexpr auto authored_aligned=align_on_entry(authored);
		inline constexpr camera_policy authored_full{camera_owner::script,head_rotation::free,head_translation::attenuated,
			script_rotation::additive,script_axes::all,rotation_source::tag,camera_entry::align};
		inline constexpr camera_policy authored_yaw_roll{camera_owner::script,head_rotation::free,head_translation::attenuated,
			script_rotation::additive,script_axes::yaw_roll,rotation_source::tag,camera_entry::align};
		inline constexpr camera_policy bounded{camera_owner::script,head_rotation::limited,head_translation::attenuated,
			script_rotation::override_view,script_axes::yaw};
		inline constexpr camera_policy fixed_scope{camera_owner::script,head_rotation::fixed,head_translation::fixed,
			script_rotation::override_view,script_axes::all};
		inline constexpr camera_policy remote_control{camera_owner::script,head_rotation::command_delta,head_translation::fixed,
			script_rotation::override_view,script_axes::all};
	}
}
