#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "native_use.hpp"
#include "native_use_range_bridge.hpp"
#include "native_carry.hpp"
#include "native_carry_model.hpp"
#include "scripted_use_proxy.hpp"
#include "pickup_visibility.hpp"
#include "sequences/gulag.hpp"
#include "interaction_debug.hpp"
#include "../head_pose_bridge.hpp"
#include "../controller_input.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::interaction::native
{
	namespace
	{
		constexpr std::uintptr_t get_list=0x140526670, update_hint=0x140527620, player_use=0x140527380;
		struct entry {game::gentity_s* entity{};float score{};std::uint32_t flags{};};
		static_assert(sizeof(entry)==16);
		utils::hook::detour list_hook,use_hook;
		bool installed{};
		bool hint_identity_ready{};
		bool controlling{}; // Native server use pass only, including its final release.
		thread_local const ray* querying{};
		thread_local const target* forced{};
		thread_local std::array<entry,4000> candidates{};
		thread_local std::array<unsigned short,16> breach_candidates{};
		thread_local unsigned breach_count{};
		thread_local unsigned surface_trace_budget{};
		bool breach_trigger(const game::gentity_s* entity)
		{
			if(!entity || !entity->script_classname)return false;
			const auto* name=game::SL_ConvertToString(entity->script_classname);
			return name && std::string_view(name)=="trigger_use_breach";
		}
		std::atomic<const char*> reason{"native use unavailable"};
		struct command_lease {target value{};bool held{};std::uint64_t reference{};controller_input::clock::time_point at{};};
		std::mutex command_mutex;
		command_lease command_state;
		template<class T> T read(const void* p,std::size_t offset)
		{T value{};std::memcpy(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));return value;}
		int number(const game::gentity_s* entity)
		{
			const auto base=reinterpret_cast<std::uintptr_t>(&game::g_entities[0]),address=reinterpret_cast<std::uintptr_t>(entity);
			if (address<=base || (address-base)%sizeof(game::gentity_s) || (address-base)/sizeof(game::gentity_s)>=4000) return -1;
			return int((address-base)/sizeof(game::gentity_s));
		}
		void eye_stub(const void* ps,float* out)
		{
			if (querying) std::copy(querying->origin.begin(),querying->origin.end(),out);
			else utils::hook::invoke<void>(0x1404B12E0,ps,out);
		}
		void direction_stub(int client,const void* ps,float* forward,float* right,float* up)
		{
			if (!querying) {utils::hook::invoke<void>(0x140680AE0,client,ps,forward,right,up);return;}
			// This callsite requests forward only; preserve the ordinary native API
			// if a future executable asks for the other axes as well.
			if (right || up) utils::hook::invoke<void>(0x140680AE0,client,ps,forward,right,up);
			std::copy(querying->forward.begin(),querying->forward.end(),forward);
		}
		int area_stub(const game::Bounds* bounds,unsigned short* output,int capacity,int contents)
		{
			if (!querying) return utils::hook::invoke<int>(0x140598590,bounds,output,capacity,contents);
			game::Bounds expanded{};
			std::copy(querying->head.begin(),querying->head.end(),expanded.midPoint);
			std::fill_n(expanded.halfSize,3,(querying->distance_meters+model_origin_margin_m)*querying->units);
			const int count=utils::hook::invoke<int>(0x140598590,&expanded,output,capacity,contents);
			breach_count=0;
			if(count>=0 && count<=capacity)for(int i=0;i<count && breach_count<breach_candidates.size();++i)
				if(output[i]>0 && output[i]<4000 && breach_trigger(&game::g_entities[output[i]]))breach_candidates[breach_count++]=output[i];
			return count;
		}
		float effective_range(float ordinary)
		{return querying ? std::max(ordinary,(querying->distance_meters+model_origin_margin_m)*querying->units) : ordinary;}
		void empty_hint_progress_stub(game::dvar_t* dvar,float value)
		{
			// The native cursor owner clears its progress dvar on an empty list.
			// Additional hover queries must not reset another hand's hold progress.
			if (!querying) utils::hook::invoke<void>(0x14061A750,dvar,value);
		}
		int pickup_admission_stub(game::gentity_s* item,const void* ps,int automatic,int dual)
		{
			// GetUseList subtracts rejected same-definition weapons from its return
			// count (0x140526EC7..0x140526F8E). A Touch_Item-only override is too late.
			if (querying && item && weapons::native_carry::duplicate_pickup_admitted(
				weapons::native_carry::entity_key(number(item)),ps,automatic,dual,weapons::carry::pickup_context::hand_query)) return 1;
			return utils::hook::invoke<int>(0x14067EA40,item,ps,automatic,dual);
		}
		void* range_stub()
		{
			// This one native call normalizes the direction and then compares its
			// REAL length in xmm0 with the per-type range in xmm6. Extend only that
			// range during a hand query; retain native lengths, scoring and script
			// orientation/volume tests. No shared dvar or player pose is modified.
			return utils::hook::assemble([](utils::hook::assembler& a) {
				emit_range_bridge(a,0x140282800,reinterpret_cast<std::uintptr_t>(effective_range));
			});
		}
		int list_stub(game::gentity_s* player,entry* output,int previous)
		{
			if (forced && player==&game::g_entities[0])
			{
				if (!live(*forced)) return 0;
				output[0]={&game::g_entities[forced->key.entity],0,0};return 1;
			}
			return list_hook.invoke<int>(player,output,previous);
		}
		bool visible(const vec& from,const vec& to,int target_entity)
		{
			game::Bounds point{};game::trace_t trace{};
			game::G_TraceCapsule(&trace,from.data(),to.data(),&point,0,0x280e831);
			return std::isfinite(trace.fraction) && ((trace.hitType==1 && trace.hitId==target_entity) ||
				(!trace.startsolid && !trace.allsolid && trace.fraction>=.999f));
		}
		oriented_volume weapon_volume(const game::gentity_s* entity,const weapons::native_carry::model_geometry& model)
		{
			oriented_volume volume;volume.origin=read<vec>(entity,0xf4);
			const auto angles=read<vec>(entity,0x100);float axis[3][3]{};
			if(!std::all_of(angles.begin(),angles.end(),[](float x){return std::isfinite(x) && std::abs(x)<1e7f;}))return volume;
			game::AnglesToAxis(angles.data(),axis);std::memcpy(volume.axis.data(),axis,sizeof(axis));
			volume.center=hands::scale(hands::add(model.low,model.high),.5f);
			volume.half=hands::scale(hands::sub(model.high,model.low),.5f);volume.valid=model.valid;return volume;
		}
		template<class Visible> target exposed_surface(const ray& aim,target_key key,std::uint32_t weapon,
			const oriented_volume& volume,Visible&& test)
		{
			if(!score_volume(aim,key,volume,weapon))return {};
			const auto local=weapons::native_carry::pickup_surface(weapon);
			std::array<vec,pickup_surface_samples::directions.size()> world{};
			if(local.size()>world.size())return {};
			for(std::size_t i=0;i<local.size();++i)world[i]=volume.world(local[i]);
			return visible_pickup_surface(aim,key,weapon,{world.data(),local.size()},surface_trace_budget,test);
		}
		int pickup_sight_stub(const float* from,const float* to,const game::Bounds* bounds,int pass,int target_entity,int contents)
		{
			const auto original=utils::hook::invoke<int>(0x1404CC070,from,to,bounds,pass,target_entity,contents);
			if(original || !querying || contents!=0x11 || target_entity<=0 || target_entity>=4000 || surface_trace_budget<2)return original;
			const auto item=weapons::native_carry::pickup_item(weapons::native_carry::entity_key(target_entity));
			if(!item.weapon)return original; // Native script models/use triggers retain their original point.
			const auto volume=weapon_volume(&game::g_entities[target_entity],weapons::native_carry::geometry(item.weapon));
			const auto proposal=exposed_surface(*querying,{target_entity,item.key.generation},item.weapon,volume,
				[&](const vec& eye,const vec& point){return utils::hook::invoke<int>(0x1404CC070,eye.data(),point.data(),bounds,pass,target_entity,contents)!=0;});
			return proposal ? 1 : original;
		}
		bool script_model_volume(const game::gentity_s* entity,oriented_volume& volume,vec& trace_point)
		{
			// GetUseList's type-5 branch uses this server DObj and local trace-point
			// helper. The matching bounds helper respects hidden model bones. Do not
			// borrow a client DObj, cache zone pointers, or interpret the 1-unit entity
			// collision box as the visible installation object's size.
			if (read<std::uint8_t>(entity,0)!=5) return false;
			const auto* object=vr::h2::sp::server_entity_dobj(entity);
			if (!object) return false;
			volume.origin=read<vec>(entity,0xf4);
			const auto angles=read<vec>(entity,0x100);
			if (!std::all_of(angles.begin(),angles.end(),[](float x){return std::isfinite(x) && std::abs(x)<1e7f;})) return true;
			float axis[3][3]{};game::AnglesToAxis(angles.data(),axis);std::memcpy(volume.axis.data(),axis,sizeof(axis));
			game::Bounds bounds{};
			utils::hook::invoke<void>(0x140654B20,object,&bounds);
			std::copy_n(bounds.midPoint,3,volume.center.begin());std::copy_n(bounds.halfSize,3,volume.half.begin());
			vec local{};utils::hook::invoke<void>(0x140588250,object,local.data());
			trace_point=volume.world(local);volume.valid=true;return true;
		}
		bool script_visible(const vec& from,const vec& to,const game::gentity_s* entity)
		{
			for (float x:to) if (!std::isfinite(x) || std::abs(x)>1e7f) return false;
			// Exactly GetUseList's point trace: zero bounds, the first signed byte
			// of its player state (the native pass-entity argument),
			// target entity number and contents 0x11. The general weapon capsule
			// mask also hits surfaces that native scripted use deliberately ignores.
			const auto* ps=utils::hook::invoke<const void*>(0x140517DF0,&game::g_entities[0]);
			if (!ps) return false;
			const game::Bounds point{};
			return utils::hook::invoke<int>(0x1404CC070,from.data(),to.data(),&point,
				int(read<std::int8_t>(ps,0)),read<std::uint16_t>(entity,0x8c),0x11)!=0;
		}
		void use_stub(game::gentity_s* player)
		{
			command_lease lease;{const std::lock_guard lock(command_mutex);lease=command_state;}
			const auto now=controller_input::clock::now();
			const bool holding=lease.held && now>=lease.at && now-lease.at<=150ms;
			const bool ours=player==&game::g_entities[0] && (holding || controlling);
			if (ours)
			{
				const auto input=controller_input::latest();head_pose_bridge::spatial_frame body;
				const bool valid_frame=holding && input.focused && now>=input.sampled_at && now-input.sampled_at<=150ms && input.reference_generation==lease.reference &&
					head_pose_bridge::get_spatial_frame(body) && body.generation==lease.reference;
				// Re-run native hint admission for precisely the held target. An empty
				// target still carries +activate to notify-only scene scripts, but cannot
				// silently activate a different object under the head's old crosshair.
				const target none{};
				forced=valid_frame && live(lease.value) ? &lease.value : &none;
				const auto leave=gsl::finally([]{forced=nullptr;});
				auto* client=read<std::byte*>(player,0x118);
				if (!client) {controlling=false;return;}
				// A cancelled VR hold may reach the server before its final usercmd.
				// Clear its native entity handle and force an empty hint until that
				// release arrives, rather than using the old head-ray target.
				const auto handle=read<std::uint16_t>(client,0xea08);
				if (handle && (!*forced || handle!=forced->key.entity+1))
					utils::hook::invoke<void>(0x1404ACCE0,client+0xea08,nullptr);
				controlling=holding || (read<std::uint32_t>(client,0xe90c)&0x8);
				utils::hook::invoke<void>(update_hint,player);
				if (read<std::uint16_t>(client,0xea08) &&
					(!*forced || read<std::uint16_t>(client,0x24)!=forced->key.entity || !read<std::uint8_t>(client,0xa)))
					utils::hook::invoke<void>(0x1404ACCE0,client+0xea08,nullptr);
				use_hook.invoke<void>(player);
				return;
			}
			use_hook.invoke<void>(player);
		}
		template<std::size_t N> bool verify(std::uintptr_t address,const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{};mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));
		}
		bool call_matches(std::uintptr_t site,std::uintptr_t target)
		{
			const auto* p=reinterpret_cast<const std::uint8_t*>(site);
			return p[0]==0xe8 && site+5+read<std::int32_t>(p,1)==target;
		}
	}
	bool initialize()
	{
		if (installed) return true;
		constexpr std::uint8_t list[]{0x40,0x55,0x53,0x48,0x8d,0xac,0x24,0xc8,0xdf,0xff,0xff};
		constexpr std::uint8_t use[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
		constexpr std::uint8_t hint[]{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0xb8,0x40,0xfa,0,0};
		constexpr std::uint8_t range_compare[]{0x0f,0x28,0xf8,0x0f,0x2f,0xfe,0x0f,0x87,0x9f,0x01,0,0};
		constexpr std::uint8_t handle[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57};
		constexpr std::uint8_t model_bounds[]{0x4c,0x8b,0xdc,0x55,0x57,0x41,0x55,0x41,0x56,0x41,0x57};
		constexpr std::uint8_t script_trace[]{0x48,0x83,0xec,0x58,0x33,0xc0,0x89,0x44,0x24,0x40};
		if (!weapons::native_carry::initialize() || !verify(get_list,list) || !verify(player_use,use) || !verify(update_hint,hint) ||
			!verify(0x140526D52,range_compare) || !verify(0x1404ACCE0,handle) || !call_matches(0x140526772,0x1404B12E0) ||
			!call_matches(0x140526789,0x140680AE0) || !call_matches(0x140526806,0x140598590) ||
			!call_matches(0x140526D4D,0x140282800) || !call_matches(0x1405272A9,0x14061A750) ||
			!call_matches(0x140526EC7,0x14067EA40) ||
			!call_matches(0x140526762,0x140517DF0) ||
			!call_matches(0x1405270D8,vr::h2::sp::server_entity_dobj.address()) || !call_matches(0x1405270ED,0x140588250) ||
			!call_matches(0x140588294,0x140654B20) || !call_matches(0x1405271D5,0x1404CC070) ||
			!verify(0x140654B20,model_bounds) || !verify(0x1404CC070,script_trace))
		{reason="native use signatures rejected";return false;}
		auto* bridge=range_stub();
		if (!bridge) {reason="native range bridge allocation failed";return false;}
		// JitRuntime does not guarantee proximity to the game. Reuse the native
		// TLS/equip relay table; widening this five-byte CALL would overwrite the
		// following comparison. Its RAX scratch is safe: the normalizer takes RCX
		// only, and this caller reloads RAX before its next use.
		auto* relay=utils::hook::create_far_jump<0x140000000>(bridge);
		struct call_patch {std::uintptr_t site;void* target;};
		const std::array calls{
			call_patch{0x140526772,reinterpret_cast<void*>(eye_stub)},
			call_patch{0x140526789,reinterpret_cast<void*>(direction_stub)},
			call_patch{0x140526806,reinterpret_cast<void*>(area_stub)},
			call_patch{0x140526D4D,relay},
			call_patch{0x1405272A9,reinterpret_cast<void*>(empty_hint_progress_stub)},
			call_patch{0x140526EC7,reinterpret_cast<void*>(pickup_admission_stub)},
			call_patch{0x1405271D5,reinterpret_cast<void*>(pickup_sight_stub)}};
		// Preflight every branch before installing any part of this adapter.
		for (const auto& patch:calls)
			if (!patch.target || utils::hook::is_relatively_far(reinterpret_cast<void*>(patch.site),patch.target))
			{reason="native use call target outside relative branch range";return false;}
		list_hook.create(get_list,list_stub);use_hook.create(player_use,use_stub);
		for (const auto& patch:calls) utils::hook::call(patch.site,patch.target);
		// The non-actor sethintstring writer and UpdateCursorHints reader agree
		// on gentity+0xb5. Failure only disables richer text, never native use.
		constexpr std::uint8_t hint_write[]{0x88,0x83,0xb5,0,0,0};
		constexpr std::uint8_t hint_read[]{0x44,0x0f,0xb6,0xaf,0xb5,0,0,0};
		hint_identity_ready=verify(0x1404EBB1C,hint_write) && verify(0x14052794A,hint_read);
		installed=true;reason="native hand use contracts ready";return true;
	}
	bool ready() noexcept {return installed;}
	bool live(const target& value) noexcept
	{
		if (!installed || !value) return false;
		const auto key=weapons::native_carry::entity_key(value.key.entity);
		const auto* entity=&game::g_entities[value.key.entity];
		return key.generation==value.key.generation && read<std::uint8_t>(entity,0xbc) &&
			(!value.weapon || read<std::uint32_t>(entity,0x80)==value.weapon) &&
			(scheduler::is_executing(scheduler::pipeline::server) ? sequences::gulag::allows_use(value.key.entity) :
				sequences::gulag::allows_native_use(value.key.entity,value.key.generation));
	}
	target query(const ray& aim,target_key held_target,debug::world_sample* diagnostic)
	{
		if (!installed || querying || !valid(aim) || !scheduler::is_executing(scheduler::pipeline::server)) return {};
		const auto* player=&game::g_entities[0];const auto* client=read<const std::byte*>(player,0x118);
		if (!client || read<int>(player,0x184)<=0 || (read<unsigned>(client,0xe908)&8)) return {};
		querying=&aim;const auto leave=gsl::finally([]{querying=nullptr;});
		surface_trace_budget=64;
		const auto proxies=scripted_use::sample(aim);
		breach_count=0;
		const auto count=list_hook.invoke<int>(&game::g_entities[0],candidates.data(),3999);
		if (count<0 || count>int(candidates.size())) {reason="native candidate count rejected";return {};}
		if (diagnostic) {diagnostic->aim=aim;diagnostic->native_count=count;}
		target selected;
		for (int i=0;i<count;++i)
		{
			const auto* entity=candidates[i].entity;const auto id=number(entity);if (id<=0) continue;
			if(!sequences::gulag::allows_use(id))continue;
			const auto key=weapons::native_carry::entity_key(id);
			// Bound script triggers are selected at their visual proxy exclusively.
			// The old left-shifted trigger volume must not remain a second target.
			if(std::any_of(proxies.begin(),proxies.end(),[&](const auto& proxy){return proxy.trigger==target_key{id,key.generation} || proxy.visual==target_key{id,key.generation};}))continue;
			const auto item=weapons::native_carry::pickup_item(key);
			// Only the instance adapter admits supported physical firearm copies.
			if (read<std::uint8_t>(entity,0)==2 && !item.weapon &&
				vr::h2::sp::weapon_inventory_type(read<std::uint32_t>(entity,0x80),false)==0) continue;
			const auto center=read<vec>(entity,0xdc);
			oriented_volume volume;
			vec trace_point=center;bool script_model=false;
			if (item.weapon)
			{
				volume=weapon_volume(entity,weapons::native_carry::geometry(item.weapon));
			}
			else
			{
				script_model=script_model_volume(entity,volume,trace_point);
				if (!script_model) {volume.center=center;volume.half=read<vec>(entity,0xe8);volume.valid=true;}
			}
			const bool breach=breach_trigger(entity);
			const auto* classname=entity->script_classname ? game::SL_ConvertToString(entity->script_classname) : nullptr;
			const bool use_volume=!item.weapon && !script_model && classname && use_trigger_class(classname);
			auto proposal=breach ? score_breach(aim,{id,key.generation},center,read<vec>(entity,0xe8)) :
				use_volume ? score_use_trigger(aim,{id,key.generation},center,read<vec>(entity,0xe8)) :
				item.weapon || script_model ? score_volume(aim,{id,key.generation},volume,item.weapon) : score(aim,{id,key.generation},center);
			if (!script_model) trace_point=proposal.position;
			const auto test_visibility=[&](const vec& from) {return script_model ? script_visible(from,trace_point,entity) : visible(from,trace_point,id);};
			bool head_clear=proposal && test_visibility(aim.head);
			bool hand_tested=proposal && (head_clear || diagnostic);
			bool hand_clear=hand_tested && test_visibility(aim.origin);
			if(proposal && item.weapon && !(head_clear && hand_clear))
			{
				const auto exposed=exposed_surface(aim,{id,key.generation},item.weapon,volume,
					[&](const vec& eye,const vec& point){return visible(eye,point,id);});
				if(exposed){proposal=exposed;trace_point=exposed.position;head_clear=hand_tested=hand_clear=true;}
			}
			const bool clear=head_clear && hand_clear;
			if (diagnostic)
			{
				debug::candidate entry{{id,key.generation},item.weapon,volume,center,read<vec>(entity,0xe8),proposal,
					!proposal ? debug::verdict::geometry : clear ? debug::verdict::admitted : debug::verdict::occluded};
				entry.trace_point=trace_point;entry.script_model=script_model;
				entry.head_visibility=!proposal ? debug::visibility::not_tested : head_clear ? debug::visibility::clear : debug::visibility::blocked;
				entry.hand_visibility=!hand_tested ? debug::visibility::not_tested : hand_clear ? debug::visibility::clear : debug::visibility::blocked;
				// Keep the nearest eight and always include an admitted candidate. No
				// unbounded per-frame history or extra trace work for diagnostics.
				auto& d=*diagnostic;
				if (d.count<d.candidates.size()) d.candidates[d.count++]=entry;
				else
				{
					unsigned farthest{};float distance=-1;
					for (unsigned n=0;n<d.count;++n) {const auto range=hands::length(hands::sub(d.candidates[n].volume.world(d.candidates[n].volume.center),aim.origin));if (range>distance) {distance=range;farthest=n;}}
					if (clear || hands::length(hands::sub(volume.world(volume.center),aim.origin))<distance) d.candidates[farthest]=entry;
				}
			}
			if (clear && prefer(proposal,selected,held_target))
			{
				proposal.position=item.weapon || script_model ? volume.world(volume.center) : center;
				if (proposal.key==held_target) {selected=proposal;break;}
				selected=proposal;
			}
		}
		const auto nearby=breach_candidates;const auto nearby_count=breach_count;
		const char* proxy_reason="script proxy: aim outside visual bounds or native phase unavailable";
		// Rank before querying, retain a held target, and cap expensive native
		// admission to four candidates regardless of the level's crate count.
		for(const auto& candidate:scripted_use::candidates(proxies,aim,held_target))
		{
			if(!candidate.source || !prefer(candidate.proposal,selected,held_target))continue;
			const auto& proxy=*candidate.source;auto proposal=candidate.proposal;
			const auto* trigger=&game::g_entities[proxy.trigger.entity];
			const auto* visual=&game::g_entities[proxy.visual.entity];
			const auto delta=hands::sub(proxy.native_center,aim.head);const auto distance=hands::length(delta);
			bool admitted{};
			const auto trigger_key=weapons::native_carry::entity_key(proxy.trigger.entity);
			const bool same_trigger=trigger_key.generation==proxy.trigger.generation && read<unsigned char>(trigger,0xbc);
			if(same_trigger && std::isfinite(distance) && distance>.01f*aim.units)
			{
				// Intent is proven against the visible model above. Aim this one
				// eligibility query at the original trigger without moving it.
				ray admission=aim;admission.origin=aim.head;admission.forward=hands::scale(delta,1.f/distance);
				querying=&admission;const int admitted_count=list_hook.invoke<int>(&game::g_entities[0],candidates.data(),3999);querying=&aim;
				admitted=admitted_count>=0 && admitted_count<=int(candidates.size()) && std::any_of(candidates.begin(),candidates.begin()+admitted_count,[&](const auto& c){return c.entity==trigger;});
			}
			const auto visual_key=weapons::native_carry::entity_key(proxy.visual.entity);
			const bool same_visual=visual_key.generation==proxy.visual.generation && read<unsigned char>(visual,0xbc);
			const bool head_clear=admitted && same_visual && script_visible(aim.head,proposal.position,visual);
			const bool hand_clear=head_clear && script_visible(aim.origin,proposal.position,visual);
			proxy_reason=!admitted?"script proxy: native trigger admission rejected":!same_visual?"script proxy: visual entity changed":!head_clear?"script proxy: head view obstructed":!hand_clear?"script proxy: hand view obstructed":"visual script target selected";
			if(diagnostic)
			{
				debug::candidate entry{proxy.trigger,0,proxy.volume,proxy.native_center,read<vec>(trigger,0xe8),proposal,
					!admitted || !same_visual?debug::verdict::native_rejected:head_clear && hand_clear?debug::verdict::admitted:debug::verdict::occluded};
				entry.trace_point=proposal.position;entry.script_model=true;
				entry.head_visibility=!admitted?debug::visibility::not_tested:head_clear?debug::visibility::clear:debug::visibility::blocked;
				entry.hand_visibility=!head_clear?debug::visibility::not_tested:hand_clear?debug::visibility::clear:debug::visibility::blocked;
				if(diagnostic->count<diagnostic->candidates.size())diagnostic->candidates[diagnostic->count++]=entry;
				else diagnostic->candidates.back()=entry;
			}
			const auto accepted=scripted_use::finish(proxy,proposal,admitted && same_visual,head_clear && hand_clear);
			if(prefer(accepted,selected,held_target))
			{
				selected=accepted;
			}
		}
		// Native centre-angle filtering may omit a door when the hand is already
		// at/past its point marker. Use only spatial candidates collected by the
		// original native area query; re-run native eligibility for the exact door.
		// This is bounded to four queries and never calls its use handler directly.
		if(!selected)
		{
			unsigned tested{};
			for(unsigned n=0;n<nearby_count && tested<4;++n)
			{
				const int id=nearby[n];const auto* entity=&game::g_entities[id];
				const auto center=read<vec>(entity,0xdc),half=read<vec>(entity,0xe8);
				const auto identity=weapons::native_carry::entity_key(id);
				auto proposal=score_breach(aim,{id,identity.generation},center,half);if(!proposal)continue;
				const auto delta=hands::sub(center,aim.head);const auto distance=hands::length(delta);
				if(!std::isfinite(distance) || distance<.01f*aim.units)continue;
				ray admission=aim;admission.origin=aim.head;admission.forward=hands::scale(delta,1.f/distance);
				querying=&admission;++tested;
				const int admitted=list_hook.invoke<int>(&game::g_entities[0],candidates.data(),3999);
				querying=&aim;
				if(admitted<0 || admitted>int(candidates.size()) ||
					std::none_of(candidates.begin(),candidates.begin()+admitted,[&](const auto& c){return c.entity==entity;}))continue;
				// Contact can put a controller just inside the door surface. Head
				// visibility plus native admission remain mandatory in that case.
				vec point=center;
				for(unsigned axis=0;axis<3;++axis)
				{
					const float radius=std::clamp(half[axis],(axis==2 ? .4f : .3f)*aim.units,aim.units);
					point[axis]=std::clamp(aim.head[axis],center[axis]-radius,center[axis]+radius);
				}
				if(!script_visible(aim.head,point,entity) || (!proposal.contact && !script_visible(aim.origin,point,entity)))continue;
				proposal.position=center;
				if(prefer(proposal,selected,held_target))selected=proposal;
				if(proposal.key==held_target)break;
			}
		}
		if(selected && !selected.weapon && hint_identity_ready)
		{
			const auto* entity=&game::g_entities[selected.key.entity];
			const auto* classname=entity->script_classname?game::SL_ConvertToString(entity->script_classname):nullptr;
			const auto type=read<std::uint8_t>(entity,0);
			if(!read<const void*>(entity,0x120) && (type==5 || type==8 || (classname && use_trigger_class(classname))))
			{
				const auto hint=read<std::uint8_t>(entity,0xb5);
				if(hint>0 && hint<32)selected.hint=hint;
			}
		}
		if (diagnostic) {diagnostic->selected=selected;for (unsigned n=0;n<diagnostic->count;++n) if (diagnostic->candidates[n].key==selected.key) diagnostic->candidates[n].result=debug::verdict::selected;}
		const bool proxy_selected=std::any_of(proxies.begin(),proxies.end(),[&](const auto& proxy){return selected && selected.key==proxy.trigger;});
		reason=!proxies.empty() && (!selected || proxy_selected)?proxy_reason:selected ? "native candidate selected by hand direction" : "no visible hand-ray candidate";
		return selected;
	}
	void set_command_lease(const target& value,bool held,std::uint64_t reference) noexcept
	{const std::lock_guard lock(command_mutex);command_state={value,held,reference,controller_input::clock::now()};}
	const char* status() noexcept {return reason.load();}
}
