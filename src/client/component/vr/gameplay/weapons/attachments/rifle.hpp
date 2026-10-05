#pragma once
#include "../../weapon_attachments.hpp"

namespace vr::gameplay::weapons::rifle_attachments
{
	// Shared exported aliases. Each receiver must still provide the required
	// parent tag; e.g. a laser model is not admitted on AK without tag_laser.
	inline constexpr std::array<assembly_attachment,21> common{{
		{{"attach_h2_silencer_01_vm","tag_silencer","tag_silencer","tag_flash_silenced"},attachment_role::silencer,2},
		{{"attach_h2_acog_2_vm","tag_acog_2","tag_acog_2"},attachment_role::optic,2},
		{{"attach_h2_acog_2_vm_arctic","tag_acog_2","tag_acog_2"},attachment_role::optic,2},
		{{"attach_h2_acog_2_vm_digital","tag_acog_2","tag_acog_2"},attachment_role::optic,2},
		{{"attach_h2_acog_2_vm_tan","tag_acog_2","tag_acog_2"},attachment_role::optic,2},
		{{"attach_h2_eotech_2_vm","tag_eotech","tag_eotech"},attachment_role::optic,2},
		{{"attach_h2_eotech_2_vm_digital","tag_eotech","tag_eotech"},attachment_role::optic,2},
		{{"attach_h2_red_dot_sight_vm","tag_red_dot","tag_red_dot"},attachment_role::optic,2},
		{{"attach_h2_red_dot_sight_vm_arctic","tag_red_dot","tag_red_dot"},attachment_role::optic,2},
		{{"attach_h2_red_dot_sight_vm_digital","tag_red_dot","tag_red_dot"},attachment_role::optic,2},
		{{"attach_h2_red_dot_sight_vm_tan","tag_red_dot","tag_red_dot"},attachment_role::optic,2},
		{{"attach_h2_thermal_scope_2_vm","tag_thermal_scope","tag_thermal_scope"},attachment_role::optic,5},
		{{"attach_h2_thermal_scope_2_vm_arctic","tag_thermal_scope","tag_thermal_scope"},attachment_role::optic,5},
		{{"attach_h2_thermal_scope_2_vm_tan","tag_thermal_scope","tag_thermal_scope"},attachment_role::optic,5},
		{{"attach_h2_heartbeat_vm","tag_heartbeat","tag_heartbeat"},attachment_role::sensor,9},
		{{"attach_h2_laser_peq6_vm","tag_laser","tag_laser"},attachment_role::laser,1},
		// Live arctic ACR: identical nine-bone hierarchy/bind to the base sensor.
		{{"attach_h2_heartbeat_vm_arctic","tag_heartbeat","tag_heartbeat"},attachment_role::sensor,9},
		// Estate woodland optics have byte-identical bind matrices to base assets.
		{{"attach_h2_acog_2_vm_woodland","tag_acog_2","tag_acog_2"},attachment_role::optic,2},
		{{"attach_h2_eotech_2_vm_woodland","tag_eotech","tag_eotech"},attachment_role::optic,2},
		{{"attach_h2_red_dot_sight_vm_woodland","tag_red_dot","tag_red_dot"},attachment_role::optic,2},
		// Registered desert AK reflex: exact two-bone bind match to the tan sight.
		{{"attach_h2_red_dot_sight_vm_desert","tag_red_dot","tag_red_dot"},attachment_role::optic,2}
	}};
	template<size_t N> constexpr auto with_common(const std::array<assembly_attachment,N>& specific)
	{
		std::array<assembly_attachment,N+common.size()> out{};
		std::copy(specific.begin(),specific.end(),out.begin());
		std::copy(common.begin(),common.end(),out.begin()+N);
		return out;
	}
}
