#pragma once
#include "physical_reload_runtime.hpp"

namespace vr::gameplay::weapons::physical_reload::debug
{
	struct sample_identity
	{
		std::uintptr_t object{}, matrices{};
		std::uint32_t epoch{};
		hold owner{};
		std::uint64_t instance{}, reference{}, input_sequence{};
		clock::time_point at{};
		const reload_profile* definition{};
		float units{};
	};
	// Both diagnostics use the exact native skeleton epoch and current placement.
	// Callers own synchronization; the bounded store never reads engine pointers.
	template<class Sample,size_t Capacity=128> struct sample_history
	{
		std::array<Sample,Capacity> samples{};
		size_t cursor{};
		void push(const Sample& s) noexcept { samples[cursor++%samples.size()]=s; }
		Sample newest() const noexcept { return cursor ? samples[(cursor-1)%samples.size()] : Sample{}; }
		bool select(std::uintptr_t object,std::uintptr_t matrices,std::uint32_t epoch,const hold& owner,
			std::uint64_t reference,clock::time_point now,Sample& out) const noexcept
		{
			for (size_t n=0;n<std::min(cursor,samples.size());++n)
			{
				const auto& s=samples[(cursor-1-n)%samples.size()];
				if (s.object!=object || s.matrices!=matrices || s.epoch!=epoch) continue;
				if (!s.definition || !s.instance || s.owner.id()!=owner.id() || s.owner.rear!=owner.rear ||
					s.owner.rear_revision!=owner.rear_revision || s.reference!=reference || now<s.at ||
					now-s.at>std::chrono::milliseconds(150)) return false;
				out=s; return true;
			}
			return false;
		}
	};
}
