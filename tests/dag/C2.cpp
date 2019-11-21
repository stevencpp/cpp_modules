export module C2;

import std.core;

import C3;

#include "funcsig.h"

export namespace C2 {
	void foo() {
		C3::foo();
		printf("%s\n", FUNCSIG);
	}
}