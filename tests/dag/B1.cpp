export module B1;

import std.core;

import D1;
import B3;
import B4;

#include "funcsig.h"

export namespace B1 {
	void foo() {
		D1::foo();
		B3::foo();
		B4::foo();
		printf("%s\n", FUNCSIG);
	}
}