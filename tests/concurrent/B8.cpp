export module B8;

#include "H8.h"

import std.core;

import C;

export namespace B8 {
	void foo() {
		C::foo();
		printf("%s\n", __FUNCSIG__);
	}
}