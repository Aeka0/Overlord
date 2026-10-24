#include <std_include.hpp>
#include "native_melee_feedback.hpp"
#include "equipment_runtime.hpp"
#include "knife_profile.hpp"
#include "melee_weapon_profile.hpp"
#include "native_weapon_sound.hpp"
#include "special_melee.hpp"
#include "campaign/cliffhanger/runtime.hpp"
#include "native_scripted_control.hpp"
#include <utils/native_memory.hpp>
#include "component/scheduler.hpp"
#include "component/fastfiles.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::melee::feedback
{
	namespace
	{
		using namespace hands;
		using hands::pose_math::compose;
		using hands::pose_math::inverse;
		struct request { anchor root{}; vr::hand hand{}; std::uint64_t revision{}, reference{}, epoch{}, at{}; weapons::weapon_identity weapon{}; unsigned pick_side{2}; equipment::melee_weapons::kind variant{}; };
		struct audio_request {vec position{};vr::hand hand{};std::uint64_t revision{},reference{},epoch{},at{};bool impact{};};
		std::mutex mutex;
		std::array<request,16> pending{};
		std::size_t count{};
		std::array<audio_request,16> audio_pending{};std::size_t audio_count{};
		std::atomic_uint64_t audio_queued{},audio_played{},audio_discarded{};
		std::atomic_uint64_t epoch{}, queued{}, emitted{}, discarded{}, missing{};
		bool ready{};
		bool audio_ready{};
		constexpr std::uintptr_t play_oriented=0x140453f50;
		bool valid(const anchor& pose)
		{
			for (float x:pose.position) if (!std::isfinite(x) || std::abs(x)>1e7f) return false;
			float norm{};
			for (float x:pose.rotation) { if (!std::isfinite(x)) return false; norm+=x*x; }
			return std::abs(norm-1.f)<.01f;
		}
		void tick()
		{
			std::array<audio_request,16> audio_batch{};std::size_t audio_size{};
			{const std::lock_guard lock(mutex);audio_size=audio_count;std::copy_n(audio_pending.begin(),audio_size,audio_batch.begin());audio_count=0;}
			const auto audio_input=controller_input::latest();const auto* audio_pause=game::Dvar_FindVar("cl_paused");
			const bool audio_gameplay=game::CL_IsCgameInitialized() && !*game::keyCatchers && audio_input.focused &&
				audio_pause && !audio_pause->current.integer && weapons::carry::active() && scripted_control::predicted_allowed();
			const auto audio_now=GetTickCount64();
			for(std::size_t i=0;i<audio_size;++i)
			{
				const auto& item=audio_batch[i];
				if(!audio_gameplay || item.epoch!=epoch || item.reference!=audio_input.reference_generation || audio_now<item.at || audio_now-item.at>250 ||
					!equipment::current_knife(item.hand,item.revision,item.reference)) {++audio_discarded;continue;}
				if(weapons::native_weapon_sound::play_baton(item.impact,item.position))++audio_played;else ++audio_discarded;
			}
			std::array<request,16> batch{};std::size_t size{};
			{ const std::lock_guard lock(mutex);size=count;std::copy_n(pending.begin(),size,batch.begin());count=0; }
			if (!size) return;
			const auto now=GetTickCount64();const auto input=controller_input::latest();
			const auto* pause=game::Dvar_FindVar("cl_paused");
			const auto* alternate=game::Dvar_FindVar("player_meleeForceAlternate");
			// These are the same native controls read by 0x1403b98e0. The
			// unnamed enable gate is registered with this hash at 0x1403baf83.
			const auto* enabled=game::Dvar_FindVar("0x769a7d7d");
			const auto* blood=game::Dvar_FindVar("cg_blood");
			const bool gameplay=game::CL_IsCgameInitialized() && !*game::keyCatchers && pause && !pause->current.integer &&
				weapons::carry::active() && scripted_control::predicted_allowed();
			if (!gameplay || !alternate || alternate->current.integer || !enabled || !enabled->current.enabled || !blood)
			{ discarded+=size; return; }
			const auto* name=blood->current.enabled ? "vfx/weaponimpact/flesh_impact_knife" : "vfx/weaponimpact/flesh_impact_knife_noblood";
			auto* effect=game::DB_FindXAssetHeader(game::ASSET_TYPE_FX,name,0).fx;
			if (!effect || !effect->name || std::string_view(effect->name)!=name)
			{ missing+=size; return; }
			for (std::size_t i=0;i<size;++i)
			{
				const auto& item=batch[i];
				if(item.epoch!=epoch || item.reference!=input.reference_generation || now<item.at || now-item.at>250)
				{++discarded;continue;}
				std::string_view model_name=equipment::melee_weapons::get(item.variant).model;
				const bool pick=item.pick_side<2;
				if(pick)
				{
					if(!equipment::special::cliffhanger::current_pickaxe(item.hand,item.pick_side,item.revision,item.reference)){++discarded;continue;}
					model_name=item.pick_side?"viewmodel_ice_picker":"viewmodel_ice_picker_03";
				}
				else if(item.weapon)
				{
					const auto live=weapons::carry::held(item.weapon);
					if(live.rear!=item.hand || live.revision!=item.revision || item.weapon.weapon>=512)
					{++discarded;continue;}
					const auto* def=game::weapon_defs[item.weapon.weapon];
					model_name=def && def->szInternalName ? weapons::special_melee::receiver(def->szInternalName) : "";
					if(model_name.empty()){++discarded;continue;}
				}
				else if(!equipment::melee_weapons::blade_feedback(item.variant) || !equipment::current_knife(item.hand,item.revision,item.reference)){++discarded;continue;}
				auto* model=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,model_name.data(),0).model;
				const auto bone_count=item.weapon ? weapons::special_melee::bone_count(model_name) : equipment::melee_weapons::get(item.variant).bones;
				if(!model || !model->name || model->name!=model_name || model->numBones!=bone_count || !model->baseMat || !model->boneNames)
				{++missing;continue;}
				const char* root_name=game::SL_ConvertToString(model->boneNames[0]);
				const char* socket_name=game::SL_ConvertToString(model->boneNames[bone_count-1]);
				if (!root_name || !socket_name || std::string_view(root_name)!=(pick?"j_gun":item.weapon ? weapons::special_melee::root(model_name) : equipment::melee_weapons::get(item.variant).root) ||
					std::string_view(socket_name)!=(pick?"tag_ice_picker_fx":"tag_knife_fx"))
				{++missing;continue;}
				std::array<game::DObjAnimMat,2> bind{};
				if (!utils::native_memory::read_bytes(&bind[0],model->baseMat,sizeof(bind[0])) ||
					!utils::native_memory::read_bytes(&bind[1],model->baseMat+bone_count-1,sizeof(bind[1]))) {++missing;continue;}
				const auto pose=[](const game::DObjAnimMat& b) -> anchor {
					return {{b.trans[0],b.trans[1],b.trans[2]},{b.quat[0],b.quat[1],b.quat[2],b.quat[3]}};
				};
				if (!valid(pose(bind[0])) || !valid(pose(bind[1]))) {++missing;continue;}
				const auto socket=compose(inverse(pose(bind[0])),pose(bind[1]));
				const auto world=compose(item.root,socket);
				if (!valid(world)) { ++discarded;continue; }
				const std::array<vec,3> axis{rotate(world.rotation,{1,0,0}),rotate(world.rotation,{0,1,0}),rotate(world.rotation,{0,0,1})};
				// Same native FX asset as EV_MELEE_BLOOD, emitted once at the physical
				// knife socket. Native FX owns particle/screen-space element lifetime;
				// no dependency on the hidden flat viewmodel's DObj or tag availability.
				utils::hook::invoke<void>(play_oriented,0,effect,game::CG_GetGameTime(0),world.position.data(),axis.data());
				++emitted;
			}
		}
	}
	bool initialize()
	{
		constexpr std::uint8_t bytes[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48};
		std::array<std::uint8_t,sizeof(bytes)> mask;mask.fill(255);
		ready=bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(play_oriented),{bytes,mask.data(),mask.size()}));
		audio_ready=weapons::native_weapon_sound::initialize();
		if (ready || audio_ready)
		{
			fastfiles::on_pre_unload([] { const std::lock_guard lock(mutex);++epoch;discarded+=count;count=0;audio_discarded+=audio_count;audio_count=0; });
			scheduler::loop(tick,scheduler::pipeline::main);
		}
		return ready;
	}
	bool knife(const anchor& root,vr::hand hand,std::uint64_t revision,std::uint64_t reference,weapons::weapon_identity weapon,equipment::melee_weapons::kind variant)
	{
		if (!ready || !scheduler::is_executing(scheduler::pipeline::server) || !vr::valid_hand(hand) || !valid(root)) return false;
		const std::lock_guard lock(mutex);
		if (count==pending.size()) { ++discarded;return false; }
		if(!weapon && !equipment::melee_weapons::blade_feedback(variant))return false;
		pending[count++]={root,hand,revision,reference,epoch.load(),GetTickCount64(),weapon,2,variant};++queued;return true;
	}
	bool baton_sound(vec position,vr::hand hand,std::uint64_t revision,std::uint64_t reference,bool impact)
	{
		if(!audio_ready || !scheduler::is_executing(scheduler::pipeline::server) || !vr::valid_hand(hand) || !revision || !reference)return false;
		for(float x:position)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
		const std::lock_guard lock(mutex);if(audio_count==audio_pending.size()){++audio_discarded;return false;}
		audio_pending[audio_count++]={position,hand,revision,reference,epoch.load(),GetTickCount64(),impact};++audio_queued;return true;
	}
	std::string status()
	{
		return std::format("knife_fx_ready={} queued={} emitted={} discarded={} missing_native_asset={} baton_audio_ready={} baton_audio_queued={} baton_audio_played={} baton_audio_discarded={}\n",
			ready,queued.load(),emitted.load(),discarded.load(),missing.load(),audio_ready,audio_queued.load(),audio_played.load(),audio_discarded.load());
	}
	bool pickaxe(const anchor& root,vr::hand hand,unsigned side,std::uint64_t lease,std::uint64_t reference)
	{
		if(!ready || side>=2 || !scheduler::is_executing(scheduler::pipeline::server) || !valid(root) ||
			!equipment::special::cliffhanger::current_pickaxe(hand,side,lease,reference))return false;
		const std::lock_guard lock(mutex);if(count==pending.size()){++discarded;return false;}
		pending[count++]={root,hand,lease,reference,epoch.load(),GetTickCount64(),{},side};++queued;return true;
	}
}
