#define CATCH_CONFIG_RUNNER
#define CATCH_CONFIG_ENABLE_BENCHMARKING

#define CATCH_CONFIG_WCHAR
#define UNICODE

#include "catch.hpp"

#include "test_config.h"

#ifdef _WIN32
#include <shellapi.h>
int applyCommandLineFromFile(Catch::Session& session, int argc, char* argv[])
{
	std::wifstream fin("command_line.txt");
	if (!fin)
		return session.applyCommandLine(argc, argv);

	std::wstring command_line = L"exec";
	std::wstring line;
	while (std::getline(fin, line)) {
		if (line.starts_with(L"#"))
			continue;
		command_line += L" " + line;
	}

	int nArgs = 0;
	LPWSTR* szArglist = CommandLineToArgvW(command_line.c_str(), &nArgs);
	if (NULL == szArglist) {
		wprintf(L"CommandLineToArgvW failed\n");
		return 1;
	}
	return session.applyCommandLine(nArgs, szArglist);
}
#else
int applyCommandLineFromFile(Catch::Session& session, int argc, char* argv[]) {
	return session.applyCommandLine(argc, argv);
}
#endif

int main(int argc, char* argv[])
{
	Catch::Session session; // There must be exactly one instance

	using namespace Catch::clara;
	auto config = TestConfig::instance();
	auto cli = session.cli();
	for (auto conf_string : config->strings) {
		std::string opt_name = "--"; opt_name += conf_string->name;
		cli |= Opt(conf_string->var, conf_string->description)
			[opt_name] (conf_string->description);
	}
	session.cli(cli);

	// writing to session.configData() here sets defaults
	// this is the preferred way to set them
	session.configData().shouldDebugBreak = true;
	
	int returnCode = applyCommandLineFromFile(session, argc, argv);
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

	int numFailed = session.run();

	// numFailed is clamped to 255 as some unices only use the lower 8 bits.
	// This clamping has already been applied, so just return it here
	// You can also do any post run clean-up here
	return numFailed;
}

// -- need tests for:
// unicode files/paths/commands
//