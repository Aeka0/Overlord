#pragma once

#include <cstdint>

namespace vr::engine_stereo_dynamic_upload
{
	enum class phase : std::uint8_t { awaiting_left, deferred, advancing, advanced };
	enum class action : std::uint8_t { defer, advance, reject };
	enum class failure : std::uint8_t
	{
		none, identity, thread, order, missing_boundary, original_did_not_return,
	};

	// H2's owner call at +0x7A8032 opens the NEXT upload with WRITE_DISCARD.
	// It must run once, after both eyes consumed the current allocation, not once
	// per replay. This policy owns no D3D resources, locks, waits or CPU payloads.
	struct cycle
	{
		std::uintptr_t data{};
		std::uint32_t thread{};
		std::uint32_t left_calls{};
		std::uint32_t right_calls{};
		std::uint32_t auxiliary_calls{};
		phase state{phase::awaiting_left};
		failure error{failure::none};
		bool recovered{};

		void fail(const failure value) noexcept
		{
			if (error == failure::none) error = value;
		}

		action boundary(const std::uint32_t eye, const std::uintptr_t identity,
			const std::uint32_t caller_thread) noexcept
		{
			if (caller_thread != thread || thread == 0)
			{
				fail(failure::thread);
				return action::reject;
			}
			if (identity == 0 || identity != data)
			{
				fail(failure::identity);
				return action::reject;
			}
			if (eye == 0) ++left_calls;
			if (eye == 1) ++right_calls;
			if (eye == 0 && state == phase::awaiting_left && left_calls == 1)
			{
				state = phase::deferred;
				return action::defer;
			}
			if (eye == 1 && state == phase::deferred && right_calls == 1)
			{
				state = phase::advancing;
				return action::advance;
			}
			fail(failure::order);
			return action::reject;
		}

		void returned() noexcept
		{
			if (state == phase::advancing) state = phase::advanced;
			else fail(failure::order);
		}

		// Optional consumer between left and right. Never opens WRITE_DISCARD;
		// the final right consumer remains the sole native advancement boundary.
		action auxiliary_boundary(const std::uintptr_t identity, const std::uint32_t caller_thread) noexcept
		{
			if (!thread || caller_thread!=thread) fail(failure::thread);
			else if (!identity || identity!=data) fail(failure::identity);
			else if (state!=phase::deferred || left_calls!=1 || right_calls || auxiliary_calls) fail(failure::order);
			if (error!=failure::none) return action::reject;
			++auxiliary_calls;
			return action::defer;
		}

		bool finish_auxiliary() noexcept
		{
			if (state!=phase::deferred || auxiliary_calls!=1 || right_calls) fail(failure::missing_boundary);
			return error==failure::none;
		}

		bool finish_eye(const std::uint32_t eye) noexcept
		{
			if ((eye == 0 && (left_calls != 1 || state != phase::deferred)) ||
				(eye == 1 && (right_calls != 1 || state != phase::advanced)) || eye >= 2)
			{
				fail(failure::missing_boundary);
			}
			return error == failure::none;
		}

		// Any normal early exit after left must still open H2's next upload on
		// this owner thread. An interrupted original call is never retried: some
		// of its nine descriptors may already be mapped.
		bool take_deferred(const std::uint32_t caller_thread) noexcept
		{
			if (state == phase::advancing) fail(failure::original_did_not_return);
			if (state != phase::deferred) return false;
			if (caller_thread != thread)
			{
				fail(failure::thread);
				return false;
			}
			recovered = true;
			state = phase::advancing;
			return true;
		}
	};

	struct report
	{
		std::uint64_t pairs{};
		std::uint64_t complete{};
		std::uint64_t failures{};
		std::uint64_t deferred{};
		std::uint64_t advances{};
		std::uint64_t recoveries{};
		std::uint64_t last_pair{};
		cycle last{};
		std::uint64_t last_failure_pair{};
		cycle last_failure{};
	};

	inline const char* to_string(const failure value) noexcept
	{
		switch (value)
		{
		case failure::none: return "none";
		case failure::identity: return "identity";
		case failure::thread: return "thread";
		case failure::order: return "order";
		case failure::missing_boundary: return "missing_boundary";
		case failure::original_did_not_return: return "original_did_not_return";
		default: return "unknown";
		}
	}
}
