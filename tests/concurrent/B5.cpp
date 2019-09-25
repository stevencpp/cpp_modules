export module B5;

import std.core;

import C;

export namespace B5 {
	void foo() {
		C::foo();
		printf("%s\n", __FUNCSIG__);
	}
}