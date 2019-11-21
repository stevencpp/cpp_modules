export module C1;

import std.core;

import D1;

#include "funcsig.h"

export namespace C1 {
	void foo() {
		D1::foo();
		printf("%s\n", FUNCSIG);
	}
}