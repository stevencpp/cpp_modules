export module B4;

import std.core;

import D4;

#include "funcsig.h"

export namespace B4 {
	void foo() {
		D4::foo();
		printf("%s\n", FUNCSIG);
	}
}