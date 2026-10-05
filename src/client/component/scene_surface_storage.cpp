#include <std_include.hpp>
#include "scene_surface_layout.hpp"
#include "scene_surface_storage_contract.hpp"
#include "console.hpp"
#include "loader/component_loader.hpp"
#include "loader/target_identity.hpp"
#include "game/game.hpp"
#include <utils/hook_validation.hpp>

namespace scene_surface_storage
{
	namespace
	{
		// Keep the SAME separation as the two native frontend records. A single
		// verified displacement then selects the matching bank from either native
		// frontend pointer, including backend workers still consuming the prior
		// frame. Do not compact this gap or replace these banks with one scratch
		// buffer: that aliases in-flight frames. Zero-filled static virtual pages
		// in the unused gap are never touched by this module.
		alignas(16) std::array<std::byte,native_frontend_stride+capacity> banks;
		static_assert(capacity==0x80000 && native_frontend_stride%index_unit==0);
		static_assert(offsetof(game::GfxPlacement,origin)==0x10);

		bool write(void* target,const void* bytes,std::size_t size) noexcept
		{
			DWORD protection{},ignored{};
			if(!VirtualProtect(target,size,PAGE_EXECUTE_READWRITE,&protection))return false;
			std::memcpy(target,bytes,size);
			const bool restored=VirtualProtect(target,size,protection,&ignored)!=0;
			return FlushInstructionCache(GetCurrentProcess(),target,size)!=0 && restored;
		}
		void verify(std::uintptr_t at,const std::uint8_t* bytes,std::size_t size)
		{
			std::array<std::uint8_t,128> mask{};mask.fill(0xff);
			if(size>mask.size() || !utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<const void*>(at),{bytes,mask.data(),size}))
				throw std::runtime_error(std::format("native surface storage ABI rejected at {:X}",at));
		}
	}
	class component final:public component_interface
	{
	public:
		void post_unpack() override
		{
			if(!target_identity::get().compatibility_probe_passed)
				throw std::runtime_error("surface storage requires the verified H2 image");
			const auto displacement=reinterpret_cast<std::intptr_t>(banks.data())-
				static_cast<std::intptr_t>(native_first_frontend);
			if(displacement<=0 || displacement>INT32_MAX || displacement%index_unit)
				throw std::runtime_error("expanded surface banks are outside native displacement range");
			auto patches=contract::patches;
			// Validate EVERY instruction before writing ANY instruction. This is a
			// startup-only ABI conversion: partial installation/mixed 4- and 8-byte
			// indexing is corruption, not a supported fallback or runtime toggle.
			for(auto& patch:patches)
			{
				verify(patch.address,patch.before.data(),patch.size);
				if(patch.dynamic)
				{
					std::uint32_t original{};
					std::memcpy(&original,patch.before.data()+patch.field,sizeof(original));
					const auto value=relocate_operand(patch.dynamic,original,displacement);
					if(!value)throw std::runtime_error("surface storage operand relocation rejected");
					std::memcpy(patch.after.data()+patch.field,&*value,sizeof(*value));
				}
			}
			// Match the complete diagnostic text before adjusting its exponent.
			// Keep native warning behavior; a future 512 KiB overflow must be visible.
			constexpr char warning[]="MAX_SCENE_SURFS_SIZE(( 1 << ( 16 + 2 ) )) exceeded - not drawing surface\n";
			constexpr std::uintptr_t warning_address=0x1409afa50;
			verify(warning_address,reinterpret_cast<const std::uint8_t*>(warning),sizeof(warning));
			const auto digit=std::string_view{warning}.find("16 + 2")+5;
			std::size_t applied{};bool committed{};bool warning_written{};
			const auto rollback=gsl::finally([&] {
				if(committed)return;
				if(warning_written)(void)write(reinterpret_cast<void*>(warning_address+digit),"2",1);
				while(applied){const auto& p=patches[--applied];(void)write(reinterpret_cast<void*>(p.address),p.before.data(),p.size);}
			});
			for(const auto& patch:patches)
			{
				++applied; // also restore the current site if its protection restore fails
				if(!write(reinterpret_cast<void*>(patch.address),patch.after.data(),patch.size))
					throw std::runtime_error("surface storage patch write failed");
				verify(patch.address,patch.after.data(),patch.size);
			}
			warning_written=true;
			if(!write(reinterpret_cast<void*>(warning_address+digit),"3",1))
				throw std::runtime_error("surface storage warning update failed");
			committed=true;
			// Allocations still use H2's original atomic cursor and original reset.
			// There is no per-model lookup, copy, new lock or per-frame allocation.
			console::info("[scene surfaces] 512 KiB per frontend; 8-byte indices; %zu ABI sites verified\n",patches.size());
		}
	};
}
REGISTER_COMPONENT(scene_surface_storage::component)
