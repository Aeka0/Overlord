#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::aa12
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: e7505533790245f78285348e5726fd86d56eee2c6fbac7374aa7f7574191a8d2
			inline constexpr std::array<std::array<unsigned, 2>, 6> surfaces{{
			    {4076, 4316},
			    {8050, 8024},
			    {3320, 3892},
			    {3967, 4904},
			    {910, 978},
			    {3803, 4196},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {4, 0, 977},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {2, 2372, 3203},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {2, 2372, 3203},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {2, 2372, 3203},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.06239707f, 0.00000000f, -1.11593368f}},
			    {cartridge_2, {0.12479414f, 0.00000000f, -2.23186736f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_aa12_base",
			    .bones = 14,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {4.73920492f, -0.68094097f, -4.71917175f},
			        {4.73920492f, -0.68094097f, -4.71917175f},
			        {4.73920492f, -0.68094097f, -4.71917175f},
			        {4.73920492f, -0.68094097f, -4.71917175f},
			    }},
			    .high = {{
			        {8.89629379f, 0.68521303f, 4.11635572f},
			        {8.89629379f, 0.68521303f, 4.11635572f},
			        {8.89629379f, 0.68521303f, 4.11635572f},
			        {8.89629379f, 0.68521303f, 4.11635572f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
