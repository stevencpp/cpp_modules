#include <catch2/catch.hpp>
#include <filesystem>
#include <stdlib.h>

#include "test_config.h"
#include "cmd_line_utils.h"
#include "util.h"
#include "system_test.h"

namespace fs = std::filesystem;

namespace msbuild {

ConfigString cfg_run_one { "msbuild-run-one", "" };
ConfigString cfg_run_set { "msbuild-run-set", "dag,concurrent,generated" };

ConfigString cfg_generator { "msbuild-generator", "Visual Studio 16 2019" };
ConfigString cfg_arch { "msbuild-arch", "Win32" };
ConfigString cfg_verbosity { "msbuild-verbosity", "m", "m for mininmal, d for diagnostic" };
ConfigString cfg_configuration { "msbuild-configuration", "Debug" };
ConfigString cfg_toolset { "msbuild-toolset", "" };

struct path_guard {
	std::string path_env = getenv("PATH");
	path_guard() { putenv((char*)fmt::format("PATH={};C:\\Program Files\\LLVM\\bin", path_env).c_str()); }
	~path_guard() {	putenv((char*)fmt::format("PATH={}", path_env).c_str()); }
};

using namespace system_test;

TEST_CASE("msbuild system test", "[msbuild]") {
	// todo: ideally CMake should be able to generate its CompilerIdCXX.vcxproj
	// with the LLVMInstallDir set so that we don't need to change the path here
	
	path_guard pguard;
	
	auto guard = make_chdir_guard();
	
	run_one_params params {
		.generator = cfg_generator,
		.arch = cfg_arch,
		.toolset = cfg_toolset,
		.verbosity = cfg_verbosity,
		.configuration = cfg_configuration
	};
	for (std::string& test : get_run_set(cfg_run_one, cfg_run_set)) {
		full_clean_one(test);
		params.toolset = cfg_toolset;
		run_one(test, params);
		if (cfg_toolset == "") {
			full_clean_one(test);
			params.toolset = "ClangCl";
			run_one(test, params);
		}
	}
}

} // namespace msbuild