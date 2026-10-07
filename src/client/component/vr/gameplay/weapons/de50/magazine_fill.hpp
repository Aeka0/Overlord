#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::de50
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: e8195b4c4ad2ef854247d7beac55d5acfeb9af7bebce761a835e5b5a54b72177
			inline constexpr std::array<std::array<unsigned, 2>, 7> surfaces{{
			    {317, 370},
			    {2429, 2496},
			    {1433, 1654},
			    {5208, 5538},
			    {538, 732},
			    {1511, 1778},
			    {4048, 4440},
			}};

			// 0 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 1> rounds_0{{
			    {5, 0, 811},
			}};

			// 1 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 2> rounds_1{{
			    {5, 0, 811},
			    {5, 1456, 1777},
			}};

			// 2 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 5> rounds_2{{
			    {5, 0, 955},
			    {5, 1100, 1115},
			    {5, 1132, 1167},
			    {5, 1204, 1329},
			    {5, 1456, 1777},
			}};

			// 3 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 1> rounds_3{{
			    {5, 0, 1777},
			}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_desert_eagle_base",
			    .bones = 11,
			    .surfaces = surfaces,
			    .faces = {rounds_0, rounds_1, rounds_2, rounds_3},
			    .low = {{
			        {-1.29158403f, -0.52732600f, -2.72026982f},
			        {-1.29158403f, -0.52732600f, -2.72026982f},
			        {-1.29158403f, -0.52732600f, -2.72026982f},
			        {-1.29158403f, -0.52732600f, -2.72026982f},
			    }},
			    .high = {{
			        {1.36858103f, 0.53014699f, 2.23919707f},
			        {1.36858103f, 0.53014699f, 2.42380495f},
			        {1.36858103f, 0.53014699f, 2.42380495f},
			        {1.36858103f, 0.53014699f, 2.42380495f},
			    }},
			};
		}

		namespace gold
		{
			// Source SEModel SHA-256: 7ab61094c002c156f5c7f7efeeb8afd1d2cd303046d4fc848f2d5c9d22bf9ac2
			inline constexpr std::array<std::array<unsigned, 2>, 5> surfaces{{
			    {1511, 1778},
			    {337, 370},
			    {1837, 2372},
			    {2540, 2686},
			    {3903, 4262},
			}};

			// 0 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 1> rounds_0{{
			    {0, 0, 811},
			}};

			// 1 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 2> rounds_1{{
			    {0, 0, 811},
			    {0, 1456, 1777},
			}};

			// 2 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 5> rounds_2{{
			    {0, 0, 955},
			    {0, 1100, 1115},
			    {0, 1132, 1167},
			    {0, 1204, 1329},
			    {0, 1456, 1777},
			}};

			// 3 magazine rounds; the complete magazine body remains selected.
			inline constexpr std::array<scene_models::surface_face_range, 1> rounds_3{{
			    {0, 0, 1777},
			}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_desert_eagle_gold",
			    .bones = 11,
			    .surfaces = surfaces,
			    .faces = {rounds_0, rounds_1, rounds_2, rounds_3},
			    .low = {{
			        {-1.29158403f, -0.52732600f, -2.72026982f},
			        {-1.29158403f, -0.52732600f, -2.72026982f},
			        {-1.29158403f, -0.52732600f, -2.72026982f},
			        {-1.29158403f, -0.52732600f, -2.72026982f},
			    }},
			    .high = {{
			        {1.36858103f, 0.53014699f, 2.23919707f},
			        {1.36858103f, 0.53014699f, 2.42380495f},
			        {1.36858103f, 0.53014699f, 2.42380495f},
			        {1.36858103f, 0.53014699f, 2.42380495f},
			    }},
			};
		}

	}

	inline constexpr std::array<magazine_fill_recipe, 2> magazine_fills{
	    magazine_geometry::standard::recipe,
	    magazine_geometry::gold::recipe,
	};
}
