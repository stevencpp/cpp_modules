export module C;

import std.core;

#include "funcsig.h"

export namespace C {
	void foo() {
		printf("%s\n", FUNCSIG);
	}
}