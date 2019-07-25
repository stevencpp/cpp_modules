//import std.core;
#include <stdio.h>

namespace C5 {
	void foo() {
		printf("%s\n", __FUNCSIG__);
	}
 }