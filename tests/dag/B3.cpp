export module B3;

import std.core;

import B2;
#include <D5.h>
//import <D6.h>; // clang pp - doesn't work yet
#include <D6.h>

#include "funcsig.h"

export namespace B3 {
	void foo() {
		B2::foo();
		D5::foo();
		D6::foo();
		printf("%s\n", FUNCSIG);
	}
}