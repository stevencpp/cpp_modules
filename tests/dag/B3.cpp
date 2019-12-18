module;
#include <D5.hpp>
export module B3;

import std.core;

import B2;

#ifdef __clang__
import <D6.hxx>;
#else
extern "C++" {
#include <D6.hxx>
}
#endif

#include "funcsig.h"

export namespace B3 {
	void foo() {
		B2::foo();
		D5::foo();
		D6::foo();
		printf("%s\n", FUNCSIG);
	}
}