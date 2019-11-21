export module D1;

import std.core;
import D2;
import D3;

#include "funcsig.h"

export namespace D1 {
	void foo() {
		printf("%s\n", FUNCSIG);
		D2::foo();
		D3::foo();
	}
}