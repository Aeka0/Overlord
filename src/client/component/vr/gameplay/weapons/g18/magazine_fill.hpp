#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::g18
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 2ed3651182a36b5c17a27d15b4080f0ea2dfdf149c2b0a9b1e06348fb02bad4f
			inline constexpr std::array<std::array<unsigned, 2>, 2> surfaces{{
			    {3415, 3392},
			    {2329, 2120},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {0, 2825, 3279},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {0, 3280, 3391},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {0, 3280, 3391},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {0, 3280, 3391},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {-0.14963301f, 0.00000000f, -0.38574696f}},
			    {cartridge_2, {-0.29926603f, 0.00000000f, -0.77149391f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_glock_base",
			    .bones = 10,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {-2.95838089f, -0.59730199f, -6.27167506f},
			        {-2.95838089f, -0.59730199f, -6.27167506f},
			        {-2.95838089f, -0.59730199f, -6.27167506f},
			        {-2.95838089f, -0.59730199f, -6.27167506f},
			    }},
			    .high = {{
			        {1.94452992f, 0.59731100f, 2.65500677f},
			        {1.94452992f, 0.59731100f, 2.72089496f},
			        {1.94452992f, 0.59731100f, 2.72089496f},
			        {1.94452992f, 0.59731100f, 2.72089496f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
