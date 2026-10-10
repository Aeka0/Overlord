// Toolchain qualification for aggregate publication. Keep the destination's
// address private: passing it to an opaque API can hide the v142 WPO defect.
#include <cstdio>

namespace
{
	struct item { int value{1}; };
	struct frame { item values[1025]{}; };
	static_assert(sizeof(frame) == 4100);
	frame published{};
}

extern "C" __declspec(dllexport) __declspec(noinline) void publish(const frame& input)
{
	published = input;
}

extern "C" __declspec(dllexport) __declspec(noinline) void read_publication(frame& output)
{
	output = published;
}

extern "C" __declspec(dllexport) __declspec(noinline) void reset_publication()
{
	published = {};
}

int main(int argc, char**)
{
	frame input{};
	input.values[0].value = argc + 9;
	input.values[1024].value = argc + 17;
	publish(input);
	frame output{};
	read_publication(output);
	if (output.values[0].value != argc + 9 || output.values[1024].value != argc + 17)
	{
		std::printf("FAIL: aggregate publication expected=%d/%d actual=%d/%d\n",
			argc + 9, argc + 17, output.values[0].value, output.values[1024].value);
		return 1;
	}
	reset_publication();
	read_publication(output);
	if (output.values[0].value != 1 || output.values[1024].value != 1)
	{
		std::puts("FAIL: aggregate reset did not restore its nonzero defaults");
		return 1;
	}
	std::puts("PASS: aggregate publication, readback and reset");
	return 0;
}
