export module B6;

#include "H6.h"

import std.core;

import C;

#include "funcsig.h"

export namespace B6 {
	void foo() {
		C::foo();
		printf("%s\n", FUNCSIG);
	}
}