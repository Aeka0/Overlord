#pragma once
#include "poses.hpp"

namespace vr::gameplay::weapons::m4
{
// GL rifle idle frame 0; source SHA-256 f60bad43c01460f0adc42154cc333b5df872f1c2bbc15c8629aab5adfcf3df29.
// Only support contact changes. Preserve the accepted rear wrist/fingers exactly.
inline constexpr hands::anchor launcher_support={{8.39944081f, 1.92888617f, -0.42517697f}, {0.32812003f, -0.06567538f, -0.14541976f, 0.93106234f}};
inline constexpr std::array<joint_pose, 15> launcher_support_fingers{{
	{"j_index_le_0", {0.64116480f, -0.25711902f, -0.12982603f, 0.71129650f}},
	{"j_mid_le_0", {0.52428156f, -0.41673350f, 0.09729377f, 0.73620375f}},
	{"j_thumb_le_0", {0.11957290f, -0.13138370f, 0.19889143f, 0.96378568f}},
	{"j_index_le_1", {0.02398752f, -0.01266516f, 0.38886632f, 0.92089477f}},
	{"j_mid_le_1", {-0.01965407f, -0.00863680f, 0.67602068f, 0.73656986f}},
	{"j_pinky_le_0", {-0.16702942f, 0.00354018f, 0.74917563f, 0.64095594f}},
	{"j_ring_le_0", {-0.21182569f, -0.17630270f, 0.55362769f, 0.78583943f}},
	{"j_thumb_le_1", {-0.00180060f, 0.17349835f, -0.18314223f, 0.96765387f}},
	{"j_index_le_2", {0.01007116f, 0.03491335f, 0.53236750f, 0.84573310f}},
	{"j_mid_le_2", {-0.02819946f, -0.00827062f, 0.18231988f, 0.98280000f}},
	{"j_pinky_le_1", {0.02041676f, 0.01913499f, 0.35733903f, 0.93355547f}},
	{"j_ring_le_1", {0.01126140f, 0.00817901f, 0.59584699f, 0.80297737f}},
	{"j_thumb_le_2", {0.00695815f, -0.06689586f, 0.00308234f, 0.99773094f}},
	{"j_pinky_le_2", {-0.03366197f, 0.00070193f, 0.15463753f, 0.98739739f}},
	{"j_ring_le_2", {-0.00036623f, -0.00320448f, 0.19025435f, 0.98172953f}},
}};
}
