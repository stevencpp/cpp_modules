export module C3;

import std.core;

import C4;

export namespace C3 {
	void foo() {
		C4::foo();
		printf("%s\n", __FUNCSIG__);
	}
}