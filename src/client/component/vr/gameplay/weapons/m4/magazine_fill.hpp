#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::m4
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 90307e116e650ae82a652c24bac94b8ef9fcec4af19f9b097e82a3c56fd3ff44
			inline constexpr std::array<std::array<unsigned, 2>, 9> surfaces{{
			    {414, 604},
			    {4167, 4750},
			    {9424, 12548},
			    {4159, 5900},
			    {15719, 15982},
			    {1390, 1442},
			    {3322, 3908},
			    {3322, 3908},
			    {540, 896},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 2> body{{
			    {3, 62, 3971},
			    {5, 0, 427},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> cartridge_0{{
			    {8, 352, 703},
			    {8, 720, 735},
			    {8, 816, 895},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> cartridge_1{{
			    {8, 0, 351},
			    {8, 704, 719},
			    {8, 736, 815},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> cartridge_2{{
			    {8, 352, 703},
			    {8, 720, 735},
			    {8, 816, 895},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_2, {-0.00000000f, -0.00000000f, -0.39153203f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_m4_base",
			    .bones = 24,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {3.50614383f, -0.60800600f, -4.17592807f},
			        {3.50614383f, -0.60800600f, -4.17592807f},
			        {3.50614383f, -0.60800600f, -4.17592807f},
			        {3.50614383f, -0.60800600f, -4.17592807f},
			    }},
			    .high = {{
			        {7.05197177f, 0.61434798f, 3.38810373f},
			        {7.05197177f, 0.61434798f, 3.40265890f},
			        {7.05197177f, 0.61434798f, 3.40265890f},
			        {7.05197177f, 0.61434798f, 3.40265890f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
