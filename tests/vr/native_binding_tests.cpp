#include "native_binding_tests.hpp"
#include <iostream>

int main()
{
	unsigned checks{}, failures{};
	const auto check = [&](bool passed, const char* message)
	{
		++checks;
		if (!passed) { ++failures; std::cerr << message << '\n'; }
	};
	native_binding_tests::run(check);
	std::cout << "native bindings: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
