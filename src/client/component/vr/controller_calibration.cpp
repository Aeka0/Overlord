#include <std_include.hpp>
#include "controller_calibration.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"

namespace vr::controller_calibration
{
	namespace
	{
		std::array<std::array<game::dvar_t*, 6>, 2> controls{};
		std::array<std::array<std::array<game::dvar_t*,3>,2>,2> hand_controls{};
		std::array<game::dvar_t*,2> independent_controls{};
		configuration native_settings(controller_pose_pipeline::mode mode) noexcept
		{
			auto values = defaults_for(mode);
			const unsigned index=mode == controller_pose_pipeline::mode::standard ? 0 : 1;
			const auto& bank = controls[index];
			if (const auto* setting=independent_controls[index]) values.independent=setting->current.enabled;
			for (unsigned i=0;i<3;++i)
			{
				if (const auto* setting=bank[i]) values.orientation[i]=setting->current.value;
				if (const auto* setting=bank[i+3]) values.position[i]=setting->current.value;
				for (unsigned h=0;h<2;++h)
					if (const auto* setting=hand_controls[index][h][i]) values.hands[h][i]=setting->current.value;
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
				const auto& hands = bank == 0 ? settings::standard_independent_hand_angles : settings::independent_hand_angles;
				const auto& independent=settings::hand_angles_independent[bank];
				independent_controls[bank]=dvars::register_bool(independent.name,independent.default_value,
					game::DVAR_FLAG_SAVED,"Use separate physical left/right aim calibration");
				const auto& initialized=settings::hand_angles_initialized[bank];
				dvars::register_bool(initialized.name,initialized.default_value,game::DVAR_FLAG_SAVED,
					"Independent hand angles have been initialized by the launcher");
				for (unsigned i = 0; i < 3; ++i)
				{
					const auto& angle = alignment[i + 3];
					controls[bank][i] = dvars::register_float(angle.name, angle.default_value, angle.min,
					    angle.max, game::DVAR_FLAG_SAVED, "Aim-local pitch/yaw/roll calibration in degrees");
					const auto& position = alignment[i];
					controls[bank][i+3] = dvars::register_float(position.name, position.default_value,
						position.min, position.max, game::DVAR_FLAG_SAVED, "Grip-local wrist point calibration in meters");
					for (unsigned h=0;h<2;++h)
					{
						const auto& field=hands[h][i];
						hand_controls[bank][h][i]=dvars::register_float(field.name,field.default_value,field.min,
							field.max,game::DVAR_FLAG_SAVED,"Physical hand aim-local pitch/yaw/roll in degrees");
					}
				}
			}
			set_settings_provider(native_settings);
		}
	};
}
REGISTER_COMPONENT(vr::controller_calibration::component)
