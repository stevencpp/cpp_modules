export module B2;

import std.core;

import C;

export namespace B2 {
	void foo() {
		C::foo();
		printf("%s\n", __FUNCSIG__);
	}
}