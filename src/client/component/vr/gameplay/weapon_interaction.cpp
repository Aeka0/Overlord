#include <std_include.hpp>
#include "designator_events.hpp"
#include "native_scripted_control.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include "shot_geometry.hpp"
#include "native_ballistics.hpp"
#include "native_shield.hpp"
#include "muzzle_clearance.hpp"
#include "weapon_carry_runtime.hpp"
#include "independent_fire_runtime.hpp"
#include "weapon_recoil.hpp"
#include "weapon_recoil_tuning.hpp"
#include "aim_assist.hpp"
#include "../settings.hpp"
#include <mutex>
#include <sstream>
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::weapons
{
	namespace
	{
		std::mutex state_mutex;
		holding_state holding;
		muzzle_frame muzzle;
		bool identity_verified{}, hooks_installed{};
		bool spread_hook_installed{};
		bool projectile_verified{};
		std::atomic<bool> alive{true};
		game::dvar_t* enabled{};
		game::dvar_t* disable_hip_fire_spread{};
		game::dvar_t* aim_assist_strength{};
		game::dvar_t* recoil_enabled{};
		game::dvar_t* recoil_penalty{};
		recoil::state recoil_state;
		std::atomic<std::uint64_t> applied{}, rejected{}, obstructed{};
		std::atomic<std::uint64_t> spread_applied{}, spread_rejected{};
		std::atomic<const char*> reason{"waiting for shot"};
		constexpr std::uintptr_t fire_address = 0x140518A40;
		constexpr std::uintptr_t parameter_address = 0x140518880;
		constexpr std::uintptr_t weapon_type_address = 0x1406A5440;
		constexpr auto bullet_address = native_ballistics::bullet_address;
		constexpr auto ads_spread_address = native_ballistics::ads_spread_address;
		// Verified FinishMove loads this token into usercmd.weapon at +0x14.
		constexpr std::uintptr_t selected_weapon_address = 0x141E8A628;
		using native_parameters = native_ballistics::parameters;
		struct shot_context
		{
			game::gentity_s* entity;
			muzzle_frame pose;
			bool disable_hip_fire_spread;
			shot_geometry committed_geometry;
		};
		thread_local const shot_context* active_shot{};

		std::uint32_t entity_weapon(const game::gentity_s* entity)
		{
			std::uint32_t value{};
			std::memcpy(&value, reinterpret_cast<const std::byte*>(entity) + 0x80, sizeof(value));
			return value;
		}
		fire_delivery entity_delivery(const game::gentity_s* entity, std::uint32_t token)
		{
			int alternate{};
			std::memcpy(&alternate, reinterpret_cast<const std::byte*>(entity) + 0x84, sizeof(alternate));
			return controller_fire_delivery(token, alternate != 0);
		}
		bool gameplay_active(const controller_input::frame& input)
		{
			const auto head = head_pose_bridge::get_status();
			const auto* paused = game::Dvar_FindVar("cl_paused");
			return input.focused && head.enabled && head.pose_available && !head.recenter_pending &&
				   head.recenter_count == input.reference_generation && game::CL_IsCgameInitialized() &&
				   *game::keyCatchers == 0 && paused && paused->current.integer == 0;
		}
		void parameters_stub(game::gentity_s* entity, native_parameters* parameters, int event_parameter)
		{
			utils::hook::invoke<void>(parameter_address, entity, parameters, event_parameter);
			// Frozen once per native shot, including every pellet/penetration segment.
			// Unrelated calls and nested NPC events must not inherit this context.
			if (active_shot && active_shot->entity == entity && parameters)
			{
				parameters->shot = active_shot->committed_geometry;
				++applied;
				reason = "controller muzzle applied";
			}
		}
		void bullets_stub(game::gentity_s* entity, float spread, native_parameters* parameters,
			game::gentity_s* attacker, int event_parameter, float range_override)
		{
			// 0x140518C2E supplies the final cone in XMM1, after the native
			// stance/ADS/bloom interpolation. Scope this to the frozen local shot;
			// never edit shared WeaponDef fields or the player's spread state.
			if (active_shot && active_shot->entity == entity && active_shot->disable_hip_fire_spread &&
				parameters && parameters->weapon == active_shot->pose.owner.weapon)
			{
				// Same attachment/alternate-aware ADS baseline query as 0x140518BB9.
				// Retain intrinsic dispersion (including shotgun pellets), removing
				// only the flat-screen hip-fire/bloom contribution to the cone.
				const auto intrinsic_spread = utils::hook::invoke<float>(ads_spread_address,
					entity->client, parameters->weapon, parameters->alternate, 0);
				if (std::isfinite(intrinsic_spread) && intrinsic_spread >= 0)
				{
					spread = intrinsic_spread;
					++spread_applied;
				}
				else
					++spread_rejected;
			}
			utils::hook::invoke<void>(bullet_address, entity, spread, parameters, attacker,
				event_parameter, range_override);
		}
		bool muzzle_path_clear(const game::gentity_s* entity, const muzzle_frame& pose)
		{
			if(!shield::firing_clear(pose.owner.id(),pose.position))return false;
			// A clipped gun must not bypass cover by starting a bullet beyond it.
			// Conservative eye-to-muzzle point sweep, on the server shot thread only.
			const auto* ps = reinterpret_cast<const game::playerState_s*>(entity->client);
			hands::vec eye{ps->origin[0], ps->origin[1], ps->origin[2] + ps->viewHeightCurrent};
			for (auto x : eye)
				if (!std::isfinite(x))
					return false;
			if (hands::length(hands::sub(eye, pose.position)) > 200)
				return false;
			game::trace_t trace{};
			game::Bounds point{};
			// Native bullet trace content mask: 0x1404AC57B -> 0x1404C9250.
			game::G_TraceCapsule(&trace, eye.data(), pose.position.data(), &point, entity->s.entityNum,
								 muzzle_clearance_mask);
			return std::isfinite(trace.fraction) && trace.fraction >= 1 && !trace.allsolid &&
				   !trace.startsolid;
		}
		void fire_stub(game::gentity_s* entity, int event_parameter)
		{
			const auto* previous = active_shot;
			active_shot = nullptr;
			const auto restore = gsl::finally([&] { active_shot = previous; });
			if (entity==&game::g_entities[0] && independent_fire::owns_native(entity->client)) return;
			if (!firing_enabled() || entity != &game::g_entities[0] || !entity->client)
			{
				utils::hook::invoke<void>(fire_address, entity, event_parameter);
				return;
			}
			const auto owner = sample_native_equipped();
			const auto input = controller_input::latest();
			shot_context shot{entity, current_muzzle(),
				spread_hook_installed && disable_hip_fire_spread && disable_hip_fire_spread->current.enabled, {}};
			const auto now = controller_input::clock::now();
			if (!gameplay_active(input) || !scripted_control::firing_allowed(entity->client) || now < input.sampled_at ||
				now - input.sampled_at > std::chrono::milliseconds(150) ||
				!ready(shot.pose, owner, input.reference_generation, now) ||
				entity_weapon(entity) != owner.weapon || entity_delivery(entity, owner.weapon)==fire_delivery::unsupported)
			{
				++rejected;
				reason = "shot rejected: ownership/tracking/weapon contract";
				return;
			}
			if (!muzzle_path_clear(entity, shot.pose))
			{
				++obstructed;
				reason = "shot rejected: eye-to-muzzle obstruction";
				return;
			}
			shot.committed_geometry = geometry(shot.pose);
			if(entity_delivery(entity,owner.weapon)==fire_delivery::scripted_device && !equipment::special::designator_events::can_fire())return;
			// Guided target selection and launch constraints remain native. Bullet
			// assistance must not change a launcher's acquired target/direction.
			if (entity_delivery(entity,owner.weapon)==fire_delivery::bullets) (void)aim_assist::apply(shot.committed_geometry,
				aim_assist_strength ? aim_assist_strength->current.value : 0.f, entity->s.entityNum,aim_assist::shot_route::projected);
			active_shot = &shot;
			// Capture accepted movement posture before native damage/scripts can
			// change the player. Input stance requests are not firing posture.
			const auto posture=controller_input::native_posture(
				reinterpret_cast<const game::playerState_s*>(entity->client)->pm_flags);
			if(entity_delivery(entity,owner.weapon)==fire_delivery::scripted_device)equipment::special::designator_events::record_shot(shot.pose);
			utils::hook::invoke<void>(fire_address, entity, event_parameter);
			if(entity_delivery(entity,owner.weapon)!=fire_delivery::scripted_device)recoil::confirmed_shot(owner, input.reference_generation, now, posture);
		}
		template <std::size_t N> bool verify(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{};
			mask.fill(0xff);
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<void*>(address), {bytes, mask.data(), N}));
		}
		void print_status()
		{
			const auto owner = current_hold();
			const auto pose = current_muzzle();
			const auto input = controller_input::latest();
			std::ostringstream out;
			out << "[VR fire] identity=" << identity_verified << " hooks=" << hooks_installed
				<< " projectile=" << projectile_verified << " delivery=" << static_cast<int>(controller_fire_delivery(owner.weapon))
				<< " enabled=" << firing_enabled() << " weapon=" << owner.weapon
				<< " rear=" << static_cast<int>(owner.rear) << " support=" << static_cast<int>(owner.support)
				<< " revision=" << owner.revision << " ready="
				<< firing_ready(owner, input.reference_generation, controller_input::clock::now())
				<< " applied=" << applied.load() << " rejected=" << rejected.load()
				<< " obstructed=" << obstructed.load() << " state=" << reason.load() << '\n'
				<< "disable_hip_fire_spread=" << (disable_hip_fire_spread && disable_hip_fire_spread->current.enabled)
				<< " spread_hook=" << spread_hook_installed << " spread_applied=" << spread_applied.load()
				<< " spread_rejected=" << spread_rejected.load() << '\n'
				<< "aim_assist_strength=" << (aim_assist_strength ? aim_assist_strength->current.value : 0.f)
				<< " aim_assist_degrees=" << settings::aim_assist_degrees(
					aim_assist_strength ? aim_assist_strength->current.value : 0.f) << '\n'
				<< aim_assist::status()
				<< "recoil=" << (recoil_enabled && recoil_enabled->current.enabled)
				<< " penalty=" << (recoil_penalty ? recoil_penalty->current.integer : 1)
				<< " climb_degrees=" << recoil::current_climb(owner,input.reference_generation,controller_input::clock::now()) << '\n'
				<< "muzzle=[" << pose.position[0] << ',' << pose.position[1] << ',' << pose.position[2]
				<< "] forward=[" << pose.axis[0][0] << ',' << pose.axis[0][1] << ',' << pose.axis[0][2]
				<< "] trigger_active/down L=" << input.trigger[0].active << '/' << input.trigger[0].down
				<< " R=" << input.trigger[1].active << '/' << input.trigger[1].down << '\n';
			const auto text = out.str();
			console::info("%s", text.c_str());
			utils::io::write_file_atomic("minidumps/h2-mod-vr-fire-latest.txt", text);
		}
	} // namespace
	namespace recoil
	{
		// Layout witnesses for the fields read by the native recoil adapter.
		static_assert(offsetof(game::WeaponDef, weapClass) == 1488);
		static_assert(offsetof(game::WeaponDef, hipGunKickPitchMin) == 3196);
		static_assert(offsetof(game::WeaponDef, hipGunKickPitchMax) == 3200);
		static_assert(offsetof(game::WeaponDef, hipViewKickPitchMin) == 3232);
		static_assert(offsetof(game::WeaponDef, hipViewKickPitchMax) == 3236);
		void confirmed_shot(const hold& owner, std::uint64_t reference, clock::time_point at,
			controller_input::posture actual) noexcept
		{
			if (!alive.load(std::memory_order_relaxed) || !recoil_enabled || !recoil_enabled->current.enabled ||
				!owner.can_fire() || !owner.weapon) return;
			const auto* definition = game::weapon_defs[owner.weapon & 0x1ffu];
			if (!definition) return;
			// The tracked gun replaces native flat-screen gun motion. Use its own
			// hip gun-kick range, falling back to view-kick magnitude only when
			// gun-kick data is absent. Neither path rotates the HMD.
			float degrees = native_pitch(definition->hipGunKickPitchMin, definition->hipGunKickPitchMax,actual);
			if (degrees <= 0) degrees = native_pitch(definition->hipViewKickPitchMin, definition->hipViewKickPitchMax,actual);
			if (degrees <= 0) return;
			const auto* raw_name=definition->szInternalName;
			const auto name=raw_name ? std::string_view{raw_name,strnlen_s(raw_name,64)} : std::string_view{};
			degrees *= weapon_scale(name,static_cast<int>(definition->weapClass));
			const auto mode = recoil_penalty ? std::clamp(recoil_penalty->current.integer, 0, 2) : 1;
			degrees *= multiplier(static_cast<penalty_mode>(mode), owner.support != hand::none,
				static_cast<int>(definition->weapClass),name,actual);
			recoil_state.shot(owner, reference, at, degrees);
		}
		float current_climb(const hold& owner, std::uint64_t reference, clock::time_point at) noexcept
		{return alive.load(std::memory_order_relaxed) && recoil_enabled && recoil_enabled->current.enabled ?
			recoil_state.current(owner, reference, at) : 0.f;}
		void clear() noexcept { recoil_state.clear(); }
	}

	hold sample_native_equipped() noexcept
	{
		if (carry::active()) return carry::current_hold();
		const auto token =
			identity_verified && alive.load(std::memory_order_relaxed) && game::CL_IsCgameInitialized()
				? *reinterpret_cast<const std::uint32_t*>(selected_weapon_address)
				: 0;
		const std::lock_guard lock(state_mutex);
		const auto before = holding.current();
		const auto result = holding.equipped(token);
		if (before.revision != result.revision)
			muzzle = {};
		return result;
	}
	hold current_hold() noexcept
	{
		if (carry::active()) return carry::current_hold();
		const std::lock_guard lock(state_mutex);
		return holding.current();
	}
	bool commit_grip(const hold& expected, grip_role role, hand owner) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (!holding.grip(expected, role, owner))
			return false;
		if (holding.current().revision != expected.revision)
			muzzle = {};
		return true;
	}
	bool publish_muzzle(const hold& expected, hand support, muzzle_frame value, hold& committed) noexcept
	{
		if (carry::active())
		{
			const auto owner=carry::current_hold();
			if (expected.id()!=owner.id() || expected.revision!=owner.revision || !valid_geometry(value)) return false;
			const std::lock_guard lock(state_mutex);
			committed=owner;value.owner=owner;muzzle=value;return true;
		}
		const std::lock_guard lock(state_mutex);
		const auto owner = holding.current();
		if (expected.id() != owner.id() || expected.revision != owner.revision || !valid_geometry(value))
			return false;
		if (owner.support != support && !holding.grip(expected, grip_role::support, support))
			return false;
		committed = holding.current();
		value.owner = committed;
		muzzle = value; // Lease and its pose become visible atomically.
		return true;
	}
	void invalidate_muzzle() noexcept
	{
		const std::lock_guard lock(state_mutex);
		muzzle = {};
	}
	muzzle_frame current_muzzle() noexcept
	{
		const std::lock_guard lock(state_mutex);
		return muzzle;
	}
	bool firing_enabled() noexcept
	{
		return alive.load(std::memory_order_relaxed) && hooks_installed && enabled &&
			   enabled->current.enabled;
	}
	fire_delivery controller_fire_delivery(std::uint32_t weapon,bool alternate) noexcept
	{
		const auto index=weapon&0x1ff;
		if (!hooks_installed || !index || !game::weapon_defs[index]) return fire_delivery::unsupported;
		if(game::weapon_defs[index]->szInternalName && std::string_view(game::weapon_defs[index]->szInternalName)=="usp_laserdesignator")return fire_delivery::scripted_device;
		const auto* name=game::weapon_defs[index]->szInternalName;
		const auto delivery=native_fire_delivery(utils::hook::invoke<int>(weapon_type_address,weapon,alternate),name ? name : "");
		return delivery==fire_delivery::native_projectile && !projectile_verified ? fire_delivery::unsupported : delivery;
	}
	bool firing_ready(const hold& owner, std::uint64_t reference,
					  controller_input::clock::time_point now) noexcept
	{
		const auto pose=current_muzzle();
		if (!firing_enabled() || !scripted_control::predicted_firing_allowed() || !ready(pose, owner, reference, now) ||
			!game::CL_IsCgameInitialized())
			return false;
		const auto* entity = &game::g_entities[0];
		return entity->client && entity_weapon(entity) == owner.weapon && entity_delivery(entity, owner.weapon)!=fire_delivery::unsupported &&
			shield::firing_clear(owner.id(),pose.position);
	}
	class component final : public component_interface
	{
	  public:
		void post_unpack() override
		{
			enabled = dvars::register_bool(
				"vr_controllerFire", true, game::DVAR_FLAG_SAVED,
				"Controller rear-grip trigger and muzzle shooting for bullet weapons and native launchers");
			disable_hip_fire_spread = dvars::register_bool(
				"vr_disableHipFireSpread", true, game::DVAR_FLAG_SAVED,
				"Remove hip-fire spread from VR muzzle shots while retaining native ADS baseline dispersion");
			command::add("vr_fire_status", print_status);
			const auto& assist = settings::aim_assist_strength;
			aim_assist_strength = dvars::register_float(assist.name, assist.default_value, assist.min, assist.max,
				game::DVAR_FLAG_SAVED, "VR muzzle aim assistance: 0 off (default), 100 selects targets within 10 degrees");
			recoil_enabled = dvars::register_bool(settings::recoil, true, game::DVAR_FLAG_SAVED,
				"Raise the tracked muzzle after each confirmed shot using native weapon recoil");
			static const char* recoil_modes[]{"all", "long", "off", nullptr};
			recoil_penalty = dvars::register_enum(settings::recoil_penalty, recoil_modes, 1, game::DVAR_FLAG_SAVED,
				"Single-hand recoil penalty: all weapons, long weapons, or off");
			constexpr std::uint8_t identity[]{0x48, 0x8d, 0x05, 0xc8, 0xa2, 0xab, 0x01, 0x48,
											  0x03, 0xf8, 0x8b, 0x47, 0x28, 0x89, 0x42, 0x14};
			identity_verified = verify(0x1403D0331, identity);
			constexpr std::uint8_t event_call[]{0xe8, 0x4e, 0xb8, 0x06, 0x00};
			constexpr std::uint8_t parameter_call[]{0xe8, 0xea, 0xfc, 0xff, 0xff};
			constexpr std::uint8_t parameter_entry[]{0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57,
													 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x70};
			constexpr std::uint8_t type_entry[]{0x40, 0x53, 0x48, 0x83, 0xec, 0x50, 0x48,
												0x8b, 0x05, 0x13, 0xff, 0x56, 0x00};
			constexpr std::uint8_t attack[]{0x83, 0x4a, 0x04, 0x01};
			constexpr std::uint8_t trace_entry[]{0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x78};
			constexpr std::uint8_t trace_mask[]{0xc7, 0x44, 0x24, 0x20, 0x31, 0xe8, 0x80, 0x02};
			if (!identity_verified || !verify(0x1404AD1ED, event_call) ||
				!verify(0x140518B91, parameter_call) || !verify(parameter_address, parameter_entry) ||
				!verify(weapon_type_address, type_entry) || !verify(0x1403CEDFC, attack) ||
				!verify(0x1404CBFE0, trace_entry) || !verify(0x1404AC57B, trace_mask))
			{
				reason = "native shot hook signature mismatch";
				console::error("[VR fire] Native signatures rejected; controller fire is unavailable\n");
				return;
			}
			utils::hook::call(0x140518B91, parameters_stub);
			utils::hook::call(0x1404AD1ED, fire_stub);
			hooks_installed = true;
			// The same parameter builder precedes BOTH projectile branches. Retain
			// PM_Weapon's ADS, delay, lock-on and ammo gates, then let G_FireWeapon
			// supply the native attacker, target, fuse and projectile ownership.
			constexpr std::uint8_t projectile_dispatch[]{0x83,0xf8,0x03,0x0f,0x85,0x29,0x01,0x00,0x00};
			constexpr std::uint8_t grenade_call[]{0xe8,0x85,0x40,0x00,0x00};
			constexpr std::uint8_t missile_call[]{0xe8,0xd5,0x47,0x00,0x00};
			constexpr std::uint8_t lock_gate[]{0x80,0xbd,0x77,0x0e,0x00,0x00,0x00,0x74,0x1b,
				0xf6,0x83,0x0c,0x06,0x00,0x00,0x02};
			constexpr std::uint8_t ads_gate[]{0xe8,0x87,0xd1,0x00,0x00,0x41,0x3b,0xc6};
			projectile_verified=verify(0x140518C6E,projectile_dispatch) && verify(0x140518CB6,grenade_call) &&
				verify(0x140518D96,missile_call) && verify(0x1406993FD,lock_gate) && verify(0x140694444,ads_gate);
			if (!projectile_verified) console::error("[VR fire] Native projectile signatures rejected; launcher input unavailable\n");

			// Verify the six-argument bullet ABI and the original ADS query.
			// A mismatch disables this policy without breaking controller aiming.
			constexpr std::uint8_t bullet_call[]{0xe8, 0xcd, 0x16, 0xf9, 0xff};
			constexpr std::uint8_t bullet_arguments[]{0x4c, 0x8d, 0x44, 0x24, 0x60,
				0xf3, 0x0f, 0x11, 0x44, 0x24, 0x28, 0x4c, 0x8b, 0xce, 0x0f, 0x28, 0xce,
				0x44, 0x89, 0x7c, 0x24, 0x20, 0x48, 0x8b, 0xce};
			constexpr std::uint8_t bullet_entry[]{0x48, 0x83, 0xec, 0x48, 0xf3, 0x0f, 0x10,
				0x44, 0x24, 0x78, 0x8b, 0x44, 0x24, 0x70, 0xf3, 0x0f, 0x11, 0x44, 0x24, 0x30,
				0xc6, 0x44, 0x24, 0x28, 0x00, 0x89, 0x44, 0x24, 0x20, 0xe8, 0x3e, 0x0a, 0x00, 0x00};
			constexpr std::uint8_t ads_spread_call[]{0xe8, 0xb2, 0x8a, 0x18, 0x00};
			constexpr std::uint8_t ads_spread_entry[]{0x48, 0x83, 0xec, 0x28, 0x45, 0x0f, 0xb6,
				0xc8, 0x8b, 0xc2, 0x4c, 0x8d, 0x05, 0xff, 0xfe, 0x75, 0x0c};
			if (verify(0x140518C2E, bullet_call) && verify(0x140518C15, bullet_arguments) &&
				verify(bullet_address, bullet_entry) && verify(0x140518BB9, ads_spread_call) &&
				verify(ads_spread_address, ads_spread_entry))
			{
				utils::hook::call(0x140518C2E, bullets_stub);
				spread_hook_installed = true;
			}
			else
				console::error("[VR fire] Native spread signatures rejected; hip-fire spread override is unavailable\n");
		}
		void pre_destroy() override
		{
			alive.store(false, std::memory_order_relaxed);
			recoil::clear();
		}
	};
} // namespace vr::gameplay::weapons
REGISTER_COMPONENT(vr::gameplay::weapons::component)
