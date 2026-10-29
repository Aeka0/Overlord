#pragma once
#include <cstdint>
#include <string>

namespace vr::gameplay::first_person_mesh_size
{
	void publish(const void* object, const void* matrices, std::uint32_t epoch, float size) noexcept;
	float for_object(const void* object) noexcept;
	enum class packet_data { all, inverse_bind, surface };
	// SkinSceneDObj still owns this local packet. Replace copied geometry and
	// inverse bind data before native rigid/CPU/GPU skin work is published.
	bool prepare_packet(void* packet, float size, packet_data data=packet_data::all);
	void clear_after_drain() noexcept;
	std::string status();
}
