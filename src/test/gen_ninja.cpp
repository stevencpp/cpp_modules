#include <catch2/catch.hpp>
#include <fmt/core.h>
#include <filesystem>
#include <fstream>

#include "test_config.h"
#include "temp_file_test.h"
#include "cmd_line_utils.h"
#include "system_test.h"

namespace gen_ninja {

namespace fs = std::filesystem;

ConfigPath ninja_path { "ninja_path", "", "path to the ninja executable" };
ConfigPath ninja_fork_path { "ninja_fork_path", "../../_deps/ninja-build/Debug/ninja.exe", "path to the ninja executable" };
ConfigPath scanner_tool_path { "scanner_tool_path", "../scanner/Debug/cppm_scanner_tool.exe", "path to scanner_tool.exe" };
ConfigPath clang_scan_deps_path { "clang_scan_deps_path", R"(c:\Program Files\LLVM\bin\clang-scan-deps.exe)" };

ConfigString ninja_run_set { "ninja_run_set", "dag,concurrent" };
ConfigString ninja_compiler { "ninja_compiler", "" };

using system_test::Compiler;

void generate_compilation_database(Compiler compiler = Compiler::msvc) {
	cppm::CmdArgs gen_cmd = { "cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE "
		"-DCMAKE_MAKE_PROGRAM=\"{}\" -G Ninja .. ", ninja_path };
	gen_cmd.append("-DCMAKE_CXX_COMPILER:PATH=\"{}\" ", get_compiler_path(compiler));
	REQUIRE(0 == run_cmd(gen_cmd));
}

void generate_ninja(Compiler compiler = Compiler::msvc) {
	cppm::CmdArgs gen_cmd = { "cmake -DCMAKE_MAKE_PROGRAM=\"{}\" -G Ninja .. ", ninja_path };
	gen_cmd.append("-DCMAKE_CXX_COMPILER:PATH=\"{}\" ", get_compiler_path(compiler));
	REQUIRE(0 == run_cmd(gen_cmd));
}

void generate_ninja_from_compilation_database() {
	cppm::CmdArgs tool_gen_cmd { "{} gen_dynamic --tool_path=\"{}\"", scanner_tool_path, clang_scan_deps_path };
	REQUIRE(0 == run_cmd(tool_gen_cmd));
}

void run_ninja(std::string current_ninja_path, bool expect_no_work_to_do) {
	cppm::CmdArgs ninja_cmd { "{}", current_ninja_path };
	bool failed = false;
	bool found_no_work_to_do = false;
	auto ret = run_cmd_read_lines(ninja_cmd, [&](std::string_view line) {
		fmt::print("> {}\n", line);
		if (line.find("no work to do") != std::string_view::npos)
			found_no_work_to_do = true;
		return true;
	}, [&](std::string_view err_line) {
		fmt::print("ERR: {}\n", err_line);
		failed = true;
		return true;
	});
	REQUIRE(ret == 0);
	//REQUIRE(failed == false); // todo: disabled due to UBSAN alignment errors
	REQUIRE(found_no_work_to_do == expect_no_work_to_do);
}

void write_ninja_config() {
	std::ofstream fout("scanner_config.txt");
	if(!clang_scan_deps_path.empty())
		fout << "tool_path " << clang_scan_deps_path;
}

TEST_CASE("ninja generator test with temp files", "[gen_ninja]") {
	TempFileTest test;

	test.create_files(R"(
> a.cpp
export module a;
import b;
> b.cpp
export module b;
> c.cpp
export module c;
> d.cpp
export module d;
import c;
> main.cpp
import a;
int main() {}
> CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(test LANGUAGES CXX)
add_executable(test a.cpp b.cpp c.cpp d.cpp main.cpp)
set_property(TARGET test PROPERTY CXX_STANDARD 20)
	)");

	if(ninja_path.empty()) ninja_path = ninja_fork_path;

	auto build_dir = test.create_dir("test_build");
	fs::current_path(build_dir);

#ifdef _WIN32
	SECTION("test dyndeps") {
		generate_compilation_database();

		generate_ninja_from_compilation_database();

		run_ninja(ninja_path, false);
		run_ninja(ninja_path, true);
	}
#endif

	SECTION("test ninja fork") {
		auto compiler = GENERATE(ALL_COMPILERS);
		generate_ninja(compiler);

		write_ninja_config();
		run_ninja(ninja_fork_path, false);
		run_ninja(ninja_fork_path, true);
	}
}

using namespace system_test;

TEST_CASE("ninja generator system test", "[ninja]") {
	auto compiler = !ninja_compiler.empty() ? get_compiler_from_str(ninja_compiler.sv()) :
		GENERATE(ALL_COMPILERS);

	for (std::string& test : get_run_set("", ninja_run_set)) {
		full_clean_one(test);
		run_one(test, { .generator = "Ninja", .compiler = compiler });
	}
}

} // namespace gen_ninja