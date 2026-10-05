#include <std_include.hpp>
#include "animation_presentation.hpp"
#include "native_animation_query.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::native_animation
{
	namespace
	{
		bool verified{},time_verified{};
		struct anim_info
		{
			std::byte prefix[12];
			std::uint16_t next, children, parent, index, leaf;
			std::byte pad[2];
			const game::XAnimParts* parts;
			std::byte state[8];
			float time;
			std::byte state_tail[24];
			float weight;
			std::byte tail[8];
		};
		static_assert(sizeof(anim_info) == 80);
		static_assert(offsetof(anim_info, parts) == 0x18);
		static_assert(offsetof(anim_info, weight) == 0x44);
		static_assert(offsetof(anim_info, time) == 0x28);
	} // namespace
	bool initialize()
	{
		constexpr std::uint8_t bytes[]{0x48, 0x63, 0xc1, 0x48, 0x8d, 0x0d, 0x06, 0xe6, 0x69, 0x0c, 0x48,
									   0x8d, 0x04, 0x80, 0x48, 0xc1, 0xe0, 0x04, 0x48, 0x03, 0xc1, 0xc3};
		std::array<std::uint8_t, sizeof(bytes)> mask{};
		mask.fill(0xff);
		verified = static_cast<bool>(utils::hook_validation::verify_masked_bytes(
			reinterpret_cast<void*>(0x14065A080), {bytes, mask.data(), sizeof(bytes)}));
		// Scr_GetAnimTime (method 0x8159) -> 0x14065E130: the returned
		// scalar is XAnimInfo[leaf].currentTime, pool base + 0x28, stride 80.
		constexpr std::uint8_t time_bytes[]{0x48,0x8d,0x05,0x59,0xa5,0x69,0x0c,0xf3,0x0f,0x10,0x04,0xc8};
		std::array<std::uint8_t,sizeof(time_bytes)> time_mask{};time_mask.fill(255);
		time_verified=static_cast<bool>(utils::hook_validation::verify_masked_bytes(
			reinterpret_cast<void*>(0x14065E158),{time_bytes,time_mask.data(),sizeof(time_bytes)}));
		return verified;
	}
	snapshot sample(const void* tree) noexcept
	{
		if (!verified || !tree)
			return {};
		std::uint16_t root{};
		// 0x140661E7B loads DObj.tree; 0x140661E97 loads tree.children at +8.
		std::memcpy(&root, static_cast<const std::byte*>(tree) + 8, sizeof(root));
		if (!root)
			return {};
		std::array<std::uint16_t, 256> pending{}, visited{};
		size_t queued = 1, seen = 0;
		pending[0] = root;
		snapshot out{};out.valid=true;out.time_valid=time_verified;
		while (queued)
		{
			const auto id = pending[--queued];
			if (!id)
				continue;
			if (seen == visited.size() ||
				std::find(visited.begin(), visited.begin() + seen, id) != visited.begin() + seen)
				return {};
			visited[seen++] = id;
			const auto* info = utils::hook::invoke<const anim_info*>(0x14065A080, static_cast<int>(id));
			if (!info || !std::isfinite(info->weight))
				return {};
			if (queued + 2 > pending.size())
				return {};
			pending[queued++] = info->next;
			if (info->weight <= 0)
				continue;
			if (!info->leaf)
			{
				pending[queued++] = info->children;
				continue;
			}
			// 0x140662854 loads leaf.parts at +0x18; leaf flag at +0x14.
			if (!info->parts || !info->parts->name)
				return {};
			const auto length = strnlen_s(info->parts->name, 160);
			if (length == 160)
				return {};
			if(out.count==out.clips.size())return {};
			out.clips[out.count++]={{info->parts->name,length},info->time,info->weight};
		}
		return out;
	}
}
namespace vr::gameplay::weapons
{
	bool initialize_animation_query() {return native_animation::initialize();}
	animation_presentation sample_animation_presentation(const void* tree,const profile& profile) noexcept
	{
		if(!profile.suppress_equip)return {};
		const auto sample=native_animation::sample(tree);
		if(!sample.valid)return {};
		animation_presentation out{true,false};
		for(const auto& clip:sample.leaves())out.equip|=profile.suppress_equip(clip.name);
		return out;
	}
}
