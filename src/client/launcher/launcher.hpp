#pragma once
#include "html/html_window.hpp"
#include "game_language.hpp"

class launcher final
{
public:
	enum class mode
	{
		none,
		singleplayer,
		multiplayer,
		server,
		// Surrogates
		survival,
		zombies,
	};

	launcher();

	mode run() const;

private:
	mode mode_ = mode::none;

	html_window main_window_;
	launcher_game_language::service game_language_;

	void select_mode(mode mode);

	void create_main_menu();

	static std::string load_content(int res);
};
