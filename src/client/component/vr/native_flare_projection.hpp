#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vr::native_flare
{
	// Caller owns this live GfxCmdBufSourceState and has validated the special
	// mode/identity WORLD0 contract. No native call or external lock is acquired.
	class projection_scope
	{
		std::byte* source_;
		std::array<float,64> saved_;
		std::uint8_t depth_hack_bit_;
		void invalidate() const noexcept
		{
			// H2 GetCodeMatrix(374) copies +2C70 to +200, comparing the valid
			// stamp +3168 with input +31EC. Also invalidate dependent WV/WVP.
			// Do NOT increment WORLD0 (+31FA); its identity and valid stamp stay paired.
			for (std::size_t offset : {0x31E8u,0x31EAu,0x31ECu,0x31F6u,0x31FCu,0x31FEu})
			{
				std::uint16_t version{};
				std::memcpy(&version, source_ + offset, sizeof(version)); ++version;
				std::memcpy(source_ + offset, &version, sizeof(version));
			}
		}
	public:
		projection_scope(std::byte* source, const std::array<float,64>& saved,
			const std::array<float,64>& projected, std::uint8_t flags) noexcept
			: source_(source), saved_(saved), depth_hack_bit_(flags & 1)
		{
			std::memcpy(source_ + 0x2BF0, projected.data(), sizeof(projected));
			*reinterpret_cast<std::uint8_t*>(source_ + 0x3340) = flags & ~1u;
			invalidate();
		}
		projection_scope(const projection_scope&) = delete;
		projection_scope& operator=(const projection_scope&) = delete;
		~projection_scope()
		{
			std::memcpy(source_ + 0x2BF0, saved_.data(), sizeof(saved_));
			auto& flags = *reinterpret_cast<std::uint8_t*>(source_ + 0x3340);
			flags = (flags & ~1u) | depth_hack_bit_;
			// Native matrix/constant caches may now contain the eye projection.
			// Restore inputs, advance versions; never restore old GPU-cache stamps.
			invalidate();
		}
	};
}
