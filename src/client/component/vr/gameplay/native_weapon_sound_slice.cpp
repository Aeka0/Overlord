#include <std_include.hpp>
#include "native_weapon_sound_slice.hpp"
#include "weapon_sound_cues.hpp"
#include "weapon_sound_slice.hpp"
#include "weapon_sound_window.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::native_weapon_sound_slice
{
	namespace
	{
		bool ready{};
		utils::hook::detour queue_hook, create_hook;
		// SND has 96 loaded channels and a separate 208-voice decoder pool.
		std::array<std::atomic<unsigned>,96> pending_ms{};
		std::array<std::atomic<std::uint64_t>,208> budgets{};
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
		thread_local const game::snd_alias_t* requested_alias{};
		thread_local unsigned requested_ms{}, creating_ms{};
		template<class T> T read(const void* p, size_t offset)
		{ T value; std::memcpy(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));return value; }
		std::atomic<std::uint64_t>* budget(void* source)
		{
			const auto address=reinterpret_cast<std::uintptr_t>(source);
			constexpr std::uintptr_t first=0x14BC7E580+0x28;
			if(address<first || (address-first)%0x2d8)return nullptr;
			const auto index=(address-first)/0x2d8;
			return index<budgets.size() ? &budgets[index] : nullptr;
		}
		int queue(void* info,int channel,int spatial,int flags)
		{
			// Reset on EVERY loaded-channel assignment, including ordinary sounds
			// and failed/replaced requests. Never inherit an earlier VR cutoff.
			if(channel>=0 && channel<int(pending_ms.size()))
				pending_ms[channel].store(read<const game::snd_alias_t*>(info,0)==requested_alias ? requested_ms : 0);
			return queue_hook.invoke<int>(info,channel,spatial,flags);
		}
		int create(void* info,int channel,int offset_ms)
		{
			const auto previous=creating_ms;
			creating_ms=channel>=0 && channel<int(pending_ms.size()) ? pending_ms[channel].exchange(0) : 0;
			const auto result=create_hook.invoke<int>(info,channel,offset_ms);
			creating_ms=previous;
			return result;
		}
		void* decoder(void* source,const void* format)
		{
			// This call site belongs only to loaded voices, before state=2 publishes
			// the voice to the audio worker. Reuse clears the previous voice budget.
			if(auto* slice=budget(source))
				slice->store(creating_ms ? (std::uint64_t(1)<<32) | (std::uint64_t(creating_ms)*read<unsigned>(source,8)/1000) : 0);
			return utils::hook::invoke<void*>(0x1405D5430,source,format);
		}
		struct pcm_ring {void* samples;unsigned write,count,starved,unknown;};
		static_assert(offsetof(pcm_ring,count)==0xc);
		bool fill(void* decoder_state,void* source,pcm_ring* ring)
		{
			auto* stored=budget(source);
			const auto packed=stored ? stored->load() : 0;
			sound_slice_budget slice{(packed>>32)!=0,static_cast<unsigned>(packed)};
			// Streamed/primed sources have their own lifetime and seek path. They
			// never use the loaded-voice budget, including recycled voice slots.
			const auto channels=read<unsigned>(source,4);
			// Native ring capacity is 1920 interleaved int16 samples. The engine
			// frees and NULLS the decoder on true, then keeps calling fill while
			// retained PCM drains. Retire our budget with that single release edge.
			const bool ended=slice.fill(decoder_state!=nullptr,read<void*>(source,0x20)!=nullptr,
				channels ? 1920/channels : 0,ring->write,ring->count,
				[&]{return utils::hook::invoke<bool>(0x1405D57F0,decoder_state,source,ring);});
			if(stored)stored->store(slice.enabled ? (std::uint64_t(1)<<32)|slice.remaining : 0);
			return ended;
		}
		template<size_t N> bool verify(std::uintptr_t address,const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{};mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));
		}
		bool copy_loaded(const game::snd_alias_t* alias,std::array<char,256>& name,unsigned& rate,unsigned& frames) noexcept
		{
			__try
			{
				if(!alias || (alias->flags&0x21) || !alias->soundFile || alias->soundFile->type!=game::SAT_LOADED || !alias->soundFile->exists)return false;
				const auto* loaded=alias->soundFile->u.loadSnd;if(!loaded || !loaded->name)return false;
				rate=read<unsigned short>(loaded,0x28);frames=read<unsigned>(loaded,0x30);
				for(size_t i=0;i<name.size();++i){name[i]=loaded->name[i];if(!name[i])return rate && frames && i;}
				name.back()=0;
				return false;
			}
			__except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION || GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
		}
	}
	bool initialize()
	{
		if(ready)return true;
		constexpr std::uint8_t queue_entry[]{0x48,0x89,0x5c,0x24,0x08};
		constexpr std::uint8_t create_entry[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18};
		constexpr std::uint8_t decoder_call[]{0xe8,0xd2,0x74,0xff,0xff};
		constexpr std::uint8_t fill_call[]{0xe8,0x00,0x75,0xff,0xff};
		constexpr std::uint8_t seek[]{0x41,0x8b,0x41,0x44,0x85,0xc0};
		constexpr std::uint8_t ring_capacity[]{0xb8,0x80,0x07,0x00,0x00};
		constexpr std::uint8_t null_decoder[]{0x4d,0x85,0xf6,0x0f,0x84,0x33,0x02,0x00,0x00};
		constexpr std::uint8_t release_then_null[]{0x84,0xc0,0x74,0x0d,0x48,0x8b,0x4b,0x38,0xe8,0xa3,0x73,0xff,0xff,0x4c,0x89,0x73,0x38};
		if(!verify(0x1405D0730,queue_entry) || !verify(0x1405D4F40,create_entry) ||
			!verify(0x1405DDF59,decoder_call) || !verify(0x1405DE2EB,fill_call) || !verify(0x1405CF14B,seek) ||
			!verify(0x1405D586D,ring_capacity) || !verify(0x1405D583C,null_decoder) ||
			!verify(0x1405DE2F0,release_then_null))return false;
		queue_hook.create(0x1405D0730,queue);create_hook.create(0x1405D4F40,create);
		utils::hook::call(0x1405DDF59,decoder);utils::hook::call(0x1405DE2EB,fill);
		ready=true;return true;
	}
	bool play(short entity,const float* origin,const char* name,sound_part part,const sound_window* window)
	{
		if(part==sound_part::whole && !window)return utils::hook::invoke<int>(0x140380430,entity,origin,name)!=-1;
		if(!ready)return false;
		// Keep the stock alias selection/randomization, WeaponDef mapping, spatial
		// bus and bookkeeping. No shared alias or extracted sound is modified.
		const auto* alias=utils::hook::invoke<const game::snd_alias_t*>(0x1403CA0D0,name,int(entity));
		std::array<char,256> file{};unsigned rate{},frames{};
		const auto* cue=copy_loaded(alias,file,rate,frames) ? find_weapon_sound_cue(file.data(),rate,frames) : nullptr;
		const auto range=cue?resolve_sound_window(*cue,part,window):resolved_sound_window{};
		if(!range.valid)
		{
			console::warn("[VR audio] Slice rejected: alias=%s file=%s rate=%u frames=%u\n",name,file.data(),rate,frames);
			return false;
		}
		const auto start=range.begin_ms;
		const auto* previous_alias=requested_alias;const auto previous_ms=requested_ms;
		requested_alias=alias;requested_ms=range.duration_ms;
		const auto id=utils::hook::invoke<int>(0x1407C7090,alias,int(entity),origin,int(start),1,0,nullptr);
		requested_alias=previous_alias;requested_ms=previous_ms;
		utils::hook::invoke<void>(0x1407C1CC0,id,alias,1,0);
		return id!=-1;
	}
}
