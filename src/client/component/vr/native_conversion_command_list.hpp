#pragma once

#include <d3d11.h>

#include <cstdint>

namespace vr::native_conversion_command_list
{
	// Private metadata distinguishes the mod's known, state-preserving color
	// conversion lists (including localized spatial-panel composition) from arbitrary lists submitted by H2 or another
	// component. The marker never changes D3D execution or resource ownership.
	inline constexpr GUID marker_guid{
		0xC6152B34, 0xD803, 0x4C93,
		{0xAB, 0x01, 0xB2, 0x85, 0x24, 0x27, 0xDA, 0x34}};
	inline constexpr std::uint64_t marker_value = 0x48325652434C3031ull;
	inline constexpr GUID recording_marker_guid{
		0x4391A304, 0xE816, 0x43DF,
		{0x9D, 0xC9, 0x73, 0xA1, 0xBA, 0x6B, 0x77, 0x21}};

	// Only private deferred contexts recording these known conversion lists may
	// carry this marker. Never mark H2's immediate context or a borrowed context.
	[[nodiscard]] inline bool mark_recording_context(ID3D11DeviceContext* context) noexcept
	{
		return context && context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED &&
			SUCCEEDED(context->SetPrivateData(recording_marker_guid,sizeof(marker_value),&marker_value));
	}
	[[nodiscard]] inline bool is_recording_context(ID3D11DeviceContext* context) noexcept
	{
		if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED)return false;
		std::uint64_t value{};UINT size=sizeof(value);
		return SUCCEEDED(context->GetPrivateData(recording_marker_guid,&size,&value)) &&
			size==sizeof(value) && value==marker_value;
	}

	[[nodiscard]] inline bool mark(ID3D11CommandList* const commands) noexcept
	{
		return commands != nullptr && SUCCEEDED(commands->SetPrivateData(marker_guid,
			static_cast<UINT>(sizeof(marker_value)), &marker_value));
	}

	[[nodiscard]] inline bool is_marked(
		ID3D11CommandList* const commands) noexcept
	{
		if (commands == nullptr) return false;
		std::uint64_t value{};
		UINT size = sizeof(value);
		return SUCCEEDED(commands->GetPrivateData(marker_guid, &size, &value)) &&
			size == sizeof(value) && value == marker_value;
	}
}
