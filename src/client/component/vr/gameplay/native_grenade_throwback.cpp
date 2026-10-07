#include <std_include.hpp>
#include "native_grenade_throwback.hpp"
#include "native_scripted_control.hpp"
#include "component/scheduler_context.hpp"
#include "game/game.hpp"
#include <utils/native_memory.hpp>
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::grenades::throwback
{
	namespace
	{
		bool installed{};
		template<class T>T read(const void* p,std::size_t offset)noexcept
		{T value{};utils::native_memory::read_bytes(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));return value;}
		template<class T>void write(void* p,std::size_t offset,T value)noexcept
		{std::memcpy(static_cast<std::byte*>(p)+offset,&value,sizeof(value));}
		template<std::size_t N>bool verify(std::uintptr_t address,const std::uint8_t(&bytes)[N])
		{std::array<std::uint8_t,N> mask;mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));}
		bool server()noexcept
		{return installed && scheduler::is_executing(scheduler::pipeline::server) && game::CL_IsCgameInitialized() && game::g_entities[0].client;}
	}
	bool initialize()
	{
		// H2 SP 0x6023ed08: HUD tests PS cursorHint==5 and cursorHintEntIndex;
		// UpdateCursorHints derives throwback time/weapon from that same missile.
		constexpr std::uint8_t hud[]{0x80,0xbb,0x1a,0xd1,1,0,5,0x75,0x17,0x0f,0xb7,0x87,0xb4,1,0,0};
		constexpr std::uint8_t timer[]{0x8b,0x8f,0x80,1,0,0,0x33,0xc0,0x2b,0x0d,0x24,0x4d,0x0a,5,0x85,0xc9,0x0f,0x4f,0xc1,0x89,0x43,0x68};
		constexpr std::uint8_t pickup[]{0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x20};
		constexpr std::uint8_t handoff[]{0x66,0x89,0x68,0x1c,0x48,0x8b,0x86,0x18,1,0,0,0x89,0x98,0xb0,3,0,0,0x48,0x8b,0x8e,0x18,1,0,0,0x8b,0x41,0x68,0x89,0x41,0x64};
		// Missile Touch_Item skips inventory allocation for class-6 frags; it
		// sends native pickup/death notifications and uses the shared free hook.
		constexpr std::uint8_t missile[]{0x33,0xd2,0x41,0x8b,0xc8,0xe8,6,0xe0,0x1d,0,0x83,0xf8,9,0x75,0x1f};
		constexpr std::uint8_t attribution[]{0x44,0x39,0x70,0x64,0x7d,0x62,0x0f,0xb7,0x48,0x1c,0x66,0x3b,0xcb,0x74,0x59};
		constexpr std::uint8_t weapon_class[]{0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x48,0x83,0xec,0x50};
		installed=verify(0x140369fb6,hud) && verify(0x140527a4e,timer) && verify(0x1404adf60,pickup) &&
			verify(0x1404adfdf,handoff) && verify(0x1404c6fb0,missile) && verify(0x1404cfe1a,attribution) && verify(0x1406a4fc0,weapon_class) &&
			read<std::uintptr_t>(reinterpret_cast<const void*>(0x140bf5060),0)==0x1404c6010;
		return installed;
	}
	bool ready()noexcept{return installed;}
	candidate query()noexcept
	{
		if(!server())return {};
		const auto* actor=&game::g_entities[0];const auto* ps=actor->client;
		if(read<int>(actor,0x184)<=0 || !scripted_control::allowed(ps) || read<std::uint8_t>(ps,0xa)!=5 ||
			read<int>(ps,0x64)!=0 || read<std::uint16_t>(ps,0x1c)!=3999 || read<int>(ps,0x68)<=0)return {};
		const int number=read<std::uint16_t>(ps,0x24);if(number<=0 || number>=3998)return {};
		const auto* entity=&game::g_entities[number];candidate result;
		result.entity=weapons::native_carry::entity_key(number);
		const auto token=read<std::uint32_t>(entity,0x80);
		if(read<std::uint8_t>(entity,0)!=3 || !read<std::uint8_t>(entity,0xbc) || read<std::uint8_t>(entity,0x14b)!=13 ||
			(read<unsigned>(entity,0xc)&0x400000) || token!=read<unsigned>(ps,0xb0) ||
			!native::describe(token,result.grenade) || !behaviors[unsigned(result.grenade.type)].cook ||
			utils::hook::invoke<int>(0x1406a4fc0,token,false)!=6)return {};
		result.deadline=read<int>(entity,0x180);const auto remaining=std::int64_t(result.deadline)-native::time();
		if(remaining<=0 || remaining>60000)return {};
		result.position=read<hands::vec>(entity,0xf4);
		for(float x:result.position)if(!std::isfinite(x) || std::abs(x)>1e7f)return {};
		const auto owner=read<std::uint16_t>(entity,0x178);
		result.owner=owner && owner<=3999 ? weapons::native_carry::world_key{owner-1,weapons::native_carry::entity_key(owner-1).generation} : weapons::native_carry::world_key{3998,0};
		result.timeline=weapons::native_ammunition::timeline();
		return result;
	}
	bool take(const candidate& expected)noexcept
	{
		const auto current=query();
		if(!expected || !current || current.entity!=expected.entity || current.grenade.weapon!=expected.grenade.weapon ||
			current.deadline!=expected.deadline || current.owner!=expected.owner || current.timeline!=expected.timeline)return false;
		auto* actor=&game::g_entities[0];auto* ps=actor->client;auto* entity=&game::g_entities[current.entity.entity];
		const auto old_owner=read<std::uint16_t>(ps,0x1c),hint=read<std::uint16_t>(ps,0x24);
		const auto old_offhand=read<unsigned>(ps,0x3b0);const auto old_cook=read<int>(ps,0x64),hint_fuse=read<int>(ps,0x68);
		utils::hook::invoke<void>(0x1404adf60,actor,entity);
		if(!game::CL_IsCgameInitialized() || actor->client!=ps || weapons::native_ammunition::timeline()!=current.timeline)return false;
		const bool consumed=!read<std::uint8_t>(entity,0xbc) || weapons::native_carry::entity_key(current.entity.entity)!=current.entity;
		if(!consumed)return false;
		// Transfer only the three stock handoff cells into VR ownership. No flat
		// offhand animation/PMove cook state is left running beside the VR state.
		if(read<std::uint16_t>(ps,0x1c)==current.owner.entity)write(ps,0x1c,old_owner);
		if(read<unsigned>(ps,0x3b0)==current.grenade.weapon)write(ps,0x3b0,old_offhand);
		if(read<int>(ps,0x64)==hint_fuse)write(ps,0x64,old_cook);
		if(read<std::uint16_t>(ps,0x24)==hint)
		{write<std::uint16_t>(ps,0x24,3999);write<std::uint8_t>(ps,0xa,0);write<int>(ps,0x68,0);write<unsigned>(ps,0xb0,0);}
		return true;
	}
	int expired_owner(const candidate& source)noexcept
	{
		if(!server() || source.timeline!=weapons::native_ammunition::timeline())return 3998;
		const int id=source.owner.entity;
		if(id<0 || id>=3998 || !read<std::uint8_t>(&game::g_entities[id],0xbc) ||
			(id>0 && weapons::native_carry::entity_key(id)!=source.owner))return 3998;
		return id;
	}
}
