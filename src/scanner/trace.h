#pragma once

#if 1
#include <fmt/core.h>
#include <chrono>
struct trace_func_guard {
	const char* name;
	using clock = std::chrono::high_resolution_clock;
	clock::time_point start;
	trace_func_guard(const char* name) : name(name) {
		start = clock::now();
	}
	~trace_func_guard() {
		auto end = clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		if (elapsed == 0)
			return;
		fmt::print("{:>50} {:>5} ms\n", name, elapsed);
	}
};
#define TRACE(...) trace_func_guard __trace { __FUNCTION__ };
#define TRACE_BLOCK(name)  trace_func_guard __trace { name };
#endif

#if 0
#include <fmt/core.h>
struct trace_func_guard {
	const char* name;
	trace_func_guard(const char* name) : name(name) {
		fmt::print("entering {}\n", name);
	}
	~trace_func_guard() {
		fmt::print("exiting {}\n", name);
	}
};
#define TRACE(...) trace_func_guard __trace { __FUNCTION__ };
#define TRACE_BLOCK(name)  trace_func_guard __trace { name };
#endif

#ifndef TRACE
#define TRACE(...)
#endif

#ifndef TRACE_BLOCK
#define TRACE_BLOCK(...)
#endif