export module D3;

import std.core;
import D4;

#include "funcsig.h"

export namespace D3 {
	void foo() {
		printf("%s\n", FUNCSIG);
		D4::foo();
	}
}