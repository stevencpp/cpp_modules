//import std.core; // clang -- fatal error : module 'std' not found
#include <stdio.h>

namespace D5 {
	void foo() {
		printf("%s\n", __FUNCSIG__);
	}
}