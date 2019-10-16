export module B4;

#include "H4.h"

import std.core;

import C;

export namespace B4 {
	void foo() {
		C::foo();
		printf("%s\n", __FUNCSIG__);
	}
}