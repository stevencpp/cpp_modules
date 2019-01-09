export module B4;

import std.core;

import D4;

export namespace B4 {
	void foo() {
		D4::foo();
		printf("%s\n", __FUNCSIG__);
	}
}