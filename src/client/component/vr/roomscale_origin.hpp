#pragma once

#include "pose_filter.hpp"
#include <array>
#include <cstdint>

namespace vr::game_view
{
	// Rendered native yaw has already passed the game's movement constraints.
	// Translate the shared tracking origin so this yaw turns about the current
	// head. Physical movement after the turn still uses the new world basis.
	class roomscale_origin
	{
		using vec = pose_filter::vec;
		using axes = pose_filter::matrix;
		struct entry
		{
			int time{};
			axes basis{};
			vec pivot{}, offset{}; // Recenter-local pivot and world-space offset, metres.
		};
		std::array<entry, 64> history_{};
		entry latest_{};
		std::size_t next_{}, count_{};
		std::uint64_t reference_{};
		int command_time_{};
		bool command_seen_{};

		static vec world(const axes& basis, vec local) noexcept
		{
			vec out{};
			for (unsigned i = 0; i < 3; ++i)
				for (unsigned j = 0; j < 3; ++j) out[j] += local[i] * basis[i][j];
			return out;
		}
		static bool bounded(vec value) noexcept
		{
			for (float x : value) if (!std::isfinite(x) || std::abs(x) > 100000.f) return false;
			return true;
		}
		void remember(const entry& value) noexcept
		{
			for (std::size_t i=0;i<count_;++i)
			{
				auto& saved=history_[(next_+history_.size()-1-i)%history_.size()];
				if (saved.time==value.time) { saved=value;return; }
			}
			history_[next_]=value;
			next_=(next_+1)%history_.size();
			if (count_<history_.size()) ++count_;
		}
	public:
		void reset() noexcept { *this = {}; }
		void record(int time) noexcept
		{
			// Command construction distinguishes a load from rendering an older
			// prediction frame. The latter must reuse its own origin, not reset it.
			if (command_seen_ && time < command_time_) reset();
			command_seen_ = true;
			command_time_ = time;
		}
		bool offset(int time, std::uint64_t reference, const axes& basis, vec head, vec& output,
			bool command_known=false) noexcept
		{
			if (!reference || !bounded(head) || !pose_filter::valid({{}, basis})) return false;
			if (reference_ != reference) { next_ = count_ = 0; reference_ = reference; }
			if (count_)
			{
				const auto& latest = latest_;
				if (time < latest.time)
				{
					for (std::size_t i = 0; i < count_; ++i)
					{
						const auto& saved = history_[(next_ + history_.size() - 1 - i) % history_.size()];
						if (saved.time != time) continue;
						auto result = saved.offset;
						if (basis != saved.basis)
							result = pose_filter::add(result, pose_filter::sub(world(saved.basis, saved.pivot), world(basis, saved.pivot)));
						if (!bounded(result)) return false;
						output = result;
						return true;
					}
					if (!command_known) return false;
					// Prediction can visit a real command which skipped rendering. Rebuild
					// about the current head, but leave the forward timeline unchanged.
					entry older{time,basis,head,pose_filter::add(latest.offset,
						pose_filter::sub(world(latest.basis,head),world(basis,head)))};
					if (!bounded(older.offset)) return false;
					remember(older);output=older.offset;return true;
				}
				entry candidate{time, basis, head, latest.offset};
				if (basis != latest.basis)
					candidate.offset = pose_filter::add(candidate.offset,
						pose_filter::sub(world(latest.basis, head), world(basis, head)));
				if (!bounded(candidate.offset)) return false;
				remember(candidate);
				latest_=candidate;
				output = candidate.offset;
				return true;
			}
			latest_={time,basis,head,{}};
			remember(latest_);
			output = {};
			return true;
		}
	};
}
