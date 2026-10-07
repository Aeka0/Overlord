#pragma once
#include "../../magazine_fill.hpp"

namespace vr::gameplay::weapons::ump
{
	namespace magazine_geometry
	{
		namespace standard
		{
			// Source SEModel SHA-256: f65f1857e9e152f906e2de28306b4d96ab9b3e64f8c5aa482845fe6ba4dc3cd4
			inline constexpr std::array<std::array<unsigned, 2>, 6> surfaces{{
			    {18366, 25984},
			    {2210, 2988},
			    {2108, 2288},
			    {418, 384},
			    {594, 970},
			    {4714, 6718},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {1, 0, 2266},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {1, 2267, 2626},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {1, 2267, 2626},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {1, 2267, 2626},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.11123130f, 0.00000000f, -0.52776607f}},
			    {cartridge_2, {0.22246260f, 0.00000000f, -1.05553214f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_ump45_base",
			    .bones = 14,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {5.64484296f, -0.63937001f, -7.56350390f},
			        {5.64484296f, -0.63937001f, -7.56350390f},
			        {5.64484296f, -0.63937001f, -7.56350390f},
			        {5.64484296f, -0.63937001f, -7.56350390f},
			    }},
			    .high = {{
			        {9.88240129f, 0.63736199f, 3.70995980f},
			        {9.88240129f, 0.63736199f, 3.84901602f},
			        {9.88240129f, 0.63736199f, 3.84901602f},
			        {9.88240129f, 0.63736199f, 3.84901602f},
			    }},
			    .stack = &stack,
			};
		}

		namespace arctic
		{
			// Source SEModel SHA-256: 9e27bf537c6c8baab8be34716640264bfbe2b89fa053be831b8ffe10fac9d956
			inline constexpr std::array<std::array<unsigned, 2>, 6> surfaces{{
			    {18366, 25984},
			    {2210, 2988},
			    {2108, 2288},
			    {418, 384},
			    {594, 970},
			    {4714, 6718},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {1, 0, 2266},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {1, 2267, 2626},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {1, 2267, 2626},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {1, 2267, 2626},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.11123130f, 0.00000000f, -0.52776607f}},
			    {cartridge_2, {0.22246260f, 0.00000000f, -1.05553214f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_ump45_base_arctic",
			    .bones = 14,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {5.64484296f, -0.63937001f, -7.56350390f},
			        {5.64484296f, -0.63937001f, -7.56350390f},
			        {5.64484296f, -0.63937001f, -7.56350390f},
			        {5.64484296f, -0.63937001f, -7.56350390f},
			    }},
			    .high = {{
			        {9.88240129f, 0.63736199f, 3.70995980f},
			        {9.88240129f, 0.63736199f, 3.84901602f},
			        {9.88240129f, 0.63736199f, 3.84901602f},
			        {9.88240129f, 0.63736199f, 3.84901602f},
			    }},
			    .stack = &stack,
			};
		}

		namespace digital
		{
			// Source SEModel SHA-256: 1c499ecc86ed48be598abef3e803eeea54ece16f1b4fde048bba7e0aab5491f5
			inline constexpr std::array<std::array<unsigned, 2>, 6> surfaces{{
			    {18366, 25984},
			    {2210, 2988},
			    {2108, 2288},
			    {418, 384},
			    {594, 970},
			    {4714, 6718},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> body{{
			    {1, 0, 2266},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_0{{
			    {1, 2267, 2626},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_1{{
			    {1, 2267, 2626},
			}};

			inline constexpr std::array<scene_models::surface_face_range, 1> cartridge_2{{
			    {1, 2267, 2626},
			}};

			// Native round orientation is retained. Offsets follow the measured magazine axis.
			inline constexpr magazine_round_stack stack{{{
			    {cartridge_0, {0.00000000f, 0.00000000f, 0.00000000f}},
			    {cartridge_1, {0.11123130f, 0.00000000f, -0.52776607f}},
			    {cartridge_2, {0.22246260f, 0.00000000f, -1.05553214f}},
			}}};

			inline constexpr magazine_fill_recipe recipe{
			    .source = "h2_viewmodel_ump45_base_digital",
			    .bones = 14,
			    .surfaces = surfaces,
			    .faces = {body, {}, {}, {}},
			    .low = {{
			        {5.64484296f, -0.63937001f, -7.56350390f},
			        {5.64484296f, -0.63937001f, -7.56350390f},
			        {5.64484296f, -0.63937001f, -7.56350390f},
			        {5.64484296f, -0.63937001f, -7.56350390f},
			    }},
			    .high = {{
			        {9.88240129f, 0.63736199f, 3.70995980f},
			        {9.88240129f, 0.63736199f, 3.84901602f},
			        {9.88240129f, 0.63736199f, 3.84901602f},
			        {9.88240129f, 0.63736199f, 3.84901602f},
			    }},
			    .stack = &stack,
			};
		}

	}
	inline constexpr std::array<magazine_fill_recipe, 3> magazine_fills{
	    magazine_geometry::standard::recipe,
	    magazine_geometry::arctic::recipe,
	    magazine_geometry::digital::recipe,
	};
}
