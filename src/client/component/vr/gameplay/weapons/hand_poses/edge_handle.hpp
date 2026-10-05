#pragma once
#include "../../part_grip_pose.hpp"
namespace vr::gameplay::weapons::hand_poses::edge_handle
{
// Left closed hand: h2_wpn_smg_mp5k_reload_empty frame 9.
// SHA256 3c4c3e1bd72f0932df4fb98c2f90b8f2e461615c9465ab35df456202aa37d06c
// Complete finger/palm chains use current glove translations; unanimated
// webbing uses the reference bind rotation. Only wrist/contact fitting differs.
inline constexpr std::array<joint_pose,18> fingers{{
	{"j_index_le_0", {0.70321080f, -0.07470966f, 0.01068153f, 0.70696460f}},
	{"j_mid_le_0", {0.54201062f, -0.42744374f, 0.32120843f, 0.64833748f}},
	{"j_pinkypalm_le", {0.66232674f, -0.26940052f, -0.11719072f, 0.68921186f}},
	{"j_ringpalm_le", {0.68304418f, -0.13867315f, -0.10882795f, 0.70878549f}},
	{"j_thumb_le_0", {-0.19855493f, -0.10925709f, 0.24085390f, 0.94373101f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.01931806f, -0.01907392f, 0.63911357f, 0.76863310f}},
	{"j_mid_le_1", {-0.01962357f, -0.00857577f, 0.67715047f, 0.73553288f}},
	{"j_pinky_le_0", {-0.09735393f, 0.18558284f, 0.43470514f, 0.87584968f}},
	{"j_ring_le_0", {-0.11563460f, 0.07589951f, 0.54396178f, 0.82763125f}},
	{"j_thumb_le_1", {0.06006060f, 0.06256313f, -0.39307956f, 0.91540539f}},
	{"j_index_le_2", {0.01525918f, 0.03302087f, 0.65440529f, 0.75526849f}},
	{"j_mid_le_2", {-0.02826014f, 0.00824000f, 0.68346198f, 0.72939234f}},
	{"j_pinky_le_1", {0.02652075f, 0.00912509f, 0.70736249f, 0.70629434f}},
	{"j_ring_le_1", {0.01214627f, 0.00683609f, 0.68421977f, 0.72914268f}},
	{"j_thumb_le_2", {0.03881962f, 0.00534075f, -0.00683616f, 0.99920858f}},
	{"j_pinky_le_2", {-0.02313309f, 0.02444539f, 0.81173339f, 0.58305762f}},
	{"j_ring_le_2", {0.00445568f, 0.03799538f, 0.80345743f, 0.59413185f}},
}};
// Open the intermediate/distal joints by 30 degrees in their anatomical flex
// axes, forming a hook around the tab rather than touching its outermost vertex.
// Each style keeps the other fingers in the original pose. Fit against the
// skinned finger curl, including its actual distal surface (not an extrapolated tip).
inline constexpr auto index_fingers=[] {auto out=fingers;
	out[6].rotation={0.02359651f, -0.01342411f, 0.41839942f, 0.90785733f};
	out[11].rotation={0.00619281f, 0.03584508f, 0.43662910f, 0.89890589f};
	return out;}();
inline constexpr auto pinky_fingers=[] {auto out=fingers;
	out[13].rotation={0.02325533f, 0.01567824f, 0.50045727f, 0.86530683f};
	out[16].rotation={-0.02867178f, 0.01762515f, 0.63316783f, 0.77328247f};
	// Redirect the index from its knuckle so it runs beside the other fingers.
	// Extra PIP/DIP curl left the fingertip jutting forward despite a shorter reach.
	out[0].rotation={0.57213039f,-0.38913536f,0.36958284f,0.62020078f};
	return out;}();
// Both hands wrap the real tab. Right-hand facing is rolled about the barrel
// before anatomical mirroring, keeping the palm outside the receiver instead
// of translating a closed glove to an unrelated outermost skin vertex.
inline constexpr float inward=0.3f/2.54f; // 3 mm toward the receiver.
inline constexpr std::array<part_grip_pose,2> source_styles{{
	{"index_side", {{}, {0.18704404f, -0.00628448f, 0.11080935f, 0.97606164f}}, {5.66377279f, 0.89504331f, 0.98541190f}, index_fingers},
	{"pinky_side", {{}, {0.97606164f, -0.11080935f, -0.00628448f, -0.18704404f}}, {2.58709947f, -1.66739818f, 1.91847141f}, pinky_fingers},
}};
inline part_grip_pose fit(part_grip_pose pose,bool opposite) noexcept
{
	if(opposite)pose.wrist.rotation=hands::multiply({1,0,0,0},pose.wrist.rotation);
	pose.wrist.position=hands::scale(hands::rotate(pose.wrist.rotation,pose.contact_in_wrist),-1);
	pose.wrist.position[1]+=opposite ? -inward : inward;
	pose.contact_in_wrist=hands::rotate(hands::conjugate(pose.wrist.rotation),hands::scale(pose.wrist.position,-1));
	return pose;
}
inline const std::array<part_grip_pose,2> right_sources{{fit(source_styles[0],true),fit(source_styles[1],true)}};
inline std::array<part_grip_pose,2> at(hands::vec contact) noexcept
{
	auto out=source_styles;
	for(size_t i=0;i<out.size();++i)
	{
		out[i]=fit(out[i],false);out[i].wrist.position=hands::add(out[i].wrist.position,contact);
		auto right=right_sources[i];right.wrist.position=hands::add(right.wrist.position,contact);
		out[i].opposite_pose=part_grip_fit{right.wrist,right.contact_in_wrist,right.fingers};
	}
	return out;
}
// Keep a weapon's already accepted right index grasp while adding the shared
// left pair and right pinky, optionally at a different exposed point on the tab.
inline std::array<part_grip_pose,2> with_right_index(const part_grip_pose& index,hands::vec contact) noexcept
{
	auto out=at(contact);
	out[0].opposite_pose=part_grip_fit{index.wrist,index.contact_in_wrist,index.fingers};
	return out;
}
inline std::array<part_grip_pose,2> with_right_index(const part_grip_pose& index) noexcept
{
	return with_right_index(index,hands::add(index.wrist.position,hands::rotate(index.wrist.rotation,index.contact_in_wrist)));
}
}
