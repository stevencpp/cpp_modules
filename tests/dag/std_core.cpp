#ifdef __clang__
module;
#include <stdio.h>
export module std.core;
export using ::printf;
#endif