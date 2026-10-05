#include <std_include.hpp>
#include "oilrig.hpp"
#include "oilrig_angle_bridge.hpp"
#include "../../../head_pose_bridge.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook_validation.hpp>

namespace vr::gameplay::sequences::oilrig
{
	namespace
	{
		std::atomic_bool ready{}, alive{true};
		std::atomic_uint64_t expanded{}, failures{};
		float yaw_parameter(unsigned index, const game::gentity_s* owner, unsigned mode)
		{
			// Preserve the original VM argument validation/error behavior first.
			// No VM stack edits, global Scr_GetFloat hook, or per-frame PS writes.
			const float original = game::Scr_GetFloat(index);
			if (!alive || owner != &game::g_entities[0] || (mode != 1 && mode != 3))
				return original;
			const auto head = head_pose_bridge::get_status();
			// This is the configured VR mode, not an instantaneous tracking gate:
			// losing a pose between the two native argument reads must not produce
			// an asymmetrically widened envelope.
			if (!head.enabled)
				return original;
			try
			{
				const float result = expanded_yaw(index, original, underwater_entry());
				if (result != original)
					++expanded;
				return result;
			}
			catch (const std::exception&)
			{
				++failures;
				return original;
			}
		}
		template <std::size_t N> bool verify(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{};
			mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),
			                                                        {bytes, mask.data(), N}));
		}
	}
	std::string angle_status()
	{
		return std::format(
		    "oilrig_yaw_adapter={} expanded_arguments={} query_failures={} extra_each_side={}\n",
		    ready.load(),
		    expanded.load(),
		    failures.load(),
		    yaw_extension);
	}
	class angle_component final : public component_interface
	{
		void post_unpack() override
		{
			// Both setters author yaw at indices 3/4, pitch at 5/6. Match the
			// producer instructions, not only a generic float-getter call target.
			constexpr std::uint8_t link_left[]{0xb9, 3, 0, 0, 0, 0xe8, 0xd5, 0xae, 0x0c, 0};
			constexpr std::uint8_t link_right[]{0xb9, 4, 0, 0, 0, 0xe8, 0x99, 0xae, 0x0c, 0};
			constexpr std::uint8_t lerp_left[]{0xb9, 3, 0, 0, 0, 0xe8, 0xff, 0x11, 0x0e, 0};
			constexpr std::uint8_t lerp_right[]{0xb9,
			                                    4,
			                                    0,
			                                    0,
			                                    0,
			                                    0x45,
			                                    0x0f,
			                                    0x28,
			                                    0xc8,
			                                    0xf3,
			                                    0x44,
			                                    0x0f,
			                                    0x5d,
			                                    0xc8,
			                                    0xe8,
			                                    0xe3,
			                                    0x11,
			                                    0x0e,
			                                    0};
			constexpr std::uint8_t melee[]{
			    0x8b, 0x88, 0x18, 0xe9, 0, 0, 0x0b, 0x88, 0x0c, 0xe9, 0, 0, 0xf6, 0xc1, 4};
			constexpr std::uint8_t mode_register[]{0x44, 0x8b, 0xf2, 0x66, 0x85, 0xc0};
			if (!verify(0x1404FC851, link_left) || !verify(0x1404FC88D, link_right) ||
			    !verify(0x1404E6527, lerp_left) || !verify(0x1404E653A, lerp_right) ||
			    !verify(0x1404B4073, melee) || !verify(0x1404FC660, mode_register))
			{
				console::error("[VR oilrig] native angle/input signatures rejected\n");
				return;
			}
			auto* link = utils::hook::assemble(
			    [](utils::hook::assembler& a)
			    { emit_angle_bridge(a, true, reinterpret_cast<void*>(yaw_parameter)); });
			auto* lerp = utils::hook::assemble(
			    [](utils::hook::assembler& a)
			    { emit_angle_bridge(a, false, reinterpret_cast<void*>(yaw_parameter)); });
			auto* link_relay = utils::hook::create_preserving_near_jump(0x1404FC856, link);
			auto* lerp_relay = utils::hook::create_preserving_near_jump(0x1404E652C, lerp);
			struct patch
			{
				std::uintptr_t site;
				void* target;
			};
			const std::array patches{patch{0x1404FC856, link_relay},
			                         patch{0x1404FC892, link_relay},
			                         patch{0x1404E652C, lerp_relay},
			                         patch{0x1404E6548, lerp_relay}};
			for (const auto& p : patches)
				if (!p.target || utils::hook::is_relatively_far(reinterpret_cast<void*>(p.site), p.target))
				{
					console::error("[VR oilrig] near call bridge rejected\n");
					return;
				}
			for (const auto& p : patches)
				utils::hook::call(p.site, p.target);
			ready = true;
		}
		void pre_destroy() override
		{
			alive = false;
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::sequences::oilrig::angle_component)
