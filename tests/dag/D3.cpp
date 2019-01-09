export module D3;

import std.core;
import D4;

export namespace D3 {
	void foo() {
		printf("%s\n", __FUNCSIG__);
		D4::foo();
	}
}