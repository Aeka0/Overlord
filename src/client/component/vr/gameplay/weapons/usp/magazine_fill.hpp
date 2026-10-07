#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::usp
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 8fc005b3fb0ca3b8d323790cea510aae4f6462530d6e288c73a24faf69f39e86
			inline constexpr std::array<std::array<unsigned, 2>, 6> surfaces{{
			    {2243, 2314},
			    {2862, 3520},
			    {48, 42},
			    {1191, 1696},
			    {4556, 4962},
			    {1346, 1990},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {3, 0, 542},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> cartridge_0{{
			    {3, 975, 1406},
			    {3, 1479, 1550},
			    {3, 1623, 1695},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> cartridge_1{{
			    {3, 543, 974},
			    {3, 1407, 1478},
			    {3, 1551, 1622},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> cartridge_2{{
			    {3, 975, 1406},
			    {3, 1479, 1550},
			    {3, 1623, 1695},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_2, {-0.12192029f, -0.00000000f, -0.84004768f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_usp_base",
			    .bones = 12,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {-1.34610398f, -0.60946298f, -1.97725803f},
			        {-1.34610398f, -0.60946298f, -1.97725803f},
			        {-1.34610398f, -0.60946298f, -1.97725803f},
			        {-1.34610398f, -0.60946298f, -1.97725803f},
			    }},
			    .high = {{
			        {1.28064203f, 0.60946200f, 2.40230598f},
			        {1.28064203f, 0.60946200f, 2.41443792f},
			        {1.28064203f, 0.60946200f, 2.41443792f},
			        {1.28064203f, 0.60946200f, 2.41443792f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
