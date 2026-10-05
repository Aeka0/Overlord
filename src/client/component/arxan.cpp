#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "game_module.hpp"
#include "scheduler.hpp"
#include "console.hpp"

#include <utils/nt.hpp>

namespace arxan
{
	namespace
	{
		enum dbg_funcs_e
		{
			DbgBreakPoint,
			DbgUserBreakPoint,
			DbgUiConnectToDbg,
			DbgUiContinue,
			DbgUiConvertStateChangeStructure,
			DbgUiDebugActiveProcess,
			DbgUiGetThreadDebugObject,
			DbgUiIssueRemoteBreakin,
			DbgUiRemoteBreakin,
			DbgUiSetThreadDebugObject,
			DbgUiStopDebugging,
			DbgUiWaitStateChange,
			DbgPrintReturnControlC,
			DbgPrompt,
			DBG_FUNCS_COUNT,
		};

		const char* dbg_funcs_names[] =
		{
			"DbgBreakPoint",
			"DbgUserBreakPoint",
			"DbgUiConnectToDbg",
			"DbgUiContinue",
			"DbgUiConvertStateChangeStructure",
			"DbgUiDebugActiveProcess",
			"DbgUiGetThreadDebugObject",
			"DbgUiIssueRemoteBreakin",
			"DbgUiRemoteBreakin",
			"DbgUiSetThreadDebugObject",
			"DbgUiStopDebugging",
			"DbgUiWaitStateChange",
			"DbgPrintReturnControlC",
			"DbgPrompt",
		};

		struct dbg_func_bytes_s
		{
			std::uint8_t buffer[15];
		};

		dbg_func_bytes_s dbg_func_bytes[DBG_FUNCS_COUNT];
		void* dbg_func_procs[DBG_FUNCS_COUNT]{};
		std::atomic_bool monitor_disabled{};
		int initialization_failure_index{-1};

		bool read_debug_function(const void* const address, dbg_func_bytes_s& bytes) noexcept
		{
			SIZE_T bytes_read{};
			return address != nullptr &&
				ReadProcessMemory(GetCurrentProcess(), address, bytes.buffer,
					sizeof(bytes.buffer), &bytes_read) != FALSE &&
				bytes_read == sizeof(bytes.buffer);
		}

		bool store_debug_functions()
		{
			const utils::nt::library ntdll("ntdll.dll");

			for (auto i = 0; i < DBG_FUNCS_COUNT; i++)
			{
				dbg_func_procs[i] = ntdll.get_proc<void*>(dbg_funcs_names[i]);
				if (!read_debug_function(dbg_func_procs[i], dbg_func_bytes[i]))
				{
					initialization_failure_index = i;
					return false;
				}
			}

			return true;
		}

		void monitor_debug_functions()
		{
			if (monitor_disabled.load(std::memory_order_acquire))
			{
				return;
			}

			for (auto i = 0; i < DBG_FUNCS_COUNT; i++)
			{
				dbg_func_bytes_s observed{};
				if (!read_debug_function(dbg_func_procs[i], observed))
				{
					if (!monitor_disabled.exchange(true, std::memory_order_acq_rel))
					{
						console::error("[ARXAN] stopped debug-function monitoring at %s: read failed\n",
							dbg_funcs_names[i]);
					}
					return;
				}

				if (std::memcmp(observed.buffer, dbg_func_bytes[i].buffer,
					sizeof(observed.buffer)) != 0)
				{
					if (!monitor_disabled.exchange(true, std::memory_order_acq_rel))
					{
						console::error("[ARXAN] %s changed; unsafe live-code restoration refused\n",
							dbg_funcs_names[i]);
					}
					return;
				}
			}
		}
	}

	class component final : public component_interface
	{
	public:

		void post_load() override
		{
			monitor_disabled.store(!store_debug_functions(), std::memory_order_release);
		}

		void post_unpack() override
		{
			if (monitor_disabled.load(std::memory_order_acquire))
			{
				const auto name = initialization_failure_index >= 0 &&
					initialization_failure_index < DBG_FUNCS_COUNT
					? dbg_funcs_names[initialization_failure_index]
					: "unknown";
				console::error("[ARXAN] debug-function monitoring unavailable at %s\n", name);
				return;
			}

			// Never rewrite live ntdll entry points from the scheduler. Other threads
			// can execute a partially written instruction even while the page is RWX.
			scheduler::loop(monitor_debug_functions, scheduler::async, 1s);
		}
	};
}

REGISTER_COMPONENT(arxan::component)
