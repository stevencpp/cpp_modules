export module C;

import std.core;

export namespace C {
	void foo() {
		printf("%s\n", __FUNCSIG__);
	}
}