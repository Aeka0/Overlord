#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::tavor
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 0c1603120beb517c31ec33de82a6d6315ae827963c16ad276151acc0c6e604a2
			inline constexpr std::array<std::array<unsigned, 2>, 6> surfaces{{
			    {15365, 16992},
			    {767, 1088},
			    {12454, 14568},
			    {4982, 4618},
			    {2988, 3058},
			    {3038, 4040},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {5, 134, 3470},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge{{
			    {5, 3471, 4039},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> follower{{
			    {5, 0, 133},
			}};

			// Centered top cartridge; lower rounds alternate across the measured magazine width.
			inline constexpr magazine_round_stack stack{
			    .rounds = {{
			        {cartridge, {-0.23699351f, -0.00000000f, 0.93117996f}},
			        {cartridge, {-0.23699351f, -0.16683298f, 0.60920760f}},
			        {cartridge, {-0.23699351f, 0.16683298f, 0.43281460f}},
			    }},
			    // The support remains present, below the lowest owned magazine round.
			    .follower_faces = follower,
			    .follower_translations = {{
			        {0.00000000f, 0.00000000f, 0.00000000f},
			        {0.00000000f, 0.00000000f, -0.20443797f},
			        {0.00000000f, 0.00000000f, -0.52641032f},
			        {0.00000000f, 0.00000000f, -0.70280332f},
			    }},
			};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_tavor_base",
			    .bones = 17,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {-8.52983430f, -0.53969096f, -4.91090797f},
			        {-8.52983430f, -0.53969096f, -4.91090797f},
			        {-8.52983430f, -0.53969096f, -4.91090797f},
			        {-8.52983430f, -0.53969096f, -4.91090797f},
			    }},
			    .high = {{
			        {-3.93709310f, 0.53969096f, 2.60963703f},
			        {-3.93709310f, 0.53969096f, 2.60963703f},
			        {-3.93709310f, 0.53969096f, 2.60963703f},
			        {-3.93709310f, 0.53969096f, 2.60963703f},
			    }},
			    .stack = &stack,
			};
		}

		namespace digital
		{
			// Source SEModel SHA-256: f25a2f08612a305aa9a640e2b309d9724fdf960ae86ae8542362bd6c32c1cd54
			inline constexpr std::array<std::array<unsigned, 2>, 6> surfaces{{
			    {15365, 16992},
			    {767, 1088},
			    {12454, 14568},
			    {4982, 4618},
			    {2988, 3058},
			    {3038, 4040},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {5, 134, 3470},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge{{
			    {5, 3471, 4039},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> follower{{
			    {5, 0, 133},
			}};

			// Centered top cartridge; lower rounds alternate across the measured magazine width.
			inline constexpr magazine_round_stack stack{
			    .rounds = {{
			        {cartridge, {-0.23699351f, -0.00000000f, 0.93117996f}},
			        {cartridge, {-0.23699351f, -0.16683298f, 0.60920760f}},
			        {cartridge, {-0.23699351f, 0.16683298f, 0.43281460f}},
			    }},
			    // The support remains present, below the lowest owned magazine round.
			    .follower_faces = follower,
			    .follower_translations = {{
			        {0.00000000f, 0.00000000f, 0.00000000f},
			        {0.00000000f, 0.00000000f, -0.20443797f},
			        {0.00000000f, 0.00000000f, -0.52641032f},
			        {0.00000000f, 0.00000000f, -0.70280332f},
			    }},
			};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_tavor_base_digital",
			    .bones = 17,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {-8.52983430f, -0.53969096f, -4.91090797f},
			        {-8.52983430f, -0.53969096f, -4.91090797f},
			        {-8.52983430f, -0.53969096f, -4.91090797f},
			        {-8.52983430f, -0.53969096f, -4.91090797f},
			    }},
			    .high = {{
			        {-3.93709310f, 0.53969096f, 2.60963703f},
			        {-3.93709310f, 0.53969096f, 2.60963703f},
			        {-3.93709310f, 0.53969096f, 2.60963703f},
			        {-3.93709310f, 0.53969096f, 2.60963703f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 2> magazine_fills{
	    magazine_geometry::standard::recipe,
	    magazine_geometry::digital::recipe,
	};
}
