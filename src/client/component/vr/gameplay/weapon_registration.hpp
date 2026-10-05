#pragma once
#include "weapon_profile.hpp"
#include <span>
namespace vr::gameplay::hands { struct model_definition; struct bone_definition; struct rig; }

namespace vr::gameplay::weapons
{
	using assembly_selector = profile_match (*)(std::span<const hands::model_definition>,
		const hands::model_definition&, const hands::rig&, std::span<const hands::bone_definition>) noexcept;

	struct profile_registration
	{
		const profile* value;
		assembly_selector select; // Null uses the shared profile attachment binder.
	};

	template <typename... Profiles>
	constexpr auto register_profiles(assembly_selector select, const Profiles&... values) noexcept
	{
		return std::array<profile_registration, sizeof...(Profiles)>{{{&values, select}...}};
	}

	template <size_t N>
	constexpr auto register_profiles(assembly_selector select, const std::array<profile, N>& values) noexcept
	{
		std::array<profile_registration, N> result{};
		for (size_t i = 0; i < N; ++i) result[i] = {&values[i], select};
		return result;
	}

	template <size_t... Sizes>
	constexpr auto join_registrations(const std::array<profile_registration, Sizes>&... groups) noexcept
	{
		std::array<profile_registration, (Sizes + ...)> result{};
		size_t count{};
		const auto append = [&](const auto& group) { for (const auto& entry : group) result[count++] = entry; };
		(append(groups), ...);
		return result;
	}

	// One immutable, pointer-deduplicated view per mechanical capability. The
	// assembly count bounds storage; only size() entries participate in lookup.
	// Built once after the authored profiles, with no heap allocation or worker
	// synchronization. Shared skins/grips share a cache slot; distinct recipes do not.
	inline constexpr size_t registration_capacity = 256;
	template <typename Capability, size_t N = registration_capacity>
	class profile_capabilities
	{
	public:
		profile_capabilities(std::span<const profile_registration> registrations,
			const Capability* profile::* member) noexcept
		{
			for (const auto& entry : registrations)
			{
				if(!entry.value){count_=0;return;}
				const auto* value = entry.value->*member;
				if (!value) continue;
				bool found{};
				for (const auto* existing : *this) if (existing == value) { found = true; break; }
				if(!found)
				{
					if(count_==N){count_=0;return;}
					values_[count_++]=value;
				}
			}
		}

		static constexpr size_t capacity() noexcept { return N; }
		size_t size() const noexcept { return count_; }
		const Capability* const* begin() const noexcept { return values_.data(); }
		const Capability* const* end() const noexcept { return begin() + count_; }
		const Capability* operator[](size_t index) const noexcept { return values_[index]; }

		template<size_t M>
		profile_capabilities(const std::array<profile_registration,M>& registrations,
			const Capability* profile::* member) noexcept
			: profile_capabilities(std::span<const profile_registration>{registrations},member) {}

	private:
		std::array<const Capability*, N> values_{};
		size_t count_{};
	};
	template<class Capability,size_t N>
	profile_capabilities(const std::array<profile_registration,N>&,const Capability* profile::*) -> profile_capabilities<Capability,N>;
}
