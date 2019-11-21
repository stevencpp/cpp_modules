export module D2;

import std.core;
import D4;

#include "funcsig.h"

export namespace D2 {
	void foo() {
		printf("%s\n", FUNCSIG);
		D4::foo();
	}
}