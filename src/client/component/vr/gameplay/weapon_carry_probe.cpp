#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#ifdef DEBUG
#include "native_ammunition.hpp"
#include "native_carry.hpp"
#include "native_carry_model.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapon_carry_profiles.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "game/scripting/entity.hpp"
#include "game/scripting/execution.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>
#include <sstream>

namespace vr::gameplay::weapons::carry_probe
{
	namespace
	{
		std::atomic_bool busy{};
		native_carry::world_item dropped{};
		template<class T> T read(const void* p,std::size_t offset)
		{ T v{}; std::memcpy(&v,static_cast<const std::byte*>(p)+offset,sizeof(v)); return v; }
		void capture(std::ostringstream& out,const char* stage)
		{
			if (!game::CL_IsCgameInitialized() || !game::g_entities[0].client) {out << stage << " no_world\n";return;}
			const auto* ps=game::g_entities[0].client;
			out << stage << " selected=" << vr::h2::sp::weapon_selection_request.read()
				<< " ps=" << read<std::uint32_t>(ps,0x3bc) << " entity=" << read<std::uint32_t>(&game::g_entities[0],0x80)
				<< " state=" << read<int>(ps,0x2c0) << " flags=" << read<std::uint32_t>(ps,0x3c0) << " owned=";
			for (unsigned i=0;i<15;++i)
			{
				const auto token=read<std::uint32_t>(ps,0x2f8+i*4);
				if (!token) continue;
				const auto ammo=native_ammunition::observe_carried(ps,token);
				out << token << ':' << ammo.valid << ':' << ammo.loaded << ':' << ammo.reserve << ',';
			}
			out << '\n';
		}
		void publish(const std::string& text)
		{
			console::info("%s",text.c_str());
			scheduler::once([text] {utils::io::write_file_atomic("minidumps/overlord-carry-probe.txt",text);},scheduler::pipeline::async);
		}
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			// Explicit diagnostic actions only. Nothing runs automatically or fabricates inventory.
			command::add("vr_carry_probe",[](const command::params& args) {
				const auto action=args.size()>1 ? args.get(1) : std::string("status");
				const std::string name=args.size()>2 ? args.get(2) : "";
				const bool left=args.size()>3 && std::string_view(args.get(3))=="left";
				if (action!="status" && action!="empty" && action!="drop" && action!="select" && action!="pickup" && action!="stow" && action!="draw") return;
				if (busy.exchange(true)) return;
				scheduler::once([action,name,left] {
					auto out=std::make_shared<std::ostringstream>();
					*out << "diagnostic_only=1 action=" << action << '\n'; capture(*out,"before");
					if (!game::CL_IsCgameInitialized() || !game::g_entities[0].client)
					{publish(out->str());busy=false;return;}
					const auto* ps=game::g_entities[0].client;
					const auto selected=read<std::uint32_t>(ps,0x3bc);
					try
					{
						if (action=="empty") game::G_SelectWeapon(0,game::Weapon{});
						if (action=="select")
						{
							const auto token=game::G_GetWeaponForName(name.c_str());
							for (unsigned i=0;i<15;++i) if (token.data && read<std::uint32_t>(ps,0x2f8+i*4)==token.data)
							{game::G_SelectWeapon(0,token);break;}
						}
						if (action=="drop")
						{
							const auto* state=reinterpret_cast<const game::playerState_s*>(ps);
							const hands::vec eye{state->origin[0],state->origin[1],state->origin[2]+state->viewHeightCurrent};
							const hands::anchor gun{hands::add(eye,{10,0,-12}),{0,0,0,1}};
							carry::instance instance;instance.id=carry::native_identity(selected);
							native_carry::world_key entity;
							const bool clear=native_carry::clearance(selected,gun,eye);
							const bool ok=clear && native_carry::drop(instance,gun,{0,0,0},entity);
							*out << "clearance=" << clear << " dropped=" << ok << " entity=" << entity.entity << " generation=" << entity.generation << " native=" << native_carry::status() << '\n';
							if (ok)
							{
								const auto* world=&game::g_entities[entity.entity];
								*out << "physics=" << read<void*>(world,0x140) << " trajectory=" << read<int>(world,0x10)
									<< " clip=" << read<int>(world,0x194) << " reserve=" << read<int>(world,0x190) << '\n';
							}
							if (ok) dropped={entity,selected,gun.position};
						}
						if (action=="pickup") {carry::identity id;*out << "pickup=" << native_carry::pickup(dropped,id) << " recovered=" << id.weapon << ':' << id.generation << " native=" << native_carry::status() << '\n';}
						if (action=="stow") *out << "stow=" << carry::debug_release(name=="left" ? carry::location::left_waist : name=="right" ? carry::location::right_waist : carry::location::back,false) << '\n';
						if (action=="draw") *out << "draw=" << carry::debug_draw(name=="left" ? carry::location::left_waist : name=="right" ? carry::location::right_waist : carry::location::back,left ? hand::left : hand::right) << '\n';
					}
					catch(const std::exception& e) {*out << "error=" << e.what() << '\n';}
					capture(*out,"after");
					scheduler::once([out,ps,selected,action] {
						capture(*out,"settled_1500ms");
						if (action=="empty" && game::CL_IsCgameInitialized() && game::g_entities[0].client==ps &&
							native_ammunition::observe_carried(ps,selected).valid) game::G_SelectWeapon(0,game::Weapon{selected});
						publish(out->str());busy=false;
					},scheduler::pipeline::server,1500ms);
				},scheduler::pipeline::server);
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::carry_probe::component)
#endif
