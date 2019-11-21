export module D4;

import std.core;

#include "funcsig.h"

export namespace D4 {
	void foo() {
		printf("%s\n", FUNCSIG);
	}
}