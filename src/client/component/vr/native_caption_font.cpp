#include <std_include.hpp>
#include "native_caption_font.hpp"
#pragma comment(lib,"gdi32.lib")

namespace vr::native_caption_font
{
	face::~face(){if(resource)RemoveFontMemResourceEx(resource);}
	std::shared_ptr<const face> make_bank_face(std::span<const std::byte> bytes) noexcept
	try
	{
		if(bytes.empty() || bytes.size()>4*1024*1024)return {};
		auto result=std::make_shared<face>();DWORD count{};
		result->resource=AddFontMemResourceEx(const_cast<std::byte*>(bytes.data()),DWORD(bytes.size()),nullptr,&count);
		return result->resource && count?std::move(result):nullptr;
	}
	catch(...){return {};}
}
