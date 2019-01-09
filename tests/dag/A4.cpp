export module A4;

import std.core;

import B2;

export namespace A4 {
	void foo() {
		B2::foo();
		printf("%s\n", __FUNCSIG__);
	}
}