#include <std_include.hpp>
#include "sentry_runtime.hpp"
#include "native_scripted_control.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/scripting/execution.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::sentry
{
	namespace
	{
		using clock=controller_input::clock;
		struct snapshot {std::uint64_t epoch{};clock::time_point at{};};
		std::mutex mutex;
		snapshot published;
		bool bound{};
		unsigned carrying{};
		std::uint64_t next_epoch{},epoch{};

		bool weapons_disabled(const void* ps) noexcept
		{
			bool dead{};std::uint32_t flags{};
			return ps && scripted_control::native_contract() && player_life::read(ps,dead) && !dead &&
				utils::native_memory::read_bytes(&flags,static_cast<const std::byte*>(ps)+0x3c0,sizeof(flags)) &&
				!scripted_control::permits_weapons(flags);
		}
		void clear()
		{
			carrying=0;epoch=0;
			const std::lock_guard lock(mutex);published={};
		}
		void bind()
		{
			clear();bound=false;
			const auto* map=game::Dvar_FindVar("mapname");
			if(!map || !map->current.string || std::string_view(map->current.string)!="invasion")return;
			const auto file=scripting::script_function_table_sort.find(scripting::get_token_single(0xd2a4));
			if(file==scripting::script_function_table_sort.end())return;
			const auto waiter=scripting::get_token_single(0xabd2);
			for(const auto& [name,pos]:file->second)if(name==waiter && pos){bound=true;break;}
		}
		void update()
		{
			if(!bound)return;
			unsigned observed{};
			if(game::CL_IsCgameInitialized() && *game::levelEntityId && weapons_disabled(game::g_entities[0].client))try
			{
				const scripting::entity player{game::scr_entref_t{0,0}};
				const auto placing=player.get("placingsentry");
				// _id_B557 sets BA84 only after pickup completes, and clears it
				// before _id_A9A6 starts the native drop/weapon restoration.
				const auto waiting=player.get(scripting::get_token_single(0xba84));
				if(placing.is<scripting::entity>() && waiting.is<int>() && waiting.as<int>()==1)
				{
					const auto turret=placing.as<scripting::entity>();
					const auto carrier=turret.get("carrier"),type=turret.get("sentrytype");
					if(carrier.is<scripting::entity>() && carrier.as<scripting::entity>()==player &&
						type.is<std::string>() && type.as<std::string>()=="sentry_minigun")observed=turret.get_entity_id();
				}
			}
			catch(const std::exception&){}
			if(!observed){clear();return;}
			if(observed!=carrying){carrying=observed;epoch=++next_epoch;}
			const std::lock_guard lock(mutex);published={epoch,clock::now()};
		}
	}
	std::uint64_t placement_epoch() noexcept
	{
		snapshot value;{const std::lock_guard lock(mutex);value=published;}
		const auto now=clock::now();
		if(!value.epoch || now<value.at || now-value.at>150ms || !game::CL_IsCgameInitialized() ||
			!weapons_disabled(game::CG_GetPredictedPlayerState(0)))return 0;
		return value.epoch;
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			scripting::on_level_start(bind);
			scripting::on_shutdown([](bool,bool after){if(!after){bound=false;clear();}});
			scheduler::loop(update,scheduler::pipeline::server);
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::sentry::component)
