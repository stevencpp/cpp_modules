export module B3;

#include "H3.h"

import std.core;

import C;

#include "funcsig.h"

export namespace B3 {
	void foo() {
		C::foo();
		printf("%s\n", FUNCSIG);
	}
}