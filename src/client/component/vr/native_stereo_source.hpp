#pragma once
#include "component/d3d11.hpp"
#include "callback_registration.hpp"
#include <string>

namespace vr::native_stereo_source
{
	enum class phase
	{
		waiting,
		ready,
		failed
	};
	// Copied renderer-owned content proof. Runtime adapters do not inspect H2
	// records or assume a readable texture proves owner/thread/generation safety.
	struct proof
	{
		phase state{};
		D3D11_TEXTURE2D_DESC source{};
		std::uint64_t generation{};
		std::uintptr_t context{};
		std::uint32_t owner_thread{};
		std::string error;
	};
	using query = proof (*)();
	using registration = callback_registration<query>;
	registration register_query(query);
	proof current();
}
