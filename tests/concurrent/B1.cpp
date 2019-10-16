export module B1;

#include "H1.h"

import std.core;

import C;

export namespace B1 {
	void foo() {
		C::foo();
		printf("%s\n", __FUNCSIG__);
	}
}