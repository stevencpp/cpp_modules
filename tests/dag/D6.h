//import std.core;
#include <stdio.h>

namespace D6 {
	void foo() {
		printf("%s\n", __FUNCSIG__);
	}
}