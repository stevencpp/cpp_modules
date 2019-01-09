export module C4;

import std.core;

export namespace C4 {
	void foo() {
		printf("%s\n", __FUNCSIG__);
	}
}