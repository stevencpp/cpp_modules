export module B2;

#include "H2.h"

import std.core;

import C;

export namespace B2 {
	void foo() {
		C::foo();
		printf("%s\n", __FUNCSIG__);
	}
}