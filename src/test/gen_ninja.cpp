#include <catch2/catch.hpp>
#include <fmt/core.h>
#include <filesystem>

#include "test_config.h"
#include "temp_file_test.h"
#include "cmd_line_utils.h"

namespace gen_ninja {

namespace fs = std::filesystem;

ConfigString vcvarsall_bat { "vcvarsall_bat", "", "the path to vcvarsall.bat" };
ConfigString ninja_path { "ninja_path", "ninja", "path to the ninja executable" };
ConfigString scanner_tool_path { "scanner_tool_path", "../scanner/Debug/cppm_scanner_tool.exe", "path to scanner_tool.exe" };

void generate_compilation_database() {
	cppm::CmdArgs gen_cmd;
	// set up the paths to ninja and the compiler toolchain
	if (vcvarsall_bat != "")
		gen_cmd.append("call \"{}\" x64 && ", vcvarsall_bat);
	gen_cmd.append("cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE "
		"-DCMAKE_MAKE_PROGRAM=\"{}\" -G Ninja ..", ninja_path);
	REQUIRE(0 == run_cmd(gen_cmd));
}

void generate_ninja_from_compilation_database() {
	cppm::CmdArgs tool_gen_cmd { "{} gen_dynamic", scanner_tool_path };
	REQUIRE(0 == run_cmd(tool_gen_cmd));
}

void run_ninja() {
	cppm::CmdArgs ninja_cmd { "{}", ninja_path };
	REQUIRE(0 == run_cmd(ninja_cmd));
}

void make_absolute(ConfigString& str) {
	str.assign(fs::absolute((std::string&)str).string());
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
> CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(test)
add_executable(test a.cpp b.cpp c.cpp d.cpp)
set_property(TARGET test PROPERTY CXX_STANDARD 20)
	)");

	make_absolute(scanner_tool_path);
	make_absolute(ninja_path);

	auto build_dir = test.create_dir("test_build");
	fs::current_path(build_dir);

	generate_compilation_database();

	generate_ninja_from_compilation_database();

	run_ninja();
#if 0
	test.create_files(R"(
> b.cpp
export module c;
> c.cpp
export module b;
	)");

	test.remove_file("d.cpp");
#endif
}

} // namespace gen_ninja