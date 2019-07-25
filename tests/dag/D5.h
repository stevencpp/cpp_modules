//import std.core;
#include <stdio.h>

namespace D5 {
	void foo() {
		printf("%s\n", __FUNCSIG__);
	}
}