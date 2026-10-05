#pragma once
#include <utils/concurrency.hpp>
#include <cstdint>
namespace game {struct VariableValue;}

namespace scripting
{
	using shared_table_t = std::unordered_map<std::string, std::string>;

	extern std::unordered_map<int, std::unordered_map<std::string, int>> fields_table;
	extern std::unordered_map<std::string, std::unordered_map<std::string, const char*>> script_function_table;
	extern std::unordered_map<std::string, std::vector<std::pair<std::string, const char*>>> script_function_table_sort;
	extern utils::concurrency::container<shared_table_t> shared_table;

	extern std::unordered_map<std::string, int> get_dvar_int_overrides;

	extern std::string current_file;

	void on_shutdown(const std::function<void(bool, bool)>& callback);
	// Startup-only registration. Runs after native spawn or saved-level loading;
	// consumers can invalidate derived state even if player pointers/time repeat.
	void on_level_start(const std::function<void()>& callback);
	// Startup-only additive event routing. The original notification and its
	// arguments are always delivered; a filter may name one additional event.
	using notify_alias=std::int32_t(*)(unsigned,std::int32_t,const game::VariableValue*);
	void on_notify_alias(notify_alias);
	std::optional<std::string> get_canonical_string(const unsigned int id);
	std::string get_token_single(unsigned int id);
	std::string get_token(unsigned int id);
}
