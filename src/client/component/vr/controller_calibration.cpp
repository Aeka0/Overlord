#include <std_include.hpp>
#include "controller_calibration.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"

namespace vr::controller_calibration
{
	namespace
	{
		std::array<std::array<game::dvar_t*, 6>, 2> controls{};
		configuration native_settings(controller_pose_pipeline::mode mode) noexcept
		{
			auto values = defaults_for(mode);
			const auto& bank = controls[mode == controller_pose_pipeline::mode::standard ? 0 : 1];
			for (unsigned i=0;i<3;++i)
			{
				if (const auto* setting=bank[i]) values.orientation[i]=setting->current.value;
				if (const auto* setting=bank[i+3]) values.position[i]=setting->current.value;
			}
			return values;
		}
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			for (unsigned bank = 0; bank < controls.size(); ++bank)
			{
				const auto& alignment = bank == 0 ? settings::standard_hand_alignment : settings::hand_alignment;
				for (unsigned i = 0; i < 3; ++i)
				{
					const auto& angle = alignment[i + 3];
					controls[bank][i] = dvars::register_float(angle.name, angle.default_value, angle.min,
					    angle.max, game::DVAR_FLAG_SAVED, "Aim-local pitch/yaw/roll calibration in degrees");
					const auto& position = alignment[i];
					controls[bank][i+3] = dvars::register_float(position.name, position.default_value,
						position.min, position.max, game::DVAR_FLAG_SAVED, "Grip-local wrist point calibration in meters");
				}
			}
			set_settings_provider(native_settings);
		}
	};
}
REGISTER_COMPONENT(vr::controller_calibration::component)
