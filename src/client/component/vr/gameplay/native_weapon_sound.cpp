#include <std_include.hpp>
#include "vehicles/runtime.hpp"
#include "native_weapon_sound.hpp"
#include "native_weapon_sound_slice.hpp"
#include "component/console.hpp"
#include "tube_profile.hpp"
#include "cylinder_profile.hpp"
#include "launcher_profile.hpp"
#include "weapon_sound_dispatch.hpp"
#include "native_ammunition.hpp"
#include "quick_reload.hpp"
#include "sound_variant_selection.hpp"
#include <utils/native_memory.hpp>
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::native_weapon_sound
{
	namespace
	{
		bool verified{};
		constexpr std::uintptr_t play_address = 0x140380430;
		struct sound_map { std::array<unsigned, 36> keys{}, values{}; short entity{}; };
		template<class Profile> bool play_observed(std::uint32_t weapon,const Profile& profile,
			const native_ammunition::reload_snapshot& observed,sound_reference sound,const std::array<float,3>& origin)
		{
			if(!observed.valid)return false;
			return dispatch_profile_sound(profile,weapon,observed.ammo.weapon,observed.native_name.data(),observed.base_capacity,observed.valid,sound,origin,
				[](auto token,auto name,auto capacity,auto reference,const auto& point){return play(token,name,capacity,reference,point);});
		}
		template<size_t N> bool verify(std::uintptr_t at, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{}; mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at), {bytes, mask.data(), N}));
		}
		// Bounded leaf copies; no native calls under SEH and no retained asset pointers.
		bool copy_map(std::uint32_t weapon, sound_map& out) noexcept
		{
			__try
			{
				const auto definition = reinterpret_cast<const std::byte* const*>(0x14CE01580)[weapon];
				if (!definition) return false;
				const unsigned *keys{}, *values{};
				std::memcpy(&keys, definition + 0xa0, sizeof(keys));
				std::memcpy(&values, definition + 0xa8, sizeof(values));
				if (!keys || !values) return false;
				std::memcpy(out.keys.data(), keys, sizeof(out.keys));
				std::memcpy(out.values.data(), values, sizeof(out.values));
				out.entity = *reinterpret_cast<const signed char*>(0x141BB3C30);
				return out.entity >= 0;
			}
			__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
				GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
			{ return false; }
		}
		bool copy_name(const char* text, char* output, size_t size) noexcept
		{
			__try
			{
				if (!text) return false;
				for (size_t i = 0; i < size; ++i) { output[i] = text[i]; if (!text[i]) return i != 0; }
				return false;
			}
			__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
				GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
			{ return false; }
		}
		bool copy_weapon_name(std::uint32_t weapon,char* output,size_t size) noexcept
		{
			__try
			{
				const auto* definition=game::weapon_defs[weapon];
				return definition && copy_name(definition->szInternalName,output,size);
			}
			__except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION || GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR ?
				EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
		}
		bool copy_reload_sound_map(std::uint32_t weapon,sound_reference sound,sound_map& out) noexcept
		{
			if(!sound.notetrack_weapon)return copy_map(weapon,out);
			if(sound.kind!=sound_reference_kind::notetrack)return false;
			// Explicit shared source only, never a fallback after a missing key.
			// Scan the bounded loaded table on the owner thread. Do not load assets,
			// keep native pointers, or change the admitted weapon/ammunition owner.
			for(std::uint32_t source=1;source<512;++source)
			{
				std::array<char,128> name{};
				if(copy_weapon_name(source,name.data(),name.size()) && !std::strcmp(name.data(),sound.notetrack_weapon))
					return copy_map(source,out);
			}
			console::warn("[VR audio] Shared notetrack source unavailable: weapon=%s key=%s\n",sound.notetrack_weapon,sound.name);
			return false;
		}
		struct quick_alias_files
		{
			std::array<char,128> alias{};
			std::array<std::array<char,256>,quick_reload::start_sound_files.size()> files{};
			unsigned count{};
		};
		bool copy_quick_alias_files(const game::snd_alias_list_t* source,quick_alias_files& out) noexcept
		{
			__try
			{
				if(!source || !source->head || !source->count || source->count>out.files.size() ||
					!copy_name(source->aliasName,out.alias.data(),out.alias.size()))return false;
				out.count=source->count;
				for(unsigned i=0;i<out.count;++i)
				{
					const auto& alias=source->head[i];const auto* file=alias.soundFile;
					if(!file || !file->exists)return false;
					std::array<char,128> secondary{},chain{};
					if(alias.secondaryAliasName && *alias.secondaryAliasName && !copy_name(alias.secondaryAliasName,secondary.data(),secondary.size()))return false;
					if(alias.chainAliasName && *alias.chainAliasName && !copy_name(alias.chainAliasName,chain.data(),chain.size()))return false;
					if(!quick_reload::start_sound_companions(secondary.data(),chain.data()))return false;
					const char* name{};
					if(file->type==game::SAT_LOADED && file->u.loadSnd)name=file->u.loadSnd->name;
					else if(file->type==game::SAT_PRIMED && file->u.primedSnd.loadedPart)name=file->u.primedSnd.loadedPart->name;
					else if(file->type==game::SAT_STREAMED && !file->u.streamSnd.filename.fileIndex)name=file->u.streamSnd.filename.info.raw.name;
					if(!copy_name(name,out.files[i].data(),out.files[i].size()))return false;
				}
				return true;
			}
			__except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION || GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR ?
				EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
		}
	}
	bool initialize()
	{
		// Proven chain: weapon notetrack 3C22D0 -> key/value script IDs ->
		// 380430(entity, origin, alias) -> 3CA0D0 alias selection -> positional SND.
		// Do not use the reconstructed WeaponDef struct's unverified map offsets.
		constexpr std::uint8_t entry[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x40,
			0x0f,0xbf,0xd9,0x48,0x8b,0xf2,0x8b,0xd3,0x49,0x8b,0xc8,0xe8,0x81,0x9c,0x04,0x00};
		constexpr std::uint8_t table[]{0x4c,0x8d,0x05,0x76,0xf2,0xa3,0x0c};
		constexpr std::uint8_t keys[]{0x48,0x8b,0x88,0xa0,0x00,0x00,0x00};
		constexpr std::uint8_t values[]{0x48,0x8b,0x80,0xa8,0x00,0x00,0x00};
		constexpr std::uint8_t positional[]{0xe8,0x03,0x6c,0x44,0x00};
		constexpr std::uint8_t client_call[]{0xe8,0x35,0xe0,0xfb,0xff};
		verified = verify(play_address, entry) && verify(0x1403C2303, table) && verify(0x1403C235A, keys) &&
			verify(0x1403C2366, values) && verify(0x140380488, positional) && verify(0x1403C23F6, client_call);
		if(verified && !native_weapon_sound_slice::initialize())
			console::warn("[VR audio] Native slice contract rejected; segmented interaction sounds are disabled.\n");
		return verified;
	}
	bool play(std::uint32_t weapon, const reload_profile& definition, sound_reference sound, const std::array<float, 3>& origin)
	{
		if (!scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized()) return false;
		const auto observed=native_ammunition::observe_reload(game::g_entities[0].client);
		return play_observed(weapon,definition,observed,sound,origin);
	}
	bool play(std::uint32_t weapon,const launcher_profile& definition,sound_reference sound,const std::array<float,3>& origin)
	{
		if(!verified || !sound.name || !weapon || weapon>=512 || !scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized())return false;
		const auto owned=native_ammunition::observe_carried(game::g_entities[0].client,weapon);
		const auto* native=game::weapon_defs[weapon];std::array<char,128> name{};
		if(!owned.valid || !native || native->clipSize!=1 || !copy_name(native->szInternalName,name.data(),name.size()) || !definition.matches(name.data()))return false;
		for(float x:origin)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
		sound_map map;if(!copy_map(weapon,map) || sound.kind!=sound_reference_kind::notetrack)return false;
		for(size_t i=0;i<map.keys.size();++i)
		{
			if(!map.keys[i] || !map.values[i] || !copy_name(game::SL_ConvertToString(map.keys[i]),name.data(),name.size()) || std::strcmp(name.data(),sound.name))continue;
			if(!copy_name(game::SL_ConvertToString(map.values[i]),name.data(),name.size()))return false;
			return native_weapon_sound_slice::play(map.entity,origin.data(),name.data(),sound.part,sound.window);
		}
		return false;
	}
	bool play(std::uint32_t weapon,const tube_profile& definition,sound_reference sound,const std::array<float,3>& origin)
	{
		const auto observed=native_ammunition::observe_owned(game::g_entities[0].client,weapon);
		return play_observed(weapon,definition,observed,sound,origin);
	}
	bool play(std::uint32_t weapon,const cylinder_profile& definition,sound_reference sound,const std::array<float,3>& origin)
	{
		if(!scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized())return false;
		const auto observed=native_ammunition::observe_reload(game::g_entities[0].client);
		return play_observed(weapon,definition,observed,sound,origin);
	}
	namespace
	{
		bool module_of(std::uint32_t host,std::uint32_t module)noexcept
		{
			if(!host || host>=512 || !module || module>=512 || host==module || !native_ammunition::observe_carried(game::g_entities[0].client,host).valid)return false;
			int alt{};return game::weapon_defs[host] && utils::native_memory::read_bytes(&alt,
				reinterpret_cast<const std::byte*>(game::weapon_defs[host])+offsetof(game::WeaponDef,altWeapon),sizeof(alt)) && alt==int(module);
		}
		bool shot(std::uint32_t weapon,const std::array<float,3>& origin)
	{
		if (!verified || !scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized() ||
			!weapon || weapon>=512) return false;
		for (float v:origin) if (!std::isfinite(v) || std::abs(v)>1e7f) return false;
		// H2 WeaponDef fireSoundPlayer (+424), verified against current loaded
		// L86/MG4/1911/USP aliases and the upstream H2 sound-list layout. The
		// asset owns the alias; copy its bounded name only for this main callback.
		const auto* def=reinterpret_cast<const std::byte*>(game::weapon_defs[weapon]);
		const void* list{};const char* name{};std::array<char,128> alias{};
		if (!def || !utils::native_memory::read_bytes(&list,def+424,sizeof(list)) || !list ||
			!utils::native_memory::read_bytes(&name,list,sizeof(name)) || !copy_name(name,alias.data(),alias.size())) return false;
		const short entity=*reinterpret_cast<const signed char*>(0x141BB3C30);if (entity<0) return false;
		return utils::hook::invoke<int>(play_address,entity,origin.data(),alias.data())!=-1;
	}
	}
	bool play_shot(std::uint32_t weapon,const std::array<float,3>& origin)
	{return scheduler::is_executing(scheduler::pipeline::main) && game::CL_IsCgameInitialized() && native_ammunition::observe_carried(game::g_entities[0].client,weapon).valid && shot(weapon,origin);}
	bool play_vehicle(std::uint32_t weapon,sound_reference sound,const std::array<float,3>& origin)
	{
		const auto state=vehicles::latest();
		if(!verified || !sound.name || !scheduler::is_executing(scheduler::pipeline::main) || !vehicles::presentation_allowed() ||
			!state.owner.can_fire() || state.owner.weapon!=weapon || !weapon || weapon>=512)return false;
		for(float value:origin)if(!std::isfinite(value) || std::abs(value)>1e7f)return false;
		sound_map map;if(!copy_map(weapon,map))return false;
		if(sound.kind==sound_reference_kind::alias)return utils::hook::invoke<int>(play_address,map.entity,origin.data(),sound.name)!=-1;
		for(size_t i=0;i<map.keys.size();++i)
		{
			if(!map.keys[i] || !map.values[i])continue;
			std::array<char,128> key{},alias{};
			if(copy_name(game::SL_ConvertToString(map.keys[i]),key.data(),key.size()) && key.data()==std::string_view(sound.name) &&
				copy_name(game::SL_ConvertToString(map.values[i]),alias.data(),alias.size()))return utils::hook::invoke<int>(play_address,map.entity,origin.data(),alias.data())!=-1;
		}
		return false;
	}
	bool play_offhand(std::uint32_t weapon,bool release,const std::array<float,3>& origin)
	{
		if(!verified || !scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized() || !weapon || weapon>=512)return false;
		const auto* definition=game::weapon_defs[weapon];
		if(!definition || definition->inventoryType!=game::WEAPINVENTORY_OFFHAND || int(definition->weapType)!=2)return false;
		for(float x:origin)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
		sound_map map;if(!copy_map(weapon,map))return false;
		std::array<char,128> alias{};bool found{};
		const std::string_view suffix=release?"_fire_plr":"_pin_plr";
		for(unsigned i=0;i<map.keys.size();++i)
		{
			if(!map.keys[i] || !map.values[i])continue;
			std::array<char,128> key{};
			if(!copy_name(game::SL_ConvertToString(map.keys[i]),key.data(),key.size()))return false;
			if(!std::string_view(key.data()).ends_with(suffix))continue;
			if(found || !copy_name(game::SL_ConvertToString(map.values[i]),alias.data(),alias.size()))return false;
			found=true;
		}
		return found && utils::hook::invoke<int>(play_address,map.entity,origin.data(),alias.data())!=-1;
	}
	bool play_module_shot(std::uint32_t host,std::uint32_t module,const std::array<float,3>& origin)
	{return scheduler::is_executing(scheduler::pipeline::main) && game::CL_IsCgameInitialized() && module_of(host,module) && shot(module,origin);}
	bool play_module(std::uint32_t host,std::uint32_t module,sound_reference sound,const std::array<float,3>& origin)
	{
		if(!verified || !sound.name || !scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized() || !module_of(host,module))return false;
		for(float f:origin)if(!std::isfinite(f)||std::abs(f)>1e7f)return false;
		sound_map map;if(!copy_map(module,map))return false;
		if(sound.kind==sound_reference_kind::alias)
		{
			std::array<char,128> alias{};
			if(!copy_name(sound.name,alias.data(),alias.size()))return false;
			return utils::hook::invoke<int>(play_address,map.entity,origin.data(),alias.data())!=-1;
		}
		if(sound.kind!=sound_reference_kind::notetrack)return false;
		for(size_t i=0;i<map.keys.size();++i)
		{
			if(!map.keys[i] || !map.values[i])continue;
			std::array<char,128> text{};
			if(!copy_name(game::SL_ConvertToString(map.keys[i]),text.data(),text.size()) || std::strcmp(text.data(),sound.name))continue;
			if(!copy_name(game::SL_ConvertToString(map.values[i]),text.data(),text.size()))return false;
			return utils::hook::invoke<int>(play_address,map.entity,origin.data(),text.data())!=-1;
		}
		return false;
	}
	bool play_prop(std::uint32_t weapon,std::string_view native_name,const char* notetrack,const std::array<float,3>& origin)
	{
		if(!verified || !notetrack || !weapon || weapon>=512 || !scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized())return false;
		for(float x:origin)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
		std::array<char,128> name{};sound_map map;
		if(!copy_weapon_name(weapon,name.data(),name.size()) || name.data()!=native_name || !copy_map(weapon,map))return false;
		for(unsigned i=0;i<map.keys.size();++i)
		{
			std::array<char,128> key{},alias{};
			if(map.keys[i] && map.values[i] && copy_name(game::SL_ConvertToString(map.keys[i]),key.data(),key.size()) &&
				std::string_view(key.data())==notetrack && copy_name(game::SL_ConvertToString(map.values[i]),alias.data(),alias.size()))
				return native_weapon_sound_slice::play(map.entity,origin.data(),alias.data(),sound_part::whole);
		}
		return false;
	}
	bool play_melee_impact(const std::array<float,3>& origin)
	{
		if (!verified || !scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized()) return false;
		for (float value:origin) if (!std::isfinite(value) || std::abs(value)>1e7f) return false;
		const short entity=*reinterpret_cast<const signed char*>(0x141BB3C30);if (entity<0) return false;
		// Loaded alias witness: melee_hit selects h1_weapons/melee/melee_hit01/02.
		// H2 firearm meleeHitSound aliases instead select melee_swing01/03 and
		// cannot supply the NPC-style blunt impact requested here. Resolve the
		// shared stock alias on this owner thread; retain no sound asset pointers.
		return utils::hook::invoke<int>(play_address,entity,origin.data(),"melee_hit")!=-1;
	}
	bool play_quick_reload_start(std::uint32_t weapon,const std::array<float,3>& origin)
	{
		return verified && scheduler::is_executing(scheduler::pipeline::main) && game::CL_IsCgameInitialized() && weapon && weapon<512 &&
			native_ammunition::observe_carried(game::g_entities[0].client,weapon).valid && play_quick_reload_start_at(origin);
	}
	bool play_quick_reload_start_at(const std::array<float,3>& origin)
	{
		if(!verified || !scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized())return false;
		for(float x:origin)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
		const short entity=*reinterpret_cast<const signed char*>(0x141BB3C30);if(entity<0)return false;
		const auto asset=game::DB_FindXAssetHeader(game::ASSET_TYPE_SOUND,quick_reload::start_sound_alias,0);
		quick_alias_files copied;
		if(!copy_quick_alias_files(asset.sound,copied) || copied.count!=quick_reload::start_sound_files.size() ||
			std::strcmp(copied.alias.data(),quick_reload::start_sound_alias))return false;
		const std::array<std::string_view,2> files{copied.files[0].data(),copied.files[1].data()};
		if(sound_variant_mask(quick_reload::start_sound_files,files)!=3)return false;
		// Native variant selection chooses one of the two verified recordings and
		// retains its stock foley layer. No full SOUND-pool scan or cached pointers.
		return utils::hook::invoke<int>(play_address,entity,origin.data(),copied.alias.data())!=-1;
	}
	bool play(std::uint32_t weapon, std::string_view native_name, int capacity, const char* key, const std::array<float,3>& origin)
	{
		return play(weapon,native_name,capacity,sound_reference{key},origin);
	}
	bool play(std::uint32_t weapon, std::string_view native_name, int capacity, sound_reference sound, const std::array<float,3>& origin)
	{
		if (!verified || !sound.name || !weapon || (weapon & ~0x1ffu) ||
			!scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized()) return false;
		for (float x : origin) if (!std::isfinite(x) || std::abs(x) > 1e7f) return false;
		const auto observed = native_ammunition::observe_reload(game::g_entities[0].client);
		if (!observed.valid || observed.ammo.weapon != weapon ||
			native_name!=observed.native_name.data() || capacity!=observed.base_capacity) return false;
		sound_map map;
		if (!copy_reload_sound_map(weapon, sound, map)) return false;
		if (sound.kind==sound_reference_kind::alias)
		{
			std::array<char,128> alias{};
			if (!copy_name(sound.name,alias.data(),alias.size())) return false;
			return native_weapon_sound_slice::play(map.entity,origin.data(),alias.data(),sound.part,sound.window);
		}
		if (sound.kind!=sound_reference_kind::notetrack) return false;
		for (size_t i = 0; i < map.keys.size(); ++i)
		{
			if (!map.keys[i] || !map.values[i]) continue;
			std::array<char, 128> name{};
			if (!copy_name(game::SL_ConvertToString(map.keys[i]), name.data(), name.size()) || std::strcmp(name.data(), sound.name)) continue;
			if (!copy_name(game::SL_ConvertToString(map.values[i]), name.data(), name.size())) return false;
			return native_weapon_sound_slice::play(map.entity,origin.data(),name.data(),sound.part,sound.window);
		}
		return false;
	}
}
