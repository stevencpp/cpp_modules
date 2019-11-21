export module B8;

#include "H8.h"

import std.core;

import C;

#include "funcsig.h"

export namespace B8 {
	void foo() {
		C::foo();
		printf("%s\n", FUNCSIG);
	}
}