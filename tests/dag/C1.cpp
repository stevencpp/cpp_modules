export module C1;

import std.core;

import D1;

export namespace C1 {
	void foo() {
		D1::foo();
		printf("%s\n", __FUNCSIG__);
	}
}