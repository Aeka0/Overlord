#include <std_include.hpp>
#include "stabilization.hpp"
#include "settings.hpp"
#include "game/dvars.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"

namespace vr::stabilization
{
	namespace
	{
		std::array<game::dvar_t*,3> enabled{},strength{};
		configuration read_settings() noexcept
		{
			configuration out;control* controls[]{&out.desktop,&out.head,&out.hand};
			for(unsigned i=0;i<3;++i)if(enabled[i] && strength[i])*controls[i]={enabled[i]->current.enabled,strength[i]->current.value};
			return out;
		}
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			constexpr const char* descriptions[]{"Smooth desktop view rotation only", "Light headset micro-jitter filter; may add tracking lag", "Smooth shared hand, aim and interaction poses"};
			for(unsigned i=0;i<3;++i)
			{
				enabled[i]=dvars::register_bool(vr::settings::stabilization_toggles[i],false,game::DVAR_FLAG_SAVED,descriptions[i]);
				const auto& s=vr::settings::stabilization_strengths[i];
				strength[i]=dvars::register_float(s.name,s.default_value,s.min,s.max,game::DVAR_FLAG_SAVED,"Stabilization strength, 0 bypasses; applies immediately");
			}
			settings_provider.store(read_settings);
			command::add("vr_stabilization_status",[]{const auto s=settings();console::info("[VR stabilization] desktop=%g head=%g hand=%g gameplay=%d epoch=%llu\n",
				s.desktop.amount(),s.head.amount(),s.hand.amount(),gameplay.load(),epoch.load());});
		}
		void pre_destroy() override {settings_provider.store(nullptr);invalidate();}
	};
}
REGISTER_COMPONENT(vr::stabilization::component)
