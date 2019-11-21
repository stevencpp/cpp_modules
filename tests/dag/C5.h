//import std.core;
#include <stdio.h>

#include "funcsig.h"

namespace C5 {
	void foo() {
		printf("%s\n", FUNCSIG);
	}
 }