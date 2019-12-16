//import std.core; // clang -- fatal error : module 'std' not found
#include <stdio.h>

#include "funcsig.h"

namespace D5 {
	void foo() {
		printf("%s\n", FUNCSIG);
	}
}