#pragma once
#include <array>
#include <cstdint>

namespace vr::gameplay::hands
{
	// Static assembly identity, separate from the native skeleton timestamp and
	// dynamic controller/weapon ownership. Float payloads use exact bit copies so
	// even a rejected NaN bind has a stable, cacheable identity.
	struct rig_model_identity
	{
		std::uintptr_t model{},name{},bone_names{},parents{},bind{},bone_info{},materials{},surfaces{};
		std::array<std::uint32_t,6> bounds{};
		unsigned bones{},roots{},surface_count{},lods{},lod_surfaces{},lod_offset{},attachment{};
		bool operator==(const rig_model_identity&) const = default;
	};
	struct rig_bone_identity
	{
		std::uint32_t name{},parent_distance{};
		std::array<std::uint32_t,7> bind{};
		bool operator==(const rig_bone_identity&) const = default;
	};
	struct rig_binding_identity
	{
		std::uintptr_t object{},model_array{};
		std::uint64_t resource{},assets{};
		std::uint32_t duplicate_parts{},admission{};
		unsigned model_count{},bone_count{};
		bool empty{};
		std::array<rig_model_identity,32> models{};
		std::array<rig_bone_identity,256> bones{};
		bool operator==(const rig_binding_identity&) const = default;
	};
	// One bounded entry belongs to each solver. Success and rejection are both
	// retained until their assembly or admission conditions change. The owner
	// keeps the static binding result; no mutable solver/gesture state is copied.
	class rig_binding_cache
	{
	public:
		bool matches(const rig_binding_identity& value) const noexcept {return populated_ && identity_==value;}
		bool accepted() const noexcept {return accepted_;}
		void store(const rig_binding_identity& value,bool accepted) noexcept
		{identity_=value;accepted_=accepted;populated_=true;}
		void invalidate() noexcept {populated_=accepted_=false;}
	private:
		rig_binding_identity identity_{};
		bool populated_{},accepted_{};
	};
}
