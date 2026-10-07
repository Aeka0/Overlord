#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::miniuzi
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 995818e57c0c2e54a9ca81656a54f983fd8325f9cb54041eb0d389697868ce58
			inline constexpr std::array<std::array<unsigned, 2>, 6> surfaces{{
			    {252, 148},
			    {972, 1414},
			    {1169, 1704},
			    {8001, 10342},
			    {4004, 4594},
			    {8, 6},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {4, 362, 1215},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {4, 3817, 4232},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {4, 3817, 4232},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {4, 3817, 4232},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.00000000f, 0.00000000f, -0.41769590f}},
			    {cartridge_2, {0.00000000f, 0.00000000f, -0.83539180f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_miniuzi_base",
			    .bones = 21,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {-0.59620100f, -0.58414603f, -5.76961698f},
			        {-0.59620100f, -0.58414603f, -5.76961698f},
			        {-0.59620100f, -0.58414603f, -5.76961698f},
			        {-0.59620100f, -0.58414603f, -5.76961698f},
			    }},
			    .high = {{
			        {1.04649996f, 0.58414603f, 2.77288392f},
			        {1.04649996f, 0.58414603f, 2.77288392f},
			        {1.04649996f, 0.58414603f, 2.77288392f},
			        {1.04649996f, 0.58414603f, 2.77288392f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
