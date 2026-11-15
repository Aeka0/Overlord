#pragma once
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

namespace vr::engine_stereo_owner_pass
{
	// Native callback contract implemented by the exact stereo transaction owner.
	[[nodiscard]] bool begin_sun_shadow(void* data, void* record) noexcept;
	void end_sun_shadow(void* data, void* record) noexcept;
}

namespace vr::engine_stereo_sun_shadow
{
	// H2's observed synchronous, uncached depth-only sun contract. Other modes
	// retain native execution. No across-frame history or camera matrices here.
	struct inputs
	{
		std::uintptr_t data{}, resource{};
		std::array<std::uint8_t, 0x104> sun_visibility{};
		std::array<std::array<std::uint8_t, 0x130>, 2> lists{};
		std::array<std::array<std::uint8_t, 0x180>, 2> partitions{};
		bool operator==(const inputs& other) const noexcept
		{
			return data == other.data && resource == other.resource && sun_visibility == other.sun_visibility &&
				lists == other.lists && partitions == other.partitions;
		}
	};
	struct range
	{
		inputs left;
		std::uint32_t begin{}, end{};
		bool open{}, valid{};
		bool accepts(const inputs& right, std::uint32_t cursor, std::uint32_t left_count, bool dirty) const noexcept
		{
			return valid && !open && !dirty && end > begin && cursor == begin && end <= left_count && left == right;
		}
	};
	struct counters { std::uint64_t left{}, skipped{}, refused{}, dirty{}, saved_boundaries{}; };
	void install() noexcept;
	bool snapshot(void* data, void* record, inputs& output,
		Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture) noexcept;
	void watch(ID3D11Resource* resource, ID3D11DeviceContext* context) noexcept;
	void unwatch() noexcept;
	void finish_left() noexcept;
	bool watching(std::uintptr_t resource, ID3D11DeviceContext* context) noexcept;
	bool modified() noexcept;
	void note_left() noexcept;
	void note_skip(std::uint32_t boundaries) noexcept;
	void note_refusal() noexcept;
	counters get_counters() noexcept;
}
