#ifdef __clang__
module;
#include <D5.hpp>
#endif
export module B3;

#ifndef __clang__
extern "C++" {
#include <D5.hpp>
}
#endif

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