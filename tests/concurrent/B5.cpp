export module B5;

#include "H5.h"

import std.core;

import C;

#include "funcsig.h"

export namespace B5 {
	void foo() {
		C::foo();
		printf("%s\n", FUNCSIG);
	}
}