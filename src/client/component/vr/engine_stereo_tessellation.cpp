#include <std_include.hpp>
#include "engine_stereo_owner_pass.hpp"
#include "engine_stereo_tessellation_view.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::engine_stereo_tessellation
{
	namespace
	{
		constexpr std::uintptr_t fill_call = 0x14029E006, fill_function = 0x14029E9D0;
		constexpr std::uintptr_t map_call = 0x14029EA02, map_function = 0x14029F930;
		constexpr std::uintptr_t unmap_tail = 0x14029EC68, unmap_function = 0x1402A0D30;
		struct upload
		{
			shared_view view;
			void* context{};
			void* buffer{};
			void* mapped{};
			bool consumed{};
		};
		thread_local upload* current{};
		std::atomic<std::uint64_t> prepared{}, applied{}, rejected{};
		std::atomic<bool> alive{true};
		bool installed{};

		void fill_stub(void* context, void* frontend)
		{
			upload active{};
			const auto previous = current;
			current = nullptr;
			const auto restore = gsl::finally([previous] { current = previous; });
			if (!alive.load(std::memory_order_relaxed) ||
				!engine_stereo_owner_pass::snapshot_tessellation_view(frontend, active.view))
			{
				utils::hook::invoke<void>(fill_function, context, frontend);
				return;
			}
			++prepared;
			active.context = context;
			current = &active;
			utils::hook::invoke<void>(fill_function, context, frontend);
			if (!active.consumed) ++rejected;
		}

		void* map_stub(void* context, int slot, void* buffer)
		{
			const auto mapped = utils::hook::invoke<void*>(map_function, context, slot, buffer);
			if (current && current->context == context && slot == 0 && !current->mapped)
			{
				current->mapped = mapped;
				current->buffer = buffer;
			}
			return mapped;
		}

		void unmap_stub(void* context, void* buffer)
		{
			if (current && current->context == context && current->buffer == buffer && !current->consumed)
			{
				current->consumed = true;
				if (replace_main_view(current->mapped, constants_size, current->view)) ++applied;
				else ++rejected;
			}
			// Preserve the native upload and its GPU ordering exactly once.
			utils::hook::invoke<void>(unmap_function, context, buffer);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			command::add("vr_tessellation_status", [] {
				console::info("[VR tessellation] hooks=%d prepared=%llu applied=%llu rejected=%llu shared_view=frontend_union\n",
					installed, prepared.load(), applied.load(), rejected.load());
			});
			constexpr std::uint8_t mask[]{255,255,255,255,255};
			constexpr std::uint8_t fill[]{0xe8,0xc5,0x09,0x00,0x00};
			constexpr std::uint8_t map[]{0xe8,0x29,0x0f,0x00,0x00};
			constexpr std::uint8_t unmap[]{0xe9,0xc3,0x20,0x00,0x00};
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(fill_call), {fill,mask,sizeof(fill)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(map_call), {map,mask,sizeof(map)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(unmap_tail), {unmap,mask,sizeof(unmap)}))
			{
				console::error("[VR tessellation] native constant-upload signature mismatch\n");
				return;
			}
			utils::hook::call(fill_call, fill_stub);
			utils::hook::call(map_call, map_stub);
			utils::hook::jump(unmap_tail, unmap_stub);
			installed = true;
		}
		void pre_destroy() override { alive.store(false, std::memory_order_relaxed); }
	};
}

REGISTER_COMPONENT(vr::engine_stereo_tessellation::component)
