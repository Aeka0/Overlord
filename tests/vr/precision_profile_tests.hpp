#pragma once
#include "component/vr/gameplay/hands/rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/weapons/m82/profile.hpp"
#include "component/vr/gameplay/weapons/wa2000/profile.hpp"
#include "precision_test_data.hpp"
#include "m200_live_assembly.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace precision_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		int failed{};
		unsigned combinations{};
		const auto check = [&](bool ok, const char* why) {
			if (!ok)
			{
				++failed;
				std::cerr << "FAIL: " << why << '\n';
			}
		};
		struct fixture
		{
			rig r{};
			std::array<bone_definition, 256> bones{};
			std::vector<model_definition> models;
			fixture(const profile& p, std::span<const assembly_attachment* const> items)
			{
				r.parent.fill(-1);
				r.gun = 68;
				r.count = 68;
				r.parent[68] = 1;
				r.arms[0].wrist = 2;
				r.arms[1].wrist = 3;
				models = {{"viewhands_us_army", 0, 68}};
				for (size_t i = 0; i < p.fingers.size(); ++i)
				{
					bones[4 + i].name = p.fingers[i].name;
					r.parent[4 + i] = p.fingers[i].name.find("_le") != std::string_view::npos ? 2 : 3;
				}
				const auto add = [&](std::string_view name, int parent) {
					for (const auto& src : precision_test_data::models)
						if (src.name == name)
						{
							const int start = r.count;
							models.push_back({name, start, int(src.bones.size())});
							for (const auto& b : src.bones)
							{
								bones[r.count].name = b.name;
								r.weapon_bones[r.count] = true;
								r.parent[r.count] = b.parent < 0 ? parent : start + b.parent;
								++r.count;
							}
							return;
						}
					throw std::runtime_error("precision fixture lacks captured model: "+std::string(name));
				};
				add(p.receiver, 1);
				for (const auto* item : items)
				{
					int parent = -1;
					for (int i = 68; i < 68 + models[1].count; ++i)
						if (bones[i].name == item->contract.receiver_parent)
							parent = i;
					add(item->contract.model, parent);
				}
				for (int i = 0; i < r.count; ++i)
				{
					bones[i].parent = r.parent[i];
					bones[i].bind.rotation = {0, 0, 0, 1};
					if (bones[i].name == "tag_flash_silenced")
						bones[i].bind.position = {6.7f, 0, 0};
				}
			}
			profile_match resolve() const
			{
				return select_profile(models, r, {bones.data(), size_t(r.count)});
			}
		};
		const auto family = [&](auto& profiles, auto& policies) {
			for (const auto& p : profiles)
			{
				fixture bare(p, {});
				std::vector<const assembly_attachment*> optics{nullptr}, bipods{nullptr}, silencers{nullptr};
				for (const auto& a : policies)
				{
					bool mount = false;
					for (int i = 68; i < bare.r.count; ++i)
						if (bare.bones[i].name == a.contract.receiver_parent)
							mount = true;
					if (!mount)
						continue;
					if (a.role == attachment_role::optic)
						optics.push_back(&a);
					if (a.role == attachment_role::bipod)
						bipods.push_back(&a);
					if (a.role == attachment_role::silencer)
						silencers.push_back(&a);
				}
				for (auto optic : optics)
					for (auto bipod : bipods)
						for (auto silencer : silencers)
							for (bool reverse : {false, true})
							{
								std::vector<const assembly_attachment*> items;
								for (auto v : {optic, bipod, silencer})
									if (v)
										items.push_back(v);
								if (reverse)
									std::reverse(items.begin(), items.end());
								fixture f(p, items);
								const auto result = f.resolve();
								check(result.value == &p,
								    "precision rifle assembly matches source topology in either order");
								check((result.muzzle >= 0) == bool(silencer), "precision suppressor owns its muzzle");
								check(bind_weapon_poses(f.r, {f.bones.data(), size_t(f.r.count)}, p).valid,
								    "precision authored fingers and physical parts bind");
								check(physical_reload::bind_parts(f.r, {f.bones.data(), size_t(f.r.count)}, *p.reload)
								          .valid,
								    "precision magazine and round parent chains bind");
								for (int i = 68 + f.models[1].count; i < f.r.count; ++i)
									if (f.bones[i].name == "tag_scope_ads_on")
										check(bool(result.hidden[i / 32] & (0x80000000u >> (i % 32))),
										    "precision alternate ADS mesh hidden without hiding physical scope");
								if (!items.empty())
								{
									f.r.parent[f.models.back().begin] = 2;
									check(!f.resolve().value, "precision attachment cannot bind to a hand");
								}
								++combinations;
							}
				check(native_reload_profile(p.reload->native_name, p.reload->ammunition.magazine_capacity, p.reload) ==
				          p.reload,
				    "precision native capacity and exact scene admitted");
				check(
				    !native_reload_profile(p.reload->native_name, p.reload->ammunition.magazine_capacity + 1, p.reload),
				    "precision wrong native capacity rejected");
			}
		};
		family(m14ebr::assemblies, m14ebr::attachments);
		family(m82::assemblies, m82::attachments);
		family(wa2000::assemblies, wa2000::attachments);
		family(cheytac::assemblies, cheytac::attachments);
		{
			const auto resolved=resolve_rig(m200_live_assembly::models,m200_live_assembly::bones);
			check(!resolved.rejection,"actual TF141 M200 skeleton and native duplicate parents resolve");
			const auto live=select_profile(m200_live_assembly::models,resolved.layout,m200_live_assembly::bones);
			check(live.value==&cheytac::assemblies[1],"actual desert M200 + silencer03 + desert scope admits the ordinary hand binding");
			if(live.value)
			{
				check(bind_weapon_poses(resolved.layout,m200_live_assembly::bones,*live.value).valid,"actual M200 glove and weapon poses bind after assembly admission");
				check(physical_reload::bind_parts(resolved.layout,m200_live_assembly::bones,*live.value->reload).valid,"actual M200 magazine and manual bolt bind after assembly admission");
				check(live.muzzle==86,"actual silencer03 supplies its observed muzzle bone");
				auto bad=resolved.layout;bad.parent[85]=68;
				check(!select_profile(m200_live_assembly::models,bad,m200_live_assembly::bones).value,"known silencer name does not bypass actual native attachment parent validation");
			}
		}
		{
			std::array<const assembly_attachment*,2> items{&cheytac::attachments[1],&cheytac::attachments[2]};
			fixture captured(cheytac::assemblies[1],items);
			check(captured.resolve().value==&cheytac::assemblies[1] && captured.resolve().value->reload==&cheytac::desert &&
				std::string_view(cheytac::desert.rigid_magazine_source)=="h2_viewmodel_cheytac_base_desert",
				"live desert M200 binds shared mechanics and its own receiver mesh source");
			check(native_reload_profile("cheytac_silencer_desert",5,&cheytac::desert)==&cheytac::desert &&
				!native_reload_profile("cheytac_silencer_desert",6,&cheytac::desert),"desert M200 retains native five-round capacity validation");
			captured.models[1].name="h2_viewmodel_cheytac_base_unknown";
			check(!captured.resolve().value,"unverified M200 skins are not wildcard-admitted");
		}
		// Captured H2 definitions, independent of the values declared by the profiles.
		check(native_reload_profile("wa2000", 10, &wa2000::physical) == &wa2000::physical &&
		          !native_reload_profile("wa2000", 6, &wa2000::physical),
		    "WA2000 live ten-round descriptor admits physical interaction");
		for (const auto name : {"m21_scoped_cloth_silenced", "m21_soap", "m21_scoped_arctic_silenced"})
			check(native_reload_profile(name, 10, &m14ebr::physical) == &m14ebr::physical,
			    "M14 EBR campaign M21 aliases admit with the matched receiver");
		check(!native_reload_profile("m210_scoped", 10, &m14ebr::physical),
		    "M21 alias retains native family boundary");
		{
			const assembly_attachment* thermal=nullptr;
			for(const auto& attachment:m14ebr::attachments)
				if(attachment.contract.model=="attach_h2_thermal_scope_2_vm")thermal=&attachment;
			check(thermal!=nullptr,"captured thermal-scope contract is registered");
			if(thermal)
			{
				const std::array<const assembly_attachment*,2> items{&m14ebr::attachments[2],thermal};
				fixture captured(m14ebr::assemblies[0],items);const auto matched=captured.resolve();
				check(captured.models.size()==4 && matched.value==&m14ebr::assemblies[0] &&
					native_reload_profile("m14ebr_thermal",10,matched.value->reload)==&m14ebr::physical,
					"Contingency M14 thermal plus bipod binds its existing H2 receiver and ten-round reload recipe");
			}
			check(!native_reload_profile("m14ebr_thermal",9,&m14ebr::physical) &&
				!native_reload_profile("m14ebr_thermal_extra",10,&m14ebr::physical),
				"thermal alias does not admit another capacity or unreviewed name suffix");
		}
		{
			std::array<const assembly_attachment*, 3> items{
			    &m14ebr::attachments[2], &m14ebr::attachments[3], &m14ebr::attachments[0]};
			fixture captured(m14ebr::assemblies[0], items);
			check(captured.models.size() == 5 && captured.resolve().value == &m14ebr::assemblies[0],
			    "captured M21 scope, bipod and silencer03 all bind together");
		}
		check(native_reload_profile("barrett", 10, &m82::physical) == &m82::physical &&
		          !native_reload_profile("m82_bipod_stand_thermal", 10, &m82::physical),
		    "M82 binds playable Barrett, never the scripted turret");
		fixture m82(m82::assemblies[0], {});
		m82.r.parent[68 + 21] = 68 + 11;
		check(!physical_reload::bind_parts(
		          m82.r, {m82.bones.data(), size_t(m82.r.count)}, vr::gameplay::weapons::m82::physical)
		           .valid,
		    "M82 intermediate carrier is mandatory, not flattened");
		fixture wa(wa2000::assemblies[0], {});
		for (const auto* definition:m14ebr::skins)
		{
			check(definition->magazine_subset_count()==1,"M14 detached magazine never contains the chamber cartridge");
			for (bool inserted:{false,true}) for (bool chamber:{false,true}) for (int rounds:{0,9})
			{
				mechanics::state ammo{};ammo.magazine_inserted=inserted;ammo.chamber_loaded=chamber;ammo.magazine_rounds=rounds;
				check(hide_reload_geometry(*definition,ammo,true,true)==!chamber &&
					hide_reload_geometry(*definition,ammo,true,false)==!inserted,
					"M14 cartridge visibility follows chamber independently of magazine ownership/contents");
			}
		}
		const auto& wa_grasp=wa2000::action_grips[0];
		const auto left_contact=compose_reload(wa_grasp.wrist,{wa_grasp.contact_in_wrist,{0,0,0,1}}).position;
		for(bool right:{false,true})for(const quat basis:{quat{0,0,0,1},normalize({.1f,-.3f,.2f,.9f})})
		{
			// Use the exact hand-retargeting path used by the presenter. A right
			// hand must select AND display the right knob, not a mirrored left knob.
			const auto grasp=right ? vr::gameplay::hands::pose_mirror::part(wa_grasp,basis) : wa_grasp;
			const auto contact=compose_reload(grasp.wrist,{grasp.contact_in_wrist,{0,0,0,1}}).position;
			auto expected=left_contact;if(right)expected[1]=2*wa2000::action_symmetry_center[1]-expected[1];
			check(length(sub(contact,expected))<.001f && (right ? contact[1]<-1.4f : contact[1]>1.4f),
				"WA2000 operating hand determines its real left/right symmetric handle contact");
			for(float travel:{0.f,wa2000::action_stroke_m*.5f,wa2000::action_stroke_m})
			{
				const vec offset{-travel*39.37007874f,0,0};auto wrist=grasp.wrist;wrist.position=add(wrist.position,offset);
				const auto candidate=choose_part_grip({&grasp,1},wrist,offset,wa2000::action_grab_low,wa2000::action_grab_high,39.37007874f);
				check(candidate.pose==0 && candidate.distance_meters<.002f,"WA2000 full-stroke acquisition and latched pose use the same per-hand handle");
			}
			auto far=grasp.wrist;far.position[2]+=30;
			check(choose_part_grip({&grasp,1},far,{},wa2000::action_grab_low,wa2000::action_grab_high,39.37007874f).distance_meters>part_grip_capture::radius_m,
				"symmetric handles retain physical reach limits");
		}
		{
			fixture port_fixture(cheytac::base, {});
			int marker=-1;
			for (int i=68;i<port_fixture.r.count;++i)
				if (port_fixture.bones[i].name=="tag_brass") marker=i;
			check(marker>=0,"M200 source contains ejection reference");
			if (marker>=0)
			{
				port_fixture.bones[68].bind.position={100,200,300};
				port_fixture.bones[marker].bind.position={107.236948f,199.337067f,304.179535f};
				const auto bind=[&] {return bind_ejection_port(port_fixture.r,
					std::span(port_fixture.bones).first(port_fixture.r.count));};
				const auto port=bind();
				check(port.valid && length(sub(port.local.position,{7.236948f,-.662933f,4.179535f}))<.0001f,
				    "ejection reference is held-receiver local, without world-model or head origin");
				port_fixture.r.parent[marker]=2;
				check(!bind().valid,"ejection marker attached to a hand is rejected");
				port_fixture.r.parent[marker]=68;
				port_fixture.bones[marker].bind.rotation={0,0,0,0};
				check(!bind().valid,"invalid ejection basis cannot produce a shell at world origin");
			}
		}
		wa.r.parent[69] = 75;
		check(!physical_reload::bind_parts(wa.r, {wa.bones.data(), size_t(wa.r.count)}, wa2000::physical).valid,
		    "WA2000 receiver-parented round contract remains explicit");
		check(valid_bolt(wa2000::bolt, wa2000::action_stroke_m) && bolt_travel(wa2000::bolt, 0, true) > .11f,
		    "WA2000 internal bolt stays locked while handle returns");
		check(bolt_travel(wa2000::bolt, .08f, false) == 0 && bolt_travel(wa2000::bolt, .14f, false) > .05f,
		    "WA2000 handle take-up retains native delayed bolt engagement");
		std::cout << "Precision rifle combinations checked: " << combinations << '\n';
		return failed;
	}
} // namespace precision_profile_tests
