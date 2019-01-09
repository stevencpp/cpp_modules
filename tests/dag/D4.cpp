export module D4;

import std.core;

export namespace D4 {
	void foo() {
		printf("%s\n", __FUNCSIG__);
	}
}