#include <std_include.hpp>
#include "weapon_actions.hpp"
#include "native_ammunition.hpp"
#include "native_animation_index_bridge.hpp"
#include "../head_pose_bridge.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::equip_presentation
{
	namespace
	{
		constexpr std::uintptr_t native_index=0x1406A6280;
		game::dvar_t* enabled{};
		std::atomic_bool alive{true};
		std::atomic_uint64_t suppressed{}, unavailable{};
		bool installed{};
		// Verified local PS right-hand XAnimParts table, also queried by native
		// 6A9530: PS+648, index*8. Never modify the PS, shared assets or XAnim tree.
		bool clip_name(const void* ps,int index,std::uint32_t weapon,std::array<char,160>& out) noexcept
		{
			if (!ps || index<=0 || index>=202) return false;
			__try
			{
				const auto* bytes=static_cast<const std::byte*>(ps);
				std::uint32_t current{}; std::memcpy(&current,bytes+0x3bc,sizeof(current));
				if (current!=weapon) return false;
				const game::XAnimParts* parts{};
				std::memcpy(&parts,bytes+0x648+index*sizeof(void*),sizeof(parts));
				if (!parts || !parts->name) return false;
				for (std::size_t i=0;i<out.size();++i)
				{ out[i]=parts->name[i]; if (!out[i]) return i!=0; }
				return false;
			}
			__except (GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION || GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR ?
				EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
		}
		std::uint64_t remap(const index_query* query)
		{
			const auto original=query->result;
			const auto* ps=reinterpret_cast<const void*>(query->ps);
			const auto weapon=static_cast<std::uint32_t>(query->weapon);
			const auto side=static_cast<int>(query->side);
			const bool alternate=(query->alternate&0xff)!=0, option=(query->option&0xff)!=0;
			if (!alive.load(std::memory_order_relaxed) || !enabled || !enabled->current.enabled || side || alternate ||
				original==0 || original>=202 ||
				!game::CL_IsCgameInitialized() || native_ammunition::local_role(ps)!=1 ||
				!head_pose_bridge::get_status().enabled) return original;
			std::array<char,160> requested{}, idle_name{};
			if (!clip_name(ps,static_cast<int>(original),weapon,requested) || !native_equip_clip(requested.data())) return original;
			// Action zero is the native idle selector, including its empty-idle
			// choice. Do not guess a global idle asset or reuse another weapon's pose.
			const int idle=utils::hook::invoke<int>(native_index,ps,0,weapon,alternate,side,option);
			if (!clip_name(ps,idle,weapon,idle_name)) { ++unavailable; return original; }
			const int result=equip_presentation_index(static_cast<int>(original),idle,requested.data(),idle_name.data(),true,side,alternate);
			if (static_cast<std::uint64_t>(result)==original) ++unavailable;
			else ++suppressed;
			return result;
		}
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			enabled=dvars::register_bool("vr_suppressEquipAnimations",true,game::DVAR_FLAG_SAVED,
				"Use current weapon idle instead of first-person pickup/switch animation, camera motion and notetracks in VR");
			// These five CLIENT presentation calls share the action->XAnim-slot
			// conversion. Never detour its server callers: weapon selection, timers,
			// reload mechanics and inventory transactions retain native authority.
			constexpr std::uintptr_t calls[]{0x1403BAC7E,0x1403BB8C6,0x1403C4C53,0x1403C5688,0x1403C5C21};
			constexpr std::uint8_t entry[]{0x48,0x89,0x5c,0x24,0x10,0x44,0x89,0x44,0x24,0x18,0x57,0x48,0x83,0xec,0x30};
			std::array<std::uint8_t,sizeof(entry)> mask{}; mask.fill(0xff);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(native_index),{entry,mask.data(),mask.size()}))
				throw std::runtime_error("native equip animation selector contract rejected");
			constexpr std::uint8_t table[]{0x48,0x8d,0xb9,0x48,0x06,0x00,0x00};
			constexpr std::uint8_t indexed[]{0x4a,0x39,0x2c,0xff};
			std::array<std::uint8_t,sizeof(table)> table_mask{}; table_mask.fill(0xff);
			std::array<std::uint8_t,sizeof(indexed)> indexed_mask{}; indexed_mask.fill(0xff);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1406A9551),{table,table_mask.data(),table_mask.size()}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1406A95C7),{indexed,indexed_mask.data(),indexed_mask.size()}))
				throw std::runtime_error("native equip animation table contract rejected");
			for (auto at:calls)
			{
				std::array<std::uint8_t,5> expected{0xe8}, exact{0xff,0xff,0xff,0xff,0xff};
				const auto displacement=static_cast<std::int32_t>(native_index-at-5);
				std::memcpy(expected.data()+1,&displacement,sizeof(displacement));
				if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),{expected.data(),exact.data(),expected.size()}))
					throw std::runtime_error("native equip presentation call contract rejected");
			}
			auto* bridge=utils::hook::assemble([](utils::hook::assembler& a) {
				emit_index_bridge(a,native_index,reinterpret_cast<std::uintptr_t>(remap));
			});
			if (!bridge) throw std::runtime_error("native equip presentation bridge allocation failed");
			// JitRuntime can place the bridge outside the game's rel32 range. Reuse
			// the same near-image absolute relay as native TLS hooks; never widen a
			// five-byte CALL into adjacent native instructions. Its RAX scratch is
			// safe here: emit_index_bridge replaces RAX before entering the selector.
			auto* relay=utils::hook::create_far_jump<0x140000000>(bridge);
			for (auto at:calls)
				if (!relay || utils::hook::is_relatively_far(reinterpret_cast<void*>(at),relay))
					throw std::runtime_error("native equip presentation relay outside call-site range");
			for (auto at:calls) utils::hook::call(at,relay);
			installed=true;
			command::add("vr_equip_status",[] {
				console::info("[VR equip presentation] installed=%d enabled=%d idle_remaps=%llu unavailable=%llu native_state_unchanged=1\n",
					installed,enabled && enabled->current.enabled,suppressed.load(),unavailable.load());
			});
		}
		void pre_destroy() override { alive=false; }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::equip_presentation::component)
