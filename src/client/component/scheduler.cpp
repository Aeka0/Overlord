#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "scheduler.hpp"
#include "game/game.hpp"
#include "component/d3d11.hpp"
#include "component/vr/engine_stereo_probe.hpp"
#include "component/vr/engine_stereo_renderer.hpp"
#include "component/vr/vr_runtime.hpp"

#include <utils/thread.hpp>
#include <utils/hook.hpp>
#include <utils/concurrency.hpp>

namespace scheduler
{
	namespace
	{
		struct task
		{
			std::function<bool()> handler{};
			std::chrono::milliseconds interval{};
			std::chrono::high_resolution_clock::time_point last_call{};
		};

		using task_list = std::vector<task>;

		class task_pipeline
		{
		public:
			void add(task&& task)
			{
				new_callbacks_.access([&task](task_list& tasks)
				{
					tasks.emplace_back(std::move(task));
				});
			}

			void execute()
			{
				callbacks_.access([&](task_list& tasks)
				{
					this->merge_callbacks();

					for (auto i = tasks.begin(); i != tasks.end();)
					{
						const auto now = std::chrono::high_resolution_clock::now();
						const auto diff = now - i->last_call;

						if (diff < i->interval)
						{
							++i;
							continue;
						}

						i->last_call = now;

						const auto res = i->handler();
						if (res == cond_end)
						{
							i = tasks.erase(i);
						}
						else
						{
							++i;
						}
					}
				});
			}

		private:
			utils::concurrency::container<task_list> new_callbacks_;
			utils::concurrency::container<task_list, std::recursive_mutex> callbacks_;

			void merge_callbacks()
			{
				callbacks_.access([&](task_list& tasks)
				{
					new_callbacks_.access([&](task_list& new_tasks)
					{
						tasks.insert(tasks.end(), std::move_iterator<task_list::iterator>(new_tasks.begin()), std::move_iterator<task_list::iterator>(new_tasks.end()));
						new_tasks = {};
					});
				});
			}
		};

		volatile bool kill = false;
		std::thread thread;
		task_pipeline pipelines[pipeline::count];
		utils::hook::detour r_end_frame_hook;
		utils::hook::detour g_run_frame_hook;
		utils::hook::detour main_frame_hook;
		utils::hook::detour hks_frame_hook;
		std::uint64_t vr_renderer_frame_index{};

		void execute(const pipeline type)
		{
			assert(type >= 0 && type < pipeline::count);
			const detail::execution_scope context(type);
			pipelines[type].execute();
		}

		vr::engine_stereo_probe::shared_camera_sample capture_camera_sample()
		{
			return {
				game::refdef->fovX,
				game::refdef->fovY,
				{game::refdef->org[0], game::refdef->org[1], game::refdef->org[2]},
				{{{game::refdef->axis[0][0], game::refdef->axis[0][1], game::refdef->axis[0][2]},
				  {game::refdef->axis[1][0], game::refdef->axis[1][1], game::refdef->axis[1][2]},
				  {game::refdef->axis[2][0], game::refdef->axis[2][1], game::refdef->axis[2][2]}}},
				{game::cgs->refdefViewAngles[0], game::cgs->refdefViewAngles[1], game::cgs->refdefViewAngles[2]},
			};
		}

		void frame_state_transition_stub()
		{
			vr::engine_stereo_renderer::begin_frame_state_transition();
			const auto boundary_scope = gsl::finally([]
			{
				vr::engine_stereo_renderer::end_frame_state_transition();
			});
			reinterpret_cast<void(*)()>(0x1403E1640)();
		}

		void frontend_handoff_stub()
		{
			vr::engine_stereo_renderer::begin_frontend_handoff();
			const auto boundary_scope = gsl::finally([]
			{
				vr::engine_stereo_renderer::end_frontend_handoff();
			});
			reinterpret_cast<void(*)()>(0x14076DFA0)();
		}

		void r_end_frame_stub()
		{
			vr::engine_stereo_renderer::begin_r_end_frame();
			const auto r_end_frame_scope = gsl::finally([]
			{
				vr::engine_stereo_renderer::end_r_end_frame();
			});
			// This hook runs after H2 has built the current frontend scene records and
			// immediately before the original R_EndFrame. Runtime/capture maintenance
			// here therefore cannot define the pose for those already-built records.
			// A native eye family must sample and freeze its pose at the proven per-view
			// owner instead of treating this boundary as pre-scene.
			auto& vr_runtime = vr::runtime::get();
			if (vr_runtime.applied_enabled())
			{
				if (const auto graphics = d3d11::get_device_snapshot(); graphics)
				{
					// D3D11 immediate-context queries must remain on the renderer
					// thread that produced the native eye copy.  Polling from Present
					// can race CopyResource/End on a game-created single-threaded
					// device and surface later as an NVIDIA driver fault in Present.
					vr_runtime.poll_capture(graphics);
					vr_runtime.prepare_frame(graphics, ++vr_renderer_frame_index);
				}
			}

			const bool probe_active = vr::engine_stereo_probe::is_active();
			if (!probe_active)
			{
				execute(pipeline::renderer);
				r_end_frame_hook.invoke<void>();
				return;
			}

			vr::engine_stereo_probe::renderer_frame_token probe_frame{};
			if (probe_active)
			{
				probe_frame = vr::engine_stereo_probe::begin_renderer_frame(capture_camera_sample());
				vr::engine_stereo_probe::record_renderer_boundary(probe_frame,
					vr::engine_stereo_probe::boundary_phase::before_original);
			}

			execute(pipeline::renderer);
			r_end_frame_hook.invoke<void>();
			vr::engine_stereo_probe::end_renderer_frame(probe_frame);
		}

		void server_frame_stub()
		{
			g_run_frame_hook.invoke<void>();
			execute(pipeline::server);
		}

		void main_frame_stub()
		{
			main_frame_hook.invoke<void>();
			execute(pipeline::main);
		}

		void hks_frame_stub()
		{
			const auto state = *game::hks::lua_state;
			if (state)
			{
				execute(pipeline::lui);
			}
			hks_frame_hook.invoke<void>();
		}
	}

	void schedule(const std::function<bool()>& callback, const pipeline type,
	              const std::chrono::milliseconds delay)
	{
		assert(type >= 0 && type < pipeline::count);

		task task;
		task.handler = callback;
		task.interval = delay;
		task.last_call = std::chrono::high_resolution_clock::now();

		pipelines[type].add(std::move(task));
	}

	void loop(const std::function<void()>& callback, const pipeline type,
	          const std::chrono::milliseconds delay)
	{
		schedule([callback]()
		{
			callback();
			return cond_continue;
		}, type, delay);
	}

	void once(const std::function<void()>& callback, const pipeline type,
	          const std::chrono::milliseconds delay)
	{
		schedule([callback]()
		{
			callback();
			return cond_end;
		}, type, delay);
	}

	void on_game_initialized(const std::function<void()>& callback, const pipeline type,
		const std::chrono::milliseconds delay)
	{
		schedule([=]()
		{
			if (game::Sys_IsDatabaseReady2())
			{
				once(callback, type, delay);
				return cond_end;
			}

			return cond_continue;
		}, pipeline::main);
	}

	class component final : public component_interface
	{
	public:
		void post_start() override
		{
			thread = utils::thread::create_named_thread("Async Scheduler", []()
			{
				while (!kill)
				{
					execute(pipeline::async);
					std::this_thread::sleep_for(10ms);
				}
			});
		}

		void post_unpack() override
		{
			// Recovered top-level order at 0x1403D8D0A is frame-state transition,
			// command cleanup, then frontend handoff. Keep all three as passive
			// ownership observations; none of these hooks may synthesize a scene.
			utils::hook::call(0x1403D8D0A,
				scheduler::frame_state_transition_stub);
			r_end_frame_hook.create(0x14076D7B0, scheduler::r_end_frame_stub);
			utils::hook::call(0x1403D8D14, scheduler::frontend_handoff_stub);
			g_run_frame_hook.create(0x1404CB030, scheduler::server_frame_stub);
			main_frame_hook.create(0x140417FA0, scheduler::main_frame_stub);
			hks_frame_hook.create(0x140327880, scheduler::hks_frame_stub);
		}

		void pre_destroy() override
		{
			kill = true;
			if (thread.joinable())
			{
				thread.join();
			}
		}
	};
}

REGISTER_COMPONENT(scheduler::component)
