export module C3;

import std.core;

import C4;

#ifdef __clang__
import <C5.h>;
#else
#include <C5.h>
#endif

#include "funcsig.h"

export namespace C3 {
	void foo() {
		C4::foo();
		C5::foo();
		printf("%s\n", FUNCSIG);
	}
}