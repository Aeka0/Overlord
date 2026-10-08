#pragma once

#include "controller_input.hpp"

namespace vr::controller_input
{
	struct input_failure
	{
		bool seen{}, runtime_focus{}, runtime_focus_known{}, gameplay_active{};
		clock::time_point at{};
		input_backend backend{};
		input_channel channel{input_channel::focus};
		input_condition condition{};
		std::uint64_t sequence{}, reference{}, initialization{};
	};

	struct channel_history
	{
		bool valid{};
		std::uint64_t valid_samples{}, activity_samples{}, losses{};
		clock::time_point last_valid_at{}, current_since{}, last_loss_at{}, last_rejection_at{};
		input_condition current{}, last_loss{}, last_rejection{};
		input_backend last_loss_backend{}, last_rejection_backend{};
		std::uint64_t last_loss_sequence{}, last_loss_reference{};
		bool last_loss_runtime_focus{}, last_loss_runtime_focus_known{}, last_loss_gameplay_active{};
		bool rejected{};
		std::uint64_t recoveries{}, api_failure_samples{};
		// Direct action rejections survive dashboard focus loss and ring eviction.
		input_failure first_action_rejection{}, last_action_rejection{};
	};
	struct input_transition
	{
		clock::time_point at{};
		input_backend backend{};
		input_condition gate{};
		std::uint64_t sequence{}, reference{};
		std::uint64_t initialization{};
		std::uint16_t valid_mask{}, loss_mask{};
		bool runtime_focus{}, runtime_focus_known{}, gameplay_active{};
		std::array<input_condition, input_channel_count> channels{};
	};
	inline constexpr std::size_t input_transition_capacity = 8;
	struct input_history_snapshot
	{
		input_backend backend{};
		bool runtime_focus{}, runtime_focus_known{}, gameplay_active{}, observed{};
		std::uint64_t samples{}, invalidations{}, sequence{}, reference_generation{};
		std::uint64_t last_sample_sequence{}, last_sample_reference{}, initialization{};
		clock::time_point started_at{}, sampled_at{}, last_sample_at{};
		input_condition gate{};
		input_failure first_api_failure{}, last_api_failure{};
		std::array<channel_history, input_channel_count> channels{};
		std::array<input_transition, input_transition_capacity> transitions{};
		std::size_t transition_count{}, transition_next{};
		std::uint64_t transitions_discarded{};
	};

	// Fixed-size process history, observed under the producer's existing lock.
	// No runtime calls, allocation, file I/O or event queues on the frame path.
	class input_history
	{
		input_history_snapshot state_;
		struct channel_sample
		{
			bool valid{}, engaged{};
		};
		struct channel_update
		{
			bool changed{}, lost{};
		};
		static_assert(input_channel_count <= 16, "input transition masks must cover every channel");

	  public:
		void observe(const frame& input) noexcept
		{
			const bool first = !state_.observed;
			bool transitioned = first || (input.source.backend != input_backend::unknown &&
			                              input.source.backend != state_.backend) ||
			                    input.source.initialization != state_.initialization;
			state_.initialization = input.source.initialization;
			if (first)
				state_.started_at = input.sampled_at;
			state_.observed = true;
			if (input.source.backend != input_backend::unknown)
				state_.backend = input.source.backend;
			state_.runtime_focus = input.source.runtime_focus;
			state_.runtime_focus_known = input.source.runtime_focus_known;
			state_.gate = input.source.gate;
			state_.sequence = input.sequence;
			state_.reference_generation = input.reference_generation;
			state_.sampled_at = input.sampled_at;
			if (input.sequence)
			{
				++state_.samples;
				state_.last_sample_at = input.sampled_at;
				state_.last_sample_sequence = input.sequence;
				state_.last_sample_reference = input.reference_generation;
			}
			else
				++state_.invalidations;

			std::uint16_t valid_mask{}, loss_mask{};
			bool error_recorded{};
			for (std::size_t i = 0; i < input_channel_count; ++i)
			{
				const auto channel = static_cast<input_channel>(i);
				const auto sample = sample_channel(input, channel);
				const auto update = observe_channel(input, channel, sample, first);
				transitioned |= update.changed;
				if (sample.valid)
					valid_mask |= channel_bit(channel);
				if (update.lost)
					loss_mask |= channel_bit(channel);
				if (!error_recorded)
					error_recorded =
					    remember_api_error(state_.channels[i].current, channel, input);
			}
			if (transitioned)
				append_transition(input, valid_mask, loss_mask);
		}
		void set_gameplay_active(bool active) noexcept
		{
			state_.gameplay_active = active;
		}
		[[nodiscard]] input_history_snapshot snapshot() const noexcept
		{
			return state_;
		}

	  private:
		// Select by named channel so adding/reordering channels cannot silently
		// misalign anonymous validity and activity arrays.
		static channel_sample sample_channel(const frame& input, input_channel channel) noexcept
		{
			switch (channel)
			{
			case input_channel::focus:
				return {input.focused, false};
			case input_channel::move:
				return {input.move_active, input.move[0] != 0 || input.move[1] != 0};
			case input_channel::turn:
				return {input.turn_active, input.turn[0] != 0 || input.turn[1] != 0};
			case input_channel::left_grip:
				return {input.grip[0].valid, false};
			case input_channel::right_grip:
				return {input.grip[1].valid, false};
			case input_channel::left_aim:
				return {input.aim[0].valid, false};
			case input_channel::right_aim:
				return {input.aim[1].valid, false};
			case input_channel::sprint:
				return {input.sprint.active, input.sprint.down};
			case input_channel::jump:
				return {input.jump.active, input.jump.down};
			case input_channel::left_trigger:
				return {input.trigger[0].active, input.trigger[0].down};
			case input_channel::right_trigger:
				return {input.trigger[1].active, input.trigger[1].down};
			case input_channel::left_squeeze:
				return {input.squeeze[0].active, input.squeeze[0].down};
			case input_channel::right_squeeze:
				return {input.squeeze[1].active, input.squeeze[1].down};
			case input_channel::count:
				break;
			}
			return {};
		}
		static std::uint16_t channel_bit(input_channel channel) noexcept
		{
			return static_cast<std::uint16_t>(1u << index(channel));
		}
		static input_condition rejection_for(const frame& input, input_channel channel, bool valid) noexcept
		{
			if (valid)
				return {input_reason::none};
			auto condition = input.source.gate.reason != input_reason::none
			                     ? input.source.gate
			                     : input.source.channels[index(channel)];
			if (condition.reason == input_reason::none || condition.reason == input_reason::not_sampled)
				condition = {input.sequence ? input_reason::action_inactive : input_reason::not_sampled};
			return condition;
		}
		channel_update observe_channel(const frame& input,
		                               input_channel channel,
		                               channel_sample sample,
		                               bool first) noexcept
		{
			auto& history = state_.channels[index(channel)];
			const auto condition = rejection_for(input, channel, sample.valid);
			const bool changed =
			    first || history.valid != sample.valid || history.current.reason != condition.reason ||
			    history.current.code != condition.code || history.current.handle != condition.handle ||
			    input.sampled_at < history.current_since;
			const bool lost = history.valid && !sample.valid;
			if (changed)
			{
				history.current_since = input.sampled_at;
				if (!sample.valid)
				{
					history.rejected = true;
					history.last_rejection = condition;
					history.last_rejection_at = input.sampled_at;
					history.last_rejection_backend = state_.backend;
				}
			}
			if (lost)
			{
				++history.losses;
				history.last_loss = condition;
				history.last_loss_at = input.sampled_at;
				history.last_loss_backend = state_.backend;
				history.last_loss_sequence = state_.last_sample_sequence;
				history.last_loss_reference = state_.last_sample_reference;
				history.last_loss_runtime_focus = input.source.runtime_focus;
				history.last_loss_runtime_focus_known = input.source.runtime_focus_known;
				history.last_loss_gameplay_active = state_.gameplay_active;
			}
			if (sample.valid)
			{
				if (!history.valid && history.valid_samples) ++history.recoveries;
				++history.valid_samples;
				history.last_valid_at = input.sampled_at;
				if (sample.engaged)
					++history.activity_samples;
			}
			if (input.sequence && is_api_failure(condition.reason)) ++history.api_failure_samples;
			if (input.sequence && !sample.valid && channel != input_channel::focus &&
			    input.source.gate.reason == input_reason::none)
			{
				auto failure = failure_at(condition, channel, input);
				if (!history.first_action_rejection.seen) history.first_action_rejection = failure;
				history.last_action_rejection = failure;
			}
			history.valid = sample.valid;
			history.current = condition;
			return {changed, lost};
		}
		input_failure failure_at(input_condition condition, input_channel channel, const frame& input) const noexcept
		{
			return {true, input.source.runtime_focus, input.source.runtime_focus_known, state_.gameplay_active,
			        input.sampled_at, state_.backend, channel, condition, input.sequence,
			        input.reference_generation, input.source.initialization};
		}
		bool remember_api_error(input_condition condition, input_channel channel, const frame& input) noexcept
		{
			if (!condition.code || !is_api_failure(condition.reason))
				return false;
			state_.last_api_failure = failure_at(condition, channel, input);
			if (!state_.first_api_failure.seen) state_.first_api_failure = state_.last_api_failure;
			return true;
		}
		void append_transition(const frame& input, std::uint16_t valid_mask, std::uint16_t loss_mask) noexcept
		{
			auto& event = state_.transitions[state_.transition_next];
			event.at = input.sampled_at;
			event.backend = state_.backend;
			event.gate = input.source.gate;
			event.sequence = input.sequence;
			event.reference = input.reference_generation;
			event.initialization = input.source.initialization;
			event.runtime_focus = input.source.runtime_focus;
			event.runtime_focus_known = input.source.runtime_focus_known;
			event.gameplay_active = state_.gameplay_active;
			event.valid_mask = valid_mask;
			event.loss_mask = loss_mask;
			for (std::size_t i = 0; i < input_channel_count; ++i)
				event.channels[i] = state_.channels[i].current;
			state_.transition_next = (state_.transition_next + 1) % input_transition_capacity;
			if (state_.transition_count < input_transition_capacity)
				++state_.transition_count;
			else
				++state_.transitions_discarded;
		}
	};

	[[nodiscard]] input_history_snapshot get_input_history() noexcept;
}
