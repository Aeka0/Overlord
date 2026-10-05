#pragma once

#define FUNC_TABLE_SIZE 0x1000

namespace gsc
{
	extern void* func_table[FUNC_TABLE_SIZE];

	void scr_error(bool force_print, const char* fmt, ...);
	void add_function(const std::string& name, game::BuiltinFunction function);
	// Startup-only, bounded observers. Returning false preserves the native call;
	// true means the interceptor supplied its return value using an isolated call.
	using builtin_interceptor=bool(*)(game::BuiltinFunction);
	void intercept_builtin(const std::string& name,builtin_interceptor);
}
