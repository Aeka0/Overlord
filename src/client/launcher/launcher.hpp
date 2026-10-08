#pragma once
#include <memory>

class launcher final
{
public:
	enum class mode { none, singleplayer, multiplayer, server, survival, zombies };
	launcher();
	~launcher();
	int run() const;
private:
	struct impl;
	std::unique_ptr<impl> impl_;
};
