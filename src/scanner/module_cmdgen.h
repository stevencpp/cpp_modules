#pragma once

#include "scanner.h"

#include <string>
#include <functional>
#include "fmt/format.h"

namespace cppm {

struct ModuleCommandGenerator
{
	fmt::memory_buffer cmd_buf;
	ScanItemSetView item_set;
	ModuleVisitor& module_visitor;
	std::size_t references_end = 0;

	enum FormatEnum {
		MSVC,
		ClangCl,
		Clang,
		GCC
	};

	struct Format {
		FormatEnum format;
		bool isClang() { return format == ClangCl || format == Clang; };
		bool isMSVC() { return format == MSVC; }
		bool isGCC() { return format == GCC; }
	};

	static Format detect_format(std::string_view cmd);

	ModuleCommandGenerator(ScanItemSetView item_set, ModuleVisitor& module_visitor);

	std::string get_bmi_file(std::string_view output_file);

	// todo: use function_ref
	void generate(scan_item_idx_t idx, Format format,
		std::function<std::string_view(scan_item_idx_t)> bmi_file_func);
	
	void full_cmd_to_string(std::string& str);

	void references_to_string(std::string& str);
};

} // namespace cppm