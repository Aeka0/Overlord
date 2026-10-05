#include <std_include.hpp>
#include "game/game.hpp"
#include "vehicle_runtime.hpp"
#include "vehicle_script_contract.hpp"
#include "../diagnostics.hpp"
#include "native_weapon_sound.hpp"
#include "weapon_feedback.hpp"
#include "../controller_haptics.hpp"
#include "component/gsc/script_extension.hpp"
#include "component/gsc/script_loading.hpp"
#include "component/scripting.hpp"
#include "component/notifies.hpp"
#include "component/scheduler.hpp"
#include "component/console.hpp"
#include "game/scripting/execution.hpp"
#include "game/scripting/stack_isolation.hpp"
#include "game/scripting/safe_execution.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::vehicles::native
{
	namespace
	{
		using namespace hands::pose_math;
		struct range {const char* begin{},*end{};explicit operator bool()const{return begin && end>begin;}};
		struct binding {kind type{};range shot{};std::array<const char*,16> hooks{};unsigned count{};};
		std::array<binding,2> bindings{};
		std::array<char,4> shoot_code{},target_code{};
		std::atomic_uint ready_mask{};
		bool shot_accepted{};
		std::uint64_t consumed_press{};
		std::atomic_uint64_t shots{},suppressed{},binding_failures{};
		unsigned opcode(xsk::gsc::opcode code){return gsc::gsc_ctx->opcode_id(code);}
		scripting::entity actor(){return scripting::entity{game::scr_entref_t{0,0}};}
		std::string file(kind k){return scripting::get_token_single(k==kind::zodiac?0xBE1D:0xAF69);}
		range function(kind k,unsigned id)
		{
			const auto table=scripting::script_function_table_sort.find(file(k));if(table==scripting::script_function_table_sort.end())return {};
			const auto name=scripting::get_token_single(id);const auto& entries=table->second;
			for(size_t i=0;i+1<entries.size();++i)if(entries[i].first==name)return {entries[i].second,entries[i+1].second};
			return {};
		}
		range named(kind k,std::string_view name)
		{
			const auto table=scripting::script_function_table_sort.find(file(k));if(table==scripting::script_function_table_sort.end())return {};
			const auto& entries=table->second;for(size_t i=0;i+1<entries.size();++i)if(entries[i].first==name)return {entries[i].second,entries[i+1].second};return {};
		}
		bool player_scope()
		{
			if(!active())return false;
			const auto id=game::scr_VmPub->function_frame->fs.localId;
			return id && game::scr_VarGlob->objectVariableValue[id].u.f.next==actor().get_entity_id();
		}
		bool skip_shot_tail(){return player_scope() && !shot_accepted;}
		bool fresh(const snapshot& s)
		{
			const auto input=controller_input::latest();const auto now=controller_input::clock::now();const auto driving=current();
			return presentation_allowed() && s.driving.epoch==driving.epoch && s.driving.entity==driving.entity && s.owner.can_fire() && s.geometry_ready &&
				s.reference==input.reference_generation && s.continuity==input.continuity_generation && input.focused && !input.orientation_settling && now>=s.at && now-s.at<=150ms &&
				input.grip[unsigned(s.owner.rear)].valid && input.aim[unsigned(s.owner.rear)].valid;
		}
		bool matches(context c)
		{
			if(!c || !ready(c.type) || !active())return false;
			const auto latest=current();if(latest.epoch!=c.epoch || latest.entity!=c.entity || latest.type!=c.type)return false;
			const auto vehicle=actor().get("vehicle");return vehicle.is<scripting::entity>() && vehicle.as<scripting::entity>().get_entity_reference().entnum==c.entity;
		}
		bool write_rounds(context c,int expected,int desired)
		{
			if(expected<0 || expected>32 || desired<0 || desired>32 || !matches(c))return false;
			const scripting::entity vehicle{game::scr_entref_t{static_cast<unsigned short>(c.entity),0}};
			const auto id=vehicle.get_entity_id(),field=ammo_field(c.type);const auto value=scripting::get_object_variable(id,field);
			if(!value.is<int>() || value.as<int>()!=expected)return false;
			scripting::set_object_variable(id,field,desired);return true;
		}
		anchor muzzle(const snapshot& s,std::string_view tag="tag_flash")
		{anchor local;return model_marker(s.driving.type,tag,local)?compose(s.gun,local):anchor{};}

		bool visible(vec start,vec end,unsigned entity)
		{
			game::trace_t trace{};game::Bounds bounds{};game::G_TraceCapsule(&trace,start.data(),end.data(),&bounds,0,0x280e831);
			return !trace.startsolid && !trace.allsolid && std::isfinite(trace.fraction) && trace.fraction>=0 && trace.fraction<=1 &&
				(trace.fraction==1 || (trace.hitType==1 && trace.hitId==entity));
		}
		struct aim_target {vec point{};scripting::script_value entity{};bool rider{};};
		struct aim_snapshot {snapshot source{};aim_target target{};scripting::script_value object{};controller_input::clock::time_point at{};bool valid{};};
		aim_snapshot prepared_aim;
		scripting::script_value fallback_target;
		std::atomic_uint64_t aim_queries{},aim_errors{};
		bool aim_ready(const snapshot& s)
		{
			const auto& p=prepared_aim;const auto now=controller_input::clock::now();
			return p.valid && p.source.driving.epoch==s.driving.epoch && p.source.driving.entity==s.driving.entity &&
				p.source.owner.id()==s.owner.id() && p.source.owner.rear_revision==s.owner.rear_revision && p.source.reference==s.reference &&
				now>=p.at && now-p.at<=150ms;
		}
		vec vector(scripting::script_value v){const auto p=v.as<scripting::vector>();return {p[0],p[1],p[2]};}
		aim_target select(const snapshot& s,anchor gun)
		{
			// Same native candidate sources, ordering and mission exceptions as
			// BE1D/A8AF and AF69/C7C1. The reference is the tracked gun, never driving yaw.
			aim_target result{add(gun.position,rotate(gun.rotation,{1500,0,0}))};
			const auto local=[&](vec p){return rotate(conjugate(gun.rotation),sub(p,gun.position));};
			if(s.driving.type==kind::zodiac)
			{
				// maps/_utility::ent_flag is exactly this saved array lookup.
				const auto flags=actor().get("ent_flag");
				const auto special=flags.is<scripting::array>()?flags.as<scripting::array>().get(std::string{"zodiac_aim_helicopter"}):scripting::script_value{};
				if(special.is<int>() && special.as<int>())
				{
					const auto helicopter=scripting::get_object_variable(*game::levelEntityId,0xCB74u);
					if(helicopter.is<scripting::entity>()){result.entity=helicopter;result.point=vector(helicopter.as<scripting::entity>().get("origin"));return result;}
				}
			}
			const auto targets=scripting::call<scripting::array>("getaiarray",{"bad_guys"});const auto count=targets.size();float best=INFINITY;
			if(count<0 || count>2048)return result;
			for(int i=0;i<count;++i)
			{
				const auto value=targets.get(i);if(!value.is<scripting::entity>())continue;const auto entity=value.as<scripting::entity>();
				if(!scripting::call<int>("isalive",{entity}))continue;
				const auto p=vector(entity.call(s.driving.type==kind::zodiac?"getshootatpos":"geteye"));
				const auto score=aim_score(s.driving.type,local(p),s.driving.type==kind::zodiac?1300.f:750.f);
				if(score<best && visible(gun.position,p,entity.get_entity_reference().entnum)){best=score;result={p,value,false};}
			}
			if(result.entity.is<scripting::entity>() || s.driving.type!=kind::zodiac)return result;
			const auto consider=[&](const scripting::script_value& v,float range,bool rider)
			{
				if(!v.is<scripting::entity>())return false;const auto entity=v.as<scripting::entity>();
				const auto p=vector(entity.get("origin"));const auto offset=local(p);const auto distance=length(offset);
				if(!std::isfinite(distance) || distance<.001f || distance>range || offset[0]/distance<.965925f || !visible(gun.position,p,entity.get_entity_reference().entnum))return false;
				result={p,v,rider};return true;
			};
			for(const auto* name:{"destructible_toy","explodable_barrel"})
			{
				const auto objects=scripting::call<scripting::array>("getentarray",{name,"targetname"});if(objects.size()<0 || objects.size()>4096)continue;
				for(int i=0;i<objects.size();++i)if(consider(objects.get(i),2300,false))
				{(void)scripting::call_script_function(actor(),file(s.driving.type),scripting::get_token_single(0xCABD),{result.entity});return result;}
			}
			const auto riders=scripting::call_script_function(actor(),file(s.driving.type),scripting::get_token_single(0xC13C),{});
			if(riders.is<scripting::array>())
			{
				const auto list=riders.as<scripting::array>();if(list.size()>=0 && list.size()<=4096)
					for(int i=0;i<list.size();++i)if(consider(list.get(i),1300,true))return result;
			}
			return result;
		}
		bool firing(const snapshot& s)
		{
			if(!fresh(s) || !aim_ready(s) || !s.inserted || s.magazine_grabbed || s.quick_loading)return false;
			const auto input=controller_input::latest();const auto h=unsigned(s.owner.rear);
			return input.squeeze[h].active && input.squeeze[h].down && input.trigger[h].active &&
				((s.fire && input.trigger[h].down) || s.press.pending(consumed_press,controller_input::clock::now()));
		}
		void shoot_button()
		{
			const auto s=latest();scripting::push_value(firing(s) && rounds(s.driving)>0?1:0);
		}
		void target_value()
		{
			// Never re-enter VM_Execute from a builtin already running inside the
			// native shooting loop. The server coordinator prepares the native
			// helper result before this callback; no engine query runs here.
			scripting::push_value(aim_ready(latest())?prepared_aim.object:fallback_target);
		}
		bool in_shot()
		{
			if(!player_scope())return false;const auto pos=game::scr_function_stack->pos;const auto c=current();
			for(const auto& b:bindings)if(b.type==c.type && pos>=b.shot.begin && pos<b.shot.end)return true;return false;
		}
		bool fire(game::BuiltinFunction original)
		{
			if(!in_shot() || game::scr_VmPub->outparamcount!=4)return false;
			diagnostics::record_trace(diagnostics::trace_event::vehicle_shot,0);
			shot_accepted=false;
			try
			{
				std::vector<scripting::script_value> args;for(unsigned i=0;i<4;++i)args.emplace_back(game::scr_VmPub->top[-int(i)]);
				const auto s=latest();if(!args[0].is<std::string>() || args[0].as<std::string>()!=weapon_name(s.driving.type) || !args[3].is<scripting::entity>() || args[3].as<scripting::entity>()!=actor())return false;
				const auto loaded=rounds(s.driving);const auto gun=muzzle(s);
				if(!firing(s) || loaded<=0 || length(gun.position)<.001f)
				{game::Scr_ClearOutParams();++suppressed;return true;}
				head_pose_bridge::spatial_frame body;
				if(!head_pose_bridge::get_spatial_frame(body) || body.generation!=s.reference){game::Scr_ClearOutParams();return true;}
				game::trace_t cover{};game::Bounds bounds{};game::G_TraceCapsule(&cover,body.head_position.data(),gun.position.data(),&bounds,0,0x280e831);
				if(cover.startsolid || cover.allsolid || !std::isfinite(cover.fraction) || (cover.fraction<1 && !(cover.hitType==1 && cover.hitId==s.driving.entity)))
				{game::Scr_ClearOutParams();++suppressed;return true;}
				args[1]=scripting::vector(gun.position.data());
				if(s.driving.type==kind::snowmobile)args[2]=scripting::vector(prepared_aim.target.point.data());
				bool fired{};
				diagnostics::record_trace(diagnostics::trace_event::vehicle_shot,1,s.owner.weapon);
				{
					scripting::stack_isolation isolated;for(auto it=args.rbegin();it!=args.rend();++it)scripting::push_value(*it);
					game::scr_VmPub->outparamcount=game::scr_VmPub->inparamcount;game::scr_VmPub->inparamcount=0;
					fired=scripting::safe_execution::call(reinterpret_cast<scripting::script_function>(original),{0xffff,0xffff});
				}
				if(!fired){game::Scr_ClearOutParams();++suppressed;return true;}
				const auto* sustain=game::Dvar_FindVar("player_sustainAmmo");if(!sustain || !sustain->current.enabled)(void)write_rounds(s.driving,loaded,loaded-1);
				game::Scr_ClearOutParams();consumed_press=s.press.serial;shot_accepted=true;++shots;feedback(s,weapons::mechanics::effect::shot);diagnostics::record_trace(diagnostics::trace_event::vehicle_shot,2,shots.load());return true;
			}
			catch(...){game::Scr_ClearOutParams();++suppressed;diagnostics::record_trace(diagnostics::trace_event::vehicle_shot,3);return true;}
		}
		bool effects(game::BuiltinFunction)
		{
			if(!in_shot() || game::scr_VmPub->outparamcount!=3)return false;
			diagnostics::record_trace(diagnostics::trace_event::vehicle_fx,0);
			try
			{
				std::array<scripting::script_value,3> args;for(unsigned i=0;i<3;++i)args[i]=scripting::script_value(game::scr_VmPub->top[-int(i)]);
				if(!args[2].is<std::string>())return false;const auto tag=args[2].as<std::string>();if(tag!="tag_flash" && tag!="tag_brass")return false;
				const auto s=latest();if(shot_accepted && fresh(s))
				{
					if(!args[1].is<scripting::entity>() || args[1].as<scripting::entity>().get_entity_reference().entnum!=s.driving.entity)return false;
					queue_effect(s,tag=="tag_brass");
				}
				game::Scr_ClearOutParams();diagnostics::record_trace(diagnostics::trace_event::vehicle_fx,1);return true;
			}
			catch(...){game::Scr_ClearOutParams();return true;}
		}
		void unbind()
		{
			prepared_aim={};fallback_target={};consumed_press=0;
			ready_mask=0;for(auto& b:bindings){for(unsigned i=0;i<b.count;++i)notifies::clear_hook(b.hooks[i]);b={};}
		}
		void bind()
		{
			unbind();
			using op=xsk::gsc::opcode;
			for(unsigned index=0;index<2;++index)
			{
				const auto k=index==0?kind::zodiac:kind::snowmobile;const auto shoot=named(k,"is_shoot_button_pressed"),loop=function(k,0xB210),shot=function(k,0xC97D),aim=function(k,0xA8AF);
				if(!shoot || !loop || !shot || (k==kind::zodiac && !aim))continue;
				if(shoot.end-shoot.begin>256 || loop.end-loop.begin>8192 || shot.end-shot.begin>4096 || unsigned(static_cast<unsigned char>(*shoot.begin))!=opcode(op::OP_checkclearparams)){++binding_failures;continue;}
				const auto target_body=k==kind::zodiac?target_body_offset({reinterpret_cast<const std::uint8_t*>(aim.begin),size_t(aim.end-aim.begin)}):0;
				if(k==kind::zodiac && !target_body){++binding_failures;continue;}
				const auto sites=inspect_script({reinterpret_cast<const std::uint8_t*>(loop.begin),size_t(loop.end-loop.begin)},static_cast<std::uint16_t>(ammo_field(k)));
				const scripting::script_value pullout{"pullout_anim"},putaway{"putaway_anim"};
				const auto animation=inspect_animation_waits({reinterpret_cast<const std::uint8_t*>(loop.begin),size_t(loop.end-loop.begin)},
					pullout.get_raw().u.stringValue,putaway.get_raw().u.stringValue,k==kind::snowmobile);
				std::array<const char*,2> tails{};unsigned tail_count{};
				for(size_t offset=0;offset+4<size_t(shot.end-shot.begin);++offset)if(const char* p=shot.begin+offset;static_cast<unsigned char>(p[0])==opcode(op::OP_CallBuiltin4) && static_cast<unsigned char>(p[1])==0xc8 && p[2]==1 && static_cast<unsigned char>(p[3])==opcode(op::OP_DecTop))
				{if(tail_count<tails.size())tails[tail_count]=p+4;++tail_count;}
				if(!sites.valid || !animation.valid || tail_count!=(k==kind::zodiac?2u:1u) || static_cast<unsigned char>(shot.end[-1])!=opcode(op::OP_End)){++binding_failures;continue;}
				if(!fallback_target.is<scripting::entity>())
				{
					// Animation iterations after stow still need a valid origin struct.
					const auto fallback=scripting::call<scripting::entity>("spawnstruct");
					const vec origin{};fallback.set("origin",scripting::vector(origin.data()));fallback_target=fallback;
				}
				auto& binding=bindings[index];binding.type=k;binding.shot=shot;
				const auto hook=[&](const char* from,const char* to){notifies::set_gsc_hook(from,to,player_scope);binding.hooks[binding.count++]=from;};
				// Preserve both native argument prologues. OP_clearparams at entry
				// would run past PRECODEPOS and release the caller's live references.
				hook(shoot.begin+1,shoot_code.data());if(k==kind::zodiac)hook(aim.begin+target_body,target_code.data());
				hook(loop.begin+sites.reload,loop.begin+sites.idle);hook(loop.begin+sites.refill,loop.begin+sites.refill_end);hook(loop.begin+sites.consume,loop.begin+sites.consume_end);
				for(size_t i=0;i<animation.count;++i)hook(loop.begin+animation.sites[i].begin,loop.begin+animation.sites[i].end);
				for(unsigned i=0;i<tail_count;++i){notifies::set_gsc_hook(tails[i],shot.end-1,skip_shot_tail);binding.hooks[binding.count++]=tails[i];}
				ready_mask.fetch_or(1u<<unsigned(k));
			}
			console::info("[VR vehicle] script bindings=%u rejected=%llu\n",ready_mask.load(),binding_failures.load());
		}
	}
	std::string status(){return std::format("aim_queries={} aim_errors={} accepted_shots={} suppressed_shots={}\n",aim_queries.load(),aim_errors.load(),shots.load(),suppressed.load());}
	bool ready(kind k) noexcept {return k!=kind::none && (ready_mask.load()&(1u<<unsigned(k)));}
	int rounds(context c) noexcept
	{
		try{if(!matches(c))return -1;const scripting::entity v{game::scr_entref_t{static_cast<unsigned short>(c.entity),0}};
			const auto value=scripting::get_object_variable(v.get_entity_id(),ammo_field(c.type));return value.is<int>() && value.as<int>()>=0 && value.as<int>()<=32?value.as<int>():-1;}catch(...){return -1;}
	}
	bool set_rounds(context c,int expected,int desired) noexcept
	{try{return scheduler::is_executing(scheduler::pipeline::server) && write_rounds(c,expected,desired);}catch(...){return false;}}
	void update_aim(const snapshot& s) noexcept
	{
		if(!scheduler::is_executing(scheduler::pipeline::server) || *game::g_script_error_level!=-1)return;
		try
		{
			if(!fresh(s)){prepared_aim={};return;}
			const auto now=controller_input::clock::now();if(aim_ready(s) && now-prepared_aim.at<50ms)return;
			aim_snapshot next;next.source=s;next.at=now;
			diagnostics::record_trace(diagnostics::trace_event::vehicle_aim_prepare,0,s.owner.weapon);
			next.target=select(s,muzzle(s));++aim_queries;
			const auto result=scripting::call<scripting::entity>("spawnstruct");
			result.set("origin",scripting::vector(next.target.point.data()));
			if(next.target.entity.is<scripting::entity>())result.set("obj",next.target.entity);
			if(next.target.rider)scripting::set_object_variable(result.get_entity_id(),0xAC56u,1);
			next.object=result;next.valid=true;prepared_aim=std::move(next);
			diagnostics::record_trace(diagnostics::trace_event::vehicle_aim_prepare,1,aim_queries.load());
		}
		catch(const std::exception& e)
		{
			prepared_aim={};if(aim_errors.fetch_add(1)==0)console::error("[VR vehicle] native target preparation failed: %s\n",e.what());
			diagnostics::record_trace(diagnostics::trace_event::vehicle_aim_prepare,2,aim_errors.load());
		}
	}
	void feedback(const snapshot& s,weapons::mechanics::effect effect,bool quick) noexcept
	{
		weapons::feedback::event event;event.kind=effect;event.owner=s.owner;event.reference=s.reference;
		event.at=controller_input::clock::now();event.position=s.gun.position;event.vehicle=true;event.quick_reload_start=quick;
		if(effect!=weapons::mechanics::effect::shot)event.definition=profile(s.driving.type).reload;
		weapons::feedback::publish(event);
	}

	class component final:public component_interface
	{
		void post_unpack()override
		{
			gsc::add_function("vr_vehicle_shoot_button",shoot_button);gsc::add_function("vr_vehicle_target",target_value);
			shoot_code=builtin_result_code(gsc::gsc_ctx->func_id("vr_vehicle_shoot_button"));
			target_code=builtin_result_code(gsc::gsc_ctx->func_id("vr_vehicle_target"));
			gsc::intercept_builtin("magicbullet",fire);gsc::intercept_builtin("playfxontag",effects);
			scripting::on_level_start(bind);scripting::on_shutdown([](bool,bool after){if(!after)unbind();});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::vehicles::native::component)
