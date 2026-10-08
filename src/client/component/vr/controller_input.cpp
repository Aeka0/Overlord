#include <std_include.hpp>
#include "controller_input.hpp"
#include "controller_calibration.hpp"
#include "stabilization.hpp"
#include "input_history.hpp"
#include <atomic>
#include <mutex>

namespace vr::controller_calibration
{
	namespace {std::atomic<settings_provider> provider{};}
	void set_settings_provider(settings_provider value) noexcept {provider.store(value);}
	configuration read_settings() noexcept
	{
		const auto read=provider.load();
		return read?read():configuration{};
	}
}

namespace vr::controller_input
{
	namespace
	{
		std::mutex mutex;
		frame current{};
		input_history history;
		std::uint64_t continuity{1};bool gameplay_active{};
		controller_calibration::calibration calibration;
		std::array<pose_filter::filter,2> hand_filters;
		frame unfiltered{};
		std::uint64_t filter_epoch{};
		float previous_hand_amount{},previous_head_amount{};
	}

	void publish(const frame& value) noexcept
	{
		const std::lock_guard lock(mutex);
		const auto settings=controller_calibration::read_settings();
		auto next = calibration.apply(value,settings.orientation,settings.pivot);
		const bool discontinuity=producer_discontinuity(unfiltered,next);
		if(discontinuity){++continuity;stabilization::invalidate();}
		unfiltered=next;
		const auto epoch=stabilization::epoch.load();
		const auto config=stabilization::settings();
		const auto finite_amount=[](float x){return std::isfinite(x)?std::clamp(x,0.f,100.f):0.f;};
		const float amount=gameplay_active && next.focused && !next.orientation_settling ? finite_amount(config.hand.amount()):0.f;
		const float head_amount=finite_amount(config.head.amount());
		const bool controls_changed=amount!=previous_hand_amount || head_amount!=previous_head_amount;
		if(controls_changed || epoch!=filter_epoch)
		{
			// Consumers discard velocity/contact history without changing buttons
			// or holding leases; a filter toggle is not a physical hand impulse.
			if(!discontinuity)++continuity;
			previous_hand_amount=amount;previous_head_amount=head_amount;
		}
		if(discontinuity || controls_changed || epoch!=filter_epoch){for(auto& f:hand_filters)f.reset();filter_epoch=epoch;}
		for(unsigned h=0;h<2;++h)
		{
			if(!next.grip[h].valid || !next.aim[h].valid){hand_filters[h].reset();continue;}
			const auto& grip=next.grip[h].tracking;
			const pose_filter::pose raw{grip.position_meters,grip.orientation};
			if(amount>0 && (!pose_filter::valid(raw) || !pose_filter::valid({next.aim[h].tracking.position_meters,next.aim[h].tracking.orientation})))
			{
				next.grip[h].valid=next.aim[h].valid=false;
				next.source.channels[index(hand_channel(input_channel::left_grip,h))]={input_reason::pose_filter_rejected};
				next.source.channels[index(hand_channel(input_channel::left_aim,h))]={input_reason::pose_filter_rejected};
				hand_filters[h].reset();continue;
			}
			const auto filtered=hand_filters[h].update(raw,next.sequence,next.reference_generation,next.sampled_at,amount,pose_filter::hand);
			if(amount<=0 || !std::isfinite(amount))continue; // Exact calibrated bypass.
			const auto transform=pose_filter::correction(raw,filtered);
			const auto aim=pose_filter::compose(transform,{next.aim[h].tracking.position_meters,next.aim[h].tracking.orientation});
			next.grip[h].tracking={filtered.position,filtered.orientation};
			next.aim[h].tracking={aim.position,aim.orientation};
		}
		for(unsigned h=0;h<2;++h)
			if(value.aim[h].valid && !next.aim[h].valid &&
				next.source.channels[index(hand_channel(input_channel::left_aim,h))].reason!=input_reason::pose_filter_rejected)
				next.source.channels[index(hand_channel(input_channel::left_aim,h))]={input_reason::calibration_invalid};
		next.continuity_generation=continuity;history.observe(next);current=next;
	}
	void set_gameplay_active(bool active) noexcept
	{
		const std::lock_guard lock(mutex);
		history.set_gameplay_active(active);
		if(active!=gameplay_active){gameplay_active=active;current.continuity_generation=++continuity;for(auto& f:hand_filters)f.reset();}
		stabilization::set_gameplay(active);
	}

	void invalidate(input_reason reason, input_backend backend, std::int64_t code, std::uint64_t initialization) noexcept
	{
		frame value;
		value.sampled_at=clock::now();value.source.backend=backend;value.source.gate={reason,code};
		value.source.initialization=initialization;
		publish(value);
	}
	input_history_snapshot get_input_history() noexcept
	{
		const std::lock_guard lock(mutex);
		return history.snapshot();
	}

	frame latest() noexcept
	{
		const std::lock_guard lock(mutex);
		return current;
	}
}
