export module D1;

import std.core;
import D2;
import D3;

export namespace D1 {
	void foo() {
		printf("%s\n", __FUNCSIG__);
		D2::foo();
		D3::foo();
	}
}