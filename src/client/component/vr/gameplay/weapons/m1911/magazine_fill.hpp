#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::m1911
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: e3b204d5acf683d75fd34ff43122c0dd83ea329191a6594bce87970ed1ae6e59
			inline constexpr std::array<std::array<unsigned, 2>, 3> surfaces{{
			    {140, 116},
			    {4555, 4914},
			    {2240, 2232},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {1, 0, 419},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 4> cartridge_0{{
			    {1, 3892, 3909},
			    {1, 3928, 3963},
			    {1, 4000, 4116},
			    {1, 4234, 4242},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 4> cartridge_1{{
			    {1, 3910, 3927},
			    {1, 3964, 3999},
			    {1, 4117, 4233},
			    {1, 4243, 4251},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 4> cartridge_2{{
			    {1, 3892, 3909},
			    {1, 3928, 3963},
			    {1, 4000, 4116},
			    {1, 4234, 4242},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_2, {-0.27829204f, -0.00000000f, -0.90616551f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_colt45_base",
			    .bones = 10,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {-1.04371000f, -0.34276400f, -2.32143590f},
			        {-1.04371000f, -0.34276400f, -2.32143590f},
			        {-1.04371000f, -0.34276400f, -2.32143590f},
			        {-1.04371000f, -0.34276400f, -2.32143590f},
			    }},
			    .high = {{
			        {1.73933694f, 0.34276400f, 1.98466684f},
			        {1.73933694f, 0.34276400f, 1.99254892f},
			        {1.73933694f, 0.34276400f, 1.99254892f},
			        {1.73933694f, 0.34276400f, 1.99254892f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
