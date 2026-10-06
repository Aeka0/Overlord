#include <std_include.hpp>
#include "weapon_carry_runtime.hpp"
#include "native_ammunition.hpp"
#include "native_carry_selection_policy.hpp"
#include "native_scripted_control.hpp"
#include "weapon_interaction.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>
#include <mutex>
#include <sstream>

namespace vr::gameplay::weapons::native_carry_selection
{
	namespace
	{
		constexpr std::uintptr_t finish_change=0x140698AE0;
		utils::hook::detour finish_hook;
		bool installed{};
		struct sample {int role{},state{},milliseconds{};std::uint32_t weapon{};};
		std::array<sample,16> samples{};std::uint64_t count{};
		std::array<std::atomic_uint64_t,2> deferred{};
		std::mutex mutex;
		template<class T> T read(const void* p,std::size_t offset) noexcept
		{T result{};std::memcpy(&result,static_cast<const std::byte*>(p)+offset,sizeof(result));return result;}
		std::uint32_t pending_device(const void* ps)
		{
			const auto pending=carry::pending_abdominal_weapon();
			return pending && controller_fire_delivery(pending)==fire_delivery::scripted_device &&
				scripted_control::allowed(ps)?pending:0;
		}
		std::uintptr_t finish_stub(game::pmove_t* pm,int state,void* native_context,bool alternate,bool arg5,bool animate)
		{
			static_assert(offsetof(game::pmove_t,cmd)+offsetof(game::usercmd_s,weapon)==0x1c);
			const auto owner=carry::current_hold();
			const auto target=pm ? pm->cmd.weapon : 0;
			const int role=pm ? native_ammunition::local_role(pm->ps) : -1;
			const int before=role>=0 ? read<int>(pm->ps,0x2c0) : -1;
			const bool local=installed && carry::active() && role>=0;
			const auto pending=local?pending_device(pm->ps):0;
			const auto action=local?admit(target,owner.weapon,pending,read<std::uint32_t>(pm->ps,0x3bc),
				read<std::uint32_t>(pm->ps,0x3c0),before,alternate):admission::native;
			if(action==admission::defer)
			{
				// Finish-change would select the stale command and consume bit27.
				// Let the next native simulation/prediction tick finish the same
				// transition when the mission's requested command has arrived.
				++deferred[role];return 0;
			}
			const auto result=finish_hook.invoke<std::uintptr_t>(pm,state,native_context,alternate,arg5,animate);
			if (action!=admission::physical || !carry::active()) return result;
			const auto live=carry::current_hold();
			const bool same_hold=target==owner.weapon && live.weapon==owner.weapon && live.revision==owner.revision;
			const bool same_request=target==pending && pending && pending_device(pm->ps)==pending;
			if ((!same_hold && !same_request) || read<std::uint32_t>(pm->ps,0x3bc)!=target) return result;
			const int after=read<int>(pm->ps,0x2c0),duration=read<int>(pm->ps,0x2b4);
			if ((after!=1 && after!=2) || duration<=0) return result;
			// Audited native finish-change owns selection, events and animation.
			// Its output still schedules the ordinary raise (850 ms in the live
			// witness) after switchtoweaponimmediate. Physical acquisition has
			// already happened or is reserved by the mission draw: expire only
			// that raise's weaponTime on simulation and prediction. The next native tick completes the state;
			// never reset reload/fire timers, set idle, or replay this routine.
			const int zero{};std::memcpy(reinterpret_cast<std::byte*>(pm->ps)+0x2b4,&zero,sizeof(zero));
			const std::lock_guard lock(mutex);samples[count++%samples.size()]={role,after,duration,target};
			return result;
		}
	}
	class component final:public component_interface
	{
	public:
		void post_unpack() override
		{
			constexpr std::array<std::uint8_t,15> bytes{0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x53,0x55,0x56,0x57,0x41,0x57};
			std::array<std::uint8_t,bytes.size()> mask;mask.fill(255);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(finish_change),{bytes.data(),mask.data(),bytes.size()})) return;
			// This is the selected-weapon store followed by consumption of the
			// immediate-switch flag. Deferral must precede both native effects.
			constexpr std::array<std::uint8_t,20> selection_bytes{0x89,0x9e,0xbc,0x03,0,0,0x0f,0xba,0xf1,0x1b,
				0x89,0x54,0x24,0x24,0x89,0x8e,0xc0,0x03,0,0};
			std::array<std::uint8_t,selection_bytes.size()> selection_mask;selection_mask.fill(255);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140698BFA),
				{selection_bytes.data(),selection_mask.data(),selection_bytes.size()})) return;
			finish_hook.create(finish_change,finish_stub);installed=true;
			command::add("vr_carry_selection_status",[] {
				std::ostringstream out;
				{const std::lock_guard lock(mutex);out << "installed=" << installed << " raises_expired=" << count
					<< " stale_draw_finishes_deferred=" << deferred[0].load() << '/' << deferred[1].load() << '\n';
				for (auto i=count>samples.size() ? count-samples.size() : 0;i<count;++i) {const auto& s=samples[i%samples.size()];
					out << "role=" << s.role << " weapon=" << s.weapon << " state=" << s.state << " raise_ms=" << s.milliseconds << "->0\n";}}
				const auto text=out.str();console::info("%s",text.c_str());
				scheduler::once([text]{utils::io::write_file_atomic("minidumps/overlord-carry-selection.txt",text);},scheduler::pipeline::async);
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::native_carry_selection::component)
