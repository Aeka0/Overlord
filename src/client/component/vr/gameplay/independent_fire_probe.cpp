#include <std_include.hpp>
#ifdef DEBUG
#include "native_ballistics.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>
#include <sstream>

namespace vr::gameplay::weapons::independent_fire_probe
{
	namespace
	{
		utils::hook::detour trace_hook;
		bool installed{};
		std::atomic<bool> busy{};
		struct trace_sample {std::uint32_t weapon{}; float fraction{}; hands::vec end{};};
		struct observation {std::array<trace_sample,32> traces{}; unsigned count{};};
		thread_local observation* active{};
		template <typename T> T read(const void* p,std::size_t offset)
		{ T value{}; std::memcpy(&value,static_cast<const std::byte*>(p)+offset,sizeof(value)); return value; }
		bool trace_stub(void* bullet,std::uint32_t weapon,bool alternate,game::gentity_s* source,void* trace,int surface)
		{
			const auto result=trace_hook.invoke<bool>(bullet,weapon,alternate,source,trace,surface);
			if (active && source==&game::g_entities[0] && trace && active->count<active->traces.size())
				active->traces[active->count++]={weapon,read<float>(trace,0),read<hands::vec>(trace,0x38)};
			return result;
		}
		bool install()
		{
			if (installed) return true;
			constexpr std::uint8_t bytes[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,
				0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40};
			std::array<std::uint8_t,sizeof(bytes)> mask{};mask.fill(0xff);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1404AC4E0),
				{bytes,mask.data(),sizeof(bytes)}) || !native_ballistics::initialize()) return false;
			trace_hook.create(0x1404AC4E0,trace_stub);
			return installed=true;
		}
		struct identity
		{
			std::uint32_t selected{}, entity{}, ps{}, flags{};
			int state{}, timer{};
			std::array<std::byte,180> inventory{};
			bool operator==(const identity&) const = default;
		};
		identity capture(const void* ps)
		{
			identity result{*reinterpret_cast<const std::uint32_t*>(0x141E8A628),
				read<std::uint32_t>(&game::g_entities[0],0x80),read<std::uint32_t>(ps,0x3bc),
				read<std::uint32_t>(ps,0x3c0),read<int>(ps,0x2c0),read<int>(ps,0x2b4)};
			std::memcpy(result.inventory.data(),static_cast<const std::byte*>(ps)+0x2f8,result.inventory.size());
			return result;
		}
		void run(const std::array<std::string,2>& names,std::size_t count)
		{
			const auto finish=gsl::finally([] {busy=false;});
			std::ostringstream out;
			out << "[VR independent fire probe] diagnostic_only=1 shots_requested=" << count << '\n';
			const auto publish=gsl::finally([&] {
				const auto text=out.str();console::info("%s",text.c_str());
				scheduler::once([text] {utils::io::write_file_atomic("minidumps/h2-mod-vr-independent-fire.txt",text);},scheduler::pipeline::async);
			});
			if (!game::CL_IsCgameInitialized() || !game::g_entities[0].client || !install())
			{out << "rejected: world or native signatures unavailable\n";return;}
			auto* ps=reinterpret_cast<game::playerState_s*>(game::g_entities[0].client);
			const auto before=capture(ps);
			if (before.state || before.timer || before.flags)
			{out << "rejected: native weapon must be idle in primary mode\n";return;}
			std::array<std::uint32_t,2> tokens{};
			std::array<native_ammunition::snapshot,2> ammunition{};
			for (std::size_t i=0;i<count;++i)
			{
				tokens[i]=game::G_GetWeaponForName(names[i].c_str()).data;
				ammunition[i]=native_ammunition::observe_carried(ps,tokens[i]);
				if (!ammunition[i].valid || ammunition[i].loaded<1 || (i && tokens[0]==tokens[1]))
				{out << "rejected: unique loaded owned weapons required\n";return;}
			}
			const auto selected_before=native_ammunition::observe_carried(ps,before.selected);
			const auto time=ps->commandTime;
			bool passed=true;
			out << "command_time=" << time << " selected=" << before.selected << '\n';
			for (std::size_t i=0;i<count;++i)
			{
				if (game::g_entities[0].client!=reinterpret_cast<game::gclient_s*>(ps) || capture(ps)!=before)
				{out << "aborted: world/selection changed between shots\n";passed=false;break;}
				observation observed;
				active=&observed;
				const auto clear=gsl::finally([] {active=nullptr;});
				// Distinct downward rays demonstrate separate origins while keeping
				// this diagnostic independent of unimplemented off-hand rendering.
				shot_geometry ray{{0,0,-1},{0,-1,0},{1,0,0},
					{ps->origin[0]+(i ? 4.f : -4.f),ps->origin[1],ps->origin[2]+ps->viewHeightCurrent}};
				const auto result=native_ballistics::fire_owned(tokens[i],ray,time);
				bool shot_ok=result.status==native_ballistics::outcome::emitted && result.after.valid &&
					result.after.loaded==ammunition[i].loaded-1 && result.after.reserve==ammunition[i].reserve && observed.count>0;
				out << "weapon=" << tokens[i] << " name=" << names[i] << " status=" << static_cast<int>(result.status)
					<< " loaded=" << result.before.loaded << "->" << result.after.loaded
					<< " reserve=" << result.before.reserve << "->" << result.after.reserve
					<< " traces=" << observed.count << '\n';
				for (unsigned t=0;t<observed.count;++t)
				{
					const auto& sample=observed.traces[t];shot_ok &= sample.weapon==tokens[i];
					out << "  trace weapon=" << sample.weapon << " fraction=" << sample.fraction
						<< " hit=[" << sample.end[0] << ',' << sample.end[1] << ',' << sample.end[2] << "]\n";
				}
				passed &= shot_ok;
			}
			if (game::g_entities[0].client!=reinterpret_cast<game::gclient_s*>(ps))
			{out << "aborted: player changed\n";return;}
			const auto after=capture(ps);
			bool selection_stable=before==after;
			passed &= selection_stable;
			if (std::find(tokens.begin(),tokens.begin()+count,before.selected)==tokens.begin()+count)
			{
				const auto selected_after=native_ammunition::observe_carried(ps,before.selected);
				passed &= selected_before==selected_after;
				out << "unrequested_selected_ammo_unchanged=" << (selected_before==selected_after) << '\n';
			}
			out << "selection_inventory_timers_unchanged=" << selection_stable << " pass=" << passed
				<< " character_damage_not_tested=1 presentation_not_tested=1\n";
		}
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			command::add("vr_fire_owned_probe",[](const command::params& args) {
				if (args.size()<2 || args.size()>3)
				{console::info("Usage: vr_fire_owned_probe <owned_weapon> [second_owned_weapon]\n");return;}
				std::array<std::string,2> names{};
				for (int i=1;i<args.size();++i)
				{
					const std::string_view name=args[i];
					if (name.empty() || name.size()>63 || !std::all_of(name.begin(),name.end(),[](char c) {
						return c=='_' || (c>='0' && c<='9') || (c>='a' && c<='z');})) return;
					names[i-1]=name;
				}
				if (busy.exchange(true)) return;
				scheduler::once([names,count=static_cast<std::size_t>(args.size()-1)] {run(names,count);},scheduler::pipeline::server);
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::independent_fire_probe::component)
#endif
