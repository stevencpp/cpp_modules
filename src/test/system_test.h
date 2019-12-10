#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace system_test {

void full_clean_one(const std::string& test);

enum Compiler {
	msvc,
	clang_cl,
	clang,
	gcc
};

#ifdef _WIN32
	#define ALL_COMPILERS Compiler::msvc, Compiler::clang_cl, Compiler::clang
#else
	#define ALL_COMPILERS Compiler::clang
#endif

std::string_view get_compiler_path(Compiler compiler);
Compiler get_compiler_from_str(std::string_view compiler);

struct run_one_params {
	std::string_view test_path;
	std::string_view generator;
	std::string_view arch;
	// msbuild specific:
	std::string_view toolset;
	std::string_view verbosity;
	std::string_view configuration;
	// ninja specific:
	Compiler compiler;
	bool expect_no_work_to_do = false;
	bool expect_out_of_date = true;
};
void run_one(const std::string& test, const run_one_params& p = {});

std::vector<std::string> get_run_set(std::string_view run_one, std::string_view run_set);

} // namespace system_test