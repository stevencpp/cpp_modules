#include "gen_ninja.h"

#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <string_view>

#include <nlohmann/json.hpp>
#pragma warning(disable:4275) // non dll-interface class 'std::runtime_error' used as base for dll-interface class 'fmt::v6::format_error'
#include <fmt/ostream.h>

#include "cmd_line_utils.h"
#include "scanner.h"
#include "trace.h"

namespace fs = std::filesystem;

namespace cppm {

std::string ninja_escape(std::string_view s) {
	std::string ret;
	int nr = 0;
	for (char c : s)
		if (c == '&' || c == ' ' || c == ':')
			nr++;
	ret.reserve(s.size() + nr);
	for (char c : s) {
		if (c == '&' || c == ' ' || c == ':')
			ret += "$";
		ret += c;
	}
	return ret;
}

std::string NinjaGenerator::comp_db_to_read(std::string_view comp_db_path, const Scanner::Config& c) {
	if (comp_db_path != "")
		return (std::string)comp_db_path;
	if (c.int_dir != "")
		return c.int_dir + "\\compile_commands.json";
	if (c.db_path != "")
		return c.db_path + "\\compile_commands.json";
	return "./compile_commands.json";
}

std::string get_output_file(ScanItem& item, Scanner::Config& c) {
	auto& cmd = c.item_set.commands[item.command_idx];
	return find_command_line_argument(cmd, "/Fo"); // todo:
}

std::string get_ifc_path(ScanItem& item, Scanner::Config& c) {
	auto& cmd = c.item_set.commands[item.command_idx];
	fs::path cl_path = get_command_line_argument(cmd, 0); // todo:
	cl_path.remove_filename();
	fs::path arch = cl_path.parent_path().filename();
	return (cl_path / "../../../ifc" / arch).string();
}

std::string get_input_file(ScanItem& item, Scanner::Config& c) {
	return fs::relative(item.path, c.int_dir).string(); // shorten the path a bit
}

std::string get_response_file(std::string_view output_file) {
	return fs::path { output_file }.replace_extension(".rsp").string();
}

std::string get_bmi_file(std::string_view output_file) {
	return fs::path { output_file }.replace_extension(".ifc").string();
}

constexpr auto dyndeps_file_name = "dyndeps.ninja";

void add_scanner(std::ofstream& fout, std::string& comp_db_path, Scanner::Config& c)
{
	fmt::print(fout, "rule scan\n command = $cmd\n");

	// note: the # is there so we don't override the command line with command_line.txt
	std::string scan_cmd = fmt::format("\"{}\" # scan ", executable_path());
	auto add = [&](std::string_view var, std::string_view var_name) {
		if (var != "" && var != "./" && var != ".") scan_cmd += fmt::format("--{}=\"{}\" ", var_name, var);
	};
	add(comp_db_path, "comp_db_path");
	add(c.tool_path, "tool_path");
	add(c.db_path, "db_path");
	add(c.int_dir, "int_dir");
	// ninja is intended to be invoked from the intdir so this doesn't need a relative path:
	std::string dyndeps_file = dyndeps_file_name;
	std::string dyndeps_file_esc = ninja_escape(dyndeps_file);
	std::string outputs = dyndeps_file_esc;
	std::string inputs = "";
	for (auto& item : c.item_set.items) {
		inputs += ninja_escape(fs::relative(item.path, c.int_dir).string()) + " ";
	}
	fmt::print(fout, "build {}: scan {}\n cmd = {}\n deps = msvc\n", outputs, inputs, scan_cmd);
}

void add_sources(std::ofstream& fout, Scanner::Config& c)
{
	fmt::print(fout, "rule cc\n command = $cmd\n");

	// todo: avoid path allocations as much as possible here
	std::string dyndeps_file = dyndeps_file_name;
	std::string dyndeps_file_esc = ninja_escape(dyndeps_file);
	for (auto& item : c.item_set.items) {
		auto& cmd = c.item_set.commands[item.command_idx];

		std::string output_file = get_output_file(item, c);
		std::string outputs = ninja_escape(output_file);

		std::string input_file = get_input_file(item, c);
		std::string rsp_file = get_response_file(output_file);
		std::string inputs = fmt::format("{} || {}", ninja_escape(input_file), dyndeps_file_esc);
		fmt::print(fout, "build {}: cc {}\n cmd = {} \"@{}\"\n dyndep = {}\n",
			outputs, inputs, cmd, rsp_file, dyndeps_file);
	}
}

int NinjaGenerator::gen_dynamic(std::string& comp_db_path, Scanner::Config& c)
{
	if (c.int_dir == "") c.int_dir = "./"; // fs::relative doesn't work if this is ""

	c.item_set = scan_item_set_from_comp_db(
		comp_db_to_read(comp_db_path, c)
	);

	std::ofstream fout(fs::path { c.int_dir } / "build.ninja");

	//fmt::print(fout, "msvc_deps_prefix = -\n");
	add_scanner(fout, comp_db_path, c);
	add_sources(fout, c);
	
	//fmt::print(stderr, "gen_dynamic");
	return 0;
}

DECL_STRONG_ID_INV(module_id_t, 0);

struct DepsCollector : public DepInfoObserver {
	ScanItemSet& item_set;
	module_id_t next_module_id = module_id_t { 1 }; // 0 is reserved for invalid
	// todo: expose a lower level API from the scanner so we don't have to duplicate this stuff
	vector_map<module_id_t, scan_item_idx_t> module_exported_by;
	// todo: create a bidirectional hash-map that doesn't store the strings twice
	vector_map<module_id_t, std::string> reverse_module_map;
	std::unordered_map<std::string, module_id_t> module_map;
	struct module_info {
		module_id_t exports = {};
		std::vector<module_id_t> imports;
	};
	vector_map<scan_item_idx_t, module_info> item_to_module_info;
	scan_item_idx_t current_item_idx = {};
	std::unordered_set<std::string> all_files;

	module_id_t register_module(DataBlockView name) {
		if (auto pname = std::get_if<std::string_view>(&name); pname) {
			auto str_name = (std::string) * pname;
			auto [itr, inserted] = module_map.try_emplace(str_name, next_module_id);
			if (inserted) {
				reverse_module_map.emplace_back(std::move(str_name));
				next_module_id++;
			}
			return itr->second;
		}
		throw std::runtime_error("failed to get module module name");
	}

	DepsCollector(ScanItemSet& item_set) : item_set(item_set) {
		auto nr_items = item_set.items.size();
		// each item exports at most one module
		// module id = 0 is reserved for the invalid state
		module_exported_by.resize(id_cast<module_id_t>(nr_items + 1));
		module_map.reserve((std::size_t)nr_items + 1);
		reverse_module_map.reserve(id_cast<module_id_t>(nr_items + 1));
		reverse_module_map.emplace_back();
		for (auto& idx : module_exported_by)
			idx.invalidate();
		item_to_module_info.resize(nr_items);
	}

	module_info& get_module_info() {
		return item_to_module_info[current_item_idx];
	}

	void results_for_item(scan_item_idx_t item_idx, bool out_of_date) override {
		current_item_idx = item_idx;
	}
	void export_module(DataBlockView name) override {
		auto id = register_module(name);
		get_module_info().exports = id;
		module_exported_by[id] = current_item_idx;
	}
	void import_module(DataBlockView name) override {
		get_module_info().imports.push_back(register_module(name));
	}
	void include_header(DataBlockView path) override {
		all_files.insert((std::string)std::get<std::string_view>(path));
	}
	void import_header(DataBlockView path) override {}
	void other_file_dep(DataBlockView path) override {
		all_files.insert((std::string)std::get<std::string_view>(path));
	}
	void item_finished() override {}

	std::vector<scan_item_idx_t> queue;
	std::vector<bool> is_in_queue;

	template<typename F>
	bool visit_transitive_imports(scan_item_idx_t root_idx, F&& visitor_func) {
		queue.resize((std::size_t)item_to_module_info.size());
		if (queue.empty())
			return true;
		is_in_queue.resize((std::size_t)item_to_module_info.size());
		queue[0] = root_idx;
		is_in_queue[(std::size_t)root_idx] = true;
		std::size_t s = 0, e = 0;
		while (s <= e) {
			auto idx = queue[s++];
			auto& info = item_to_module_info[idx];
			if(idx != root_idx)
				visitor_func(idx);
			for (module_id_t id : info.imports) {
				auto exp_idx = module_exported_by[id];
				if (!exp_idx.is_valid()) {
					if (reverse_module_map[id] == "std.core")
						continue;
					fmt::print(stderr, "'{}' imports '{}' which is not exported by any TU\n",
						item_set.items[exp_idx].path, reverse_module_map[id]);
					return false;
				}
				if (!is_in_queue[(std::size_t)exp_idx]) {
					is_in_queue[(std::size_t)exp_idx] = true;
					queue[++e] = exp_idx;
				}
			}
		}
		is_in_queue.clear();
		return true;
	}
};

// todo: if the file and all of its transitive dependencies are up-to-date then
// we should be able to know that the command shouldn't change without reading in the file
// but it's possible that all of those were successfully scanned and we crashed before writing the file
// so maybe just stat the file instead of reading it, check that it's after the last scan, 
// or we could store a record in the DB after the file is written and check that without additional I/O ?
struct write_if_changed_guard {
	fmt::memory_buffer &buf;
	std::string_view file_name;
	write_if_changed_guard(fmt::memory_buffer& buf, std::string_view file_name) :
		buf(buf), file_name(file_name) {}
	~write_if_changed_guard() {
		std::ifstream fin(file_name);
		std::string line;
		if (std::getline(fin, line) &&
			(line.size() == buf.size()) &&
			std::memcmp(line.data(), buf.data(), line.size()) == 0) 
		{
			// data is the same, don't write anything
			return;
		}
		fin.close();
		fs::create_directories(fs::path { file_name }.remove_filename());
		std::ofstream fout(file_name);
		fout.write(buf.data(), buf.size());
	}
};

int NinjaGenerator::scan(std::string& comp_db_path, Scanner::Config& c)
{
	TRACE();
	if (c.tool_path == "") c.tool_path = R"(c:\Program Files\LLVM\bin\clang-scan-deps.exe)";

	if (c.int_dir == "") c.int_dir = c.db_path; // can provide either one, for convenience
	if (c.db_path == "") c.db_path = c.int_dir;

	if (c.int_dir == "") c.int_dir = "./"; // fs::relative doesn't work if this is ""
	if (c.db_path == "") c.db_path = "./"; // the scanner throws an error otherwise

	c.item_set = scan_item_set_from_comp_db(
		NinjaGenerator::comp_db_to_read(comp_db_path, c)
	);
	DepsCollector collector(c.item_set);
	c.observer = &collector;
	c.submit_previous_results = true;

	auto config_owned_view = Scanner::ConfigOwnedView::from(c);
	auto config_view = Scanner::ConfigView::from(config_owned_view);

	Scanner scanner;
	try {
		scanner.scan(config_view);
	} catch (std::exception & e) {
		fmt::print(stderr, "scanner failed: {}\n", e.what());
		return 1;
	}

	for (auto file : collector.all_files) {
		auto deps_prefix = "Note: including file:"; // = "-";
		fmt::print("{} {}\n", deps_prefix, file); // ninja looks for the prefix
	}

	std::ofstream dd_fout(dyndeps_file_name);
	fmt::print(dd_fout, "ninja_dyndep_version = 1\n");

	vector_map<scan_item_idx_t, std::string> output_files, ninja_bmi_files, bmi_files;
	output_files.resize(c.item_set.items.size());
	ninja_bmi_files.resize(c.item_set.items.size());
	bmi_files.resize(c.item_set.items.size());
	for (auto i : c.item_set.items.indices()) {
		auto& item = c.item_set.items[i];
		output_files[i] = get_output_file(item, c);
		auto& info = collector.item_to_module_info[i];
		if (info.exports.is_valid()) {
			bmi_files[i] = get_bmi_file(output_files[i]);
			ninja_bmi_files[i] = ninja_escape(bmi_files[i]);
		}
	}

	fmt::memory_buffer rsp_mem;
	rsp_mem.reserve(16 * 1024);
	for (auto i : c.item_set.items.indices()) {
		std::string& output_file = output_files[i];
		std::string response_file = get_response_file(output_file);
		fmt::print(dd_fout, "build {}", ninja_escape(output_file));

		rsp_mem.clear();
		write_if_changed_guard rsp_guard(rsp_mem, response_file);

		auto& info = collector.item_to_module_info[i];
		if (info.exports.is_valid() || !info.imports.empty()) {
			std::string ifcdir = get_ifc_path(c.item_set.items[i], c);
			fmt::format_to(rsp_mem, "/experimental:module /module:stdIfcDir \"{}\"", ifcdir);
		}

		if (info.exports.is_valid()) {
			fmt::print(dd_fout, " | {}", ninja_bmi_files[i]);
			fmt::format_to(rsp_mem, " /module:interface /module:output \"{}\"", bmi_files[i]);
		}

		fmt::print(dd_fout, ": dyndep | {}", ninja_escape(response_file));
		if (info.imports.empty()) {
			fmt::print(dd_fout, "\n");
			continue;
		}

		bool ok = collector.visit_transitive_imports(i, [&](scan_item_idx_t exported_by_item_idx) {
			fmt::print(dd_fout, " {}", ninja_bmi_files[exported_by_item_idx]);
			fmt::format_to(rsp_mem, " /module:reference \"{}\"", bmi_files[exported_by_item_idx]);
		});
		if (!ok)
			return 1;
		fmt::print(dd_fout, "\n");
	}
	return 0;
}

int NinjaGenerator::gen_static()
{
	fmt::print(stderr, "gen_static");
	return 1;
}

} // namespace cppm