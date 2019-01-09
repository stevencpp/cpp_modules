export module B3;

import std.core;

import B2;

export namespace B3 {
	void foo() {
		B2::foo();
		printf("%s\n", __FUNCSIG__);
	}
}