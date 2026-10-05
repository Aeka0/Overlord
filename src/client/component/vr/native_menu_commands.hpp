#pragma once

#include "native_hud_quad.hpp"
#include <algorithm>
#include <span>
#include <string_view>
#include <limits>
#include <vector>

namespace vr::native_menu
{
	struct range { std::uintptr_t begin{},end{};unsigned surface{}; };
	namespace commands
	{
		inline constexpr unsigned excluded_surface=std::numeric_limits<unsigned>::max();
		inline const range* owner(std::span<const range> ranges,std::uintptr_t address,std::size_t size) noexcept
		{
			auto it=std::upper_bound(ranges.begin(),ranges.end(),address,[](auto at,const auto& r){return at<r.begin;});
			if(it==ranges.begin())return nullptr;--it;
			return address<it->end&&size<=it->end-address?&*it:nullptr;
		}
		inline bool vignette_material(std::string_view material) noexcept
		{return material=="h1_ui_bg_vignette";}
		struct emission {range bytes;std::uint64_t fingerprint{};bool growing_lines{};};
		inline bool contains(std::uintptr_t arena,std::uintptr_t end,std::uintptr_t stream) noexcept
		{return arena&&end>arena&&stream>=arena&&stream<end;}
		inline bool lines_2d(const void* data,std::size_t available) noexcept
		{
			using native_hud_quad::field;
			if(available<8||field<std::uint8_t>(data,2)!=25||field<std::uint8_t>(data,3)||field<std::uint8_t>(data,7)!=2)return false;
			const auto count=field<std::uint16_t>(data,4);
			return count&&count<=2047&&field<std::uint16_t>(data,0)==8+32*count&&8+32*count<=available;
		}
		inline void hash_byte(std::uint64_t& hash,unsigned char byte,std::size_t offset,bool growing_lines) noexcept
		{
			// H2 appends line vertices across LUI callbacks, updating only the
			// command size and line count. Payload, flags and mode remain immutable.
			if(growing_lines&&(offset<2||(offset>=4&&offset<6)))return;
			hash^=byte;hash*=1099511628211ull;
		}
		struct selection {std::vector<range> ranges;std::uintptr_t end{};bool valid{};unsigned rejected_command{};};
		template<class Read> selection select(std::uintptr_t begin,std::uintptr_t limit,
			std::span<const emission> records,Read read)
		{
			selection result;
			if(!begin||limit<=begin||limit-begin>0x400000||records.size()>2048)return result;
			for(std::size_t i=0;i<records.size();++i)
				if(records[i].bytes.end<=records[i].bytes.begin||(i&&records[i].bytes.begin<records[i-1].bytes.end))return result;
			std::size_t first{};
			for(auto at=begin;at<limit;)
			{
				std::array<std::uint8_t,8> header{};
				if(limit-at<4||!read(at,header.data(),4))return result;
				const auto size=native_hud_quad::field<std::uint16_t>(header.data(),0);
				if(!header[2]){result.end=at;result.valid=true;return result;}
				if(size<4||size>limit-at)return result;
				const auto end=at+size;
				while(first<records.size()&&records[first].bytes.end<=at)++first;
				if(first<records.size()&&records[first].bytes.begin<end)
				{
					// A finalized native batch can span several emissions. Capture it
					// only when its entire byte range belongs to one menu surface.
					auto covered=at;const auto surface=records[first].bytes.surface;
					for(auto i=first;i<records.size()&&covered<end;++i)
					{
						const auto& r=records[i].bytes;
						if(r.begin>covered||r.surface!=surface)break;
						covered=std::min(end,r.end);
					}
					if(covered!=end)return result;
					if(size>=8&&!read(at+4,header.data()+4,4))return result;
					if(surface!=excluded_surface&&(header[3]||!(native_hud_quad::is_command(header.data(),size)||
						(header[2]==18&&size==120)||(header[2]==20&&size>=233)||lines_2d(header.data(),size))))
					{result.rejected_command=unsigned(header[2])|(unsigned(header[3])<<8);return result;}
					result.ranges.push_back({at,end,surface});
				}
				at=end;result.end=at;
			}
			result.valid=true;return result;
		}
	}
}
