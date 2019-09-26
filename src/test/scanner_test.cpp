#include "scanner.h"
#include <nlohmann/json.hpp>
#include <fstream>
#pragma warning(disable:4275) // non dll-interface class 'std::runtime_error' used as base for dll-interface class 'fmt::v6::format_error'
#include <fmt/core.h>
#include <catch2/catch.hpp>
#include <array>
#include "util.h"

#include <filesystem>

#include "test_config.h"

namespace scanner_test {

ConfigString clang_scan_deps_path { "clang_scan_deps_path", R"(c:\Program Files\LLVM\bin\clang-scan-deps.exe)" };
ConfigString default_command { "scanner_default_command", R"("cl.exe" /c /Zi /nologo /W3 /WX- /diagnostics:column /Od /Ob0 /D WIN32 /D _WINDOWS /D _MBCS /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /GR /std:c++latest /Gd /TP)" };
ConfigString single_comp_db { "scanner_comp_db", "" };
ConfigString single_db_path { "scanner_db_path", "" };

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

std::ostream& operator <<(std::ostream& os, depinfo::DataBlock const& value) {
	std::visit(overloaded {
		[&](const std::string& s) {
			os << "'" << s << "'";
		}, [&](auto&) {}
		}, value);
	return os;
}

struct ScanItem {
	std::string path; // UTF_8
	cppm::cmd_idx_t command_idx = {};
	cppm::target_idx_t target_idx = {};
};

struct ScanItemSet {
	vector_map<cppm::cmd_idx_t, std::string> commands;
	vector_map<cppm::target_idx_t, std::string> targets;
	std::vector<ScanItem> items;
};

auto from_comp_db(std::string comp_db_path) {
	ScanItemSet item_set;
	item_set.targets = { "x" };
	std::ifstream fin(comp_db_path);
	auto json_db = nlohmann::json::parse(fin);
	for (auto& json_item : json_db) {
		item_set.items.push_back({
			.path = json_item["file"],
			.command_idx = item_set.commands.size(),
			.target_idx = item_set.targets.indices().front()
			});
		item_set.commands.push_back(json_item["command"]);
	}
	return item_set;
}

struct ScanItemViewSet {
	std::vector<std::string_view> commands;
	std::vector<std::string_view> targets;
	std::vector<cppm::ScanItemView> items;
};

ScanItemViewSet to_scan_item_views(const ScanItemSet& item_set) {
	ScanItemViewSet view_set;
	for (auto& command : item_set.commands)
		view_set.commands.push_back(command);
	for (auto& target : item_set.targets)
		view_set.targets.push_back(target);
	for (auto& item : item_set.items)
		view_set.items.push_back(cppm::ScanItemView { item.path, item.command_idx, item.target_idx });
	return view_set;
}

struct DepInfoCollector : public cppm::DepInfoObserver {
public:
	constexpr static bool print_results = false;

	std::vector<depinfo::DepInfo> all_infos;
	std::size_t nr_results = 0;
	DepInfoCollector(tcb::span<const cppm::ScanItemView> items) : items(items) {
		all_infos.resize(items.size());
	}
private:
	tcb::span<const cppm::ScanItemView> items;
	std::vector<depinfo::DataBlock> data_table;
	depinfo::DepInfo* cur_info = nullptr;
	std::size_t current_item_idx = 0;

	depinfo::DataBlock get_data(DataBlockView view) {
		return std::visit(overloaded {
			[&](std::string_view sv) -> depinfo::DataBlock {
				return (std::string)sv;
			}, [&](IndexedStringView isv) -> depinfo::DataBlock {
				while (data_table.size() <= isv.idx) // todo: 
					data_table.emplace_back();
				data_table[isv.idx] = (std::string)isv.sv;
				return (std::string)isv.sv;
			}, [&](std::size_t idx) -> depinfo::DataBlock {
				if (idx >= data_table.size())
					throw std::runtime_error("index out of range");
				return data_table[idx];
			}, [&](RawDataBlockView rdv) -> depinfo::DataBlock {
				throw std::runtime_error("raw data blocks are not supported");
			}, [&](IndexedRawDataBlockView irdv) -> depinfo::DataBlock {
				throw std::runtime_error("raw data blocks are not supported");
		} }, view);
	}
	std::string_view get_str(DataBlockView view) {
		return std::get<std::string_view>(view);
	}
	template<typename T>
	T& ens(std::optional<T>& opt) {
		if (!opt.has_value())
			opt = T {};
		return opt.value();
	}
	void results_for_item(std::size_t item_idx) override {
		current_item_idx = item_idx;
		cur_info = &all_infos[current_item_idx];
		//cur_info->input = (std::string)items[current_item_idx].path;
		cur_info->input = (std::size_t)current_item_idx;
		if (print_results) fmt::print("test: current item = {}\n", items[current_item_idx].path);
	}
	void export_module(DataBlockView name) override {
		ens(ens(cur_info->future_compile).provide).push_back(
			{ .logical_name = get_data(name) }
		);
		if (print_results) fmt::print("test: \texport module {}\n", get_str(name));
	}
	void import_module(DataBlockView name) override {
		ens(ens(cur_info->future_compile).require).push_back(
			{ .logical_name = get_data(name) }
		);
		if (print_results) fmt::print("test: \timport module {}\n", get_str(name));
	}
	void include_header(DataBlockView path) override {
		ens(cur_info->depends).push_back(
			get_data(path)
		);
		if (print_results) fmt::print("test: \tinclude {}\n", get_str(path));
	}
	void import_header(DataBlockView path) override {
		ens(ens(cur_info->future_compile).require).push_back(
			{ .source_path = get_data(path) }
		);
		if (print_results) fmt::print("test: \timport header {}\n", get_str(path));
	}
	void other_file_dep(DataBlockView path) override {
		ens(cur_info->depends).push_back(
			get_data(path)
		);
		if (print_results) fmt::print("test: \tdep {}\n", get_str(path));
	}
	void item_finished() override {
		nr_results++;
	}
};

TEST_CASE("scanner - from a compilation database", "[scanner_comp_db]") {
	if (single_comp_db.empty())
		throw std::invalid_argument("must provide a compilation database path with --scanner_comp_db=\"...\"");
	if (single_db_path.empty())
		throw std::invalid_argument("must provide a db path with --scanner_db_path=\"...\"");
	try {
		timer t;
		cppm::Scanner scanner;

		auto item_set = from_comp_db(single_comp_db);
		auto item_views = to_scan_item_views(item_set);
		DepInfoCollector collector(item_views.items);

		cppm::Scanner::Config config;
		config.tool_type = cppm::Scanner::Type::CLANG_SCAN_DEPS;
		config.tool_path = clang_scan_deps_path;
		config.db_path = single_db_path;
		config.int_dir = config.db_path;
		config.item_root_path = ""; //R"(.\)";
		config.commands = item_views.commands;
		config.commands_contain_item_path = true;
		config.targets = item_views.targets;
		config.items = item_views.items;
		config.build_start_time = 0;
		config.concurrent_targets = false;
		config.file_tracker_running = false;
		config.observer = &collector;

		t.start();
		scanner.scan(config);
		t.stop();
	}
	catch (std::exception & e) {
		fmt::print("caught exception: {}", e.what());
	}
}

struct TempFileScanTest {
private:
	std::filesystem::path tmp_path;
	std::string tmp_path_str;
	std::vector<std::string> all_files_created;
	ScanItemSet item_set;
	std::vector<depinfo::DepInfo> results; // indexed the same as items
	std::size_t nr_results = 0;
	std::unordered_map<std::string, std::size_t> item_lookup;

	template<typename F>
	void create_files(std::string_view file_def, F&& file_visitor) {
		std::ofstream fout;
		std::size_t idx = 0;
		while (!file_def.empty()) {
			std::string_view line = file_def.substr(0, file_def.find_first_of("\n\r"));
			if (line.starts_with(">")) {
				if (fout) fout.close();
				std::string_view file = line.substr(2); // todo: allow arbitrary whitespace after >
				auto full_path = tmp_path / file;
				fout.open(full_path);
				file_visitor(idx, (std::string)file);
				idx++;
				all_files_created.push_back((std::string)file);
			} else if (fout.is_open()) {
				fout << line << "\n";
			}
			if (file_def.size() == line.size())
				break;
			file_def.remove_prefix(line.size() + 1); // UB if file_def doesn't have at least size + 1 elems
		};
	}
public:
	TempFileScanTest() {
		// note: std::tmpnam has a deprecation warning
		char tmp_dir_name[256];
		REQUIRE(tmpnam_s(tmp_dir_name) == 0);
		tmp_path = std::filesystem::temp_directory_path() / "cppm" / std::filesystem::path(tmp_dir_name).filename();
		REQUIRE(std::filesystem::create_directories(tmp_path));
		tmp_path_str = tmp_path.string();

		all_files_created.push_back("scanner.mdb"); // todo: pass the names to scanner
		all_files_created.push_back("scanner.mdb-lock");
		all_files_created.push_back("pp_commands.json");

		item_set.targets = { "x" };
		item_set.commands = { default_command };
	}

	void create_deps(const char* file_def) {
		create_files(file_def, [&](std::size_t, const std::string&) {});
	}

	void create_items(const char* file_def) {
		create_files(file_def, [&](std::size_t idx, std::string file) {
			item_set.items.push_back({ .path = file });
			item_lookup[file] = idx;
			});
	}

	void scan() {
		auto item_views = to_scan_item_views(item_set);

		DepInfoCollector collector(item_views.items);

		cppm::Scanner::Config config;
		config.tool_type = cppm::Scanner::Type::CLANG_SCAN_DEPS;
		config.tool_path = clang_scan_deps_path;
		config.db_path = tmp_path_str;
		config.int_dir = config.db_path;
		config.item_root_path = tmp_path_str;
		config.commands = item_views.commands;
		config.commands_contain_item_path = false;
		config.targets = item_views.targets;
		config.items = item_views.items;
		config.build_start_time = 0;
		config.concurrent_targets = false;
		config.file_tracker_running = false;
		config.observer = &collector;

		cppm::Scanner scanner;
		
		timer t;
		t.start();
		scanner.scan(config);
		t.stop();

		results = std::move(collector.all_infos);
		nr_results = collector.nr_results;
	}

	void clean() {
		auto item_views = to_scan_item_views(item_set);

		cppm::Scanner::Config config;
		config.db_path = tmp_path_str;
		config.item_root_path = tmp_path_str;
		config.targets = item_views.targets;
		config.items = item_views.items;

		cppm::Scanner scanner;
		scanner.clean(config);
	}

	using opt_db_vec = std::optional<std::vector<depinfo::DataBlock>>;

	void canonicalize(opt_db_vec& result_opt, opt_db_vec& expected_opt) {
		if (result_opt.has_value())
			for (auto& file : result_opt.value())
				file = std::filesystem::canonical(std::get<std::string>(file)).string();
		if (expected_opt.has_value())
			for (auto& file : expected_opt.value())
				file = std::filesystem::canonical(tmp_path / std::get<std::string>(file)).string();
	}

	void check_unordered_equals(opt_db_vec& result_opt, opt_db_vec& expected_opt)
	{
		REQUIRE(result_opt.has_value() == expected_opt.has_value());
		if (expected_opt.has_value()) {
			auto& result = result_opt.value(); auto& expected = expected_opt.value();
			//std::cout << result << "\n" << expected << "\n";
			CHECK_THAT(result, Catch::Matchers::UnorderedEquals(expected));
		}
	}

	void check(depinfo::DepFormat deps) {
		CHECK(nr_results == deps.sources.size());
		for (auto& expected_info : deps.sources) {
			std::string input_file = std::get<std::string>(expected_info.input);
			std::size_t item_idx = item_lookup[input_file];
			depinfo::DepInfo& result_info = results[item_idx];

			//CHECK(std::get<std::string>(result_info.input) == input_file);
			CHECK(std::get<std::size_t>(result_info.input) == item_idx);
			canonicalize(result_info.depends, expected_info.depends);
			check_unordered_equals(result_info.depends, expected_info.depends);
		}
	}

	void scan_check(depinfo::DepFormat deps) {
		scan();
		check(deps);
	}

	void touch(const char* file_name) {
		std::filesystem::last_write_time(tmp_path / file_name, std::filesystem::file_time_type::clock::now());
	}

	~TempFileScanTest() {
		try {
			for (auto& file : all_files_created)
				std::filesystem::remove(tmp_path / file);
			std::filesystem::remove(tmp_path); // note: this fails if the scanner chdir-s to it
		}
		catch (std::exception & e) {
			fmt::print("cleanup failed because {}\n", e.what());
		}
	}
};

TEST_CASE("test1", "[scanner]") {
	TempFileScanTest test;

	test.scan_check({}); // nothing to scan

	test.create_deps(R"(
> a.h
#include "b.h"
> b.h
	)");
	test.create_items(R"(
> a.cpp
#include "a.h"
> b.cpp
#include "b.h"
> c.cpp
	)");

	using vdb = std::vector<depinfo::DataBlock>;
	depinfo::DepInfo a_cpp_info = { .input = "a.cpp", .depends = vdb{ "a.h", "b.h" } };
	depinfo::DepInfo b_cpp_info = { .input = "b.cpp", .depends = vdb{ "b.h" } };
	depinfo::DepInfo c_cpp_info = { .input = "c.cpp" };

	test.scan_check({ .sources = { a_cpp_info, b_cpp_info, c_cpp_info } }); // first scan

	test.scan_check({}); // everything up to date

	test.touch("a.h");
	test.scan_check({ .sources = { a_cpp_info } }); // only a.cpp depends on a.h

	test.scan_check({}); // no changes again

	test.touch("b.h");
	test.scan_check({ .sources = { a_cpp_info, b_cpp_info } }); // both depend on b.h

	test.touch("b.cpp");
	test.scan_check({ .sources = { b_cpp_info } }); // just b.cpp this time

	test.touch("c.cpp");
	test.scan_check({ .sources = { c_cpp_info } }); // just c.cpp this time

	test.scan_check({}); // no changes again

	test.touch("b.h");
	test.touch("c.cpp");
	test.scan_check({ .sources = { a_cpp_info, b_cpp_info, c_cpp_info } }); // all three again

	test.scan_check({}); // no changes again

	test.clean();
	test.scan_check({ .sources = { a_cpp_info, b_cpp_info, c_cpp_info } }); // all items were removed from the DB

	test.scan_check({}); // no changes again
}

} // namespace scanner_test