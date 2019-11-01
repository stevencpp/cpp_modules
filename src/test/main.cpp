#define CATCH_CONFIG_RUNNER
#define CATCH_CONFIG_ENABLE_BENCHMARKING

#define CATCH_CONFIG_WCHAR
#define UNICODE

#include "catch.hpp"

#include "test_config.h"
#include "cmd_line_utils.h"

#include <filesystem>
#include <stdlib.h>

namespace fs = std::filesystem;

ConfigString vcvarsall_bat { "vcvarsall_bat", "", "the path to vcvarsall.bat" };
ConfigString working_dir { "working_dir", "", "working dir" };

bool update_environment() {
	fmt::print("updating the environment ... ");
	// set up the paths to ninja and the compiler toolchain
	bool initialized = false, failed = false;
	cppm::CmdArgs vars_cmd { "call \"{}\" x64 && set", vcvarsall_bat };
	auto ret = run_cmd_read_lines(vars_cmd, [&](std::string_view line) {
		//fmt::print("> {}\n", line);
		if (!initialized) {
			if (line.find("Environment initialized") != std::string_view::npos)
				initialized = true;
		} else if (0 != putenv((char*)((std::string)line).c_str())) {
			fmt::print("> {}\n", line);
			failed = true;
			return false;
		}
		return true;
	}, [&](std::string_view err_line) {
		fmt::print("ERR: {}\n", err_line);
		failed = true;
		return true;
	});
	if (ret != 0 || failed) {
		fmt::print("failed\n");
		return false;
	}
	fmt::print("ok\n");
	return true;
}

int unguarded_main(int argc, char* argv[])
{
	Catch::Session session; // There must be exactly one instance

	using namespace Catch::clara;
	auto config = TestConfig::instance();
	auto cli = session.cli();
	for (auto& [key, conf_string] : config->strings) {
		std::string opt_name = "--"; opt_name += conf_string->name;
		cli |= Opt(*conf_string, conf_string->description)
			[opt_name] (conf_string->description);
	}
	session.cli(cli);

	// writing to session.configData() here sets defaults
	// this is the preferred way to set them
	session.configData().shouldDebugBreak = true;
	
	int returnCode = cppm::apply_command_line_from_file(argc, argv, [&](int argc, char* argv[]) {
		return session.applyCommandLine(argc, argv);
	});
	if (returnCode != 0) // Indicates a command line error
		return returnCode;

	for (auto& [key, conf_string] : config->strings)
		for (auto& alt_string : conf_string->alts)
			alt_string->assign(conf_string->str());

	if (session.configData().testsOrTags.empty()) {
		session.configData().testsOrTags = {
			"[scanner]"
		};
	}

	// writing to session.configData() or session.Config() here 
	// overrides command line args
	// only do this if you know you need to

	if (working_dir != "")
		fs::current_path(working_dir.str());
	if (vcvarsall_bat != "" && !update_environment())
		return 1;

	int numFailed = session.run();

	// numFailed is clamped to 255 as some unices only use the lower 8 bits.
	// This clamping has already been applied, so just return it here
	// You can also do any post run clean-up here
	return numFailed;
}

int main(int argc, char * argv[]) {
	try {
		return unguarded_main(argc, argv);
	} catch (...) {
		return 1;
	}
}

// -- need tests for:
// unicode files/paths/commands
//