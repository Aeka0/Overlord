#include <std_include.hpp>
#include "component_loader.hpp"

#include <condition_variable>

namespace
{
	enum class teardown_state
	{
		not_started,
		running,
		complete,
	};

	std::mutex teardown_mutex;
	std::condition_variable teardown_complete;
	teardown_state current_teardown_state = teardown_state::not_started;
	std::thread::id teardown_thread;

	void report_teardown_exception(const char* const message)
	{
		OutputDebugStringA("component pre_destroy failed: ");
		OutputDebugStringA(message != nullptr ? message : "unknown exception");
		OutputDebugStringA("\n");
	}
}

void component_loader::register_component(std::unique_ptr<component_interface>&& component_)
{
	get_components().push_back(std::move(component_));
}

bool component_loader::post_start()
{
	static auto handled = false;
	if (handled) return true;
	handled = true;

	try
	{
		for (const auto& component_ : get_components())
		{
			component_->post_start();
		}
	}
	catch (premature_shutdown_trigger&)
	{
		return false;
	}

	return true;
}

bool component_loader::post_load()
{
	static auto handled = false;
	if (handled) return true;
	handled = true;

	clean();

	try
	{
		for (const auto& component_ : get_components())
		{
			component_->post_load();
		}
	}
	catch (premature_shutdown_trigger&)
	{
		return false;
	}

	return true;
}

void component_loader::post_unpack()
{
	static auto handled = false;
	if (handled) return;
	handled = true;

	for (const auto& component_ : get_components())
	{
		component_->post_unpack();
	}
}

void component_loader::pre_destroy()
{
	{
		std::unique_lock lock(teardown_mutex);
		if (current_teardown_state == teardown_state::complete)
		{
			return;
		}

		if (current_teardown_state == teardown_state::running)
		{
			if (teardown_thread == std::this_thread::get_id())
			{
				return;
			}

			teardown_complete.wait(lock, []
			{
				return current_teardown_state == teardown_state::complete;
			});
			return;
		}

		current_teardown_state = teardown_state::running;
		teardown_thread = std::this_thread::get_id();
	}

	for (const auto& component_ : get_components())
	{
		try
		{
			component_->pre_destroy();
		}
		catch (const std::exception& error)
		{
			report_teardown_exception(error.what());
		}
		catch (...)
		{
			report_teardown_exception("unknown exception");
		}
	}

	{
		const std::lock_guard lock(teardown_mutex);
		current_teardown_state = teardown_state::complete;
		teardown_thread = {};
	}
	teardown_complete.notify_all();
}

void component_loader::clean()
{
	auto& components = get_components();
	for (auto i = components.begin(); i != components.end();)
	{
		if (!(*i)->is_supported())
		{
			(*i)->pre_destroy();
			i = components.erase(i);
		}
		else
		{
			++i;
		}
	}
}

void* component_loader::load_import(const std::string& library, const std::string& function)
{
	void* function_ptr = nullptr;

	for (const auto& component_ : get_components())
	{
		auto* const component_function_ptr = component_->load_import(library, function);
		if (component_function_ptr)
		{
			function_ptr = component_function_ptr;
		}
	}

	return function_ptr;
}

void component_loader::trigger_premature_shutdown()
{
	throw premature_shutdown_trigger();
}

std::vector<std::unique_ptr<component_interface>>& component_loader::get_components()
{
	using component_vector = std::vector<std::unique_ptr<component_interface>>;
	static auto* const components = new component_vector;
	return *components;
}
