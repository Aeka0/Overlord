#include "component/gui/input_capture.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
	void require(const bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	void native_menu_without_overlay()
	{
		gui::input_capture overlay;
		int catchers = 0x10;
		for (unsigned frame = 0; frame < 120; ++frame)
		{
			require(!overlay.is_open(), "native menu must not open the developer overlay");
			overlay.set_open(catchers, false);
			require(catchers == 0x10, "closed overlay must never clear native menu input");
		}
		catchers = 0; // Native resume owns its own transition.
		require(!overlay.is_open() && catchers == 0, "native resume needs no overlay cleanup");
	}

	void overlay_over_native_menu()
	{
		for (const int original : {0x10, 0x11, 0x30})
		{
			gui::input_capture overlay;
			int catchers = original;
			overlay.set_open(catchers, true);
			require(overlay.is_open() && catchers == original, "borrow existing native menu catcher");
			overlay.set_open(catchers, true);
			catchers |= 0x40; // Another input owner changes an unrelated bit.
			overlay.set_open(catchers, false);
			require(!overlay.is_open() && catchers == (original | 0x40),
				"closing overlay must preserve native menu, console and later input owners");
		}
	}

	void overlay_over_gameplay()
	{
		gui::input_capture overlay;
		int catchers = 0;
		for (unsigned cycle = 0; cycle < 3; ++cycle)
		{
			overlay.set_open(catchers, true);
			require(overlay.is_open() && (catchers & 0x10), "opening captures immediately, before rendering");
			overlay.set_open(catchers, true); // Repeated requests must not lose ownership.
			catchers |= 1;
			overlay.set_open(catchers, false); // Escape, F10 and shutdown share this path.
			require(!overlay.is_open() && catchers == 1, "release only the bit added by this overlay");
			overlay.set_open(catchers, false);
			require(catchers == 1, "repeated close does not alter another input owner");
		}
		overlay.set_open(catchers, true);
		catchers = 0; // Engine reset while the overlay is open.
		require(overlay.is_open() && catchers == 0, "render observation must not reassert input bits");
		overlay.set_open(catchers, false);
		require(catchers == 0, "close after native reset does not restore stale bits");
	}

	void native_menu_takes_over_open_overlay()
	{
		gui::input_capture overlay;
		int catchers = 0;
		overlay.set_open(catchers, true);
		// Native pause explicitly claims the SAME bit. Its numerical value does
		// not change, so comparing a before/after snapshot cannot detect this.
		catchers |= 0x10;
		overlay.native_catcher_write();
		overlay.set_open(catchers, false);
		require(catchers == 0x10 && !overlay.is_open(), "closing F10 must retain a later native menu claim");
		catchers = 0;
		overlay.set_open(catchers, true);
		catchers = 1; // Native replacement preserves only the console.
		overlay.native_catcher_write();
		overlay.set_open(catchers, false);
		require(catchers == 1, "native replacement must not be undone on overlay close");
		catchers = 0;
		overlay.set_open(catchers, true);
		catchers &= ~0x10;
		overlay.native_catcher_write();
		catchers |= 0x10;
		overlay.native_catcher_write();
		overlay.set_open(catchers, false);
		require(catchers == 0x10, "native close/reopen while F10 is open retains the new menu owner");
	}
}

int main()
{
	try
	{
		native_menu_without_overlay(); overlay_over_native_menu(); overlay_over_gameplay();
		native_menu_takes_over_open_overlay();
		std::cout << "PASS: GUI input capture ownership and native menu preservation (CPU only)\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
