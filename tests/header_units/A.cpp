#define ERROR
import "A.hpp";

import C;

#ifndef ERROR
#error "header unit macro isolation not working"
#endif

#include "E.hpp" // translated

int main() {
	foo_a();
	foo_c();
	foo_d();
	foo_d1();
	foo_e();
	
	return 0;
}