export module B7;

#include "H7.h"

import std.core;

import C;

#include "funcsig.h"

export namespace B7 {
	void foo() {
		C::foo();
		printf("%s\n", FUNCSIG);
	}
}