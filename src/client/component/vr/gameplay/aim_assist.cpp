#include <std_include.hpp>
#include "aim_assist.hpp"
#include "aim_assist_geometry.hpp"
#include "game/game.hpp"
#include "game/scripting/array.hpp"
#include "game/scripting/entity.hpp"
#include "game/scripting/execution.hpp"
#include <mutex>
#include <sstream>

namespace vr::gameplay::aim_assist
{
	namespace
	{
		struct evaluation
		{
			unsigned candidates{},protected_targets{},traces{};int target{-1};float correction{};
			bool neutral_enabled{};
			const char* reason{"setting disabled"};
		};
		struct counters
		{
			std::uint64_t disabled{},no_target{},applied{},errors{};
			evaluation last{};
		};
		std::mutex diagnostic_mutex;
		std::array<counters,2> diagnostics{};
		bool civilian_target(const scripting::entity& target)
		{
			// maps/_friendlyfire::friendly_fire_think classifies civilians
			// independently of team. Never interpret no_friendly_fire_penalty as
			// permission to attract a shot, or noncombat as a civilian marker.
			const auto type=target.get("type");
			if(type.is<std::string>() && type.as<std::string>()=="civilian")return true;
			const auto name=target.get("targetname");
			if(name.is<std::string>() && name.as<std::string>()=="upperdeck_canned_deaths_drone")return true;
			const auto classname=target.get("classname");
			return classname.is<std::string>() && classname.as<std::string>().find("civilian")!=std::string::npos;
		}
		bool campaign_allows_assist(std::string_view map)
		{
			if (map!="favela") return true;
			if (!*game::levelEntityId) return false;
			const scripting::entity level{*game::levelEntityId};
			const auto start=level.get("start_point");
			if (start.is<std::string>())
			{
				// Native later start points skip the opening flag transitions entirely.
				const auto point=start.as<std::string>();
				if (point=="soccer" || point=="hilltop" || point=="trailer1" ||
					point=="trailer2" || point=="trailer3" || point=="end") return true;
			}
			// maps/favela: torso/head hits fail the runner objective. Keep manual
			// muzzle aim through capture/interrogation until normal combat resumes.
			// Read saved native state per shot so restart/checkpoint rollback cannot
			// retain an enabled override. Never change the user's saved strength.
			const auto flags=level.get("flag");
			if (!flags.is<scripting::array>()) return false;
			const auto done=flags.as<scripting::array>().get(std::string{"torture_sequence_done"});
			return done.is<int>() && done.as<int>()!=0;
		}
		outcome evaluate(weapons::shot_geometry& shot, float strength, unsigned shooter,evaluation& report) noexcept
		{
			if (settings::aim_assist_degrees(strength) <= 0) return outcome::disabled;
			try
			{
				const auto* map_dvar=game::Dvar_FindVar("mapname");
				const std::string_view map=map_dvar && map_dvar->current.string ? map_dvar->current.string : "";
				if (!campaign_allows_assist(map)) {report.reason="campaign restriction";return outcome::disabled;}
				// Museum displays are intentionally neutral. The native parser ORs
				// its team arguments, so one query admits enemies and neutral actors
				// without admitting allies. Re-read the exact map every shot; no
				// exception can persist into a campaign level or checkpoint load.
				report.neutral_enabled=map=="ending" || map=="museum";
				const auto targets = report.neutral_enabled ?
					scripting::call<scripting::array>("getaiarray", {"bad_guys","neutral"}) :
					scripting::call<scripting::array>("getaiarray", {"bad_guys"});
				const auto count = targets.size();
				if (count < 0 || count > static_cast<int>(max_candidates)) {report.reason="invalid native candidate count";return outcome::query_failed;}
				report.candidates=static_cast<unsigned>(count);
				report.reason=count ? "no eligible visible target in muzzle cone/range" :
					report.neutral_enabled ? "no hostile or museum neutral AI" : "no hostile AI";
				selection selected(shot, strength);
				for (unsigned i = 0; i < static_cast<unsigned>(count); ++i)
				{
					const auto value = targets.get(i);
					if (!value.is<scripting::entity>()) continue;
					const auto target = value.as<scripting::entity>();
					const auto ref = target.get_entity_reference();
					if (ref.classnum != 0 || ref.entnum == shooter || ref.entnum >= max_candidates) continue;
					if (!scripting::call<int>("isalive", {target})) continue;
					const auto point = target.call("getshootatpos").as<scripting::vector>();
					selected.consider(ref.entnum, {point[0], point[1], point[2]},
						[&](unsigned entity, const hands::vec& destination) {
							// Role reads only for candidates that could improve the shot.
							// A protected candidate cannot hide a valid enemy behind it in
							// angular ranking; the normal visibility trace still owns cover.
							if(civilian_target(target)){++report.protected_targets;return false;}
							++report.traces;
							game::trace_t trace{};
							game::Bounds bounds{};
							// Same mask/trace ABI as the existing eye-to-muzzle cover guard.
							game::G_TraceCapsule(&trace, shot.origin.data(), destination.data(),
								&bounds, shooter, 0x280e831);
							return std::isfinite(trace.fraction) && trace.fraction >= 0 && trace.fraction <= 1 &&
								!trace.startsolid && !trace.allsolid &&
								(trace.fraction == 1 || (trace.hitType == 1 && trace.hitId == entity));
						});
				}
				if(!selected.apply(shot))return outcome::no_target;
				report.target=selected.entity();report.correction=selected.correction_degrees();report.reason="bullet direction corrected";
				return outcome::applied;
			}
			catch (const std::exception&)
			{
				// Selection is committed only after every query succeeds. Never let a
				// script query failure cancel a valid native shot or retain a stale target.
				report.reason="native target query failed";return outcome::query_failed;
			}
		}
	}
	outcome apply(weapons::shot_geometry& shot,float strength,unsigned shooter,shot_route route) noexcept
	{
		evaluation report;const auto result=evaluate(shot,strength,shooter,report);
		// Native/VM work is complete before taking the diagnostics lock. The
		// independent owner must not silently discard its assistance outcome.
		const std::lock_guard lock(diagnostic_mutex);
		auto& value=diagnostics[route==shot_route::independent ? 1 : 0];value.last=report;
		switch(result)
		{
		case outcome::disabled:++value.disabled;break;
		case outcome::no_target:++value.no_target;break;
		case outcome::applied:++value.applied;break;
		case outcome::query_failed:++value.errors;break;
		}
		return result;
	}
	std::string status()
	{
		std::array<counters,2> current;{const std::lock_guard lock(diagnostic_mutex);current=diagnostics;}
		std::ostringstream out;
		out<<"assist_applied="<<current[0].applied+current[1].applied<<" assist_no_target="<<current[0].no_target+current[1].no_target
			<<" assist_errors="<<current[0].errors+current[1].errors<<" assist_disabled="<<current[0].disabled+current[1].disabled<<'\n';
		for(unsigned i=0;i<current.size();++i)
		{
			const auto& value=current[i];const auto& last=value.last;
			out<<"assist_route="<<(i ? "independent" : "projected")<<" applied="<<value.applied<<" no_target="<<value.no_target
				<<" errors="<<value.errors<<" disabled="<<value.disabled<<" last_candidates="<<last.candidates
				<<" last_neutral_enabled="<<last.neutral_enabled
				<<" last_protected_targets="<<last.protected_targets
				<<" last_visibility_traces="<<last.traces<<" last_target="<<last.target<<" last_correction_degrees="<<last.correction
				<<" state="<<(value.applied+value.no_target+value.errors+value.disabled ? last.reason : "waiting for shot")<<'\n';
		}
		return out.str();
	}
}
