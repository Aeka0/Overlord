#include "scripted_sequence_tests.hpp"
#include "oilrig_sequence_tests.hpp"
#include "snowmobile_boarding_tests.hpp"
#include <iostream>

int main()
{
	unsigned checks{}, failures{};
	const auto check = [&](bool passed, const char* message)
	{
		++checks;
		if (!passed) { ++failures; std::cerr << message << '\n'; }
	};
	scripted_sequence_tests(check);
	oilrig_sequence_tests(check);
	snowmobile_boarding_tests(check);
	std::cout << "campaign policy: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
