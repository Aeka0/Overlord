#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::vector
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: b7801e00d2aec40edb6f9e5ef7bc2a9e4d263ab149167cc8e5c0a2b9321ece7b
			inline constexpr std::array<std::array<unsigned, 2>, 8> surfaces{{
			    {3070, 4188},
			    {638, 728},
			    {41, 60},
			    {1096, 1748},
			    {10614, 12644},
			    {1008, 970},
			    {2666, 3476},
			    {2666, 3476},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {1, 0, 503},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {1, 504, 727},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {1, 504, 727},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {1, 504, 727},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {-0.16214495f, 0.00000000f, -0.33845070f}},
			    {cartridge_2, {-0.32428991f, 0.00000000f, -0.67690141f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_kriss_super_v_base",
			    .bones = 18,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {3.70209987f, -0.44906698f, -7.37451043f},
			        {3.70209987f, -0.44906698f, -7.37451043f},
			        {3.70209987f, -0.44906698f, -7.37451043f},
			        {3.70209987f, -0.44906698f, -7.37451043f},
			    }},
			    .high = {{
			        {8.62206286f, 0.44906698f, 1.09205396f},
			        {8.62206286f, 0.44906698f, 1.23450193f},
			        {8.62206286f, 0.44906698f, 1.23450193f},
			        {8.62206286f, 0.44906698f, 1.23450193f},
			    }},
			    .stack = &stack,
			};
		}

		namespace black
		{
			// Source SEModel SHA-256: a96b4c8361d02dea269af76b10e34722c4d3d43c0f8450c5632e23301239664a
			inline constexpr std::array<std::array<unsigned, 2>, 8> surfaces{{
			    {3070, 4188},
			    {638, 728},
			    {41, 60},
			    {1096, 1748},
			    {10614, 12644},
			    {1008, 970},
			    {2666, 3476},
			    {2666, 3476},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {1, 0, 503},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {1, 504, 727},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {1, 504, 727},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {1, 504, 727},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {-0.16214495f, 0.00000000f, -0.33845070f}},
			    {cartridge_2, {-0.32428991f, 0.00000000f, -0.67690141f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_kriss_super_v_base_black",
			    .bones = 18,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {3.70209987f, -0.44906698f, -7.37451043f},
			        {3.70209987f, -0.44906698f, -7.37451043f},
			        {3.70209987f, -0.44906698f, -7.37451043f},
			        {3.70209987f, -0.44906698f, -7.37451043f},
			    }},
			    .high = {{
			        {8.62206286f, 0.44906698f, 1.09205396f},
			        {8.62206286f, 0.44906698f, 1.23450193f},
			        {8.62206286f, 0.44906698f, 1.23450193f},
			        {8.62206286f, 0.44906698f, 1.23450193f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 2> magazine_fills{
	    magazine_geometry::standard::recipe,
	    magazine_geometry::black::recipe,
	};
}
