#include <nlohmann/json.hpp>
#include <string_view>
#include <fmt/core.h>

#include "scanner.h"

int print_usage() {
	fmt::print("usage: cppm_scanner_wrapper <tool-type> <tool-path>");
	return 1;
}

int main(int argc, const char * argv[]) {
	if (argc < 3)
		return print_usage();

	std::string_view tool_type = argv[1], tool_path = argv[2];

	Scanner scanner;

	return 0;
}