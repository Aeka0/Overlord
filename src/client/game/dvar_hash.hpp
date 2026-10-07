#pragma once
#include <cstdlib>

namespace dvars
{
	// Shared by native dvar lookup and offline launcher configuration editing.
	constexpr int generate_hash(const char* string)
	{
		const char* v1;
		char v2, v6;
		int v4, v5, v7;
		char* end_ptr;

		v1 = string;
		v2 = *string;

		if (v2 == 48 && v1[1] == 120)
		{
			return std::strtoul(v1 + 2, &end_ptr, 16);
		}

		v4 = v2;

		if ((v2 - 65) <= 0x19u)
		{
			v4 = v2 + 32;
		}

		v5 = 0xB3CB2E29 * static_cast<unsigned int>(v4 ^ 0x319712C3);

		if (v2)
		{
			do
			{
				v6 = *++v1;
				v7 = v6;
				if ((v6 - 65) <= 0x19u)
				{
					v7 = v6 + 32;
				}

				v5 = 0xB3CB2E29 * static_cast<unsigned int>(v5 ^ v7);
			} while (v6);
		}

		return v5;
	}
}
