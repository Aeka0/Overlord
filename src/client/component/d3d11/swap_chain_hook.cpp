#include <std_include.hpp>

#include "swap_chain_hook.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

#include <shared_mutex>

#pragma comment(lib, "dxgi.lib")

namespace d3d11::swap_chain_hook
{
	namespace
	{
		using present_function = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
		using resize_buffers_function = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

		constexpr std::size_t present_vtable_slot = 8;
		constexpr std::size_t resize_buffers_vtable_slot = 13;

		std::shared_mutex hook_lifecycle_mutex;
		std::mutex hook_mutex;
		utils::hook::detour present_hook;
		utils::hook::detour resize_buffers_hook;
		void* present_target{};
		void* resize_buffers_target{};
		IDXGISwapChain* active_swap_chain{};
		std::uint64_t active_generation{};
		std::uint64_t active_swap_chain_epoch{};
		std::uint64_t next_swap_chain_epoch{};
		std::recursive_mutex callback_mutex;
		std::atomic_bool shutting_down{};
		std::atomic_bool service_shutdown{};
		std::atomic_bool shutdown_pending{};
		std::atomic_uint64_t next_frame_index{};
		std::atomic_uint64_t next_resize_index{};
		std::atomic_uint32_t active_callback_count{};
		std::mutex pending_install_mutex;
		std::optional<device_snapshot> pending_install;
		thread_local unsigned int hook_lifecycle_depth{};
		thread_local bool inside_present{};
		thread_local bool inside_resize_buffers{};

		class scoped_hook_lifecycle_read
		{
		public:
			scoped_hook_lifecycle_read()
			{
				if (hook_lifecycle_depth == 0)
				{
					this->lock_.emplace(hook_lifecycle_mutex);
				}

				++hook_lifecycle_depth;
			}

			~scoped_hook_lifecycle_read()
			{
				--hook_lifecycle_depth;
			}

			scoped_hook_lifecycle_read(const scoped_hook_lifecycle_read&) = delete;
			scoped_hook_lifecycle_read& operator=(const scoped_hook_lifecycle_read&) = delete;

		private:
			std::optional<std::shared_lock<std::shared_mutex>> lock_;
		};

		class scoped_callback_activity
		{
		public:
			scoped_callback_activity()
			{
				active_callback_count.fetch_add(1, std::memory_order_acq_rel);
			}

			~scoped_callback_activity()
			{
				this->reset();
			}

			void reset() noexcept
			{
				if (active_)
				{
					active_ = false;
					if (active_callback_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
					{
						detail::process_deferred_graphics_shutdown();
					}
				}
			}

			scoped_callback_activity(const scoped_callback_activity&) = delete;
			scoped_callback_activity& operator=(const scoped_callback_activity&) = delete;

		private:
			bool active_{true};
		};

		class scoped_reentrancy_guard
		{
		public:
			explicit scoped_reentrancy_guard(bool& flag) : flag_(flag)
			{
				flag_ = true;
			}

			~scoped_reentrancy_guard()
			{
				flag_ = false;
			}

			scoped_reentrancy_guard(const scoped_reentrancy_guard&) = delete;
			scoped_reentrancy_guard& operator=(const scoped_reentrancy_guard&) = delete;

		private:
			bool& flag_;
		};

		bool same_identity(IUnknown* left, IUnknown* right)
		{
			if (left == nullptr || right == nullptr)
			{
				return false;
			}

			Microsoft::WRL::ComPtr<IUnknown> left_identity;
			Microsoft::WRL::ComPtr<IUnknown> right_identity;
			return SUCCEEDED(left->QueryInterface(IID_PPV_ARGS(&left_identity))) &&
				SUCCEEDED(right->QueryInterface(IID_PPV_ARGS(&right_identity))) &&
				left_identity.Get() == right_identity.Get();
		}

		bool validate_swap_chain(IDXGISwapChain* swap_chain, const device_snapshot& graphics,
			DXGI_SWAP_CHAIN_DESC& description)
		{
			if (swap_chain == nullptr || !graphics)
			{
				return false;
			}

			if (FAILED(swap_chain->GetDesc(&description)) || game::hWnd == nullptr ||
				description.OutputWindow != *game::hWnd ||
				(description.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT) == 0)
			{
				return false;
			}

			Microsoft::WRL::ComPtr<ID3D11Device> swap_chain_device;
			return SUCCEEDED(swap_chain->GetDevice(IID_PPV_ARGS(&swap_chain_device))) &&
				same_identity(swap_chain_device.Get(), graphics.device.Get());
		}

		constexpr GUID swap_chain_epoch_guid = {0xb2f526b7, 0x9681, 0x4d5d, {0x9e, 0xa4, 0x94, 0xba, 0xd5, 0xaf, 0xb1, 0xd8}};

		enum class swap_chain_selection
		{
			rejected,
			active,
			candidate,
		};

		bool read_swap_chain_epoch(IDXGISwapChain* const swap_chain, std::uint64_t& epoch)
		{
			UINT size = static_cast<UINT>(sizeof(epoch));
			return SUCCEEDED(swap_chain->GetPrivateData(swap_chain_epoch_guid, &size, &epoch)) &&
				size == sizeof(epoch);
		}

		swap_chain_selection inspect_swap_chain(IDXGISwapChain* const swap_chain, device_snapshot& graphics,
			DXGI_SWAP_CHAIN_DESC& description)
		{
			if (swap_chain == nullptr || shutting_down.load(std::memory_order_acquire))
			{
				return swap_chain_selection::rejected;
			}

			Microsoft::WRL::ComPtr<ID3D11Device> swap_chain_device;
			if (FAILED(swap_chain->GetDesc(&description)) || game::hWnd == nullptr ||
				description.OutputWindow != *game::hWnd ||
				(description.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT) == 0 ||
				FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&swap_chain_device))))
			{
				return swap_chain_selection::rejected;
			}

			graphics = detail::inspect_candidate_device(swap_chain_device.Get());
			if (!graphics || !validate_swap_chain(swap_chain, graphics, description))
			{
				return swap_chain_selection::rejected;
			}

			const std::lock_guard lock(hook_mutex);
			if (active_swap_chain == nullptr || active_generation == 0 || graphics.generation > active_generation)
			{
				return swap_chain_selection::candidate;
			}
			if (graphics.generation != active_generation)
			{
				return swap_chain_selection::rejected;
			}
			std::uint64_t epoch{};
			if (active_swap_chain == swap_chain && read_swap_chain_epoch(swap_chain, epoch) &&
				epoch == active_swap_chain_epoch)
			{
				return swap_chain_selection::active;
			}
			if (read_swap_chain_epoch(swap_chain, epoch) && epoch <= active_swap_chain_epoch)
			{
				return swap_chain_selection::rejected;
			}
			return swap_chain_selection::candidate;
		}

		bool commit_active_swap_chain(IDXGISwapChain* const swap_chain, ID3D11Device* const device,
			const DXGI_SWAP_CHAIN_DESC& description, device_snapshot& graphics)
		{
			const auto epoch = ++next_swap_chain_epoch;
			if (FAILED(swap_chain->SetPrivateData(swap_chain_epoch_guid,
				static_cast<UINT>(sizeof(epoch)), &epoch)))
			{
				return false;
			}

			graphics = detail::promote_candidate_device(device);
			if (!graphics)
			{
				return false;
			}
			{
				const std::lock_guard lock(hook_mutex);
				if (shutting_down.load(std::memory_order_acquire))
				{
					return false;
				}
				active_generation = graphics.generation;
				active_swap_chain = swap_chain;
				active_swap_chain_epoch = epoch;
			}
			detail::set_active_swap_chain(description, graphics.generation);
			return true;
		}

		bool select_active_swap_chain_for_resize(IDXGISwapChain* const swap_chain, device_snapshot& graphics)
		{
			graphics = get_device_snapshot();
			DXGI_SWAP_CHAIN_DESC description{};
			if (!validate_swap_chain(swap_chain, graphics, description))
			{
				return false;
			}

			const std::lock_guard lock(hook_mutex);
			std::uint64_t epoch{};
			return active_swap_chain == swap_chain && active_generation == graphics.generation &&
				read_swap_chain_epoch(swap_chain, epoch) && epoch == active_swap_chain_epoch;
		}

		bool consume_pending_install(device_snapshot& graphics)
		{
			const std::lock_guard lock(pending_install_mutex);
			if (!pending_install)
			{
				return false;
			}

			graphics = std::move(*pending_install);
			pending_install.reset();
			return true;
		}

		void run_pending_install() noexcept;

		HRESULT __stdcall present_stub(IDXGISwapChain* swap_chain, const UINT sync_interval, const UINT flags)
		{
			scoped_callback_activity callback_activity;
			std::optional<scoped_hook_lifecycle_read> lifecycle_lock;
			lifecycle_lock.emplace();
			std::unique_lock callback_lock(callback_mutex);
			const auto trampoline = reinterpret_cast<present_function>(present_hook.get_original());
			if (trampoline == nullptr)
			{
				return E_FAIL;
			}
			const auto call_present = [&]
			{
				return trampoline(swap_chain, sync_interval, flags);
			};

			if (inside_present)
			{
				return call_present();
			}

			std::optional<scoped_reentrancy_guard> guard;
			guard.emplace(inside_present);
			const auto finish_callback = [&]
			{
				guard.reset();
				callback_lock.unlock();
				lifecycle_lock.reset();
				callback_activity.reset();
				run_pending_install();
			};

			device_snapshot graphics;
			DXGI_SWAP_CHAIN_DESC description{};
			const auto selection = inspect_swap_chain(swap_chain, graphics, description);
			if (selection == swap_chain_selection::rejected ||
				(selection == swap_chain_selection::active && (flags & DXGI_PRESENT_TEST) != 0))
			{
				const auto result = call_present();
				finish_callback();
				return result;
			}
			if (selection == swap_chain_selection::candidate)
			{
				Microsoft::WRL::ComPtr<ID3D11Device> device;
				if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device))))
				{
					const auto result = call_present();
					finish_callback();
					return result;
				}

				const auto result = call_present();
				if (SUCCEEDED(result) && (flags & DXGI_PRESENT_TEST) == 0)
				{
					commit_active_swap_chain(swap_chain, device.Get(), description, graphics);
				}
				finish_callback();
				return result;
			}

			const auto started = std::chrono::steady_clock::now();
			const present_event event{
				swap_chain,
				graphics,
				next_frame_index.fetch_add(1, std::memory_order_relaxed) + 1,
				started,
				sync_interval,
				flags,
			};
			detail::record_present_begin(graphics.generation, event.frame_index,
				started, GetCurrentThreadId());

			detail::notify_present_pre(event);
			const auto result = trampoline(swap_chain, sync_interval, flags);
			const auto completed = std::chrono::steady_clock::now();
			if (FAILED(result) && graphics.device != nullptr)
			{
				detail::record_device_removed_reason(graphics.generation,
					graphics.device->GetDeviceRemovedReason());
			}
			detail::record_present_result(graphics.generation, result, started, completed);
			detail::notify_present_post(event, result);
			finish_callback();
			return result;
		}

		HRESULT __stdcall resize_buffers_stub(IDXGISwapChain* swap_chain, const UINT buffer_count,
			const UINT width, const UINT height, const DXGI_FORMAT new_format, const UINT swap_chain_flags)
		{
			scoped_callback_activity callback_activity;
			std::optional<scoped_hook_lifecycle_read> lifecycle_lock;
			lifecycle_lock.emplace();
			std::unique_lock callback_lock(callback_mutex);
			const auto trampoline = reinterpret_cast<resize_buffers_function>(resize_buffers_hook.get_original());
			if (trampoline == nullptr)
			{
				return E_FAIL;
			}
			const auto call_resize = [&]
			{
				return trampoline(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
			};

			if (inside_resize_buffers)
			{
				return call_resize();
			}

			std::optional<scoped_reentrancy_guard> guard;
			guard.emplace(inside_resize_buffers);
			const auto finish_callback = [&]
			{
				guard.reset();
				callback_lock.unlock();
				lifecycle_lock.reset();
				callback_activity.reset();
				run_pending_install();
			};

			device_snapshot graphics;
			if (!select_active_swap_chain_for_resize(swap_chain, graphics))
			{
				const auto result = call_resize();
				finish_callback();
				return result;
			}

			const auto started = std::chrono::steady_clock::now();
			const resize_event event{
				swap_chain,
				graphics,
				buffer_count,
				width,
				height,
				new_format,
				swap_chain_flags,
				next_resize_index.fetch_add(1, std::memory_order_relaxed) + 1,
				started,
			};

			detail::notify_resize_before(event);
			const auto result = trampoline(swap_chain, buffer_count, width, height, new_format,
				swap_chain_flags);
			const auto completed = std::chrono::steady_clock::now();
			detail::record_resize_result(graphics.generation, result, started, completed);

			if (SUCCEEDED(result))
			{
				DXGI_SWAP_CHAIN_DESC description{};
				if (SUCCEEDED(swap_chain->GetDesc(&description)))
				{
					detail::set_active_swap_chain(description, graphics.generation);
				}
			}

			finish_callback();
			return result;
		}

		bool discover_targets(const device_snapshot& graphics, void*& discovered_present,
			void*& discovered_resize_buffers)
		{
			Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
			Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
			Microsoft::WRL::ComPtr<IDXGIFactory> factory;
			if (FAILED(graphics.device.As(&dxgi_device)) ||
				FAILED(dxgi_device->GetAdapter(&adapter)) ||
				FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
			{
				return false;
			}

			const auto dummy_window = CreateWindowExW(0, L"STATIC", L"h2-mod DXGI probe", WS_POPUP,
				0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			if (dummy_window == nullptr)
			{
				return false;
			}

			DXGI_SWAP_CHAIN_DESC description{};
			description.BufferDesc.Width = 1;
			description.BufferDesc.Height = 1;
			description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			description.BufferCount = 1;
			description.OutputWindow = dummy_window;
			description.Windowed = TRUE;
			description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

			Microsoft::WRL::ComPtr<IDXGISwapChain> dummy_swap_chain;
			const auto result = factory->CreateSwapChain(graphics.device.Get(), &description, &dummy_swap_chain);
			if (FAILED(result))
			{
				DestroyWindow(dummy_window);
				return false;
			}

			auto* const vtable = *reinterpret_cast<void***>(dummy_swap_chain.Get());
			discovered_present = vtable[present_vtable_slot];
			discovered_resize_buffers = vtable[resize_buffers_vtable_slot];
			const auto present_validation = utils::hook_validation::validate_executable_target(discovered_present);
			const auto resize_validation = utils::hook_validation::validate_executable_target(discovered_resize_buffers);
			dummy_swap_chain.Reset();
			DestroyWindow(dummy_window);
			return static_cast<bool>(present_validation) && static_cast<bool>(resize_validation);
		}

		void run_pending_install() noexcept
		{
			if (shutdown_pending.exchange(false, std::memory_order_acq_rel))
			{
				shutdown();
				return;
			}

			device_snapshot graphics;
			if (!consume_pending_install(graphics))
			{
				return;
			}

			try
			{
				install(graphics);
			}
			catch (const std::exception& error)
			{
				detail::set_graphics_hook_status(false, false, false, error.what());
			}
			catch (...)
			{
				detail::set_graphics_hook_status(false, false, false,
					"Unknown exception while processing a deferred DXGI hook install");
			}
		}
	}

	bool is_inside_callback() noexcept
	{
		return inside_present || inside_resize_buffers;
	}

	bool has_active_callbacks() noexcept
	{
		return active_callback_count.load(std::memory_order_acquire) != 0;
	}

	void begin_shutdown() noexcept
	{
		const std::lock_guard lock(hook_mutex);
		service_shutdown.store(true, std::memory_order_release);
		shutting_down.store(true, std::memory_order_release);
	}

	bool install(const device_snapshot& graphics)
	{
		if (is_inside_callback() || has_active_callbacks())
		{
			{
				const std::lock_guard lock(pending_install_mutex);
				pending_install = graphics;
			}

			bool present_installed{};
			bool resize_installed{};
			{
				const std::lock_guard lock(hook_mutex);
				present_installed = present_hook.is_enabled();
				resize_installed = resize_buffers_hook.is_enabled();
			}
			detail::set_graphics_hook_status(present_installed, resize_installed, false,
				"DXGI swap-chain hook install deferred until the current callback completes");
			return false;
		}

		const std::unique_lock lifecycle_lock(hook_lifecycle_mutex);
		if (service_shutdown.load(std::memory_order_acquire) || shutdown_pending.load(std::memory_order_acquire))
		{
			return false;
		}
		if (!graphics)
		{
			detail::set_graphics_hook_status(false, false, false, "D3D11 device snapshot is unavailable");
			return false;
		}

		void* discovered_present{};
		void* discovered_resize_buffers{};
		if (!discover_targets(graphics, discovered_present, discovered_resize_buffers))
		{
			bool present_installed{};
			bool resize_installed{};
			{
				const std::lock_guard lock(hook_mutex);
				present_installed = present_hook.is_enabled();
				resize_installed = resize_buffers_hook.is_enabled();
			}
			detail::set_graphics_hook_status(present_installed, resize_installed, false,
				"DXGI swap-chain hook targets are unavailable or non-executable");
			return false;
		}

		bool alternate_targets{};
		{
			const std::lock_guard lock(hook_mutex);
			const bool present_registered = present_hook.get_original() != nullptr;
			const bool resize_registered = resize_buffers_hook.get_original() != nullptr;
			alternate_targets = (present_registered && present_target != discovered_present) ||
				(resize_registered && resize_buffers_target != discovered_resize_buffers);
		}
		if (alternate_targets)
		{
			bool present_installed{};
			bool resize_installed{};
			{
				const std::lock_guard lock(hook_mutex);
				present_installed = present_hook.is_enabled();
				resize_installed = resize_buffers_hook.is_enabled();
			}
			detail::set_graphics_hook_status(present_installed, resize_installed, true,
				"Ignoring alternate DXGI hook targets after the swap-chain hooks were installed");
			return false;
		}

		try
		{
			const std::lock_guard lock(hook_mutex);
			shutting_down.store(false, std::memory_order_release);
			const bool first_install = present_hook.get_original() == nullptr ||
				resize_buffers_hook.get_original() == nullptr;
			if (first_install)
			{
				active_swap_chain = nullptr;
				active_generation = 0;
				active_swap_chain_epoch = 0;
				next_frame_index.store(0, std::memory_order_relaxed);
				next_resize_index.store(0, std::memory_order_relaxed);
			}
			if (!present_hook.is_enabled())
			{
				present_target = discovered_present;
				if (present_hook.get_original() == nullptr)
				{
					present_hook.create(discovered_present, present_stub);
				}
				else
				{
					present_hook.enable();
				}
			}
			if (!resize_buffers_hook.is_enabled())
			{
				resize_buffers_target = discovered_resize_buffers;
				if (resize_buffers_hook.get_original() == nullptr)
				{
					resize_buffers_hook.create(discovered_resize_buffers, resize_buffers_stub);
				}
				else
				{
					resize_buffers_hook.enable();
				}
			}
		}
		catch (const std::exception& error)
		{
			bool present_installed{};
			bool resize_installed{};
			{
				const std::lock_guard lock(hook_mutex);
				present_installed = present_hook.is_enabled();
				resize_installed = resize_buffers_hook.is_enabled();
			}
			detail::set_graphics_hook_status(present_installed, resize_installed, true, error.what());
			return false;
		}
		catch (...)
		{
			bool present_installed{};
			bool resize_installed{};
			{
				const std::lock_guard lock(hook_mutex);
				present_installed = present_hook.is_enabled();
				resize_installed = resize_buffers_hook.is_enabled();
			}
			detail::set_graphics_hook_status(present_installed, resize_installed, true,
				"Unknown exception while installing DXGI swap-chain hooks");
			return false;
		}

		detail::set_graphics_hook_status(true, true, true);
		return true;
	}

	void reset_active_swap_chain(const std::uint64_t generation)
	{
		if (is_inside_callback())
		{
			return;
		}

		const std::unique_lock lifecycle_lock(hook_lifecycle_mutex);
		{
			const std::lock_guard lock(hook_mutex);
			if (active_generation != generation)
			{
				return;
			}
			shutting_down.store(true, std::memory_order_release);

			active_swap_chain = nullptr;
			active_generation = 0;
			next_frame_index.store(0, std::memory_order_relaxed);
			next_resize_index.store(0, std::memory_order_relaxed);
		}

		detail::clear_active_swap_chain(generation);
	}

	void shutdown()
	{
		begin_shutdown();
		if (is_inside_callback())
		{
			shutdown_pending.store(true, std::memory_order_release);
			shutting_down.store(true, std::memory_order_release);
			return;
		}

		const std::unique_lock lifecycle_lock(hook_lifecycle_mutex);
		shutdown_pending.store(false, std::memory_order_release);
		shutting_down.store(true, std::memory_order_release);
		{
			const std::lock_guard lock(pending_install_mutex);
			pending_install.reset();
		}
		bool present_disabled{};
		bool resize_disabled{};
		std::string disable_error;
		{
			const std::lock_guard lock(hook_mutex);
			active_swap_chain = nullptr;
			active_generation = 0;
			active_swap_chain_epoch = 0;
			next_frame_index.store(0, std::memory_order_relaxed);
			next_resize_index.store(0, std::memory_order_relaxed);
			try
			{
				if (present_hook.is_enabled())
				{
					present_hook.disable();
				}
				present_disabled = !present_hook.is_enabled();
			}
			catch (const std::exception& error)
			{
				disable_error = error.what();
			}
			try
			{
				if (resize_buffers_hook.is_enabled())
				{
					resize_buffers_hook.disable();
				}
				resize_disabled = !resize_buffers_hook.is_enabled();
			}
			catch (const std::exception& error)
			{
				if (!disable_error.empty())
				{
					disable_error.append("; ");
				}
				disable_error.append(error.what());
			}
		}

		const auto disabled = present_disabled && resize_disabled;
		detail::set_graphics_hook_status(!present_disabled, !resize_disabled, false,
			disabled ? std::string{} : std::move(disable_error));
	}
}
