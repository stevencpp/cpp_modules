namespace dummy {} // workaround for a clang bug
#ifdef ERROR
#error "header unit macro isolation not working"
#endif
#define TEST
import "B.hpp";
#ifndef TEST
#error "header unit macro isolation not working"
#endif

inline void foo_a() { foo_b(); }
