export module A2;

import std.core;

import D2;
import A3;

export namespace A2 {
	void foo() {
		D2::foo();
		A3::foo();
		printf("%s\n", __FUNCSIG__);
	}
}