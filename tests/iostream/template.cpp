#ifndef USE_PCH
	#ifdef USE_MODULES
		#ifdef USE_HEADER_UNITS
			import <iostream>;
		#else
			import iostream;
		#endif
	#else
		#include <iostream>
	#endif
#endif

using namespace std;

static void f() {
	cout << "Hello, World!" << endl;
}