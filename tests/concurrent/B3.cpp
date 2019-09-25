export module B3;

import std.core;

import C;

export namespace B3 {
	void foo() {
		C::foo();
		printf("%s\n", __FUNCSIG__);
	}
}