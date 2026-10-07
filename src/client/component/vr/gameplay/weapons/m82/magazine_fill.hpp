#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::m82
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: eab1f4ba514dfe97509d87d5f8df82ea3e6e2873cbd936f9bf09b7506e699b2c
			inline constexpr std::array<std::array<unsigned, 2>, 3> surfaces{{
			    {1806, 2246},
			    {8828, 9154},
			    {4732, 5498},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {0, 0, 1397},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {0, 1398, 1821},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {0, 1822, 2245},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {0, 1398, 1821},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_2, {-0.00000000f, -0.00000000f, -1.16759732f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_m82_base",
			    .bones = 23,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {4.15725783f, -0.84647701f, -2.66691493f},
			        {4.15725783f, -0.84647701f, -2.66691493f},
			        {4.15725783f, -0.84647701f, -2.66691493f},
			        {4.15725783f, -0.84647701f, -2.66691493f},
			    }},
			    .high = {{
			        {10.68723483f, 0.84647701f, 3.50533808f},
			        {10.68723483f, 0.84647701f, 3.50533808f},
			        {10.68723483f, 0.84647701f, 3.50533808f},
			        {10.68723483f, 0.84647701f, 3.50533808f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
