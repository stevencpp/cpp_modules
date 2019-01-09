export module D2;

import std.core;
import D4;

export namespace D2 {
	void foo() {
		printf("%s\n", __FUNCSIG__);
		D4::foo();
	}
}