#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::dragunov
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: e50169fa08487e5ee5ab9be58e5ca8e47203ffecfd79da4a4d81bf6388e9d32d
			inline constexpr std::array<std::array<unsigned, 2>, 4> surfaces{{
			    {4765, 6532},
			    {9147, 9288},
			    {5226, 6060},
			    {24, 22},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {2, 354, 4307},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {2, 5010, 5321},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {2, 5010, 5321},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {2, 5010, 5321},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.00000000f, 0.00000000f, -0.49226440f}},
			    {cartridge_2, {0.00000000f, 0.00000000f, -0.98452880f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_dragunov_base",
			    .bones = 19,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {5.46496909f, -0.65619500f, -2.53418994f},
			        {5.46496909f, -0.65619500f, -2.53418994f},
			        {5.46496909f, -0.65619500f, -2.53418994f},
			        {5.46496909f, -0.65619500f, -2.53418994f},
			    }},
			    .high = {{
			        {9.17743097f, 0.65619500f, 1.94668695f},
			        {9.17743097f, 0.65619500f, 1.94668695f},
			        {9.17743097f, 0.65619500f, 1.94668695f},
			        {9.17743097f, 0.65619500f, 1.94668695f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
