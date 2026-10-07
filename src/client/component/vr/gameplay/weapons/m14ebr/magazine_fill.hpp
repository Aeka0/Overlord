#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::m14ebr
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 630604208cd35ec7c7ed24dded5adb02a2336b8fc54ad95768dfd88fa1a6be9b
			inline constexpr std::array<std::array<unsigned, 2>, 7> surfaces{{
			    {15142, 17042},
			    {1702, 1800},
			    {616, 744},
			    {198, 192},
			    {7714, 8618},
			    {1545, 1880},
			    {3192, 3258},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 4> body{{
			    {1, 0, 11},
			    {1, 14, 485},
			    {1, 569, 576},
			    {1, 587, 746},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge{{
			    {1, 1255, 1799},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> follower{{
			    {1, 12, 13},
			    {1, 486, 568},
			    {1, 577, 586},
			}};

			// Centered top cartridge; lower rounds alternate across the measured magazine width.
			inline constexpr magazine_round_stack stack{
			    .rounds = {{
			        {cartridge, {0.14968144f, -0.11454900f, -0.98934402f}},
			        {cartridge, {0.14968144f, -0.37612845f, -1.50215494f}},
			        {cartridge, {0.14968144f, 0.14703045f, -1.78506994f}},
			    }},
			    // The support remains present, below the lowest owned magazine round.
			    .follower_faces = follower,
			    .follower_translations = {{
			        {0.00000000f, 0.00000000f, 0.00000000f},
			        {0.00000000f, 0.00000000f, -0.03451977f},
			        {0.00000000f, 0.00000000f, -0.54733069f},
			        {0.00000000f, 0.00000000f, -0.83024569f},
			    }},
			};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_m14ebr_base",
			    .bones = 18,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {6.65775509f, -0.63084298f, -2.87248600f},
			        {6.65775509f, -0.63084298f, -2.87248600f},
			        {6.65775509f, -0.63084298f, -2.87248600f},
			        {6.65775509f, -0.63084298f, -2.87248600f},
			    }},
			    .high = {{
			        {10.93259796f, 0.63348596f, 4.13680602f},
			        {10.93259796f, 0.63348596f, 4.13680602f},
			        {10.93259796f, 0.63348596f, 4.13680602f},
			        {10.93259796f, 0.63348596f, 4.13680602f},
			    }},
			    .stack = &stack,
			};
		}

		namespace arctic
		{
			// Source SEModel SHA-256: 7e47bc7f89c646a0c415c96ada5afc298d92e69a941d3cf7e196c7c74d0f1f33
			inline constexpr std::array<std::array<unsigned, 2>, 7> surfaces{{
			    {15142, 17042},
			    {1702, 1800},
			    {616, 744},
			    {198, 192},
			    {7714, 8618},
			    {1545, 1880},
			    {3192, 3258},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 4> body{{
			    {1, 0, 11},
			    {1, 14, 485},
			    {1, 569, 576},
			    {1, 587, 746},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge{{
			    {1, 1255, 1799},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> follower{{
			    {1, 12, 13},
			    {1, 486, 568},
			    {1, 577, 586},
			}};

			// Centered top cartridge; lower rounds alternate across the measured magazine width.
			inline constexpr magazine_round_stack stack{
			    .rounds = {{
			        {cartridge, {0.14968144f, -0.11454900f, -0.98934402f}},
			        {cartridge, {0.14968144f, -0.37612845f, -1.50215494f}},
			        {cartridge, {0.14968144f, 0.14703045f, -1.78506994f}},
			    }},
			    // The support remains present, below the lowest owned magazine round.
			    .follower_faces = follower,
			    .follower_translations = {{
			        {0.00000000f, 0.00000000f, 0.00000000f},
			        {0.00000000f, 0.00000000f, -0.03451977f},
			        {0.00000000f, 0.00000000f, -0.54733069f},
			        {0.00000000f, 0.00000000f, -0.83024569f},
			    }},
			};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_m14ebr_base_arctic",
			    .bones = 18,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {6.65775509f, -0.63084298f, -2.87248600f},
			        {6.65775509f, -0.63084298f, -2.87248600f},
			        {6.65775509f, -0.63084298f, -2.87248600f},
			        {6.65775509f, -0.63084298f, -2.87248600f},
			    }},
			    .high = {{
			        {10.93259796f, 0.63348596f, 4.13680602f},
			        {10.93259796f, 0.63348596f, 4.13680602f},
			        {10.93259796f, 0.63348596f, 4.13680602f},
			        {10.93259796f, 0.63348596f, 4.13680602f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 2> magazine_fills{
	    magazine_geometry::standard::recipe,
	    magazine_geometry::arctic::recipe,
	};
}
