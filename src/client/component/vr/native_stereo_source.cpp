#include <std_include.hpp>
#include "native_stereo_source.hpp"

namespace vr::native_stereo_source
{
	namespace
	{
		std::atomic<query> source_query{};
	}
	registration register_query(query value)
	{
		return {source_query, value};
	}
	proof current()
	{
		const auto query = source_query.load();
		return query ? query() : proof{};
	}
}
