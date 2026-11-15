#include <std_include.hpp>
#include "engine_stereo_sun_shadow.hpp"
#include "engine_stereo_execution.hpp"
#include "engine_stereo_output_merger.hpp"
#include "engine_stereo_resource_ops.hpp"
#include "native_conversion_command_list.hpp"
#include "native_render_contract.hpp"
#include "component/console.hpp"
#include "game/assets.hpp"
#include <atomic>
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::engine_stereo_sun_shadow
{
	namespace
	{
		constexpr std::uintptr_t site = 0x1407A7E3F, native = 0x1407B8210;
		std::atomic<ID3D11Resource*> watched{};
		std::atomic<ID3D11DeviceContext*> owner_context{};
		std::atomic_bool changed{};
		std::atomic_bool binding_uncertain{};
		thread_local bool producing_left{};
		std::atomic_uint64_t left_count{}, skipped{}, refused{}, dirty_count{}, saved{};
		template<class T> T read(const void* data, std::size_t offset) noexcept
		{
			T result{}; std::memcpy(&result, static_cast<const std::uint8_t*>(data) + offset, sizeof(result)); return result;
		}
		void write(ID3D11Resource* resource) noexcept
		{
			if (producing_left) return;
			if (resource && resource == watched.load(std::memory_order_acquire)) changed.store(true, std::memory_order_release);
		}
		void write_view(ID3D11View* view) noexcept
		{
			if (!view || !watched.load(std::memory_order_acquire)) return;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource; view->GetResource(&resource); write(resource.Get());
		}
		void resource_operation(const engine_stereo_resource_ops::event& event) noexcept
		{
			if (!watched.load(std::memory_order_acquire)) return;
			write(event.destination); write_view(event.view);
		}
		void clear_color(ID3D11DeviceContext*, ID3D11RenderTargetView* view, std::uintptr_t,
			std::uint32_t, std::uintptr_t, std::uint64_t) noexcept { write_view(view); }
		void clear_depth(ID3D11DeviceContext*, ID3D11DepthStencilView* view, std::uint32_t,
			std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint64_t) noexcept { write_view(view); }
		void invocation(ID3D11DeviceContext* context, engine_stereo_execution::api operation, std::uintptr_t,
			std::uint32_t, std::uintptr_t, std::uint64_t, std::uint8_t, const std::array<std::uint64_t, 6>& args) noexcept
		{
			if (!watched.load(std::memory_order_acquire) || producing_left) return;
			using api = engine_stereo_execution::api;
			if (operation == api::execute_command_list)
			{
				// The production conversion owner marks only private, state-restoring
				// lists. Arbitrary native/foreign lists invalidate the provenance.
				if (!args[1] || !native_conversion_command_list::is_marked(reinterpret_cast<ID3D11CommandList*>(args[0]))) changed = true;
				else binding_uncertain = true; // driver replay may update the binding observer before RestoreState
				return;
			}
			if (engine_stereo_execution::in_known_conversion(context)) return;
			if (operation == api::dispatch || operation == api::dispatch_indirect) return; // admitted depth resource has no UAV bind
			if (context != owner_context.load(std::memory_order_acquire)) { changed = true; return; }
			if (binding_uncertain.load(std::memory_order_acquire))
			{
				Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
				context->OMGetRenderTargets(0, nullptr, &depth);
				write_view(depth.Get()); return;
			}
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			if (!binding.valid) { changed = true; return; }
			write_view(reinterpret_cast<ID3D11DepthStencilView*>(binding.depth_stencil));
		}
		void invoke(void* data, void* record)
		{
			if (engine_stereo_owner_pass::begin_sun_shadow(data, record)) return;
			bool returned{};
			const auto exit = gsl::finally([&] { if (returned) engine_stereo_owner_pass::end_sun_shadow(data, record); else unwatch(); });
			reinterpret_cast<void(*)(void*,void*)>(native)(data, record);
			returned = true;
		}
	}
	bool snapshot(void* data, void* record, inputs& output, Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture) noexcept
	{
		if (!data || !record || read<std::uintptr_t>(record, 0x1F68) != reinterpret_cast<std::uintptr_t>(data) ||
			read<unsigned>(record, 0x200) != 1 || read<unsigned>(record, 0x220) || read<unsigned>(record, 0x2FB8) ||
			read<unsigned>(record, 0x1FA0) != 2048 || read<unsigned>(record, 0x1FA4) != 3 ||
			read<unsigned>(record, 0x1FA8) != 26 || read<unsigned>(record, 0x1FB0) != 304 || read<int>(data, 0x61ADA0) != 2) return false;
		for (unsigned i{}; i < 2; ++i)
		{
			if (read<unsigned>(data, 0x540FC8 + (26 + i) * 4)) return false; // delegated/native list progression stays original
			std::memcpy(output.lists[i].data(), static_cast<const std::uint8_t*>(record) + 0x2FC8 + (26 + i) * 0x130, 0x130);
			std::memcpy(output.partitions[i].data(), static_cast<const std::uint8_t*>(data) + 0x542650 + i * 0x48270, 0x180);
		}
		std::memcpy(output.sun_visibility.data(), static_cast<const std::uint8_t*>(data) + 0x540D0C, output.sun_visibility.size());
		auto* image = *reinterpret_cast<game::GfxImage**>(native_render_contract::target_registry_base + 26 * native_render_contract::target_registry_stride);
		if (!image || !image->texture.shaderView) return false;
		Microsoft::WRL::ComPtr<ID3D11Resource> resource; image->texture.shaderView->GetResource(&resource);
		if (!resource || FAILED(resource.As(&texture)) || texture.Get() != image->texture.map) return false;
		D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
		if (d.Width != 2048 || d.Height != 6144 || d.Format != DXGI_FORMAT_R24G8_TYPELESS || d.MipLevels != 1 ||
			d.ArraySize != 1 || d.SampleDesc.Count != 1 || d.SampleDesc.Quality || d.Usage != D3D11_USAGE_DEFAULT ||
			d.BindFlags != (D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE) || d.CPUAccessFlags || d.MiscFlags) return false;
		output.data = reinterpret_cast<std::uintptr_t>(data); output.resource = reinterpret_cast<std::uintptr_t>(resource.Get());
		return true;
	}
	void watch(ID3D11Resource* resource, ID3D11DeviceContext* context) noexcept
	{
		unwatch(); changed = false; binding_uncertain = false; owner_context = context; producing_left = true;
		engine_stereo_execution::set_invocation_observer(engine_stereo_execution::invocation_observer_channel::sun_reuse, invocation);
		engine_stereo_resource_ops::set_observer(engine_stereo_resource_ops::observer_channel::sun_reuse, resource_operation);
		engine_stereo_output_merger::set_clear_observers(engine_stereo_output_merger::clear_observer_channel::sun_reuse, clear_color, clear_depth);
		watched.store(resource, std::memory_order_release);
	}
	void unwatch() noexcept
	{
		producing_left = false;
		if (!watched.exchange(nullptr, std::memory_order_acq_rel)) return;
		engine_stereo_execution::set_invocation_observer(engine_stereo_execution::invocation_observer_channel::sun_reuse, nullptr);
		engine_stereo_resource_ops::set_observer(engine_stereo_resource_ops::observer_channel::sun_reuse, nullptr);
		engine_stereo_output_merger::set_clear_observers(engine_stereo_output_merger::clear_observer_channel::sun_reuse, nullptr, nullptr);
	}
	void finish_left() noexcept { producing_left = false; }
	bool watching(std::uintptr_t resource, ID3D11DeviceContext* context) noexcept
	{ return reinterpret_cast<std::uintptr_t>(watched.load(std::memory_order_acquire)) == resource && owner_context.load() == context; }
	bool modified() noexcept { return changed.load(std::memory_order_acquire); }
	void note_left() noexcept { ++left_count; }
	void note_skip(std::uint32_t count) noexcept { ++skipped; saved += count; }
	void note_refusal() noexcept { ++refused; if (modified()) ++dirty_count; }
	counters get_counters() noexcept { return {left_count.load(),skipped.load(),refused.load(),dirty_count.load(),saved.load()}; }
	void install() noexcept
	{
		constexpr std::array<std::uint8_t,5> expected{0xE8,0xCC,0x03,0x01,0x00};
		if (!utils::hook_validation::validate_executable_target(reinterpret_cast<const void*>(site)) ||
			std::memcmp(reinterpret_cast<const void*>(site), expected.data(), expected.size()))
		{ console::error("[VR] Sun reuse unavailable: native callsite mismatch\n"); return; }
		try { utils::hook::call(site, reinterpret_cast<void*>(invoke)); }
		catch (const std::exception& e) { console::error("[VR] Sun reuse unavailable: %s\n", e.what()); }
	}
}
