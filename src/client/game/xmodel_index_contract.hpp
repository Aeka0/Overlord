#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace game::xmodel_index_contract
{
	inline constexpr std::size_t pool_header=8, pool_stride=696;
	inline constexpr std::uintptr_t getter=0x140413D90, mapping_boundary=getter+7;
	// movss xmm0,[r8+rcx*4+disp32] / movss [r8+rcx*4+disp32],xmm0.
	// Byte 5 is the SIB, NOT the displacement. Patching at +5 corrupts both the
	// address calculation and the high displacement byte (off-frustum path).
	inline constexpr std::size_t table_displacement=6, table_instruction_size=10;
	inline constexpr std::array<std::uint8_t,6> table_load{0xf3,0x41,0x0f,0x10,0x84,0x88};
	inline constexpr std::array<std::uint8_t,6> table_store{0xf3,0x41,0x0f,0x11,0x84,0x88};
	inline bool relocate_table(std::span<std::uint8_t> bytes,bool store,std::uint32_t rva) noexcept
	{
		if (bytes.size()!=table_instruction_size) return false;
		const auto& prefix=store ? table_store : table_load;
		for (std::size_t n=0;n<prefix.size();++n) if (bytes[n]!=prefix[n]) return false;
		for (std::size_t n=0;n<4;++n) bytes[table_displacement+n]=std::uint8_t(rva>>(8*n));
		return true;
	}
}
