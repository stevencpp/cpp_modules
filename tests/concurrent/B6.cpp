export module B6;

import std.core;

import C;

export namespace B6 {
	void foo() {
		C::foo();
		printf("%s\n", __FUNCSIG__);
	}
}