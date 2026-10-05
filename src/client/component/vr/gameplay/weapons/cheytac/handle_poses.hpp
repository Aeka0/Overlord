#pragma once
#include "../../part_grip_pose.hpp"
#include "../hand_poses/right_handle.hpp"
namespace vr::gameplay::weapons::cheytac
{
// Complete anatomical chains; native source travel removed before mesh fitting.
// h2_wpn_sni_cheytac_rechamber, frame 11.
// SHA256 06b76517a3123fb7401732165cad36e71514943ece6ced70c970cca65ce1873d
inline constexpr std::array<joint_pose, 18> handle_pose_fingers_0{{
	{"j_index_le_0", {0.63125298f, -0.48293116f, 0.45509785f, 0.40147618f}},
	{"j_mid_le_0", {0.25260097f, -0.69795631f, 0.64000176f, 0.19861390f}},
	{"j_pinkypalm_le", {0.68995364f, -0.23190716f, -0.19040252f, 0.65873358f}},
	{"j_ringpalm_le", {0.71477881f, -0.11456726f, -0.10510646f, 0.68184913f}},
	{"j_thumb_le_0", {-0.01644957f, -0.22303118f, 0.18329575f, 0.95728218f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987477f}},
	{"j_index_le_1", {0.00805680f, -0.02581861f, 0.92601952f, 0.37650542f}},
	{"j_mid_le_1", {-0.02139364f, -0.00070194f, 0.90249845f, 0.43016086f}},
	{"j_pinky_le_0", {0.00927765f, 0.07867710f, 0.83187787f, 0.54927502f}},
	{"j_ring_le_0", {0.05746661f, 0.04754795f, 0.90490793f, 0.41902079f}},
	{"j_thumb_le_1", {0.09152343f, 0.09890891f, -0.36871855f, 0.91972122f}},
	{"j_index_le_2", {0.02200360f, 0.02893129f, 0.80415625f, 0.59330562f}},
	{"j_mid_le_2", {-0.02301061f, 0.01818876f, 0.90778832f, 0.41840177f}},
	{"j_pinky_le_1", {0.02792406f, 0.00140384f, 0.87599187f, 0.48151481f}},
	{"j_ring_le_1", {0.01373342f, 0.00253300f, 0.88174766f, 0.47151462f}},
	{"j_thumb_le_2", {-0.01571699f, -0.07025338f, -0.22913223f, 0.97072955f}},
	{"j_pinky_le_2", {-0.02778165f, 0.01906253f, 0.67092735f, 0.74075724f}},
	{"j_ring_le_2", {-0.00250257f, -0.00189219f, 0.83693864f, 0.54728774f}},
}};

// M14 frame-73 finger chains, relaxed at PIP/DIP to leave the inclined
// Cheytac handle inside the curl. The last two fingers relax from the knuckle;
// their bones keep native lengths and each hand shares this complete chain.
inline constexpr auto right_up_power_fingers=[] {
	auto out=hand_poses::right_handle::fingers;
	out[0].rotation={0.63239725f,-0.28496502f,0.14201643f,0.70618694f};
	out[6].rotation={0.01903028f,-0.01922665f,0.64859665f,0.76065141f};
	out[7].rotation={-0.01744114f,-0.01248500f,0.51187288f,0.85879339f};
	out[8].rotation={-0.07550442f,-0.01724259f,0.58146749f,0.80987489f};
	out[9].rotation={-0.16301070f,-0.07171173f,0.56060791f,0.80870496f};
	out[11].rotation={0.01257948f,0.03409402f,0.59406718f,0.80359414f};
	out[12].rotation={-0.02940314f,-0.00042796f,0.43969188f,0.89766716f};
	out[13].rotation={0.02207744f,0.01725098f,0.44013614f,0.89749383f};
	out[14].rotation={0.01092467f,0.00867188f,0.55794987f,0.82975743f};
	out[16].rotation={-0.03211879f,0.01030194f,0.42883205f,0.90275430f};
	out[17].rotation={-0.00109565f,-0.00288363f,0.41340296f,0.91054296f};
	return out;
}();

inline const auto& m200_left_down_fingers=right_up_power_fingers;

inline const part_grip_fit right_up_fit{{{-0.87238055f, -7.21295796f, 0.25042513f}, {-0.19951670f, 0.03807025f, 0.40472884f, 0.89159313f}},{2.82911944f, 0.60676774f, 1.67513128f},right_up_power_fingers};
// The physical handle axis is (0, .803701, .595033), not gun-local Y.
// Mirror across the plane containing that axis and the barrel: anatomical
// mirroring supplies the reflection, this roll supplies the inclined plane.
inline const part_grip_fit left_down_fit=[] {
	auto out=right_up_fit;
	const auto contact=hands::add(out.wrist.position,hands::rotate(out.wrist.rotation,out.contact_in_wrist));
	constexpr hands::quat flip{-0.803700903f,0.f,0.f,0.595033493f};
	out.wrist.position=hands::add(contact,hands::rotate(flip,hands::sub(out.wrist.position,contact)));
	out.wrist.rotation=hands::normalize(hands::multiply(flip,out.wrist.rotation));
	out.contact_in_wrist=hands::rotate(hands::conjugate(out.wrist.rotation),hands::sub(contact,out.wrist.position));
	return out;
}();

inline const std::array<part_grip_pose,2> action_grips{{
	{"left_down_right_up",left_down_fit.wrist,left_down_fit.contact_in_wrist,left_down_fit.fingers,nullptr,right_up_fit},
	{"left_down_right_down",left_down_fit.wrist,left_down_fit.contact_in_wrist,left_down_fit.fingers,nullptr,
		part_grip_fit{{{-2.03594892f,-2.77863200f,1.97901935f},{-0.95642922f,0.17016143f,0.09284043f,-0.21833207f}},
			{2.89928234f,0.04394992f,0.85427569f},handle_pose_fingers_0}},
}};
}
