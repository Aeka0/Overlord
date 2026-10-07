#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::fn2000
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 35af0bd35ce62c5f23747d7e3da55b251c7840371f34beed8b6777978ce0d999
			inline constexpr std::array<std::array<unsigned, 2>, 9> surfaces{{
			    {5053, 8080},
			    {1865, 2296},
			    {2254, 2656},
			    {499, 512},
			    {2696, 3952},
			    {1714, 2874},
			    {9734, 14682},
			    {1804, 2758},
			    {8079, 11702},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {0, 72, 3599},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> rounds_0{{
			    {0, 72, 3599},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 4> rounds_1{{
			    {0, 72, 3599},
			    {0, 6494, 6507},
			    {0, 7450, 7679},
			    {0, 7978, 8079},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 7> rounds_2{{
			    {0, 72, 3599},
			    {0, 6466, 6479},
			    {0, 6494, 6507},
			    {0, 6914, 7143},
			    {0, 7450, 7679},
			    {0, 7778, 7879},
			    {0, 7978, 8079},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 7> rounds_3{{
			    {0, 72, 3599},
			    {0, 6452, 6479},
			    {0, 6494, 6507},
			    {0, 6608, 6785},
			    {0, 6914, 7143},
			    {0, 7450, 7879},
			    {0, 7978, 8079},
			}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_fn2000_base",
			    .bones = 21,
			    .surfaces = surfaces,
			    .faces = {rounds_0, rounds_1, rounds_2, rounds_3},
			    .low = {{
			        {-6.19465009f, -0.39393700f, -3.78186008f},
			        {-6.19465009f, -0.39393700f, -3.78186008f},
			        {-6.19465009f, -0.39393700f, -3.78186008f},
			        {-6.19465009f, -0.39393700f, -3.78186008f},
			    }},
			    .high = {{
			        {-3.11638183f, 0.47244096f, 2.69195883f},
			        {-3.11638183f, 0.47244096f, 2.69195883f},
			        {-3.11638183f, 0.47244096f, 2.69195883f},
			        {-3.11638183f, 0.47244096f, 2.69195883f},
			    }},
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
