export module A4;

import std.core;

import B2;

#include "funcsig.h"

export namespace A4 {
	void foo() {
		B2::foo();
		printf("%s\n", FUNCSIG);
	}
}