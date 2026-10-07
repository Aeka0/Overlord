#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::cheytac
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 7895595603031f94d318cfb8b789bc1f4c10a5e4543885efe1862e738fdaf035
			inline constexpr std::array<std::array<unsigned, 2>, 5> surfaces{{
			    {1024, 1160},
			    {1750, 2280},
			    {17707, 18748},
			    {2579, 3242},
			    {2256, 2762},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {0, 0, 873},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {0, 874, 1159},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {0, 874, 1159},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {0, 874, 1159},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.00000000f, 0.00000000f, -0.68621674f}},
			    {cartridge_2, {0.00000000f, 0.00000000f, -1.37243348f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_cheytac_base",
			    .bones = 17,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {4.94795784f, -0.58579999f, -2.66842692f},
			        {4.94795784f, -0.58579999f, -2.66842692f},
			        {4.94795784f, -0.58579999f, -2.66842692f},
			        {4.94795784f, -0.58579999f, -2.66842692f},
			    }},
			    .high = {{
			        {10.11595839f, 0.58579999f, 3.65687280f},
			        {10.11595839f, 0.58579999f, 3.67024189f},
			        {10.11595839f, 0.58579999f, 3.67024189f},
			        {10.11595839f, 0.58579999f, 3.67024189f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
