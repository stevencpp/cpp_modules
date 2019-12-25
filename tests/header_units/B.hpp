void foo_b();
#ifdef ERROR
#error "header unit macro isolation not working"
#endif
#define ERROR

import "C.hpp";

#undef ERROR
#undef TEST