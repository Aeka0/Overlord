#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::m16
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: 58be751a7b9f437110a5ac2176cd92fe12702cb195af7b8a5d463de700a5cfeb
			inline constexpr std::array<std::array<unsigned, 2>, 10> surfaces{{
			    {1157, 1122},
			    {5034, 6346},
			    {494, 696},
			    {540, 896},
			    {12398, 13670},
			    {2193, 3290},
			    {3364, 4760},
			    {8569, 11578},
			    {4556, 6384},
			    {722, 910},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 2> body{{
			    {6, 3468, 4759},
			    {8, 0, 3909},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> cartridge_0{{
			    {3, 352, 703},
			    {3, 720, 735},
			    {3, 816, 895},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> cartridge_1{{
			    {3, 0, 351},
			    {3, 704, 719},
			    {3, 736, 815},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 3> cartridge_2{{
			    {3, 352, 703},
			    {3, 720, 735},
			    {3, 816, 895},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_2, {-0.00000000f, -0.00000000f, -0.39153278f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_m16_base",
			    .bones = 28,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {3.68860500f, -0.63981095f, -3.88408796f},
			        {3.68860500f, -0.63981095f, -3.88408796f},
			        {3.68860500f, -0.63981095f, -3.88408796f},
			        {3.68860500f, -0.63981095f, -3.88408796f},
			    }},
			    .high = {{
			        {7.24097650f, 0.75472495f, 3.67994384f},
			        {7.24097650f, 0.75472495f, 3.69449976f},
			        {7.24097650f, 0.75472495f, 3.69449976f},
			        {7.24097650f, 0.75472495f, 3.69449976f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 1> magazine_fills{
	    magazine_geometry::standard::recipe,
	};
}
