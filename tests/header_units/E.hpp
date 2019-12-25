#ifdef ERROR
#error "header unit macro isolation not working"
#endif

#define ERROR
#include "F.hpp" // translated

inline void foo_e() { foo_f(); }