#include <std_include.hpp>
#include "sun_shadow.hpp"
#include "sun_shadow_readback.hpp"
#include "../debug_options.hpp"
#include "../engine_stereo_owner_pass.hpp"
#include "../engine_stereo_execution.hpp"
#include "../engine_stereo_output_merger.hpp"
#include "../engine_stereo_resource_ops.hpp"
#include "../native_render_contract.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

namespace vr::diagnostics::sun_shadow
{
	namespace
	{
		using owner = engine_stereo_owner_pass::transaction;
		using api = engine_stereo_execution::api;
		static_assert(engine_stereo_resource_ops::api_count == 8);
		using clock = std::chrono::steady_clock;
		constexpr unsigned max_samples = 6, max_subpasses = 64, view_count = 3;
		constexpr std::uint64_t duration_ms = 24000, spacing_ms = 4000;
		constexpr std::uintptr_t sun_address = 0x1407B8210;
		constexpr std::array<std::uintptr_t, 3> sub_addresses{0x140736470, 0x140736600, 0x140736A90};
		utils::hook::detour sun_hook;
		std::array<utils::hook::detour, 3> sub_hooks;
		std::atomic_bool opted_in{}, installed{};
		const char* installation_state = "not_requested"; // immutable after loader install

		template<class T> T field(const void* bytes, const std::size_t offset) noexcept
		{
			T result{};
			std::memcpy(&result, static_cast<const std::uint8_t*>(bytes) + offset, sizeof(result));
			return result;
		}
		template<std::size_t Size> auto bytes_at(const void* bytes, const std::size_t offset = 0) noexcept
		{
			std::array<std::uint8_t, Size> result{};
			if (bytes) std::memcpy(result.data(), static_cast<const std::uint8_t*>(bytes) + offset, Size);
			return result;
		}
		struct binding
		{
			std::uintptr_t context{}, rtv{}, dsv{}, color_resource{}, depth_resource{};
			D3D11_RTV_DIMENSION rtv_dimension{};
			D3D11_DSV_DIMENSION dsv_dimension{};
			DXGI_FORMAT color_format{}, depth_format{};
			D3D11_DEVICE_CONTEXT_TYPE context_type{};
			std::array<D3D11_VIEWPORT, 4> viewports{};
			unsigned viewport_count{};
		};
		struct pass
		{
			unsigned kind{}, partition{}, cache_index{}, ordinal_begin{}, ordinal_end{}, geometry_views{}, dropped_views{};
			std::uintptr_t descriptor{}, parameters{}, primary{}, rebase{};
			std::array<std::uint8_t, 0x130> list{};
			std::array<std::uint8_t, 0x170> primary_view{};
			std::array<std::uint64_t, engine_stereo_execution::api_count> calls{};
			std::uint64_t indices{}, cpu_ns{};
			binding first_binding;
		};
		struct output_group
		{
			std::uintptr_t context{}, rtv{}, dsv{}, first_caller{}, last_caller{};
			unsigned target{};
			bool binding_known{};
			unsigned render_target_count{};
			std::array<std::uint64_t, 3> phases{}; // before sun, inside sun, after sun
			std::array<std::uint64_t, engine_stereo_execution::api_count> calls{};
			binding observed;
		};
		struct write_group
		{
			unsigned operation{}, phase{};
			std::uintptr_t context{}, destination{}, source{}, caller{};
			std::uint64_t calls{};
		};
		struct eye
		{
			std::uintptr_t record{}, data{};
			unsigned roots{}, sun_enabled{}, transparent_enabled{}, cache_enabled{}, tile{}, configured{}, main_target{}, trans_target{};
			int partitions{};
			unsigned ordinal_begin{}, ordinal_end{}, dropped_subpasses{};
			std::uint64_t cpu_ns{};
			std::array<std::uint64_t, engine_stereo_execution::api_count> calls{};
			std::array<std::array<std::uint8_t, 0x180>, 4> partition_before{}, partition_after{};
			std::array<std::array<std::array<std::uint8_t, 0x20>, 9>, 4> cache_before{}, cache_after{};
			std::array<std::uint32_t, 68> job_flags_before{}, job_flags_after{};
			std::vector<pass> subpasses;
			std::vector<output_group> output_groups;
			std::vector<write_group> resource_writes;
			unsigned dropped_output_groups{}, dropped_resource_writes{};
		};
		struct surface
		{
			const char* state = "eye_not_captured";
			unsigned target{}, rows{}, busy_polls{};
			HRESULT result = E_PENDING;
			std::uintptr_t image{}, resource{}, texture{}, srv{};
			D3D11_TEXTURE2D_DESC description{};
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_description{};
			std::uint64_t size{};
			std::unique_ptr<std::uint8_t[]> pixels;
		};
		struct sample
		{
			std::uint64_t request{}, pair{}, generation{}, tick{}, finished_tick{};
			std::uintptr_t frontend{}, context{};
			unsigned thread{}, captured_mask{}, completed_mask{};
			bool auxiliary{}, pair_closed{}, pair_complete{};
			const char* error = "none";
			std::array<eye, view_count> eyes;
			std::array<std::array<surface, 2>, view_count> surfaces;
		};
		struct control
		{
			std::uint64_t id{}, started{}, deadline{};
			std::atomic_uint started_samples{}, published_samples{}, producer_busy{};
			std::atomic_uint allocation_failures{};
			std::atomic_bool processing{};
			std::mutex mutex;
			std::shared_ptr<sample> ready;
		};
		struct gpu_state
		{
			std::shared_ptr<control> request;
			std::shared_ptr<sample> pending;
			std::array<std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2>, view_count> staging;
			std::uint64_t bytes{}, last_poll_pair{};
		};
		std::atomic_uint64_t serial{}, active_id{};
		std::atomic<std::shared_ptr<control>> current;
		std::atomic_bool retire_requested{};
		std::mutex gpu_mutex;
		gpu_state gpu;
		thread_local std::shared_ptr<sample> active_sample;
		thread_local const owner* active_owner{};
		thread_local eye* active_eye{};
		thread_local pass* active_pass{};
		thread_local unsigned sun_depth{};

		unsigned ordinal() noexcept
		{
			return !active_owner || active_owner->current_view > 1 ? UINT_MAX :
				active_owner->current_view == 0 ? active_owner->dynamic_index_left_count :
				active_owner->dynamic_index_right_count;
		}
		void observe_binding(binding& value, ID3D11DeviceContext* context) noexcept
		{
			value.context = reinterpret_cast<std::uintptr_t>(context);
			value.context_type = context->GetType();
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
			context->OMGetRenderTargets(1, &rtv, &dsv);
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			if (rtv)
			{
				D3D11_RENDER_TARGET_VIEW_DESC description{}; rtv->GetDesc(&description);
				value.rtv = reinterpret_cast<std::uintptr_t>(rtv.Get());
				value.rtv_dimension = description.ViewDimension; value.color_format = description.Format;
				rtv->GetResource(&resource); value.color_resource = reinterpret_cast<std::uintptr_t>(resource.Get());
			}
			if (dsv)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC description{}; dsv->GetDesc(&description);
				value.dsv = reinterpret_cast<std::uintptr_t>(dsv.Get());
				value.dsv_dimension = description.ViewDimension; value.depth_format = description.Format;
				resource.Reset(); dsv->GetResource(&resource); value.depth_resource = reinterpret_cast<std::uintptr_t>(resource.Get());
			}
			value.viewport_count = static_cast<unsigned>(value.viewports.size());
			context->RSGetViewports(&value.viewport_count, value.viewports.data());
		}
		unsigned current_phase() noexcept { return sun_depth ? 1 : active_eye->roots ? 2 : 0; }
		void note_write(const unsigned operation, ID3D11DeviceContext* context,
			const std::uintptr_t destination, const std::uintptr_t source, const std::uintptr_t caller) noexcept
		{
			if (!active_eye || !destination) return;
			const auto phase = current_phase();
			for (auto& group : active_eye->resource_writes)
				if (group.operation == operation && group.phase == phase && group.destination == destination &&
					group.source == source && group.caller == caller && group.context == reinterpret_cast<std::uintptr_t>(context))
				{ ++group.calls; return; }
			if (active_eye->resource_writes.size() == 128) { ++active_eye->dropped_resource_writes; return; }
			active_eye->resource_writes.push_back({operation, phase, reinterpret_cast<std::uintptr_t>(context), destination, source, caller, 1});
		}
		void resource_write(const engine_stereo_resource_ops::event& event) noexcept
		{
			if (!active_eye) return;
			Microsoft::WRL::ComPtr<ID3D11Resource> viewed;
			if (event.view) event.view->GetResource(&viewed);
			auto* const destination = event.destination ? event.destination : viewed.Get();
			if (!destination) return;
			D3D11_RESOURCE_DIMENSION dimension{}; destination->GetType(&dimension);
			if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return;
			note_write(static_cast<unsigned>(event.operation), event.context,
				reinterpret_cast<std::uintptr_t>(destination),
				reinterpret_cast<std::uintptr_t>(event.source), event.caller);
		}
		void clear_color(ID3D11DeviceContext* context, ID3D11RenderTargetView* view,
			std::uintptr_t caller, std::uint32_t, std::uintptr_t, std::uint64_t) noexcept
		{
			if (!active_eye || !view) return;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource; view->GetResource(&resource);
			note_write(8, context, reinterpret_cast<std::uintptr_t>(resource.Get()), 0, caller);
		}
		void clear_depth(ID3D11DeviceContext* context, ID3D11DepthStencilView* view,
			std::uint32_t, std::uintptr_t caller, std::uint32_t, std::uintptr_t, std::uint64_t) noexcept
		{
			if (!active_eye || !view) return;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource; view->GetResource(&resource);
			note_write(9, context, reinterpret_cast<std::uintptr_t>(resource.Get()), 0, caller);
		}
		void invocation(ID3D11DeviceContext* context, api operation, std::uintptr_t caller,
			std::uint32_t, std::uintptr_t, std::uint64_t, std::uint8_t,
			const std::array<std::uint64_t, 6>& arguments) noexcept
		{
			if (!active_eye) return;
			// Keep the entire native owner visible, including later writers that
			// could hide a root-pass difference before the completed-eye copy.
			const auto bound = engine_stereo_output_merger::get_current_binding(context);
			output_group* selected{};
			for (auto& group : active_eye->output_groups)
				if (group.context == reinterpret_cast<std::uintptr_t>(context) && group.rtv == bound.render_target_0 &&
					group.dsv == bound.depth_stencil && group.target == bound.target_id) { selected = &group; break; }
			if (!selected && active_eye->output_groups.size() < 64)
			{
				selected = &active_eye->output_groups.emplace_back();
				selected->context = reinterpret_cast<std::uintptr_t>(context); selected->rtv = bound.render_target_0;
				selected->dsv = bound.depth_stencil; selected->target = bound.target_id; selected->binding_known = bound.valid;
				selected->render_target_count = bound.render_target_count;
				selected->first_caller = caller; observe_binding(selected->observed, context);
			}
			if (selected)
			{
				++selected->phases[current_phase()]; ++selected->calls[static_cast<std::size_t>(operation)]; selected->last_caller = caller;
			}
			else ++active_eye->dropped_output_groups;
			if (!sun_depth) return;
			++active_eye->calls[static_cast<std::size_t>(operation)];
			if (!active_pass) return;
			++active_pass->calls[static_cast<std::size_t>(operation)];
			if (operation == api::draw_indexed) active_pass->indices += arguments[0];
			else if (operation == api::draw_indexed_instanced) active_pass->indices += arguments[0] * arguments[1];
			if (!active_pass->first_binding.context) observe_binding(active_pass->first_binding, context);
		}
		void partition_snapshot(eye& value, const void* data, const bool after) noexcept
		{
			auto& partitions = after ? value.partition_after : value.partition_before;
			auto& caches = after ? value.cache_after : value.cache_before;
			for (int i{}; i < (std::min)(value.partitions, 4); ++i)
			{
				partitions[i] = bytes_at<0x180>(data, 0x542650 + i * 0x48270);
				for (unsigned c{}; c < caches[i].size(); ++c)
					caches[i][c] = bytes_at<0x20>(data, 0x5427D0 + i * 0x48270 + c * 0x8018);
			}
			auto& jobs = after ? value.job_flags_after : value.job_flags_before;
			std::memcpy(jobs.data(), static_cast<const std::uint8_t*>(data) + 0x540FC8, sizeof(jobs));
		}
		void sun_stub(void* data, void* record)
		{
			if (!active_eye || !active_owner || record != active_owner->record(active_owner->current_view))
			{
				sun_hook.invoke<void>(data, record); return;
			}
			auto& value = *active_eye;
			++value.roots;
			value.data = reinterpret_cast<std::uintptr_t>(data);
			value.partitions = field<int>(data, 0x61ADA0);
			value.ordinal_begin = ordinal();
			partition_snapshot(value, data, false);
			const auto started = clock::now();
			++sun_depth;
			const auto done = gsl::finally([&]
			{
				--sun_depth;
				value.cpu_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - started).count();
				value.ordinal_end = ordinal();
				partition_snapshot(value, data, true);
			});
			// Observation only: H2 always executes both complete sun passes.
			sun_hook.invoke<void>(data, record);
		}
		pass* begin_subpass(unsigned kind, void* record, void* descriptor,
			unsigned partition, unsigned cache_index) noexcept
		{
			if (!active_eye || !sun_depth || !active_owner || record != active_owner->record(active_owner->current_view)) return nullptr;
			if (active_eye->subpasses.size() == max_subpasses) { ++active_eye->dropped_subpasses; return nullptr; }
			auto& value = active_eye->subpasses.emplace_back();
			value.kind = kind; value.partition = partition; value.cache_index = cache_index;
			value.ordinal_begin = ordinal(); value.descriptor = reinterpret_cast<std::uintptr_t>(descriptor);
			value.list = bytes_at<0x130>(descriptor);
			return &value;
		}
		template<unsigned Kind> void sub_stub(void* record, void* descriptor, unsigned partition, void* context)
		{
			auto* const previous = active_pass;
			active_pass = begin_subpass(Kind, record, descriptor, partition, 0);
			const auto started = active_pass ? clock::now() : clock::time_point{};
			const auto done = gsl::finally([&]
			{
				if (active_pass) { active_pass->ordinal_end = ordinal(); active_pass->cpu_ns =
					std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - started).count(); }
				active_pass = previous;
			});
			sub_hooks[Kind].invoke<void>(record, descriptor, partition, context);
		}
		void cache_stub(void* record, void* descriptor, unsigned partition, unsigned cache_index, void* context)
		{
			auto* const previous = active_pass;
			active_pass = begin_subpass(0, record, descriptor, partition, cache_index);
			const auto started = active_pass ? clock::now() : clock::time_point{};
			const auto done = gsl::finally([&]
			{
				if (active_pass) { active_pass->ordinal_end = ordinal(); active_pass->cpu_ns =
					std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - started).count(); }
				active_pass = previous;
			});
			sub_hooks[0].invoke<void>(record, descriptor, partition, cache_index, context);
		}

		void copy_surface(const owner& active, surface& value, const unsigned output)
		{
			const auto& view = gpu.pending->eyes[active.current_view];
			value.target = output ? view.trans_target : view.main_target;
			if (output && !view.transparent_enabled) { value.state = "disabled"; return; }
			if (!view.roots) { value.state = "sun_not_executed"; return; }
			if (value.target != field<unsigned>(active.record(active.current_view), output ? 0x1FB0 : 0x1FA8))
			{ value.state = "target_changed_during_owner"; return; }
			if (value.target >= native_render_contract::target_registry_capacity) { value.state = "invalid_target"; return; }
			const auto* entry = reinterpret_cast<const void*>(native_render_contract::target_registry_base +
				value.target * native_render_contract::target_registry_stride);
			auto* const image = field<game::GfxImage*>(entry, 0);
			value.image = reinterpret_cast<std::uintptr_t>(image);
			if (!image || !image->texture.shaderView) { value.state = "missing_image_or_srv"; return; }
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			image->texture.shaderView->GetResource(&resource);
			if (!resource || FAILED(resource.As(&texture)) || !texture || texture.Get() != image->texture.map)
			{ value.state = "image_texture_mismatch"; return; }
			value.texture = reinterpret_cast<std::uintptr_t>(texture.Get());
			value.resource = reinterpret_cast<std::uintptr_t>(resource.Get());
			value.srv = reinterpret_cast<std::uintptr_t>(image->texture.shaderView);
			texture->GetDesc(&value.description); image->texture.shaderView->GetDesc(&value.srv_description);
			value.size = surface_bytes(value.description);
			if (!value.size) { value.state = "unsupported_description"; return; }
			if (gpu.bytes + value.size > memory_limit) { value.state = "memory_budget"; return; }
			Microsoft::WRL::ComPtr<ID3D11Device> device, source_device;
			active.context->GetDevice(&device); texture->GetDevice(&source_device);
			if (device != source_device || active.context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
			{ value.state = "device_or_context_mismatch"; return; }
			auto& staging = gpu.staging[active.current_view][output];
			value.result = create_staging(device.Get(), value.description, &staging);
			if (FAILED(value.result)) { value.state = "staging_creation_failed"; return; }
			// Uninitialized CPU storage is committed only by the bounded row copies;
			// do not zero hundreds of MiB on this native render boundary.
			value.pixels.reset(new std::uint8_t[static_cast<std::size_t>(value.size)]);
			active.context->CopyResource(staging.Get(), texture.Get());
			gpu.bytes += value.size;
			value.state = "copy_queued";
		}
		void poll(const owner& active)
		{
			if (!gpu.pending || !gpu.pending->pair_closed || gpu.last_poll_pair == active.records.pair_id) return;
			gpu.last_poll_pair = active.records.pair_id;
			auto& value = *gpu.pending;
			if (value.generation != active.device_generation || value.context != reinterpret_cast<std::uintptr_t>(active.context.Get()))
			{ gpu = {}; return; }
			std::size_t budget = readback_budget;
			bool pending{};
			for (unsigned eye_index{}; eye_index < view_count; ++eye_index)
				for (unsigned output{}; output < 2; ++output)
				{
					auto& surface = value.surfaces[eye_index][output];
					auto& staging = gpu.staging[eye_index][output];
					if (!staging) continue;
					if (surface.rows < surface.description.Height)
					{
						if (GetTickCount64() - value.tick > 8000)
						{ surface.state = "readback_timeout"; staging.Reset(); continue; }
						if (budget < std::size_t(surface.description.Width) * pixel_bytes(surface.description.Format))
						{ pending = true; continue; }
						surface.result = read_rows(active.context.Get(), staging.Get(), surface.description,
							surface.pixels.get(), surface.rows, budget);
						if (surface.result == DXGI_ERROR_WAS_STILL_DRAWING) { ++surface.busy_polls; pending = true; continue; }
						if (FAILED(surface.result)) { surface.state = "map_failed"; staging.Reset(); continue; }
						if (surface.rows < surface.description.Height) { pending = true; continue; }
						surface.state = "ready";
					}
					staging.Reset();
				}
			if (pending) return;
			value.finished_tick = GetTickCount64();
			const std::unique_lock lock(gpu.request->mutex, std::try_to_lock);
			if (!lock.owns_lock() || gpu.request->ready) { ++gpu.request->producer_busy; return; }
			gpu.request->ready = std::move(gpu.pending);
			++gpu.request->published_samples;
			gpu.bytes = 0;
		}
		std::string hex(const std::uint8_t* bytes, const std::size_t size)
		{
			constexpr char digits[] = "0123456789abcdef";
			std::string result(size * 2, '0');
			for (std::size_t i{}; i < size; ++i) { result[i * 2] = digits[bytes[i] >> 4]; result[i * 2 + 1] = digits[bytes[i] & 15]; }
			return result;
		}
		template<class T> std::string hex(const T& value) { return hex(reinterpret_cast<const std::uint8_t*>(value.data()), sizeof(value)); }
		nlohmann::json describe(const surface& value)
		{
			const auto& d = value.description;
			return {{"state", value.state}, {"target", value.target}, {"image", value.image}, {"resource", value.resource}, {"texture", value.texture},
				{"srv", value.srv}, {"result", static_cast<std::int64_t>(value.result)}, {"rows_read", value.rows}, {"busy_polls", value.busy_polls},
				{"bytes", value.size}, {"width", d.Width}, {"height", d.Height}, {"format", d.Format}, {"mips", d.MipLevels},
				{"array", d.ArraySize}, {"samples", d.SampleDesc.Count}, {"quality", d.SampleDesc.Quality}, {"usage", d.Usage},
				{"bind", d.BindFlags}, {"cpu_access", d.CPUAccessFlags}, {"misc", d.MiscFlags},
				{"srv_descriptor_hex", hex(reinterpret_cast<const std::uint8_t*>(&value.srv_description), sizeof(value.srv_description))}};
		}
		nlohmann::json compare(const surface& left, const surface& right)
		{
			nlohmann::json result{{"state", "unavailable"}};
			if (std::strcmp(left.state, "ready") || std::strcmp(right.state, "ready")) return result;
			if (left.description.Width != right.description.Width || left.description.Height != right.description.Height ||
				left.description.Format != right.description.Format || left.size != right.size)
			{ result["state"] = "description_mismatch"; return result; }
			const auto stride = pixel_bytes(left.description.Format);
			const auto comparison = compare_pixels(left.pixels.get(), right.pixels.get(), static_cast<std::size_t>(left.size), stride);
			const auto differences = comparison.differences, first = comparison.first;
			result = {{"state", differences ? "different" : "equal"}, {"logical_bytes_compared", left.size},
				{"differing_bytes", differences}, {"left_nonuniform_pixels", comparison.left_varied}, {"right_nonuniform_pixels", comparison.right_varied}};
			if (differences)
			{
				const auto row_bytes = std::uint64_t(left.description.Width) * stride;
				result["first_difference"] = {{"byte", first}, {"x", first % row_bytes / stride}, {"y", first / row_bytes},
					{"left_hex", hex(left.pixels.get() + first, static_cast<std::size_t>((std::min)(std::uint64_t(32), left.size - first)))},
					{"right_hex", hex(right.pixels.get() + first, static_cast<std::size_t>((std::min)(std::uint64_t(32), right.size - first)))}};
			}
			return result;
		}
		nlohmann::json format(const sample& value)
		{
			nlohmann::json result{{"request", value.request}, {"pair", value.pair}, {"generation", value.generation},
				{"tick", value.tick}, {"readback_finished_tick", value.finished_tick}, {"frontend", value.frontend}, {"context", value.context},
				{"owner_thread", value.thread}, {"auxiliary_requested", value.auxiliary}, {"captured_mask", value.captured_mask},
				{"completed_mask", value.completed_mask}, {"pair_complete", value.pair_complete}, {"error", value.error}};
			for (unsigned i{}; i < view_count; ++i)
			{
				const auto& e = value.eyes[i];
				nlohmann::json eye{{"view", i}, {"record", e.record}, {"data", e.data}, {"root_calls", e.roots},
					{"sun_enabled", e.sun_enabled}, {"transparent_enabled", e.transparent_enabled}, {"cache_enabled_field", e.cache_enabled},
					{"tile_size", e.tile}, {"configured_partitions", e.configured}, {"active_partitions", e.partitions},
					{"partition_snapshot_truncated", e.partitions < 0 || e.partitions > 4}, {"data_matches_frontend", e.data != 0 && e.data == value.frontend},
					{"main_target", e.main_target}, {"transparent_target", e.trans_target}, {"ordinal_begin", e.ordinal_begin}, {"ordinal_end", e.ordinal_end},
					{"cpu_ns_inclusive", e.cpu_ns}, {"per_api", e.calls}, {"dropped_subpasses", e.dropped_subpasses},
					{"job_flags_before", e.job_flags_before}, {"job_flags_after", e.job_flags_after}};
				eye["dropped_output_groups"] = e.dropped_output_groups;
				eye["dropped_resource_writes"] = e.dropped_resource_writes;
				for (const auto& group : e.output_groups)
					eye["owner_output_groups"].push_back({{"context", group.context}, {"rtv", group.rtv}, {"dsv", group.dsv},
						{"target", group.target}, {"binding_known", group.binding_known}, {"first_caller", group.first_caller}, {"last_caller", group.last_caller},
						{"render_target_count", group.render_target_count},
						{"phases", group.phases}, {"per_api", group.calls}, {"color_resource", group.observed.color_resource},
						{"depth_resource", group.observed.depth_resource}, {"context_type", group.observed.context_type}});
				for (const auto& group : e.resource_writes)
					eye["owner_resource_writes"].push_back({{"operation", group.operation}, {"phase", group.phase}, {"context", group.context},
						{"destination", group.destination}, {"source", group.source}, {"caller", group.caller}, {"calls", group.calls}});
				for (int p{}; p < (std::min)(e.partitions, 4); ++p)
				{
					eye["partitions"].push_back({{"index", p}, {"before_hex", hex(e.partition_before[p])}, {"after_hex", hex(e.partition_after[p])},
						{"cache_headers_before_hex", hex(e.cache_before[p])}, {"cache_headers_after_hex", hex(e.cache_after[p])}});
				}
				for (const auto& p : e.subpasses)
				{
					const auto& b = p.first_binding;
					nlohmann::json item{{"kind", p.kind}, {"partition", p.partition}, {"cache_index", p.cache_index}, {"descriptor", p.descriptor},
						{"descriptor_hex", hex(p.list)}, {"geometry_views", p.geometry_views}, {"dropped_geometry_views", p.dropped_views},
						{"parameters", p.parameters}, {"primary_view", p.primary}, {"rebase_view", p.rebase}, {"primary_view_hex", hex(p.primary_view)},
						{"ordinal_begin", p.ordinal_begin}, {"ordinal_end", p.ordinal_end}, {"cpu_ns_inclusive", p.cpu_ns}, {"per_api", p.calls}, {"direct_indices", p.indices},
						{"first_binding", {{"context", b.context}, {"context_type", b.context_type}, {"rtv", b.rtv}, {"dsv", b.dsv},
							{"color_resource", b.color_resource}, {"depth_resource", b.depth_resource}, {"color_format", b.color_format}, {"depth_format", b.depth_format},
							{"rtv_dimension", b.rtv_dimension}, {"dsv_dimension", b.dsv_dimension}, {"viewport_count", b.viewport_count},
							{"viewports_hex", hex(reinterpret_cast<const std::uint8_t*>(b.viewports.data()), sizeof(b.viewports))}}}};
					eye["subpasses"].push_back(std::move(item));
				}
				for (const auto& surface : value.surfaces[i]) eye["outputs"].push_back(describe(surface));
				result["eyes"].push_back(std::move(eye));
			}
			for (unsigned output{}; output < 2; ++output)
			{
				result["left_right"].push_back(compare(value.surfaces[0][output], value.surfaces[1][output]));
				if (value.auxiliary) result["left_auxiliary"].push_back(compare(value.surfaces[0][output], value.surfaces[2][output]));
			}
			return result;
		}
	}

	bool enabled() noexcept { return opted_in.load(std::memory_order_acquire); }
	bool capturing() noexcept { return active_id.load(std::memory_order_acquire) != 0; }
	void install() noexcept
	{
		if (!debug_options::enabled(debug_options::probe::sun_shadow)) return;
		const auto publish_selection = gsl::finally([]
		{
			// Publish the immutable loader result before the support thread can
			// select this capture mode, including a failed optional installation.
			opted_in.store(true, std::memory_order_release);
			if (!installed) console::error("[VR] Sun-shadow observer unavailable: %s\n", installation_state);
		});
		// H2 0x6023ED08, traced from owner 0x1407A7E3F. Full source chain and
		// argument layouts are documented in docs/vr-sun-shadow-capture.md.
		const std::array<std::uint8_t, 12> sun_bytes{0x48,0x8B,0xC4,0x53,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x58};
		const std::array<std::array<std::uint8_t, 12>, 3> sub_bytes{{
			{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0xB8,0x10,0x34,0,0},
			{0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41},
			{0x41,0x54,0x41,0x55,0x41,0x57,0xB8,0x70,0x34,0,0,0xE8}}};
		const auto matches = [](std::uintptr_t address, const auto& expected)
		{
			return utils::hook_validation::validate_executable_target(reinterpret_cast<const void*>(address)) &&
				std::memcmp(reinterpret_cast<const void*>(address), expected.data(), expected.size()) == 0;
		};
		if (!matches(sun_address, sun_bytes)) { installation_state = "sun_entry_mismatch"; return; }
		for (unsigned i{}; i < sub_addresses.size(); ++i)
			if (!matches(sub_addresses[i], sub_bytes[i])) { installation_state = "subpass_entry_mismatch"; return; }
		try
		{
			const std::array<void*, 3> callbacks{reinterpret_cast<void*>(cache_stub), reinterpret_cast<void*>(sub_stub<1>), reinterpret_cast<void*>(sub_stub<2>)};
			for (unsigned i{}; i < sub_addresses.size(); ++i) sub_hooks[i].create(sub_addresses[i], callbacks[i]);
			sun_hook.create(sun_address, reinterpret_cast<void*>(sun_stub));
			installed = true; installation_state = "installed";
			console::info("[VR] Sun-shadow observation prepared; Ctrl+Shift+F8 collects 24 seconds. Both eyes still render all shadows.\n");
		}
		catch (const std::exception& error)
		{
			(void)sun_hook.clear(); for (auto& hook : sub_hooks) (void)hook.clear();
			installation_state = "hook_installation_failed";
			console::error("[VR] Sun-shadow observation unavailable: %s\n", error.what());
		}
	}
	std::uint64_t request()
	{
		auto value = std::make_shared<control>();
		value->id = ++serial; value->started = GetTickCount64(); value->deadline = value->started + duration_ms;
		current.store(value);
		engine_stereo_execution::set_invocation_observer(engine_stereo_execution::invocation_observer_channel::sun_shadow, invocation);
		engine_stereo_resource_ops::set_observer(engine_stereo_resource_ops::observer_channel::sun_shadow, resource_write);
		engine_stereo_output_merger::set_clear_observers(engine_stereo_output_merger::clear_observer_channel::sun_shadow, clear_color, clear_depth);
		active_id.store(value->id, std::memory_order_release);
		return value->id;
	}
	void begin_owner(const owner& active) noexcept
	{
		if (!active.production || active.failed || active.current_view >= view_count || active.owner_thread_id != GetCurrentThreadId() || !installed) return;
		if (retire_requested.exchange(false))
		{
			const std::unique_lock lock(gpu_mutex, std::try_to_lock);
			if (!lock.owns_lock()) retire_requested = true;
			else if (gpu.request && gpu.request->id != active_id.load()) gpu = {};
		}
		const auto id = active_id.load(std::memory_order_acquire);
		if (!id) return;
		try
		{
			const std::unique_lock lock(gpu_mutex, std::try_to_lock);
			if (!lock.owns_lock()) return;
			if (id != active_id.load(std::memory_order_acquire)) return;
			if (!gpu.request || gpu.request->id != id) { gpu = {}; gpu.request = current.load(); }
			if (!gpu.request || gpu.request->id != id) return;
			if (!gpu.pending && active.current_view == 0)
			{
				const auto count = gpu.request->started_samples.load();
				const auto now = GetTickCount64();
				if (count >= max_samples || now >= gpu.request->deadline || now < gpu.request->started + count * spacing_ms) return;
				const std::unique_lock publication(gpu.request->mutex, std::try_to_lock);
				if (!publication.owns_lock() || gpu.request->ready || gpu.request->processing) { ++gpu.request->producer_busy; return; }
				auto candidate = std::make_shared<sample>();
				auto& value = *candidate;
				value.request = id; value.pair = active.records.pair_id; value.generation = active.device_generation;
				value.tick = now; value.frontend = active.claim.frontend; value.context = reinterpret_cast<std::uintptr_t>(active.context.Get());
				value.thread = GetCurrentThreadId(); value.auxiliary = active.auxiliary.valid;
				for (auto& eye : value.eyes)
				{
					eye.subpasses.reserve(max_subpasses); eye.output_groups.reserve(64); eye.resource_writes.reserve(128);
				}
				gpu.pending = std::move(candidate);
				++gpu.request->started_samples;
			}
			if (!gpu.pending || gpu.pending->pair != active.records.pair_id) return;
			active_sample = gpu.pending; active_owner = &active;
			active_eye = &active_sample->eyes[active.current_view];
			auto& value = *active_eye;
			const auto* record = active.record(active.current_view);
			value.record = reinterpret_cast<std::uintptr_t>(record);
			value.sun_enabled = field<unsigned>(record, 0x200); value.transparent_enabled = field<unsigned>(record, 0x220);
			value.tile = field<unsigned>(record, 0x1FA0); value.configured = field<unsigned>(record, 0x1FA4);
			value.main_target = field<unsigned>(record, 0x1FA8); value.trans_target = field<unsigned>(record, 0x1FB0);
			value.cache_enabled = field<unsigned>(record, 0x2FB8);
		}
		catch (...)
		{
			end_owner();
			const auto request = current.load();
			if (request && request->id == id) ++request->allocation_failures;
		}
	}
	void end_owner() noexcept { active_pass = nullptr; active_eye = nullptr; active_owner = nullptr; active_sample.reset(); sun_depth = 0; }
	void geometry_view(const void* parameters, const void* primary, const void* rebase) noexcept
	{
		if (!active_pass || !sun_depth) return;
		if (active_pass->geometry_views++) { ++active_pass->dropped_views; return; }
		active_pass->parameters = reinterpret_cast<std::uintptr_t>(parameters);
		active_pass->primary = reinterpret_cast<std::uintptr_t>(primary); active_pass->rebase = reinterpret_cast<std::uintptr_t>(rebase);
		active_pass->primary_view = bytes_at<0x170>(primary);
	}
	void capture_eye(const owner& active) noexcept
	{
		if (!active_id.load(std::memory_order_acquire) || active.current_view >= view_count) return;
		try
		{
			const std::unique_lock lock(gpu_mutex, std::try_to_lock);
			if (!lock.owns_lock()) return;
			poll(active);
			if (!gpu.pending || gpu.pending->request != active_id || gpu.pending->pair != active.records.pair_id) return;
			for (unsigned output{}; output < 2; ++output) copy_surface(active, gpu.pending->surfaces[active.current_view][output], output);
			gpu.pending->captured_mask |= 1u << active.current_view;
		}
		catch (...)
		{
			const std::unique_lock lock(gpu_mutex, std::try_to_lock);
			if (lock.owns_lock() && gpu.pending) gpu.pending->error = "capture_allocation_failed";
		}
	}
	void end_pair(const std::uint64_t pair, const bool complete, const std::uint32_t completed_mask) noexcept
	{
		if (!active_id.load(std::memory_order_acquire)) return;
		const std::unique_lock lock(gpu_mutex, std::try_to_lock);
		if (!lock.owns_lock() || !gpu.pending || gpu.pending->pair != pair) return;
		gpu.pending->pair_closed = true; gpu.pending->pair_complete = complete;
		gpu.pending->completed_mask = completed_mask;
	}
	void drain(const std::uint64_t id, nlohmann::json& samples)
	{
		const auto value = current.load();
		if (!value || value->id != id) return;
		std::shared_ptr<sample> ready;
		{
			const std::lock_guard lock(value->mutex);
			if (!value->ready) return;
			value->processing = true; ready = std::move(value->ready);
		}
		const auto done = gsl::finally([&] { ready.reset(); value->processing = false; });
		samples.push_back(format(*ready));
	}
	void cancel(const std::uint64_t id) noexcept
	{
		auto expected = id;
		if (active_id.compare_exchange_strong(expected, 0))
		{
			engine_stereo_execution::set_invocation_observer(engine_stereo_execution::invocation_observer_channel::sun_shadow, nullptr);
			engine_stereo_resource_ops::set_observer(engine_stereo_resource_ops::observer_channel::sun_shadow, nullptr);
			engine_stereo_output_merger::set_clear_observers(engine_stereo_output_merger::clear_observer_channel::sun_shadow, nullptr, nullptr);
			retire_requested = true;
		}
	}
	nlohmann::json finish(const std::uint64_t id)
	{
		cancel(id);
		// A producer may have passed its request check just before cancellation.
		// Close that CPU publication before reporting totals; this never waits for GPU completion.
		const std::lock_guard producer(gpu_mutex);
		const auto value = current.load();
		nlohmann::json report{{"schema", 1}, {"request", id}, {"installation", installation_state}, {"reuse_enabled", false},
			{"max_samples", max_samples}, {"duration_ms", duration_ms}, {"spacing_ms", spacing_ms},
			{"memory_limit_bytes", memory_limit}, {"readback_bytes_per_pair", readback_budget},
			{"api_order", {"DrawIndexed", "Draw", "DrawIndexedInstanced", "DrawInstanced", "DrawAuto", "DrawIndexedInstancedIndirect", "DrawInstancedIndirect", "Dispatch", "DispatchIndirect", "ExecuteCommandList"}},
			{"subpass_kinds", {"cached", "main", "transparent"}},
			{"writer_phases", {"before_sun", "inside_sun", "after_sun"}},
			{"resource_write_operations", {"CopySubresourceRegion", "CopyResource", "UpdateSubresource", "CopyStructureCount", "ClearUAVUint", "ClearUAVFloat", "GenerateMips", "ResolveSubresource", "ClearRenderTarget", "ClearDepthStencil"}},
			{"limits", "CPU wall times include observation overhead; opaque command lists and unobserved writers require separate interpretation. Equal output alone does not authorize reuse."}};
		if (value && value->id == id)
		{
			report["started_tick"] = value->started; report["deadline_tick"] = value->deadline;
			report["started_samples"] = value->started_samples.load(); report["published_samples"] = value->published_samples.load();
			report["producer_busy"] = value->producer_busy.load();
			report["allocation_failures"] = value->allocation_failures.load();
		}
		return report;
	}
	void invalidate_device() noexcept
	{
		const std::lock_guard lock(gpu_mutex); gpu = {};
	}
	void stop() noexcept
	{
		active_id = 0;
		engine_stereo_execution::set_invocation_observer(engine_stereo_execution::invocation_observer_channel::sun_shadow, nullptr);
		engine_stereo_resource_ops::set_observer(engine_stereo_resource_ops::observer_channel::sun_shadow, nullptr);
		engine_stereo_output_merger::set_clear_observers(engine_stereo_output_merger::clear_observer_channel::sun_shadow, nullptr, nullptr);
		invalidate_device(); current.store(std::shared_ptr<control>{});
	}
}
