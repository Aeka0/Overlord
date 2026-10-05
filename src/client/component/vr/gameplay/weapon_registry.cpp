#include "weapon_registry.hpp"
#include "weapon_profiles.hpp"
#include "weapon_profile_binding.hpp"
#include "weapon_reload_profiles.hpp"
#include "tube_profiles.hpp"
#include "break_action_profiles.hpp"
#include "cylinder_profiles.hpp"
#include "weapon_native_traits.hpp"
#include "weapons/rpg/profile.hpp"
#include "weapons/at4/profile.hpp"
#include "weapons/stinger/profile.hpp"
#include "weapons/javelin/profile.hpp"
#include "weapons/m9/profile.hpp"
#include "weapons/m4/profile.hpp"
#include "weapons/m16/profile.hpp"
#include "weapons/m14ebr/profile.hpp"
#include "weapons/dragunov/profile.hpp"
#include "weapons/m82/profile.hpp"
#include "weapons/wa2000/profile.hpp"
#include "weapons/cheytac/profile.hpp"
#include "weapons/m1911/profile.hpp"
#include "weapons/de50/profile.hpp"
#include "weapons/usp/profile.hpp"
#include "weapons/designator/profile.hpp"
#include "weapons/magnum44/profile.hpp"
#include "weapons/ak47/profile.hpp"
#include "weapons/g18/profile.hpp"
#include "weapons/m93r/profile.hpp"
#include "weapons/tmp/profile.hpp"
#include "weapons/miniuzi/profile.hpp"
#include "weapons/acr/profile.hpp"
#include "weapons/vector/profile.hpp"
#include "weapons/ump/profile.hpp"
#include "weapons/aug/profile.hpp"
#include "weapons/mp5/profile.hpp"
#include "weapons/fal/profile.hpp"
#include "weapons/scar/profile.hpp"
#include "weapons/tavor/profile.hpp"
#include "weapons/fn2000/profile.hpp"
#include "weapons/aa12/profile.hpp"
#include "weapons/p90/profile.hpp"
#include "weapons/striker/profile.hpp"
#include "weapons/m240/profile.hpp"
#include "weapons/mg4/profile.hpp"
#include "weapons/rpd/profile.hpp"
#include "weapons/m1014/profile.hpp"
#include "weapons/spas12/profile.hpp"
#include "weapons/model1887/profile.hpp"
#include "weapons/winchester1200/profile.hpp"
#include "weapons/pp2000/profile.hpp"
#include "weapons/l86/profile.hpp"
#include "weapons/famas/profile.hpp"
#include "weapons/ranger/profile.hpp"
#include "weapons/m79/profile.hpp"
#include "weapons/riot_shield/profile.hpp"
#include "weapons/special_knives/profile.hpp"

namespace vr::gameplay::weapons
{
	namespace
	{
	const auto catalog = join_registrations(
		register_profiles(rpg::select,rpg::base),
		register_profiles(at4::select,at4::base),
		register_profiles(stinger::select,stinger::base),
		register_profiles(javelin::select,javelin::base),
		register_profiles(riot_shield::select, riot_shield::base),
		register_profiles(special_knives::select,special_knives::ending,special_knives::bloody,special_knives::bayonet),
		register_profiles(nullptr, m9::base),
		register_profiles(nullptr, m1911::base),
		register_profiles(nullptr, de50::base, de50::gold),
		register_profiles(usp::select, usp::base, usp::silenced),
		register_profiles(nullptr, designator::base),
		register_profiles(m4::select, m4::foregrip, m4::grenadier, m4::arctic_grenadier),
		register_profiles(ak47::select, ak47::assemblies),
		register_profiles(nullptr, g18::base),
		register_profiles(nullptr, m93r::base),
		register_profiles(tmp::select, tmp::base),
		register_profiles(nullptr, miniuzi::base),
		register_profiles(acr::select, acr::assemblies),
		register_profiles(vector::select, vector::assemblies),
		register_profiles(mp5::select, mp5::assemblies),
		register_profiles(aug::select, aug::assemblies),
		register_profiles(ump::select, ump::assemblies),
		register_profiles(fal::select, fal::bare, fal::shotgun),
		register_profiles(pp2000::select, pp2000::base),
		register_profiles(l86::select, l86::base),
		register_profiles(famas::select, famas::assemblies),
		register_profiles(m16::select, m16::bare, m16::grenadier),
		register_profiles(m14ebr::select, m14ebr::assemblies),
		register_profiles(m82::select, m82::assemblies),
		register_profiles(wa2000::select, wa2000::assemblies),
		register_profiles(cheytac::select, cheytac::assemblies),
		register_profiles(scar::select, scar::bare, scar::shotgun, scar::grenadier, scar::foregrip),
		register_profiles(tavor::select, tavor::assemblies),
		register_profiles(fn2000::select, fn2000::assemblies),
		register_profiles(aa12::select, aa12::base),
		register_profiles(dragunov::select, dragunov::base, dragunov::arctic, dragunov::woodland),
		register_profiles(p90::select, p90::assemblies),
		register_profiles(m240::select, m240::base, m240::arctic),
		register_profiles(mg4::select, mg4::base, mg4::arctic),
		register_profiles(rpd::select, rpd::base, rpd::digital),
		register_profiles(m1014::select, m1014::assemblies),
		register_profiles(spas12::select, spas12::base, spas12::arctic),
		register_profiles(winchester1200::select, winchester1200::base),
		register_profiles(model1887::select, model1887::base),
		register_profiles(striker::select, striker::base, striker::woodland),
		register_profiles(nullptr, magnum44::base),
		register_profiles(ranger::select,ranger::base),
		register_profiles(m79::select,m79::base));
		static_assert(catalog.size() <= registration_capacity);
	}
	const std::span<const profile_registration> registered_profiles{catalog};
	const profile_capabilities<reload_profile> reload_profiles{registered_profiles,&profile::reload};
	const profile_capabilities<tube_profile> tube_definitions{registered_profiles,&profile::tube};
	const profile_capabilities<break_action_profile> break_action_definitions{registered_profiles,&profile::break_open};
	const profile_capabilities<cylinder_profile> cylinder_definitions{registered_profiles,&profile::cylinder};
	bool admits_native_reload(std::string_view name,int capacity,bool no_partial,bool segmented,int add) noexcept
	{
		if(native_break_action_profile(name,capacity))return true;
		return !no_partial && (native_tube_profile(name,capacity)?
			native_tube_shape_supported(name,capacity,segmented,add):native_reload_shape_supported(name,capacity,segmented,add));
	}
	profile_match select_profile(std::span<const hands::model_definition> models,
		const hands::rig& rig, std::span<const hands::bone_definition> bones) noexcept
	{
		if (rig.count <= 0 || rig.count > 256 || rig.gun < 0 || rig.gun >= rig.count || models.size() > 32) return {};
		int count{};
		for (const auto& model : models)
		{
			if (model.begin != count || model.count <= 0 || model.count > rig.count-count) return {};
			count += model.count;
		}
		if (count != rig.count) return {};

		for (const auto& receiver : models)
			if (rig.gun >= receiver.begin && rig.gun < receiver.begin + receiver.count)
			{
				for (const auto& entry : registered_profiles)
					if (entry.value->receiver == receiver.name)
					{
						if (!entry.select) return bind_profile_attachments(*entry.value, models, receiver, rig, bones);
						const auto match = entry.select(models, receiver, rig, bones);
						if (!match.value) return match;
						// A selector cannot opt an unregistered variant into rendering
						// while leaving its mechanics out of the native catalogs.
						for (const auto& candidate : registered_profiles)
							if (candidate.value == match.value && candidate.select == entry.select &&
								candidate.value->receiver == receiver.name) return match;
						return {nullptr, "assembly selector returned an unregistered variant"};
					}
				return {};
			}
		return {};
	}
}
