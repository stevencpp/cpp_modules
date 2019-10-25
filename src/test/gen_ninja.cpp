#include <catch2/catch.hpp>
#include <fmt/core.h>
#include <filesystem>

#include "test_config.h"
#include "temp_file_test.h"
#include "cmd_line_utils.h"

namespace gen_ninja {

namespace fs = std::filesystem;

ConfigString ninja_path { "ninja_path", "ninja", "path to the ninja executable" };
ConfigString ninja_fork_path { "ninja_fork_path", "../../_deps/ninja-build/Debug/ninja.exe", "path to the ninja executable" };
ConfigString scanner_tool_path { "scanner_tool_path", "../scanner/Debug/cppm_scanner_tool.exe", "path to scanner_tool.exe" };

void generate_compilation_database() {
	cppm::CmdArgs gen_cmd = { "cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE "
		"-DCMAKE_MAKE_PROGRAM=\"{}\" -G Ninja ..", ninja_path };
	REQUIRE(0 == run_cmd(gen_cmd));
}

void generate_ninja() {
	cppm::CmdArgs gen_cmd = { "cmake -DCMAKE_MAKE_PROGRAM=\"{}\" -G Ninja ..", ninja_path };
	REQUIRE(0 == run_cmd(gen_cmd));
}

void generate_ninja_from_compilation_database() {
	cppm::CmdArgs tool_gen_cmd { "{} gen_dynamic", scanner_tool_path };
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
	REQUIRE(failed == false);
	REQUIRE(found_no_work_to_do == expect_no_work_to_do);
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
> main.cpp
import a;
int main() {}
> CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(test)
add_executable(test a.cpp b.cpp c.cpp d.cpp main.cpp)
set_property(TARGET test PROPERTY CXX_STANDARD 20)
	)");

	make_absolute(scanner_tool_path);
	make_absolute(ninja_path);
	make_absolute(ninja_fork_path);

	auto build_dir = test.create_dir("test_build");
	fs::current_path(build_dir);

	SECTION("test dyndeps") {
		generate_compilation_database();

		generate_ninja_from_compilation_database();

		run_ninja(ninja_path, false);
		run_ninja(ninja_path, true);
	}

	SECTION("test ninja fork") {
		generate_ninja();

		run_ninja(ninja_fork_path, false);
		run_ninja(ninja_fork_path, true);
	}
}

} // namespace gen_ninja