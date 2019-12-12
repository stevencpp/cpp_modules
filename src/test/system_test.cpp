#include "system_test.h"

#include <filesystem>
#include <catch2/catch.hpp>
#pragma warning(disable:4275) // non dll-interface class 'std::runtime_error' used as base for dll-interface class 'fmt::v6::format_error'
#include <fmt/color.h>
// the split_view code doesn't compile with Apple Clang 11 :(
#define NO_RANGE_V3
#ifndef NO_RANGE_V3
#include <range/v3/view/split.hpp>
#endif
#include "test_config.h"
#include "cmd_line_utils.h"

namespace fs = std::filesystem;

namespace system_test {

ConfigPath cfg_test_path { "system_test_path", "../../../tests" };
ConfigPath clang_cxx_path { "clang_cxx_path", R"(C:\Program Files\LLVM\bin\clang++.exe)" };
ConfigPath clang_cl_path { "clang_cl_path", R"(C:\Program Files\LLVM\bin\clang-cl.exe)" };
ConfigPath clang_scan_deps_path { "clang_scan_deps_path", R"(c:\Program Files\cpp_modules\bin\clang-scan-deps.exe)" };

void full_clean_one(const std::string& test)
{
	auto test_path = fs::path { cfg_test_path.str() } / test;
	REQUIRE(fs::exists(test_path));
	auto build_path = test_path / "build";
	if (!fs::exists(build_path))
		return;
	for (auto file : fs::directory_iterator { build_path })
		if (file.is_directory() && file.path().extension() == ".dir")
			fs::remove_all(file);
	for (auto dir : { "Debug", "intermediate", "x64", "CMakeFiles" })
		fs::remove_all(build_path / dir);
	for (auto file : { "CMakeCache.txt", "build.ninja", ".ninja_log", ".ninja_deps",
		"scanner.mdb", "scanner.mdb-lock" })
		fs::remove(build_path / file);
}

void run_one_msbuild(const std::string& test, const run_one_params& p, const fs::path& build_path) {
	cppm::CmdArgs generate_cmd { "cmake -G \"{}\" -A \"{}\" ../ ", p.generator, p.arch };
	if (p.toolset != "")
		generate_cmd.append("-DCMAKE_GENERATOR_TOOLSET={} ", p.toolset);
	if (p.toolset == "ClangCl") {
		generate_cmd.append("-DCMAKE_VS_GLOBALS:STRING=\"LLVMInstallDir={}\" ", R"(c:\Program Files\LLVM\bin)");
		generate_cmd.append("-DCMAKE_CXX_COMPILER:PATH=\"{}\" ", clang_cl_path);
		// none of this works in 3.16, just don't do this test until it's fixed
		return;
	}
	if (!clang_scan_deps_path.empty())
		generate_cmd.append("-DCPPM_SCANNER_PATH=\"{}\" ", clang_scan_deps_path);
	REQUIRE(0 == run_cmd(generate_cmd));

	// try to make the build environment the same as running from the IDE 
	auto build_params = fmt::format("SolutionDir={};SolutionPath={}",
		build_path.string(), (build_path / ("test_" + test + ".sln")).string());
	std::string verbosity_long = "minimal";
	if (p.verbosity == "d") verbosity_long = "diagnostic";
	auto log_params = fmt::format("LogFile=build.log;Verbosity={}", verbosity_long);
	cppm::CmdArgs build_cmd { "cmake --build . --config {} --parallel -- -v:{} \"/p:{}\" -flp:{}",
		p.configuration, p.verbosity, build_params, log_params };
	CHECK(0 == run_cmd(build_cmd));

	CHECK(fs::exists(build_path / p.configuration / "A.exe"));
}

std::string_view get_compiler_path(Compiler compiler) {
	if (compiler == msvc)
		return "cl.exe";
	else if (compiler == clang_cl)
		return clang_cl_path;
	else if (compiler == clang)
		return clang_cxx_path;
	throw std::runtime_error("unsupported compiler");
}

// true iff a contains b -- todo: use string_view::contains in C++20
static bool contains(std::string_view a, std::string_view b) {
	return a.find(b) != std::string_view::npos;
}

void run_one_ninja(const std::string& test, const run_one_params& p, const fs::path& build_path) {
	cppm::CmdArgs generate_cmd { "cmake -G Ninja -DCMAKE_BUILD_TYPE={} ../ ", p.configuration };
	//generate_cmd.append("-DCMAKE_VERBOSE_MAKEFILE=ON ");
	if(!clang_scan_deps_path.empty())
		generate_cmd.append("-DCPPM_SCANNER_PATH=\"{}\" ", clang_scan_deps_path);
	generate_cmd.append("-DCMAKE_CXX_COMPILER:PATH=\"{}\" ", get_compiler_path(p.compiler));

	REQUIRE(0 == run_cmd(generate_cmd));

	cppm::CmdArgs run_ninja_cmd { "cmake --build . --parallel " };
	
	bool failed = false;
	bool found_no_work_to_do = false;
	bool found_out_of_date = false;
	auto ret = run_cmd_read_lines(run_ninja_cmd, [&](std::string_view line) {
		fmt::print("> {}\n", line);
		if (contains(line, "no work to do"))
			found_no_work_to_do = true;
		// if we scanned some items (> 0) then they were out of date
		if (contains(line, "scanned") && !contains(line, "scanned 0"))
			found_out_of_date = true;
		return true;
	}, [&](std::string_view err_line) {
		fmt::print("ERR: {}\n", err_line);
		failed = true;
		return true;
	});

	CHECK(ret == 0);
	CHECK(!failed);
	CHECK(found_out_of_date == p.expect_out_of_date);
	CHECK(found_no_work_to_do == p.expect_no_work_to_do);

#ifdef _WIN32
	CHECK(fs::exists(build_path / "A.exe"));
#else
	CHECK(fs::exists(build_path / "A"));
#endif
}

void run_one(const std::string& test, const run_one_params& p) {
	auto test_path = fs::path {
		p.test_path.empty() ? cfg_test_path.sv() : p.test_path
	} / test;
	REQUIRE(fs::exists(test_path));
	auto build_path = test_path / "build";
	fs::create_directory(build_path);
	fs::current_path(build_path);

	fmt::print(fmt::fg(fmt::color::yellow), "=== running '{}' with {} / {} ===\n", 
		test, (p.generator == "Ninja" ? "ninja" : "msbuild"), compiler_name(p.compiler));

	if (p.generator == "Ninja") {
		run_one_ninja(test, p, build_path);
	} else {
		run_one_msbuild(test, p, build_path);
	}
}

#ifdef NO_RANGE_V3
std::pair<std::string_view, std::string_view> split_in_two(
	std::string_view str, std::string_view delim) {
	auto first = str.substr(0, str.find(delim));
	auto second = str.substr(std::min(first.size() + 1, str.size()));
	return { first, second };
}
#else
template<typename ContiguousRange>
std::string_view to_sv(ContiguousRange&& range) {
	return { &(*range.begin()), (std::size_t)ranges::distance(range.begin(), range.end()) };
}
#endif

std::vector<std::string> get_run_set(std::string_view run_one, std::string_view run_set) {
	std::vector<std::string> ret;
	if (!run_one.empty()) {
		ret.emplace_back(run_one);
	} else {
#ifdef NO_RANGE_V3
		while (true) {
			auto [test, remaining] = split_in_two(run_set, ",");
			ret.emplace_back(test);
			if (remaining.empty()) break;
			run_set = remaining;
		}
#else
		for (auto test : ranges::split_view { run_set, ',' })
			ret.emplace_back(to_sv(test));
#endif
	}
	return ret;
}

Compiler get_compiler_from_str(std::string_view compiler) {
	if (compiler == "msvc") return Compiler::msvc;
	else if (compiler == "clang_cl") return Compiler::clang_cl;
	else if (compiler == "clang") return Compiler::clang;
	else if (compiler == "gcc") return Compiler::gcc;
	throw std::invalid_argument(fmt::format("unsupported compiler: {}", compiler));
}

std::string_view compiler_name(Compiler compiler) {
	if (compiler == Compiler::msvc) return "msvc";
	else if (compiler == Compiler::clang_cl) return"clang_cl";
	else if (compiler == Compiler::clang) return"clang";
	else if (compiler == Compiler::gcc) return"gcc";
	throw std::invalid_argument(fmt::format("unsupported compiler: {}", compiler));
}

} // namespace system_test