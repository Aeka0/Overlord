#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::famas
{
	namespace magazine_geometry
	{
		namespace arctic
		{
			// Source SEModel SHA-256: 991c921bb8913c95b1337b19f695be00078699b840b68e69439d19290034c3e2
			inline constexpr std::array<std::array<unsigned, 2>, 7> surfaces{{
			    {10179, 11638},
			    {1765, 1664},
			    {10703, 13206},
			    {452, 490},
			    {5292, 6218},
			    {796, 874},
			    {4676, 5320},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {5, 0, 873},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge{{
			    {3, 0, 489},
			}};

			// Centered top cartridge; lower rounds alternate across the measured magazine width.
			inline constexpr magazine_round_stack stack{
			    .rounds = {{
			        {cartridge, {0.00000000f, -0.00000000f, 0.00000000f}},
			        {cartridge, {0.00000000f, -0.14888847f, -0.25602113f}},
			        {cartridge, {0.00000000f, 0.14888847f, -0.25602113f}},
			    }},
			};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_famas_base_arctic",
			    .bones = 20,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {-5.99687381f, -0.36448698f, -3.45034787f},
			        {-5.99687381f, -0.36448698f, -3.45034787f},
			        {-5.99687381f, -0.36448698f, -3.45034787f},
			        {-5.99687381f, -0.36448698f, -3.45034787f},
			    }},
			    .high = {{
			        {-3.67908177f, 0.36339499f, 2.26522994f},
			        {-3.67908177f, 0.36339499f, 2.26522994f},
			        {-3.67908177f, 0.36339499f, 2.26522994f},
			        {-3.67908177f, 0.36339499f, 2.26522994f},
			    }},
			    .stack = &stack,
			};
		}

		namespace tape
		{
			// Source SEModel SHA-256: a578aa8073b779c8c648d249c267b98c71e4092d05ce4e243bd4128292062f36
			inline constexpr std::array<std::array<unsigned, 2>, 7> surfaces{{
			    {10179, 11638},
			    {1765, 1664},
			    {10703, 13206},
			    {452, 490},
			    {5292, 6218},
			    {796, 874},
			    {4676, 5320},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {5, 0, 873},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge{{
			    {3, 0, 489},
			}};

			// Centered top cartridge; lower rounds alternate across the measured magazine width.
			inline constexpr magazine_round_stack stack{
			    .rounds = {{
			        {cartridge, {0.00000000f, -0.00000000f, 0.00000000f}},
			        {cartridge, {0.00000000f, -0.14888847f, -0.25602113f}},
			        {cartridge, {0.00000000f, 0.14888847f, -0.25602113f}},
			    }},
			};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_famas_base_tape",
			    .bones = 20,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {-5.99687381f, -0.36448698f, -3.45034787f},
			        {-5.99687381f, -0.36448698f, -3.45034787f},
			        {-5.99687381f, -0.36448698f, -3.45034787f},
			        {-5.99687381f, -0.36448698f, -3.45034787f},
			    }},
			    .high = {{
			        {-3.67908177f, 0.36339499f, 2.26522994f},
			        {-3.67908177f, 0.36339499f, 2.26522994f},
			        {-3.67908177f, 0.36339499f, 2.26522994f},
			        {-3.67908177f, 0.36339499f, 2.26522994f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 2> magazine_fills{
	    magazine_geometry::arctic::recipe,
	    magazine_geometry::tape::recipe,
	};
}
