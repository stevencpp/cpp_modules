#include "system_test.h"

#include <filesystem>
#include <catch2/catch.hpp>
#pragma warning(disable:4275) // non dll-interface class 'std::runtime_error' used as base for dll-interface class 'fmt::v6::format_error'
#include <fmt/color.h>
#include <range/v3/view/split.hpp>
#include "test_config.h"
#include "cmd_line_utils.h"

namespace fs = std::filesystem;

namespace system_test {

ConfigPath cfg_test_path { "system_test_path", "../../../tests" };
ConfigPath clang_cxx_path { "clang_cxx_path", R"(C:\Program Files\LLVM\bin\clang++.exe)" };
ConfigPath clang_cl_path { "clang_cl_path", R"(C:\Program Files\LLVM\bin\clang-cl.exe)" };
ConfigPath clang_scan_deps_path { "clang_scan_deps_path", R"(c:\Program Files\LLVM\bin\clang-scan-deps.exe)" };

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
	for (auto file : { "CMakeCache.txt", "build.ninja", ".ninja_log", ".ninja_deps" })
		fs::remove(build_path / file);
}

void run_one_msbuild(const std::string& test, const run_one_params& p, const fs::path& build_path) {
	cppm::CmdArgs generate_cmd { "cmake -G \"{}\" -A \"{}\" ../ ", p.generator, p.arch };
	if (p.toolset != "")
		generate_cmd.append("-DCMAKE_GENERATOR_TOOLSET={} ", p.toolset);
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
	REQUIRE(0 == run_cmd(build_cmd));

	REQUIRE(fs::exists(build_path / p.configuration / "A.exe"));
}

void run_one_ninja(const std::string& test, const run_one_params& p, const fs::path& build_path) {
	cppm::CmdArgs generate_cmd { "cmake -G Ninja -DCMAKE_BUILD_TYPE={} ../ ", p.configuration };
	if(!clang_scan_deps_path.empty())
		generate_cmd.append("-DCPPM_SCANNER_PATH=\"{}\" ", clang_scan_deps_path);
	if (p.compiler == msvc)
		generate_cmd.append("-DCMAKE_CXX_COMPILER:PATH=cl.exe");
	else if (p.compiler == clang_cl)
		generate_cmd.append("-DCMAKE_CXX_COMPILER:PATH=\"{}\" ", clang_cl_path);
	else if(p.compiler == clang)
		generate_cmd.append("-DCMAKE_CXX_COMPILER:PATH=\"{}\" ", clang_cxx_path);
	REQUIRE(0 == run_cmd(generate_cmd));

	cppm::CmdArgs run_ninja_cmd { "cmake --build . --parallel " };
	REQUIRE(0 == run_cmd(run_ninja_cmd));

	REQUIRE(fs::exists(build_path / "A.exe"));
}

void run_one(const std::string& test, const run_one_params& p) {
	auto test_path = fs::path {
		p.test_path.empty() ? cfg_test_path.sv() : p.test_path
	} / test;
	REQUIRE(fs::exists(test_path));
	auto build_path = test_path / "build";
	fs::create_directory(build_path);
	fs::current_path(build_path);

	fmt::print(fmt::fg(fmt::color::yellow), "=== running {} ===\n", test);

	if (p.generator == "Ninja") {
		run_one_ninja(test, p, build_path);
	} else {
		run_one_msbuild(test, p, build_path);
	}
}

template<typename ContiguousRange>
std::string_view to_sv(ContiguousRange&& range) {
	return { &(*range.begin()), (std::size_t)ranges::distance(range.begin(), range.end()) };
}

std::vector<std::string> get_run_set(std::string_view run_one, std::string_view run_set) {
	std::vector<std::string> ret;
	if (!run_one.empty()) {
		ret.emplace_back(run_one);
	} else {
		for (auto test : ranges::split_view { run_set, ',' })
			ret.emplace_back(to_sv(test));
	}
	return ret;
}

} // namespace system_test