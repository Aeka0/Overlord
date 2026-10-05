#include <std_include.hpp>
#include "controller_calibration.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"

namespace vr::controller_calibration
{
	namespace
	{
		std::array<game::dvar_t*,6> controls{};
		configuration native_settings() noexcept
		{
			configuration values;
			for (unsigned i=0;i<3;++i)
			{
				if (const auto* setting=controls[i]) values.orientation[i]=setting->current.value;
				if (const auto* setting=controls[i+3]) values.pivot[i]=setting->current.value;
			}
			return values;
		}
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			constexpr const char* descriptions[]{"Controller-local pitch calibration in degrees; positive tilts up",
				"Controller-local yaw calibration in degrees; positive turns left",
				"Controller-local roll calibration in degrees; positive rolls right side down"};
			for (unsigned i=0;i<3;++i)
			{
				const auto& s=settings::hand_angles[i];
				controls[i]=dvars::register_float(s.name,s.default_value,s.min,s.max,game::DVAR_FLAG_SAVED,descriptions[i]);
				const auto& p=settings::wrist_pivots[i];
				controls[i+3]=dvars::register_float(p.name,p.default_value,p.min,p.max,game::DVAR_FLAG_SAVED,
					"Physical grip-local wrist pivot in meters; independent of hand alignment offsets");
			}
			set_settings_provider(native_settings);
		}
	};
}
REGISTER_COMPONENT(vr::controller_calibration::component)
