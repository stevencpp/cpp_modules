#define CATCH_CONFIG_RUNNER
#define CATCH_CONFIG_ENABLE_BENCHMARKING

#define CATCH_CONFIG_WCHAR
#define UNICODE

#include "catch.hpp"

#include "test_config.h"
#include "cmd_line_utils.h"

#include <filesystem>

namespace fs = std::filesystem;

int unguarded_main(int argc, char* argv[])
{
	Catch::Session session; // There must be exactly one instance

	using namespace Catch::clara;
	auto config = TestConfig::instance();
	auto cli = session.cli();
	for (auto& conf_string : config->strings) {
		std::string opt_name = "--"; opt_name += conf_string->name;
		cli |= Opt(*conf_string, conf_string->description)
			[opt_name] (conf_string->description);
	}
	std::string working_dir;
	cli |= Opt(working_dir, "working dir")["--working_dir"];
	session.cli(cli);

	// writing to session.configData() here sets defaults
	// this is the preferred way to set them
	session.configData().shouldDebugBreak = true;
	
	int returnCode = cppm::apply_command_line_from_file(argc, argv, [&](int argc, char* argv[]) {
		return session.applyCommandLine(argc, argv);
	});
	if (returnCode != 0) // Indicates a command line error
		return returnCode;

	if (session.configData().testsOrTags.empty()) {
		session.configData().testsOrTags = {
			"[scanner]"
		};
	}

	// writing to session.configData() or session.Config() here 
	// overrides command line args
	// only do this if you know you need to

	if (working_dir != "")
		fs::current_path(working_dir);

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