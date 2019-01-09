export module A3;

import std.core;

import D4;
import A4;

export namespace A3 {
	void foo() {
		D4::foo();
		A4::foo();
		printf("%s\n", __FUNCSIG__);
	}
}