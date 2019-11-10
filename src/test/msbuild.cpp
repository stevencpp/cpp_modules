#include <catch2/catch.hpp>
#include <range/v3/view/split.hpp>
#include <filesystem>
#include <stdlib.h>
#pragma warning(disable:4275) // non dll-interface class 'std::runtime_error' used as base for dll-interface class 'fmt::v6::format_error'
#include <fmt/core.h>
#include <fmt/color.h>

#include "test_config.h"
#include "cmd_line_utils.h"
#include "util.h"

namespace fs = std::filesystem;

namespace msbuild {

ConfigString cfg_run_one { "msbuild-run-one", "" };
ConfigString cfg_run_set { "msbuild-run-set", "dag,concurrent" };
ConfigString cfg_test_path { "msbuild-test-path", "../../../tests" };
ConfigString cfg_generator { "msbuild-generator", "Visual Studio 16 2019" };
ConfigString cfg_arch { "msbuild-arch", "X64" };
ConfigString cfg_verbosity { "msbuild-verbosity", "m", "m for mininmal, d for diagnostic" };
ConfigString cfg_configuration { "msbuild-configuration", "Debug" };
ConfigString cfg_toolset { "msbuild-toolset", "" };

template<typename ContiguousRange>
std::string_view to_sv(ContiguousRange&& range) {
	return { &(*range.begin()), (std::size_t)ranges::distance(range.begin(), range.end()) };
}

auto get_run_set() {
	std::vector<std::string> ret;
	if (!cfg_run_one.empty()) {
		ret.push_back(cfg_run_one);
	} else {
		for (auto test : ranges::split_view { cfg_run_set, ',' })
			ret.push_back((std::string)to_sv(test));
	}
	return ret;
}

void full_clean_one(const std::string& test) {
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
	fs::remove(build_path / "CMakeCache.txt");
}

struct run_one_params {
	std::string_view test_path = cfg_test_path;
	std::string_view generator = cfg_generator;
	std::string_view arch = cfg_arch;
	std::string_view toolset = cfg_toolset;
	std::string_view verbosity = cfg_verbosity;
	std::string_view configuration = cfg_configuration;
};
void run_one(const std::string& test, const run_one_params& p = {}) {
	auto test_path = fs::path { p.test_path } / test;
	REQUIRE(fs::exists(test_path));
	auto build_path = test_path / "build";
	fs::create_directory(build_path);
	fs::current_path(build_path);

	fmt::print(fmt::fg(fmt::color::yellow), "=== running {} ===\n", test);

	cppm::CmdArgs generate_cmd { "cmake -G \"{}\" -A \"{}\" ", p.generator, p.arch };
	if(p.toolset != "")
		generate_cmd.append("-DCMAKE_GENERATOR_TOOLSET={} ", p.toolset);
	generate_cmd.append("../");
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

struct path_guard {
	std::string path_env = getenv("PATH");
	path_guard() { putenv(fmt::format("PATH={};C:\\Program Files\\LLVM\\bin", path_env).c_str()); }
	~path_guard() {	putenv(fmt::format("PATH={}", path_env).c_str()); }
};

TEST_CASE("msbuild system test", "[msbuild]") {
	// todo: ideally CMake should be able to generate its CompilerIdCXX.vcxproj
	// with the LLVMInstallDir set so that we don't need to change the path here
	
	path_guard pguard;
	
	auto guard = make_chdir_guard();
	
	for (std::string& test : get_run_set()) {
		full_clean_one(test);
		run_one(test);
		full_clean_one(test);
		run_one(test, { .toolset = "ClangCl" } );
	}
}

} // namespace msbuild