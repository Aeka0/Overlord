#include <std_include.hpp>
#include "region_capture_queue.hpp"
#include "debug_options.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scene_models.hpp"
#include "component/scene_surface_layout.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace vr::region_capture
{
	namespace
	{
		queue<16384> pending;
		std::atomic_bool shutdown{}, requested_stop{};
		// 0=idle, 1=start requested, 2=recording, 3=finishing.
		std::atomic_uint32_t state{};
		std::atomic_uint32_t requested_seconds{600};
		std::mutex wake_mutex;
		std::condition_variable wake;
		constexpr std::uint64_t byte_limit = 128ull * 1024 * 1024;
		row stamp(row value) noexcept
		{
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			value.tick = now.QuadPart;
			value.thread = GetCurrentThreadId();
			return value;
		}
		void enqueue(row value) noexcept { pending.push(stamp(value)); }
		std::atomic_uint64_t rigid_sequence{};
		void observe_surface_budget(const scene_models::scene_observation& value) noexcept
		{
			if (!enabled()) return;
			const auto frontend=read<std::uintptr_t>(reinterpret_cast<const std::uint8_t*>(0x150F91188));
			if (!frontend) return;
			const auto* frame=reinterpret_cast<const std::uint8_t*>(frontend);
			emit({0,0,static_cast<std::uint64_t>(kind::surface_budget),value.sequence,value.stage,
				frontend,read<std::uint32_t>(frame+0x540e14),
				read<std::uint32_t>(reinterpret_cast<const std::uint8_t*>(0x1513dd9b4)),value.index,
				value.scratch ? read<std::uint32_t>(static_cast<const std::uint8_t*>(value.scratch)+0x524) : UINT32_MAX,
				reinterpret_cast<std::uintptr_t>(value.slot)});
		}
		void observe_rigid_model(const scene_models::build_observation& value) noexcept
		{
			if (!enabled() || !value.record || !value.entry || !value.submitted || !value.resolved) return;
			const auto id=rigid_sequence.fetch_add(1,std::memory_order_relaxed)+1;
			const auto* record=static_cast<const std::uint8_t*>(value.record);
			const auto* entry=static_cast<const std::uint8_t*>(value.entry);
			const auto frontend=read<std::uintptr_t>(reinterpret_cast<const std::uint8_t*>(0x150F91188));
			const auto outcome=(std::uint64_t{static_cast<unsigned>(value.outcome)}<<32) |
				static_cast<std::uint32_t>(value.native_result);
			emit({0,0,static_cast<std::uint64_t>(kind::rigid_model),id,0,
				reinterpret_cast<std::uintptr_t>(value.record),reinterpret_cast<std::uintptr_t>(value.entry),
				reinterpret_cast<std::uintptr_t>(value.model),read<std::uint64_t>(entry+0x68),outcome,
				reinterpret_cast<std::uintptr_t>(value.owner)});
			// Record metadata is read while that exact preparation command owns it.
			// Draw-info on omitted calls can be stale: outcome disambiguates it.
			emit({0,0,static_cast<std::uint64_t>(kind::rigid_context),id,0,
				frontend,
				read<std::uint32_t>(record+0x1f78),read<std::uint32_t>(record+0x1f10),
				read<std::uint32_t>(entry+0x5c),read<std::uint32_t>(entry),
					hash(record+0x100,12*sizeof(float))});
			// Stage 3 records the arena cursor immediately after this owned model's
			// result. No allocator changes; overflow remains a native return value.
			if (frontend) emit({0,0,static_cast<std::uint64_t>(kind::surface_budget),id,3,
				frontend,read<std::uint32_t>(reinterpret_cast<const std::uint8_t*>(frontend)+0x540e14),
				0,read<std::uint32_t>(record+0x1f78),outcome,reinterpret_cast<std::uintptr_t>(value.entry)});
			for(unsigned stage=0;stage<2;++stage)
			{
				const auto* placement=reinterpret_cast<const std::uint8_t*>(stage ? value.resolved : value.submitted);
				emit({0,0,static_cast<std::uint64_t>(kind::rigid_placement),id,stage,
					read<std::uint32_t>(placement+0x10),read<std::uint32_t>(placement+0x14),read<std::uint32_t>(placement+0x18),
					read<std::uint32_t>(record+0x190),read<std::uint32_t>(record+0x194),read<std::uint32_t>(record+0x198)});
			}
		}

		void run_session()
		{
			std::filesystem::create_directories("minidumps/vr-region-capture");
			const auto unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			const auto path = "minidumps/vr-region-capture/region-" +
				std::to_string(unix_ms) + "-p" + std::to_string(GetCurrentProcessId()) + ".bin";
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output) throw std::runtime_error("cannot open region capture file");
			LARGE_INTEGER frequency;
			QueryPerformanceFrequency(&frequency);
			const std::array<std::uint64_t, 3> header{
				static_cast<std::uint64_t>(frequency.QuadPart), GetCurrentProcessId(),
				static_cast<std::uint64_t>(unix_ms)};
			output.write("H2VREG01", 8);
			output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
			const auto layout=stamp({0,0,static_cast<std::uint64_t>(kind::surface_layout),0,0,
				scene_surface_storage::capacity,scene_surface_storage::index_unit,
				scene_surface_storage::hidden_bytes,scene_surface_storage::brush_prefix_bytes});
			output.write(reinterpret_cast<const char*>(&layout),sizeof(layout));
			std::array<row, 256> batch;
			while (pending.pop(batch.data(), batch.size()) != 0) {}
			const auto dropped_start = pending.dropped.load();
			std::uint64_t bytes = 32+sizeof(layout);
			const auto start = std::chrono::steady_clock::now();
			const auto duration=std::chrono::seconds(requested_seconds.load());
			state.store(2);
			sink.store(enqueue, std::memory_order_release);
			if (debug_options::enabled(debug_options::probe::scene_models))
			{
				scene_models::observe_builds(observe_rigid_model);
				scene_models::observe_scenes(observe_surface_budget);
			}
			const auto stop_observing=gsl::finally([] {
				scene_models::observe_builds(nullptr);scene_models::observe_scenes(nullptr);
			});
			console::info("[VR] region capture started: %s (CPU only, %lld seconds / 128 MiB maximum)\n", path.c_str(),duration.count());
			std::uint64_t reason{};
			for (;;)
			{
				const auto elapsed = std::chrono::steady_clock::now() - start;
				if (shutdown.load() || requested_stop.load()) reason = 1;
				else if (elapsed >= duration) reason = 2;
				else if (bytes + sizeof(batch) + sizeof(row) >= byte_limit) reason = 3;
				if (reason) { sink.store(nullptr, std::memory_order_release); state.store(3); }
				// A bounded drain prevents a high producer rate from starving stop/time checks.
				for (unsigned i{}; i < 64 && bytes + sizeof(batch) + sizeof(row) < byte_limit; ++i)
				{
					const auto count = pending.pop(batch.data(), batch.size());
					if (!count) break;
					output.write(reinterpret_cast<const char*>(batch.data()), count * sizeof(row));
					bytes += count * sizeof(row);
				}
				const auto health = stamp({0, 0, static_cast<std::uint64_t>(
					reason ? kind::stopped : kind::heartbeat), 0, 0,
					pending.dropped.load() - dropped_start, bytes, reason});
				output.write(reinterpret_cast<const char*>(&health), sizeof(health));
				bytes += sizeof(health);
				output.flush();
				if (!output) throw std::runtime_error("region capture write/flush failed");
				if (reason) break;
				std::unique_lock lock(wake_mutex);
				wake.wait_for(lock, std::chrono::milliseconds(200), [] { return shutdown.load() || requested_stop.load(); });
			}
			console::info("[VR] region capture saved: %s; dropped=%llu stop_reason=%llu\n",
				path.c_str(), pending.dropped.load() - dropped_start, reason);
		}
		void worker() noexcept
		{
			while (!shutdown.load())
			{
				{
					std::unique_lock lock(wake_mutex);
					wake.wait(lock, [] { return shutdown.load() || state.load() == 1; });
				}
				if (shutdown.load()) break;
				try { run_session(); }
				catch (const std::exception& error)
				{
					sink.store(nullptr, std::memory_order_release);
					console::error("[VR] region capture stopped: %s; game rendering unchanged\n", error.what());
				}
				state.store(0);
			}
		}
	}
	class component final : public component_interface
	{
		std::thread writer;
	public:
		void post_unpack() override
		{
			if (!debug_options::enabled(debug_options::probe::perf) &&
				!debug_options::enabled(debug_options::probe::scene_models))
			{
				command::add("vr_perfStart", []
				{
					console::info("[VR] Enable CPU performance capture or Scene surface diagnostics in VR Settings > Debug, then restart the game\n");
				});
				return;
			}
			writer = std::thread(worker);
			command::add("vr_perfStart", [](const command::params& args)
			{
				unsigned seconds=600;
				if (args.size()>2) {console::info("[VR] usage: vr_perfStart [seconds: 1..600]\n");return;}
				if (args.size()==2)
				{
					const std::string text=args[1];
					if (text.empty() || text.size()>3 || !std::all_of(text.begin(),text.end(),[](char c){return c>='0' && c<='9';}))
					{console::info("[VR] capture seconds must be an integer from 1 to 600\n");return;}
					seconds=static_cast<unsigned>(std::stoul(text));
					if (!seconds || seconds>600) {console::info("[VR] capture seconds must be from 1 to 600\n");return;}
				}
				const std::lock_guard lock(wake_mutex);
				std::uint32_t expected{};
				if (!state.compare_exchange_strong(expected, 1))
				{ console::info("[VR] region capture already requested/running\n"); return; }
				requested_stop.store(false);
				requested_seconds.store(seconds);
				wake.notify_one();
			});
			command::add("vr_perfStop", [] { requested_stop.store(true); wake.notify_one(); });
			command::add("vr_perfMark", [](const command::params& args)
			{
				std::uint64_t label{};
				if (args.size() == 2)
				{
					const std::string value = args[1];
					if (value == "normal") label = 1;
					else if (value == "slow") label = 2;
					else if (value == "visual") label = 3;
				}
				if (!label || !enabled())
				{ console::info("[VR] start with vr_perfStart; mark with vr_perfMark normal|slow|visual\n"); return; }
				event(kind::mark, 0, 0, label);
				console::info("[VR] region marker=%llu; earlier frames already recorded\n", label);
			});
		}
		void pre_destroy() override
		{
			sink.store(nullptr, std::memory_order_release);
			shutdown.store(true);
			wake.notify_one();
			if (writer.joinable()) writer.join();
		}
	};
}
REGISTER_COMPONENT(vr::region_capture::component)
