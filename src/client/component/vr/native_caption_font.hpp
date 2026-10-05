#pragma once
#include <memory>
#include <span>
#include <cstddef>

namespace vr::native_caption_font
{
	// Owns a private GDI face made from the game's loaded TTF bytes. Shared
	// leases keep unload from removing the resource during a caption draw.
	struct face
	{
		face()=default;
		face(const face&)=delete;
		face& operator=(const face&)=delete;
		void* resource{};
		~face();
		const wchar_t* family()const noexcept{return L"BankGothic Md BT";}
	};
	std::shared_ptr<const face> make_bank_face(std::span<const std::byte>) noexcept;
	std::shared_ptr<const face> bank() noexcept;
}
