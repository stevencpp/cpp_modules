#ifndef USE_PCH
	#ifdef USE_HEADER_UNITS
		import <iostream>;
	#elif defined(USE_MODULES)
		import iostream;
	#else
		#include <iostream>
	#endif
#endif

using namespace std;

static void f() {
	cout << "Hello, World!" << endl;
}