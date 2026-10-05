#include "component/vr/native_menu_commands.hpp"
#include "component/vr/native_waypoints.hpp"
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace vr::native_menu;
	void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
	struct stream
	{
		static constexpr std::uintptr_t base=0x180f0000;
		std::vector<std::uint8_t> bytes;
		std::vector<commands::emission> records;
		std::size_t append(unsigned size,unsigned op,unsigned flags=0)
		{
			const auto at=bytes.size();bytes.resize(at+size);
			bytes[at]=static_cast<std::uint8_t>(size);bytes[at+1]=static_cast<std::uint8_t>(size>>8);
			bytes[at+2]=static_cast<std::uint8_t>(op);bytes[at+3]=static_cast<std::uint8_t>(flags);return at;
		}
		void own(std::size_t begin,std::size_t end,unsigned surface=0)
		{records.push_back({{base+begin,base+end,surface}});}
		auto select(std::size_t begin=0)
		{
			return commands::select(base+begin,base+bytes.size(),records,[&](auto at,void* to,std::size_t size)
			{
				if(at<base||at-base>bytes.size()||size>bytes.size()-(at-base))return false;
				std::memcpy(to,bytes.data()+(at-base),size);return true;
			});
		}
	};
	std::uint64_t hash(std::span<const std::uint8_t> bytes,bool lines)
	{
		std::uint64_t value=14695981039346656037ull;
		for(std::size_t i=0;i<bytes.size();++i)commands::hash_byte(value,bytes[i],i,lines);
		return value;
	}
	void adjacent_arenas_and_streams()
	{
		// Live pause buffers were only 0x30000 apart; a guessed 4 MiB span
		// selected the first arena for both, producing a successful empty plan.
		require(!commands::contains(0x180f0000,0x180f47b8,0x18120000),"adjacent frame aliases earlier arena");
		require(commands::contains(0x18120000,0x181247b8,0x18120000),"actual arena rejected");
		stream s;s.append(8,28);s.append(104,17);s.own(8,112);s.append(4,0);
		const auto second=s.append(8,28);s.append(272,20);s.own(second+8,s.bytes.size(),1);s.append(4,0);
		const auto first=s.select();require(first.valid&&first.ranges.size()==1&&first.ranges[0].surface==0,"crossed stream terminator");
		const auto next=s.select(second);require(next.valid&&next.ranges.size()==1&&next.ranges[0].surface==1,"second stream lost");
		stream unowned;unowned.append(8,28);unowned.append(104,17);unowned.append(4,0);
		require(unowned.select().valid&&unowned.select().ranges.empty(),"unowned stream captured");
	}
	void growing_line_batch()
	{
		stream s;const auto at=s.append(40,25);s.bytes[at+4]=1;s.bytes[at+7]=2;s.bytes[at+8]=0x8e;
		require(commands::lines_2d(s.bytes.data(),40),"initial native line rejected");
		const auto initial=hash(s.bytes,true),strict=hash(s.bytes,false);
		s.own(0,40);s.bytes.resize(72);s.bytes[0]=72;s.bytes[4]=2;s.own(40,72);
		require(hash(std::span(s.bytes).first(40),true)==initial,"normal batch append invalidated immutable payload");
		require(hash(std::span(s.bytes).first(40),false)!=strict,"test failed to reproduce old full-record hash rejection");
		require(commands::lines_2d(s.bytes.data(),72),"finalized native line rejected");
		auto result=s.select();require(result.valid&&result.ranges.size()==1&&result.ranges[0].end==stream::base+72,"line fragments not merged at native command boundary");
		s.bytes[8]^=1;require(hash(std::span(s.bytes).first(40),true)!=initial,"payload corruption escaped hash");s.bytes[8]^=1;
		s.bytes[6]^=1;require(hash(std::span(s.bytes).first(40),true)!=initial,"line style corruption escaped hash");s.bytes[6]^=1;
		s.records[1].bytes.surface=1;require(!s.select().valid,"mixed-owner batch admitted");s.records[1].bytes.surface=0;
		s.records[1].bytes.begin+=4;require(!s.select().valid,"unowned batch gap admitted");s.records[1].bytes.begin-=4;
		s.bytes[4]=3;require(!s.select().valid,"inconsistent final line count admitted");s.bytes[4]=2;
		s.bytes[7]=3;require(!s.select().valid,"3D debug lines admitted as 2D menu");
	}
	void bounds_and_flags()
	{
		const vr::narrative_ui::backdrop hint{{205,310,1434.25f,374},.9f};
		vr::native_waypoints::group image;image.narrative=true;image.hint_backdrop=hint;image.begin=0x1000;image.end=0x1068;
		const std::array images{image};
		require(vr::native_waypoints::owns_hint_backdrop(images,0x1000,104),"standalone script image did not suppress its original flagged blur");
		vr::native_waypoints::group border;border.narrative=true;border.script_ink=true;border.begin=0x1100;border.end=0x1138;
		const std::array borders{border};
		require(vr::native_waypoints::owns_script_ink(borders,0x1100,56)&&!vr::native_waypoints::owns_script_ink(borders,0x1000,56),
			"standalone script border ownership is missing or too broad");
		stream compass;compass.append(120,18);compass.own(0,120);
		require(compass.select().valid&&compass.select().ranges.size()==1,"native pause minimap rejected the complete menu");
		compass.bytes[0]=104;require(!compass.select().valid,"compass quad accepted an ordinary-quad layout");
		stream excluded;excluded.append(104,17,0x40);excluded.own(0,104,commands::excluded_surface);
		const auto suppression=excluded.select();
		require(suppression.valid&&suppression.ranges.size()==1&&suppression.ranges[0].surface==commands::excluded_surface,
			"excluded pause effect lost its ownership or was admitted for drawing");
		require(commands::owner(suppression.ranges,stream::base,104)!=nullptr&&
			!commands::owner(suppression.ranges,stream::base+104,4),"excluded pause fade cannot be separated from story fade");
		require(commands::vignette_material("h1_ui_bg_vignette")&&
			!commands::vignette_material("h1_ui_bg_vignette_extra"),"vignette routing selected unrelated materials");
		stream s;s.append(104,17);s.append(104,17);s.own(0,208);
		require(s.select().valid&&s.select().ranges.size()==2,"multi-command emission lost");
		s.bytes[3]=0x40;require(!s.select().valid,"filtered blur command admitted");s.bytes[3]=0;
		s.bytes[0]=0;s.bytes[1]=0;require(!s.select().valid,"zero size loops forever");
		stream huge;huge.bytes.resize(0x400001);require(!huge.select().valid,"unbounded stream read");
		stream truncated;truncated.append(8,25);truncated.bytes[0]=72;truncated.own(0,8);
		require(!truncated.select().valid,"truncated command admitted");
		stream overlap;overlap.append(104,17);overlap.own(0,104);overlap.own(4,100);
		require(!overlap.select().valid,"overlapping provenance admitted");
	}
}
int main()
{
	try{adjacent_arenas_and_streams();growing_line_batch();bounds_and_flags();std::cout<<"native menu command tests passed\n";return 0;}
	catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
