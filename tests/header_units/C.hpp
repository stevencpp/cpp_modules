inline void foo_c() {}
import D;
import "D1.hpp";

#ifdef ERROR
#error "header unit macro isolation not working"
#endif