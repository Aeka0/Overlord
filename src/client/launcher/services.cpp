#include <std_include.hpp>
#include "services.hpp"
#include "vr_settings.hpp"
#include "localization.hpp"
#include "preflight.hpp"
#include "preflight_policy.hpp"
#include "product.hpp"
#include <version.h>
#include <utils/flags.hpp>

namespace launcher_bridge
{
	namespace
	{
		using nlohmann::json;
		json execute(const request& request)
		{
			const auto& params = request.params;
			if (request.method == "game.launch")
			{
				const auto report = launcher_preflight::check();
				return {{"ok", true}, {"launched", false}, {"preflight", report},
					{"approved", launcher_preflight::launch_allowed(report, params.at("warnings"))}};
			}
			if (request.method == "game.preflight")
			{
				if (!params.empty()) throw std::runtime_error("Invalid preflight parameters.");
				return launcher_preflight::check();
			}
			if (request.method == "game.fix")
			{
				if (params.size() != 1 || !params.contains("id") || !params["id"].is_string()) throw std::runtime_error("Invalid repair parameters.");
				return launcher_preflight::fix(params["id"].get<std::string>());
			}
			if (request.method == "bootstrap")
			{
				if (!params.empty()) throw std::runtime_error("Invalid bootstrap parameters.");
				return {{"ok", true}, {"settings", json::parse(launcher_vr_settings::load())},
					{"preferences", json::parse(launcher_localization::load())}, {"version", VERSION}, {"product", product::name},
					{"renderProbe", utils::flags::get_flag("launcher-smoke").has_value()},
					{"renderProbePage", utils::flags::get_flag("launcher-probe-page").value_or("settings")}};
			}
			if (request.method == "settings.save")
			{
				if (params.size() != 1 || !params.contains("values")) throw std::runtime_error("Invalid settings parameters.");
				return json::parse(launcher_vr_settings::save(params["values"].dump()));
			}
			if (request.method == "settings.disableRisk")
			{
				if (!params.empty()) throw std::runtime_error("Invalid risk-setting parameters.");
				return json::parse(launcher_vr_settings::disable_risk_settings());
			}
			if (request.method == "preferences.language")
			{
				if (params.size() != 1 || !params.contains("language") || !params["language"].is_string()) throw std::runtime_error("Invalid language parameters.");
				return json::parse(launcher_localization::save(params["language"].get<std::string>()));
			}
			throw std::runtime_error("Unsupported launcher service.");
		}
	}

	services::services() : worker_([this] { run(); }) {}
	services::~services()
	{
		{ std::lock_guard lock(mutex_); stopping_ = true; requests_.clear(); }
		wake_.notify_one();
		worker_.join();
	}
	void services::enqueue(request request)
	{
		{ std::lock_guard lock(mutex_);
			if (stopping_ || requests_.size() + responses_.size() >= 64) throw std::runtime_error("Too many pending launcher requests.");
			requests_.push_back(std::move(request)); }
		wake_.notify_one();
	}
	bool services::idle() { std::lock_guard lock(mutex_); return !busy_ && requests_.empty(); }
	std::vector<nlohmann::json> services::drain()
	{
		std::lock_guard lock(mutex_);
		std::vector<nlohmann::json> result;
		while (!responses_.empty()) { result.push_back(std::move(responses_.front())); responses_.pop_front(); }
		return result;
	}
	void services::run()
	{
		for (;;)
		{
			request next;
			{
				std::unique_lock lock(mutex_);
				wake_.wait(lock, [&] { return stopping_ || !requests_.empty(); });
				if (stopping_) return;
				next = std::move(requests_.front()); requests_.pop_front(); busy_ = true;
			}
			nlohmann::json result;
			try { result = execute(next); }
			catch (const std::exception& error) { result = {{"ok", false}, {"error", error.what()}}; }
			{
				std::lock_guard lock(mutex_);
				busy_ = false;
				if (!stopping_) responses_.push_back(response(next, std::move(result)));
			}
		}
	}
}
