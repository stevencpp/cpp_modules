export module B3;

import std.core;

import B2;
#include <D5.h>
#include <D6.h>

export namespace B3 {
	void foo() {
		B2::foo();
		D5::foo();
		D6::foo();
		printf("%s\n", __FUNCSIG__);
	}
}