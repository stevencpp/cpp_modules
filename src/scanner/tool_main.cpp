#include <string_view>
#include <string>
#include <filesystem>

#include <fmt/core.h>
#include <clara.hpp>

#include "cmd_line_utils.h"
#include "gen_ninja.h"

namespace fs = std::filesystem;

auto config_command_line_opts(cppm::Scanner::Config& c) {
	using namespace clara;
	return
		Opt(c.tool_path, "tool path")["--tool_path"]("default: " + c.tool_path) |
		Opt(c.db_path, "db path")["--db_path"] |
		Opt(c.int_dir, "int dir")["--int_dir"] |
		Opt(c.item_set.item_root_path, "item root path")["--item_root_path"];
}

int main(int argc, char * argv[])
{
	using namespace clara;

	cppm::NinjaGenerator gen_ninja;

	std::string command;
	std::string working_dir;
	std::string comp_db_path;
	cppm::Scanner::Config scanner_config;

	auto cli = Arg(command, "command") |
		Opt(working_dir, "change to this directory before proceeding")["--working_dir"] |
		Opt(comp_db_path, "compilation database path")["--comp_db_path"] |
		config_command_line_opts(scanner_config) |
		gen_ninja.command_line_opts();

	auto result = cppm::apply_command_line_from_file(argc, argv, [&](int argc, char* argv[]) {
		return cli.parse(Args(argc, argv));
	});
	if (!result) {
		fmt::print(stderr, "Error in command line: {}\n", result.errorMessage());
		return 1;
	}

	if (working_dir != "")
		fs::current_path(working_dir);

	try {
		if (command == "scan")
			return gen_ninja.scan(comp_db_path, scanner_config);
		else if (command == "gen_dynamic")
			return gen_ninja.gen_dynamic(comp_db_path, scanner_config);
		else if (command == "gen_static")
			return gen_ninja.gen_static();
		fmt::print(stderr, "invalid command '{}'\n", command);
	} catch (std::exception & e) {
		fmt::print(stderr, "caught exception: {}\n", e.what());
	} catch (...) {
		fmt::print(stderr, "caught unknown exception\n");
	}
	
	return 1;
}