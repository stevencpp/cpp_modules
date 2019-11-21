export module C3;

import std.core;

import C4;

#include <C5.h>

#include "funcsig.h"

export namespace C3 {
	void foo() {
		C4::foo();
		C5::foo();
		printf("%s\n", FUNCSIG);
	}
}