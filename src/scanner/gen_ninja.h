#pragma once

#include <clara.hpp>

#include "scanner.h"

namespace cppm {

struct NinjaGenerator {
	std::string incremental_scanner_path;

	auto command_line_opts() {
		using namespace clara;
		return Opt(incremental_scanner_path, "incremental scanner path")["--inc_scanner_path"];
	}

	int gen_dynamic(std::string& comp_db_path, cppm::Scanner::Config& c);

	int scan(std::string& comp_db_path, cppm::Scanner::Config& c);

	int gen_static();

	static std::string comp_db_to_read(std::string_view comp_db_path, const cppm::Scanner::Config & c);
};

} // namespace cppm