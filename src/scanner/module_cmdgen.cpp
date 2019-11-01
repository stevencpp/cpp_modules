#include "module_cmdgen.h"

#include "cmd_line_utils.h"

namespace fs = std::filesystem;

namespace cppm {

std::string get_ifc_path(std::string_view cmd) {
	fs::path cl_path = get_command_line_argument(cmd, 0);  // todo:
	cl_path.remove_filename();
	fs::path arch = cl_path.parent_path().filename();
	return (cl_path / "../../../ifc" / arch).string();
}

std::string ModuleCommandGenerator::get_bmi_file(std::string_view output_file) {
	return fs::path { output_file }.replace_extension(".ifc").string();
}

ModuleCommandGenerator::ModuleCommandGenerator(ScanItemSetView item_set, ModuleVisitor& module_visitor) :
	item_set(item_set), module_visitor(module_visitor)
{
	cmd_buf.reserve(16 * 1024);
}

void ModuleCommandGenerator::generate(scan_item_idx_t idx, Format format,
	std::function<std::string_view(scan_item_idx_t)> bmi_file_func)
{
	cmd_buf.clear();

	bool has_export = (!module_visitor.exports[idx].empty());
	bool has_import = (!module_visitor.imports_item[idx].empty());
	if (!has_export && !has_import)
		return;

	// in all cases assume that the C++20 flag has already been set

	if (format.isMSVC()) {
		auto cmd_idx = item_set.items[idx].command_idx;
		std::string ifcdir = get_ifc_path(item_set.commands[cmd_idx]);
		fmt::format_to(cmd_buf, " /experimental:module /module:stdIfcDir \"{}\"",
			ifcdir);

		if (has_export)
			fmt::format_to(cmd_buf, " /module:interface /module:output \"{}\"", bmi_file_func(idx));

		module_visitor.visit_transitive_imports(idx, [&](scan_item_idx_t imp_idx) {
			fmt::format_to(cmd_buf, " /module:reference \"{}\"", bmi_file_func(imp_idx));
		});
	} else if(format.isClang()) {
		module_visitor.visit_transitive_imports(idx, [&](scan_item_idx_t imp_idx) {
			fmt::format_to(cmd_buf, " -Xclang -fmodule-file={}=\"{}\"", 
				module_visitor.exports[imp_idx], bmi_file_func(imp_idx));
		});

		references_end = cmd_buf.size();

		if (has_export)
			fmt::format_to(cmd_buf, " -Xclang -emit-module-interface -Xclang -fmodule-name={}", module_visitor.exports[idx]);
	}
}

void ModuleCommandGenerator::full_cmd_to_string(std::string& str)
{
	if (cmd_buf.size() > 0) {
		str.resize(cmd_buf.size());
		memcpy(&str[0], cmd_buf.data(), cmd_buf.size());
	}
}

void ModuleCommandGenerator::references_to_string(std::string& str)
{
	if (references_end > 0) {
		str.resize(references_end);
		memcpy(&str[0], cmd_buf.data(), references_end);
	}
}

ModuleCommandGenerator::Format ModuleCommandGenerator::detect_format(std::string_view cmd) {
	// todo:
	if (cmd.find("/Fo") != std::string_view::npos)
		return { MSVC };
	return { Clang };
}

} // namespace cppm