#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::scar
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 9062f1d89ff16e6a57afb0c77c86bd097b27a0d81a0a8956894f35816afa78cf
			inline constexpr std::array<std::array<unsigned, 2>, 10> surfaces{{
			    {20230, 21884},
			    {3499, 4430},
			    {1144, 1120},
			    {1144, 1120},
			    {5254, 6354},
			    {1853, 2448},
			    {4078, 4638},
			    {710, 1014},
			    {3756, 5078},
			    {3756, 5078},
			}};

			// 0 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 1> rounds_0{{
			    {5, 0, 2447},
			}};

			// 1 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 3> rounds_1{{
			    {4, 5280, 5567},
			    {4, 5896, 5943},
			    {5, 0, 2447},
			}};

			// 2 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 4> rounds_2{{
			    {4, 5280, 5703},
			    {4, 5896, 5943},
			    {4, 5992, 6036},
			    {5, 0, 2447},
			}};

			// 3 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 3> rounds_3{{
			    {4, 5280, 5815},
			    {4, 5896, 6036},
			    {5, 0, 2447},
			}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_scar_h_base",
			    .bones = 19,
			    .surfaces = surfaces,
			    .faces = {rounds_0, rounds_1, rounds_2, rounds_3},
			    .low = {{
			        {3.90263280f, -0.54284498f, -3.16979190f},
			        {3.90263280f, -0.54284498f, -3.16979190f},
			        {3.90263280f, -0.54284498f, -3.16979190f},
			        {3.90263280f, -0.54284498f, -3.16979190f},
			    }},
			    .high = {{
			        {7.22998672f, 0.54341602f, 3.35067501f},
			        {7.22998672f, 0.54341602f, 3.35067501f},
			        {7.22998672f, 0.54341602f, 3.35067501f},
			        {7.22998672f, 0.54341602f, 3.35067501f},
			    }},
			};
		}

	}

	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
