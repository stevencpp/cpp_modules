export module B2;

import std.core;

import C1;
import C2;
import C3;
import C4;

#include "funcsig.h"

export namespace B2 {
	void foo() {
		C1::foo();
		C2::foo();
		C3::foo();
		C4::foo();
		printf("%s\n", FUNCSIG);
	}
}