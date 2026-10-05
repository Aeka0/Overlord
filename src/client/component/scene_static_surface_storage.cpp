#include <std_include.hpp>
#include "scene_static_surface_layout.hpp"
#include "console.hpp"
#include "loader/component_loader.hpp"
#include "loader/target_identity.hpp"
#include <utils/hook_validation.hpp>

namespace scene_static_surfaces
{
	namespace
	{
		// The two native frontends can be consumed concurrently. Match their
		// separation so every original atomic allocation selects the same bank.
		alignas(16) std::array<std::byte,scene_surface_storage::native_frontend_stride+capacity> banks;
		static_assert(capacity<scene_surface_storage::native_frontend_stride);
		bool write(std::uintptr_t at,const void* bytes,std::size_t count)noexcept
		{
			auto* target=reinterpret_cast<void*>(at);DWORD protection{},ignored{};
			if(!VirtualProtect(target,count,PAGE_EXECUTE_READWRITE,&protection))return false;
			std::memcpy(target,bytes,count);
			const bool restored=VirtualProtect(target,count,protection,&ignored)!=0;
			return FlushInstructionCache(GetCurrentProcess(),target,count)!=0 && restored;
		}
		void verify(std::uintptr_t at,const void* bytes,std::size_t count)
		{
			std::array<std::uint8_t,64> mask{};mask.fill(255);
			if(count>mask.size() || !utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),
				{static_cast<const std::uint8_t*>(bytes),mask.data(),count}))
				throw std::runtime_error(std::format("static-model surface storage contract rejected at {:X}",at));
		}
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			if(!target_identity::get().compatibility_probe_passed)throw std::runtime_error("static surface storage requires verified H2 image");
			const auto displacement=reinterpret_cast<std::uintptr_t>(banks.data())-scene_surface_storage::native_first_frontend;
			std::array<std::array<std::uint8_t,8>,operands.size()> replacements{};
			for(unsigned i=0;i<operands.size();++i)
			{
				const auto& item=operands[i];verify(item.address,item.bytes.data(),item.size);
				const auto at=relocate(item,displacement);if(!at)throw std::runtime_error("static surface bank displacement rejected");
				replacements[i]=item.bytes;std::memcpy(replacements[i].data()+item.field,&*at,4);
			}
			verify(budget_address,native_budgets.data(),sizeof(native_budgets));
			// Each changed reservation is <=4 times the original. The backing
			// bank grows by that factor too; no neighboring native fields move.
			// Sorting counts, 16-instance batches, draw keys and resets stay native.
			constexpr std::uint16_t expanded=transparent_bytes;
			const auto budget=budget_address+transparent_region*4;
			unsigned applied{};bool budget_written{},committed{};
			const auto rollback=gsl::finally([&] {
				if(committed)return;
				if(budget_written)(void)write(budget,&native_budgets[transparent_region][0],2);
				while(applied){const auto& item=operands[--applied];(void)write(item.address,item.bytes.data(),item.size);}
			});
			for(unsigned i=0;i<operands.size();++i)
			{
				++applied;
				if(!write(operands[i].address,replacements[i].data(),operands[i].size))throw std::runtime_error("static surface relocation failed");
			}
			budget_written=true;
			if(!write(budget,&expanded,sizeof(expanded)))throw std::runtime_error("static transparent surface budget failed");
			committed=true;
			console::info("[static scene surfaces] transparent lists: 4096 bytes/type; %zu-byte frontend banks; %zu verified relocation sites\n",capacity,operands.size());
		}
	};
}
REGISTER_COMPONENT(scene_static_surfaces::component)
