#pragma once

namespace scheduler
{
	enum pipeline
	{
		// Asynchronous pipeline, disconnected from the game.
		async = 0,
		renderer,
		lui,
		server,
		main,
		count,
	};

	namespace detail
	{
		inline thread_local pipeline executing = pipeline::count;
		// Internal dispatcher scope, not a permanent OS-thread assignment. H2
		// may run startup and in-level server frames on different threads.
		class execution_scope final
		{
		public:
			explicit execution_scope(pipeline type) noexcept : previous_(executing) { executing = type; }
			~execution_scope() { executing = previous_; }
			execution_scope(const execution_scope&) = delete;
			execution_scope& operator=(const execution_scope&) = delete;
		private:
			pipeline previous_;
		};
	}
	inline bool is_executing(pipeline type) noexcept
	{
		return type >= pipeline::async && type < pipeline::count && detail::executing == type;
	}
}
