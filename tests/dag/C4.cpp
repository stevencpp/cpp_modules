export module C4;

import std.core;

#include "funcsig.h"

export namespace C4 {
	void foo() {
		printf("%s\n", FUNCSIG);
	}
}