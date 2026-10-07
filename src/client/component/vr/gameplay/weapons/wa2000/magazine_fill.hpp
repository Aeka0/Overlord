#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::wa2000
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 5437b0056b7717d1c0d5931b6e6451712af6dc43a625463b1281678727060328
			inline constexpr std::array<std::array<unsigned, 2>, 6> surfaces{{
			    {3901, 4918},
			    {2474, 3070},
			    {855, 980},
			    {7938, 9776},
			    {1937, 2184},
			    {3286, 4752},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {2, 0, 522},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {2, 523, 979},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {2, 523, 979},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {2, 523, 979},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.00000000f, 0.00000000f, -0.56111669f}},
			    {cartridge_2, {0.00000000f, 0.00000000f, -1.12223337f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_wa2000_base",
			    .bones = 13,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {-7.52267838f, -0.60623903f, -0.60345798f},
			        {-7.52267838f, -0.60623903f, -0.60345798f},
			        {-7.52267838f, -0.60623903f, -0.60345798f},
			        {-7.52267838f, -0.60623903f, -0.60345798f},
			    }},
			    .high = {{
			        {-3.77517385f, 0.60224200f, 4.71053311f},
			        {-3.77517385f, 0.60224200f, 4.71053311f},
			        {-3.77517385f, 0.60224200f, 4.71053311f},
			        {-3.77517385f, 0.60224200f, 4.71053311f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
