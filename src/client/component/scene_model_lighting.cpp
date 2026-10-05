#include <std_include.hpp>
#include "scene_model_lighting_policy.hpp"
#include "scene_model_lighting_bridge.hpp"
#include "console.hpp"
#include "loader/component_loader.hpp"
#include "loader/target_identity.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace scene_model_lighting
{
	namespace
	{
		template<std::size_t N>void verify(std::uintptr_t at,const std::array<std::uint8_t,N>& bytes)
		{
			std::array<std::uint8_t,N> mask;mask.fill(255);
			if(!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),{bytes.data(),mask.data(),N}))
				throw std::runtime_error(std::format("native model lighting sizing rejected at {:X}",at));
		}
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			if(!target_identity::get().compatibility_probe_passed)throw std::runtime_error("model lighting requires verified H2 image");
			constexpr std::array<std::uintptr_t,2> sites{0x14075E27E,0x14075E296};
			constexpr std::array<std::uint8_t,6> compare{0x81,0xfa,0x00,0x10,0x00,0x00};
			constexpr std::uintptr_t reuse_site=0x14075D032;
			constexpr std::array<std::uint8_t,9> reuse_branch{0x45,0x85,0xdb,0x0f,0x84,0x62,0x01,0x00,0x00};
			// R8D is the original dynamic reservation, unchanged across the loop.
			// The native function still computes all CPU/GPU sizes, exponents,
			// texture height and shader UV constants before resource creation.
			verify(0x14075E245,std::array<std::uint8_t,12>{0x44,0x8b,0xc1,0xb9,0x20,0,0,0,0x41,0xc1,0xe0,0x0c});
			verify(0x14075E290,std::array<std::uint8_t,6>{0x03,0xd0,0xff,0xc1,0xd1,0xc0});
			for(const auto at:sites)verify(at,compare);
			verify(reuse_site,reuse_branch);
			verify(0x14075D03B,std::array<std::uint8_t,13>{0x45,0x8b,0xd3,0x44,0x2b,0x15,0x3f,0xe3,0x7a,0x0e,0x41,0xff,0xca});
			// The consumer subtracts the static partition before indexing dynamic
			// buffers. Check that the initializer still publishes both boundaries.
			verify(0x14075E2B7,std::array<std::uint8_t,6>{0x89,0x05,0xcb,0xd0,0x7a,0x0e});
			verify(0x14075E2D3,std::array<std::uint8_t,6>{0x89,0x15,0xab,0xd0,0x7a,0x0e});
			std::array<void*,2> relays{};
			for(unsigned i=0;i<sites.size();++i)
			{
				const auto continuation=sites[i]+compare.size();
				const auto bridge=utils::hook::assemble([continuation](auto& a) {
					using namespace asmjit::x86;
					const auto legacy=a.new_label(),done=a.new_label();
					a.cmp(r8d,reserved_limit);a.ja(legacy);
					a.cmp(edx,expanded_minimum);a.jmp(done);
					a.bind(legacy);a.cmp(edx,native_minimum);
					a.bind(done);a.jmp(continuation);
				});
				if(!bridge || !(relays[i]=utils::hook::create_preserving_near_jump(sites[i],bridge)))
					throw std::runtime_error("model lighting quota bridge allocation failed");
			}
			// Savegames archive DynEntityClient::lightingHandle, including the old
			// partition offset. An out-of-range handle is a cache miss, not an
			// index: let the native allocator rebuild it before any buffer access.
			const auto reuse_bridge=utils::hook::assemble([](auto& a) {
				emit_dynamic_handle_guard(a,0x14EF0B384,
					std::uintptr_t{0x14075D03B},std::uintptr_t{0x14075D19D});
			});
			const auto reuse_relay=reuse_bridge?utils::hook::create_preserving_near_jump(reuse_site,reuse_bridge):nullptr;
			if(!reuse_relay)throw std::runtime_error("model lighting handle guard allocation failed");
			// Prepare every bridge and verify every contract before writing. Install
			// the reuse guard before expanding the partition during initialization.
			utils::hook::jump(reuse_site,reuse_relay);
			utils::hook::nop(reuse_site+5,reuse_branch.size()-5);
			for(unsigned i=0;i<sites.size();++i){utils::hook::jump(sites[i],relays[i]);utils::hook::nop(sites[i]+5,1);}
			console::info("[model lighting] bounded native cache expansion: single-client static slots 4096 -> 12288; restored handles guarded\n");
		}
	};
}
REGISTER_COMPONENT(scene_model_lighting::component)
