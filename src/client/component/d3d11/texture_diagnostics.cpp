#include "texture_diagnostics.hpp"
#include <exception/minidump.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <intrin.h>
#include <sstream>

namespace d3d11::texture_diagnostics
{
	namespace
	{
		using create_texture = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*,
			const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
		struct slot
		{
			std::atomic_flag writing = ATOMIC_FLAG_INIT;
			event value{};
			std::atomic<std::uint64_t> active{};
		};
		struct hook
		{
			std::atomic<create_texture> original{};
			std::atomic<void**> table{};
		};
		struct identity
		{
			std::atomic<ID3D11Device*> device{};
			std::atomic<std::uint64_t> generation{};
			std::atomic<create_texture> original{};
		};
		std::array<slot, capacity> recent;
		std::array<slot, active_capacity> active;
		slot first_failure;
		std::array<hook, 32> hooks;
		std::array<identity, 32> identities;
		std::atomic_flag installing = ATOMIC_FLAG_INIT;
		std::atomic<std::uint64_t> sequence{}, next_call{}, calls{}, dropped{}, failures{}, active_overflow{};
		std::atomic<std::uint64_t> hook_count{}, hook_failures{}, identity_overflow{};
		snapshot dump_snapshot;

		bool supplement(MINIDUMP_USER_STREAM& stream) noexcept
		{
			capture(dump_snapshot);
			stream = {stream_type, sizeof(dump_snapshot), &dump_snapshot};
			return true;
		}

		void publish(event& value) noexcept
		{
			value.sequence = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
			value.tick = GetTickCount64();
			value.thread = GetCurrentThreadId();
			auto& target = recent[value.sequence % capacity];
			if (target.writing.test_and_set(std::memory_order_acquire)) { ++dropped; return; }
			if (target.value.sequence > value.sequence)
			{
				++dropped;
				target.writing.clear(std::memory_order_release);
				return;
			}
			target.value = value;
			target.writing.clear(std::memory_order_release);
		}

		std::size_t begin_active(const event& value) noexcept
		{
			for (std::size_t index{}; index < active.size(); ++index)
			{
				auto& target = active[index];
				if (target.writing.test_and_set(std::memory_order_acquire)) continue;
				if (target.active.load(std::memory_order_acquire)) { target.writing.clear(std::memory_order_release); continue; }
				target.value = value;
				target.active.store(value.call, std::memory_order_release);
				target.writing.clear(std::memory_order_release);
				return index;
			}
			++active_overflow;
			return active_capacity;
		}

		void record_failure(const event& value) noexcept
		{
			++failures;
			if (first_failure.writing.test_and_set(std::memory_order_acquire)) { ++dropped; return; }
			const auto device_error = [](const std::uint64_t code)
			{
				return code == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_REMOVED) ||
					code == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_HUNG) ||
					code == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_RESET) ||
					code == static_cast<std::uint32_t>(DXGI_ERROR_DRIVER_INTERNAL_ERROR);
			};
			if (!first_failure.value.sequence || (device_error(value.result) && !device_error(first_failure.value.result)))
				first_failure.value = value;
			first_failure.writing.clear(std::memory_order_release);
		}

		void finish_active(const event& value, const std::size_t index) noexcept
		{
			if (index >= active_capacity) return;
			auto expected = value.call;
			(void)active[index].active.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
		}

		template <typename T> bool copy_pointer(const T* source, T& destination) noexcept
		{
			SIZE_T received{};
			return source && ReadProcessMemory(GetCurrentProcess(), source, &destination, sizeof(T), &received) && received == sizeof(T);
		}

		HRESULT STDMETHODCALLTYPE create_texture_stub(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* description,
			const D3D11_SUBRESOURCE_DATA* initial, ID3D11Texture2D** output)
		{
			const auto previous_error = GetLastError();
			event value;
			value.operation = static_cast<std::uint64_t>(phase::texture_begin);
			value.device = reinterpret_cast<std::uintptr_t>(device);
			value.caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			value.initial_data = initial != nullptr;
			create_texture original{};
			bool observed{};
			for (const auto& entry : identities)
			{
				if (entry.device.load(std::memory_order_acquire) != device) continue;
				value.generation = entry.generation.load(std::memory_order_acquire);
				original = entry.original.load(std::memory_order_acquire);
				observed = true;
				break;
			}
			if (!original)
			{
				const auto table = *reinterpret_cast<void***>(device);
				for (const auto& entry : hooks)
					if (entry.table.load(std::memory_order_acquire) == table) { original = entry.original.load(std::memory_order_acquire); break; }
			}
			// Runtime implementations share vtables across devices. An unrelated
			// WebView/overlay device must retain its original call and stay outside
			// H2's incident ring even when it shares this method implementation.
			if (!observed)
			{
				SetLastError(previous_error);
				if (original) return original(device, description, initial, output);
				++hook_failures;
				return E_UNEXPECTED;
			}
			value.call = next_call.fetch_add(1, std::memory_order_relaxed) + 1;
			++calls;
			D3D11_TEXTURE2D_DESC descriptor{};
			if (copy_pointer(description, descriptor))
			{
				value.descriptor_valid = 1;
				value.width = descriptor.Width; value.height = descriptor.Height;
				value.mips = descriptor.MipLevels; value.array_size = descriptor.ArraySize;
				value.format = descriptor.Format; value.samples = descriptor.SampleDesc.Count;
				value.quality = descriptor.SampleDesc.Quality; value.usage = descriptor.Usage;
				value.bind = descriptor.BindFlags; value.cpu = descriptor.CPUAccessFlags; value.misc = descriptor.MiscFlags;
			}
			publish(value);
			const auto pending = begin_active(value);
			SetLastError(previous_error);
			if (!original) ++hook_failures;
			const auto result = original ? original(device, description, initial, output) : E_UNEXPECTED;
			const auto result_error = GetLastError();
			value.operation = static_cast<std::uint64_t>(phase::texture_end);
			value.result = static_cast<std::uint32_t>(result);
			ID3D11Texture2D* texture{};
			if (SUCCEEDED(result) && copy_pointer(output, texture)) value.texture = reinterpret_cast<std::uintptr_t>(texture);
			publish(value);
			if (FAILED(result)) record_failure(value);
			finish_active(value, pending);
			SetLastError(result_error);
			return result;
		}

		bool identify(ID3D11Device* device, const std::uint64_t generation, const create_texture original) noexcept
		{
			identity* empty{};
			for (auto& entry : identities)
			{
				const auto known = entry.device.load(std::memory_order_acquire);
				if (known == device)
				{
					entry.original.store(original, std::memory_order_release);
					entry.generation.store(generation, std::memory_order_release);
					return true;
				}
				if (!known && !empty) empty = &entry;
			}
			if (!empty) { ++identity_overflow; return false; }
			empty->original.store(original, std::memory_order_relaxed);
			empty->generation.store(generation, std::memory_order_relaxed);
			empty->device.store(device, std::memory_order_release);
			return true;
		}

		void format_event(std::ostringstream& output, const char* label, const event& value)
		{
			output << label << " seq=" << value.sequence << " call=" << value.call << " tick=" << value.tick
				<< " tid=" << value.thread << " phase=" << value.operation << " generation=" << value.generation
				<< " pair=" << value.pair << " eye=" << value.eye << " result=0x" << std::hex << value.result
				<< " device=0x" << value.device << " context=0x" << value.context << " caller=0x" << value.caller
				<< " texture=0x" << value.texture << " source=0x" << value.source << " command=0x" << value.command << std::dec
				<< " desc=" << value.descriptor_valid << " size=" << value.width << 'x' << value.height
				<< " format=" << value.format << " samples=" << value.samples << '/' << value.quality
				<< " mips=" << value.mips << " array=" << value.array_size << " usage=" << value.usage
				<< " bind=" << value.bind << " cpu=" << value.cpu << " misc=" << value.misc << " initial=" << value.initial_data
				<< " detail=" << value.detail << "\r\n";
		}
	}

	bool install(ID3D11Device* device, const std::uint64_t generation) noexcept
	{
		const auto previous_error = GetLastError();
		if (!device || installing.test_and_set(std::memory_order_acquire)) { ++hook_failures; return false; }
		// COM ID3D11Device order in the SDK: IUnknown(3), CreateBuffer,
		// CreateTexture1D, then CreateTexture2D. No native game addresses are used.
		const auto table = *reinterpret_cast<void***>(device);
		auto* entry = table + 5;
		const auto current = reinterpret_cast<create_texture>(*entry);
		hook* available{};
		bool success{};
		if (current && exception::register_dump_supplement(supplement))
		{
			for (auto& candidate : hooks)
			{
				const auto known = candidate.table.load(std::memory_order_acquire);
				if (known == table)
				{
					// Leave later middleware patches in place; repatching one could
					// form a recursion cycle through its saved original function.
					if (current == create_texture_stub)
					{
						success = identify(device, generation, candidate.original.load(std::memory_order_acquire));
					}
					available = nullptr;
					break;
				}
				if (!known && !available) available = &candidate;
			}
			if (available && current != create_texture_stub)
			{
				DWORD protection{};
				MEMORY_BASIC_INFORMATION region{};
				const auto queried = VirtualQuery(entry, &region, sizeof(region));
				const auto writable = (region.Protect & 0xf0) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
				if (queried && VirtualProtect(entry, sizeof(void*), writable, &protection))
				{
					if (identify(device, generation, current))
					{
						available->original.store(current, std::memory_order_relaxed);
						available->table.store(table, std::memory_order_release);
						InterlockedExchangePointer(entry, reinterpret_cast<void*>(create_texture_stub));
						++hook_count;
						success = true;
					}
					DWORD ignored{};
					const auto restored = VirtualProtect(entry, sizeof(void*), protection, &ignored);
					if (!restored) ++hook_failures;
				}
			}
		}
		if (!success) ++hook_failures;
		else
		{
			event value;
			value.operation = static_cast<std::uint64_t>(phase::device_observed);
			value.device = reinterpret_cast<std::uintptr_t>(device);
			value.generation = generation;
			publish(value);
		}
		installing.clear(std::memory_order_release);
		SetLastError(previous_error);
		return success;
	}

	cpu_scope::cpu_scope(const phase begin, const phase end, const std::uint64_t generation,
		const std::uint64_t pair, const std::uint64_t eye, ID3D11DeviceContext* context,
		const void* source, const void* destination, const void* command, const std::uint64_t detail) noexcept : end_(end)
	{
		value_.operation = static_cast<std::uint64_t>(begin); value_.generation = generation;
		value_.pair = pair; value_.eye = eye; value_.context = reinterpret_cast<std::uintptr_t>(context);
		value_.source = reinterpret_cast<std::uintptr_t>(source); value_.texture = reinterpret_cast<std::uintptr_t>(destination);
		value_.command = reinterpret_cast<std::uintptr_t>(command); value_.detail = detail;
		value_.call = next_call.fetch_add(1, std::memory_order_relaxed) + 1;
		publish(value_);
		active_slot_ = begin_active(value_);
	}

	cpu_scope::~cpu_scope() noexcept
	{
		if (!value_.call) return;
		value_.operation = static_cast<std::uint64_t>(phase::scope_abandoned);
		publish(value_);
		finish_active(value_, active_slot_);
	}

	void cpu_scope::complete(const std::uint64_t result) noexcept
	{
		if (!value_.call) return;
		value_.operation = static_cast<std::uint64_t>(end_);
		value_.result = result;
		publish(value_);
		finish_active(value_, active_slot_);
		value_.call = 0;
	}

	void record_phase(const phase operation, const std::uint64_t generation, const std::uint64_t pair,
		const std::uint64_t eye, ID3D11DeviceContext* context, const void* source, const void* destination, const void* command) noexcept
	{
		event value;
		value.operation = static_cast<std::uint64_t>(operation); value.generation = generation;
		value.pair = pair; value.eye = eye; value.context = reinterpret_cast<std::uintptr_t>(context);
		value.source = reinterpret_cast<std::uintptr_t>(source); value.texture = reinterpret_cast<std::uintptr_t>(destination);
		value.command = reinterpret_cast<std::uintptr_t>(command);
		publish(value);
	}

	void capture(snapshot& output) noexcept
	{
		std::memset(&output, 0, sizeof(output));
		output.version = 1; output.event_bytes = sizeof(event); output.capacity = capacity;
		output.sampled_tick = GetTickCount64();
		output.newest = sequence.load(std::memory_order_acquire); output.dropped = dropped.load();
		output.texture_calls = calls.load(); output.texture_failures = failures.load();
		output.active_overflow = active_overflow.load(); output.hooks = hook_count.load();
		output.hook_failures = hook_failures.load(); output.identity_overflow = identity_overflow.load();
		const auto first = output.newest > capacity ? output.newest - capacity + 1 : 1;
		for (auto id = first; id <= output.newest; ++id)
		{
			auto& entry = recent[id % capacity];
			if (entry.writing.test_and_set(std::memory_order_acquire)) { ++output.skipped; continue; }
			if (entry.value.sequence == id) output.recent[output.recent_count++] = entry.value;
			else ++output.skipped;
			entry.writing.clear(std::memory_order_release);
		}
		for (auto& entry : active)
		{
			if (entry.writing.test_and_set(std::memory_order_acquire)) { ++output.skipped; continue; }
			if (entry.active.load(std::memory_order_acquire)) output.active[output.active_count++] = entry.value;
			entry.writing.clear(std::memory_order_release);
		}
		if (!first_failure.writing.test_and_set(std::memory_order_acquire))
		{
			output.first_failure = first_failure.value;
			output.first_failure_present = output.first_failure.sequence != 0;
			first_failure.writing.clear(std::memory_order_release);
		}
		else ++output.skipped;
	}

	std::string format_recent(const std::size_t limit)
	{
		snapshot output;
		capture(output);
		std::ostringstream text;
		text << "D3D11 texture/CPU-submit evidence v1 newest=" << output.newest << " calls=" << output.texture_calls
			<< " failures=" << output.texture_failures << " hooks=" << output.hooks << " hook_failures=" << output.hook_failures
			<< " dropped=" << output.dropped << " skipped=" << output.skipped << " active_overflow=" << output.active_overflow
			<< " identity_overflow=" << output.identity_overflow
			<< " failure_policy=first_device_error_else_first_failure"
			<< " ownership=pointer_identity_only completion=cpu_call_return_only gpu_retire_proof=none\r\n";
		if (output.first_failure_present) format_event(text, "first_failure", output.first_failure);
		for (std::size_t index{}; index < output.active_count; ++index) format_event(text, "active", output.active[index]);
		const auto count = (std::min)(limit, static_cast<std::size_t>(output.recent_count));
		for (auto index = output.recent_count - count; index < output.recent_count; ++index) format_event(text, "recent", output.recent[index]);
		return text.str();
	}
}
