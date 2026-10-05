#pragma once
#include "component/vr/gameplay/weapons/aug/profile.hpp"
#include "component/vr/gameplay/weapons/de50/profile.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/famas/profile.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/g18/profile.hpp"
#include "component/vr/gameplay/weapons/m1014/profile.hpp"
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/m1911/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/m82/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/m93r/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/weapons/usp/profile.hpp"
#include "component/vr/gameplay/weapons/vector/profile.hpp"
#include "component/vr/gameplay/weapon_sound_cues.hpp"
#include "component/vr/gameplay/weapon_sound_slice.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/weapon_sound_dispatch.hpp"

namespace weapon_sound_slice_tests
{
	template<class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;
		// Mirror the native worker: it keeps calling fill after releasing the
		// decoder, until the already-produced ring is drained by the mixer.
		// The old policy returned true again here and released a null decoder.
		for(unsigned channels:{1u,2u})for(unsigned length:{0u,1u,2640u,2832u,9696u,24000u})
		{
			const unsigned capacity=1920/channels;std::vector<unsigned> pcm(capacity),heard;
			w::sound_slice_budget slice{true,length};unsigned write=capacity-13,count=0,read=write,generated=0,releases=0,null_calls=0;
			bool decoder_live=true;
			const auto decode=[&]{
				if(!decoder_live){++null_calls;return false;}
				const auto produced=std::min(137u,capacity-count);
				for(unsigned i=0;i<produced;++i){pcm[write]=generated++;write=(write+1)%capacity;++count;}
				return false;
			};
			for(unsigned tick=0;tick<1000 && (decoder_live || count);++tick)
			{
				if(slice.fill(decoder_live,false,capacity,write,count,decode))
				{
					check(decoder_live,"a slice completion must never request native release of a null decoder");
					decoder_live=false;++releases;
				}
				for(unsigned i=0,n=std::min(73u,count);i<n;++i){heard.push_back(pcm[read]);read=(read+1)%capacity;--count;}
			}
			for(int n=0;n<3;++n)check(!slice.fill(false,false,capacity,write,count,decode),"retired decoder callbacks retain native drain semantics without repeating completion");
			bool ordered=heard.size()==length;for(size_t n=0;n<heard.size();++n)ordered=ordered && heard[n]==n;
			check(releases==1 && !slice.enabled && !decoder_live && count==0 && null_calls>=3 && ordered,
				"slice ends once and all retained PCM drains in order, including FAL's 9696-frame boundary");
		}
		{
			w::sound_slice_budget slice{true,1000};unsigned write=0,count=0;
			check(slice.fill(true,false,960,write,count,[&]{write=count=100;return true;}) && !slice.enabled && count==100,
				"natural decoder end before the cue also retires the slice budget without discarding its final PCM");
			check(!slice.fill(false,false,960,write,count,[]{return false;}),"natural EOF followed by a null drain callback does not release twice");
			slice={true,1};write=count=0;
			check(!slice.fill(true,true,960,write,count,[&]{write=count=100;return false;}) && !slice.enabled && count==100,
				"streamed voice bypasses slicing and cannot inherit a loaded-voice budget");
			slice={true,1};write=count=0;
			check(slice.fill(true,false,960,write,count,[&]{write=count=100;return false;}) && count==1 && !slice.enabled,
				"reused voice can initialize and finish a fresh slice after earlier retirement");
		}
		// Simulate independent producer/mixer blocks, including wraparound,
		// retained PCM, both channel layouts and a boundary inside a block.
		for(unsigned channels:{1u,2u})for(unsigned length:{1u,48u,6592u,24000u})
		{
			const unsigned capacity=1920/channels;
			std::vector<unsigned> pcm(capacity),heard;
			w::sound_slice_budget slice{true,length};
			unsigned write=capacity-13,count{},read=write,generated{};bool ended{};
			while(!ended || count)
			{
				if(!ended)
				{
					const auto before=count;
					const auto produced=std::min(137u,capacity-count);
					for(unsigned i=0;i<produced;++i){pcm[write]=generated++;write=(write+1)%capacity;++count;}
					ended=slice.trim(before,capacity,write,count);
				}
				const auto consume=std::min(73u,count);
				for(unsigned i=0;i<consume;++i){heard.push_back(pcm[read]);read=(read+1)%capacity;--count;}
			}
			check(heard.size()==length,"slice drains precisely the requested source frames");
			for(unsigned i=0;i<heard.size();++i)if(heard[i]!=i){check(false,"slice preserves PCM order through ring wrap");break;}
		}
		for(const auto& cue:w::weapon_sound_cues)
		{
			check(cue.split_ms>0 && std::uint64_t(cue.split_ms)*cue.rate/1000<cue.frames,"cue lies inside recording");
			check(w::find_weapon_sound_cue(cue.file,cue.rate,cue.frames)==&cue,"cue resolves the exact recording");
			check(!w::find_weapon_sound_cue(cue.file,cue.rate,cue.frames+1),"different recording length cannot borrow a cue");
		}
		check(w::find_weapon_sound_cue("h1_foley\\wpfoly_m4_reload_chamber.wav",48000,25474)!=nullptr,
			"native slash and extension forms resolve the same recording");
		using enum w::mechanics::effect;
		for(const auto* p:{&w::m4::physical,&w::m16::physical,&w::scar::physical,&w::fal::physical})
		{
			const auto rear=p->interaction_sound(action_rear),close=p->interaction_sound(action_close);
			check(rear.name && close.name && std::string_view(rear.name)==close.name &&
				rear.part==w::sound_part::first && close.part==w::sound_part::second,
				"independent rear and close select different ranges of one native sound");
		}
		for(const auto* p:{&w::m9::physical,&w::m93r::physical,&w::m82::physical,&w::famas::physical,&w::fn2000::physical,&w::vector::physical,&w::aug::physical})
		{
			check(p->interaction_sound(magazine_out).part==w::sound_part::first &&
				p->interaction_sound(magazine_take).part==w::sound_part::first,"drop and catch both trim composite removal audio");
			check(p->interaction_sound(magazine_in).part==w::sound_part::whole,"insertion retains its own native recording");
		}
		for(const auto* p:{&w::usp::physical,&w::g18::physical,&w::ump::physical,&w::de50::physical})
			check(p->interaction_sound(action_rear).part==w::sound_part::first && p->interaction_sound(action_close).part==w::sound_part::whole,
				"already-correct standalone close is preserved");
		const auto de50_close=w::de50::physical.interaction_sound(action_close);
		check(de50_close.name && std::string_view(de50_close.name)=="weap_de50_chamber_plr" &&
			de50_close.kind==w::sound_reference_kind::notetrack && !de50_close.notetrack_weapon && !de50_close.window &&
			w::de50::physical.matches_native("deserteagle",7) && w::de50::physical.matches_native("deserteagle_gold",7),
			"standard and gold Desert Eagle retain their native whole slide-release cue");
		for(const auto* p:{&w::usp::physical,&w::usp::silenced_physical})
		{
			const auto insertion=p->interaction_sound(magazine_in);
			for(auto event:{magazine_out,magazine_take})
			{
				const auto removal=p->interaction_sound(event);
				check(removal.name && insertion.name && std::string_view(removal.name)==insertion.name &&
					removal.part==w::sound_part::first && insertion.part==w::sound_part::second &&
					!removal.notetrack_weapon && !insertion.notetrack_weapon,
					"both USP variants split drop/catch and insertion from their own composite recording");
			}
		}
		const auto m93r_rear=w::m93r::physical.interaction_sound(action_rear);
		check(m93r_rear.name && m93r_rear.notetrack_weapon &&
			std::string_view(m93r_rear.name)==w::m9::physical.interaction_sound(action_rear).name &&
			std::string_view(m93r_rear.notetrack_weapon)==w::m9::native_name &&
			m93r_rear.kind==w::sound_reference_kind::notetrack && m93r_rear.part==w::sound_part::whole && !m93r_rear.window,
			"M93R explicitly reuses M9 rear notetrack resolution");
		for(auto event:{magazine_out,magazine_in,action_close})
			check(!w::m93r::physical.interaction_sound(event).notetrack_weapon,"M93R existing reload bindings keep their own map");
		bool emitted{};
		const auto emit=[&](auto token,auto name,auto capacity,auto sound,const auto&){
			emitted=true;
			check(token==42 && name==w::m93r::native_name && capacity==20 && sound.notetrack_weapon==m93r_rear.notetrack_weapon,
				"shared sound keeps M93R identity through dispatch");return true;};
		check(w::dispatch_profile_sound(w::m93r::physical,42,42,w::m93r::native_name,20,true,m93r_rear,{},emit) && emitted,
			"shared source reaches playback after normal admission");
		emitted=false;
		check(!w::dispatch_profile_sound(w::m93r::physical,42,43,w::m93r::native_name,20,true,m93r_rear,{},emit) && !emitted,
			"shared sound cannot bypass weapon token mismatch");
		check(!w::dispatch_profile_sound(w::m93r::physical,42,42,w::m9::native_name,15,true,m93r_rear,{},emit) && !emitted,
			"donor identity cannot substitute for M93R admission");
		const auto colt_rear=w::m1911::physical.interaction_sound(action_rear),colt_close=w::m1911::physical.interaction_sound(action_close);
		check(colt_rear.name && colt_close.name && std::string_view(colt_rear.name)==colt_close.name &&
			colt_rear.part==w::sound_part::whole && colt_close.part==w::sound_part::whole,
			"M1911 reuses whole close sound for the rear stop");
		const auto benelli_close=w::m1014::sound(w::tube::effect::rack_close);
		for(auto event:{w::tube::effect::rack_open,w::tube::effect::load_port})
		{
			const auto sound=w::m1014::sound(event);
			check(sound.name && benelli_close.name && std::string_view(sound.name)==benelli_close.name &&
				sound.part==w::sound_part::whole && benelli_close.part==w::sound_part::whole,
				"M1014 reuses whole close sound for opening and preserves port loading");
		}
	}
}
