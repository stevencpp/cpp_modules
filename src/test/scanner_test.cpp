#include "scanner.h"
#include <nlohmann/json.hpp>
#include <fstream>
#pragma warning(disable:4275) // non dll-interface class 'std::runtime_error' used as base for dll-interface class 'fmt::v6::format_error'
#include <fmt/core.h>
#include <catch2/catch.hpp>
#include "util.h"

#include <filesystem>

#include "test_config.h"
#include "temp_file_test.h"

namespace scanner_test {

ConfigString clang_scan_deps_path { "clang_scan_deps_path", R"(c:\Program Files\LLVM\bin\clang-scan-deps.exe)" };
ConfigString default_command { "scanner_default_command", R"("cl.exe" /c /Zi /nologo /W3 /WX- /diagnostics:column /Od /Ob0 /D WIN32 /D _WINDOWS /D _MBCS /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /GR /std:c++latest /Gd /TP)" };
ConfigString single_comp_db { "scanner_comp_db", "" };
ConfigString single_db_path { "scanner_db_path", "" };
ConfigString single_file_to_touch { "scanner_file_to_touch", "" };
ConfigString single_item_root_path { "scanner_item_root_path", "" };

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

struct DepInfoCollector : public cppm::DepInfoObserver {
public:
	constexpr static bool print_results = false;

	vector_map<cppm::scan_item_idx_t, depinfo::DepInfo> all_infos;
	std::size_t nr_results = 0;
	DepInfoCollector(span_map<cppm::scan_item_idx_t, const cppm::ScanItemView> items) : items(items) {
		all_infos.resize(items.size());
	}
private:
	span_map<cppm::scan_item_idx_t, const cppm::ScanItemView> items;
	std::vector<depinfo::DataBlock> data_table;
	depinfo::DepInfo* cur_info = nullptr;
	cppm::scan_item_idx_t current_item_idx = {};

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
	void results_for_item(cppm::scan_item_idx_t item_idx, bool out_of_date) override {
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

void scan_single_item_set(cppm::ScanItemSet& item_set) {
	timer t;
	cppm::Scanner scanner;

	auto item_set_owned_view = cppm::ScanItemSetOwnedView::from(item_set);
	auto item_set_view = cppm::ScanItemSetView::from(item_set_owned_view);
	//DepInfoCollector collector(item_set_view.items);

	cppm::Scanner::ConfigView config;
	config.tool_type = cppm::Scanner::Type::CLANG_SCAN_DEPS;
	config.tool_path = clang_scan_deps_path;
	config.db_path = single_db_path;
	config.int_dir = config.db_path;
	config.item_set = item_set_view;
	config.build_start_time = 0;
	config.concurrent_targets = false;
	config.file_tracker_running = false;
	//config.observer = &collector;

	t.start();
	scanner.scan(config);
	t.stop();
}

TEST_CASE("scanner - from a compilation database", "[scanner_comp_db]") {
	if (single_comp_db.empty())
		throw std::invalid_argument("must provide a compilation database path with --scanner_comp_db=\"...\"");
	if (single_db_path.empty())
		throw std::invalid_argument("must provide a db path with --scanner_db_path=\"...\"");
	try {
		auto item_set = cppm::scan_item_set_from_comp_db(single_comp_db, single_item_root_path);
		scan_single_item_set(item_set);
		scan_single_item_set(item_set);
		if (single_file_to_touch != "") {
			fs::last_write_time(single_file_to_touch.str(), fs::file_time_type::clock::now());
			scan_single_item_set(item_set);
			scan_single_item_set(item_set);
		}
	}
	catch (std::exception & e) {
		fmt::print("caught exception: {}\n", e.what());
	}
}

struct ood_idx { // avoid vector initalizer list problems by using a struct
	std::size_t idx;
};

using namespace Catch::Matchers;

struct TempFileScanTest : public TempFileTest {
private:
	cppm::ScanItemSet item_set;
	vector_map<cppm::scan_item_idx_t, depinfo::DepInfo> all_results;
	std::unique_ptr<cppm::ModuleVisitor> module_visitor_result;
	std::size_t nr_results = 0;
	vector_map<cppm::scan_item_idx_t, depinfo::DepInfo> all_expected;
	std::vector<std::vector<std::size_t>> expected_module_imports;
public:
	bool submit_previous_results = false;

	void set_expected(depinfo::DepFormat expected, std::vector<std::vector<std::size_t>> expected_module_imports = {}) {
		init_optionals(expected);
		// todo: this assumes expected.sources is ordered the same as item_set.items
		for (auto& depinfo : expected.sources)
			all_expected.push_back(std::move(depinfo));
		this->expected_module_imports = std::move(expected_module_imports);
		this->expected_module_imports.resize((std::size_t)all_expected.size());
	}

	TempFileScanTest() : TempFileTest() {
		all_files_created.insert("scanner.mdb"); // todo: pass the names to scanner
		all_files_created.insert("scanner.mdb-lock");
		all_files_created.insert("pp_commands.json");
		item_set.item_root_path = tmp_path_str;
		item_set.commands_contain_item_path = false; // todo: test when this is true
	}

	void create_deps(std::string_view file_def) {
		create_files(file_def, [&](const std::string&) {});
	}

	void add_item(const std::string& file, cppm::target_idx_t target_idx) {
		item_set.items.push_back({ 
			.path = file,
			.command_idx = item_set.commands.size(),
			.target_idx = target_idx
		});
		std::string cmd = fmt::format("{} /Fo\"{}.{}.obj\"", default_command, file, item_set.targets[target_idx]);
		item_set.commands.emplace_back(std::move(cmd));
	}

	void create_items(std::string_view target_name, std::string_view file_def,
		std::vector<std::string> file_refs = {})
	{
		auto target_idx = item_set.targets.size();
		item_set.targets.push_back((std::string)target_name);
		create_files(file_def, [&](std::string file) {
			add_item(file, target_idx);
		});
		for (auto& file : file_refs)
			add_item(file, target_idx);
	}

	void scan() {
		auto item_set_owned_view = cppm::ScanItemSetOwnedView::from(item_set);
		auto item_set_view = cppm::ScanItemSetView::from(item_set_owned_view);

		module_visitor_result = std::make_unique<cppm::ModuleVisitor>();

		DepInfoCollector collector(item_set_view.items);

		cppm::Scanner::ConfigView config;
		config.tool_type = cppm::Scanner::Type::CLANG_SCAN_DEPS;
		config.tool_path = clang_scan_deps_path;
		config.db_path = tmp_path_str;
		config.int_dir = config.db_path;
		config.item_set = item_set_view;
		config.build_start_time = 0;
		config.concurrent_targets = false;
		config.file_tracker_running = false;
		config.observer = &collector;
		config.submit_previous_results = submit_previous_results;
		if(submit_previous_results)
			config.module_visitor = module_visitor_result.get();

		cppm::Scanner scanner;
		
		timer t;
		t.start();
		scanner.scan(config);
		t.stop();

		all_results = std::move(collector.all_infos);
		nr_results = collector.nr_results;
	}

	void clean() {
		auto item_set_owned_view = cppm::ScanItemSetOwnedView::from(item_set);
		auto item_set_view = cppm::ScanItemSetView::from(item_set_owned_view);

		cppm::Scanner::ConfigView config;
		config.db_path = tmp_path_str;
		config.item_set = item_set_view;

		cppm::Scanner scanner;
		scanner.clean(config);
	}

	using opt_db_vec = std::optional<std::vector<depinfo::DataBlock>>;

	void canonicalize(opt_db_vec& result_opt, opt_db_vec& expected_opt) {
		for (auto& file : result_opt.value())
			file = fs::canonical(std::get<std::string>(file)).string();
		for (auto& file : expected_opt.value())
			file = fs::canonical(tmp_path / std::get<std::string>(file)).string();
	}

	template<typename T>
	void init_opt(std::optional<T>& opt) {
		if (!opt.has_value())
			opt.emplace();
	}

	void init_optionals(depinfo::DepInfo& depinfo) {
		init_opt(depinfo.outputs);
		init_opt(depinfo.depends);
		init_opt(depinfo.future_compile);
		auto& fc = depinfo.future_compile.value();
		init_opt(fc.output);
		init_opt(fc.provide);
		init_opt(fc.require);
		for (auto& mod : fc.provide.value())
			init_opt(mod.compiled_module_path);
		for (auto& mod : fc.require.value())
			init_opt(mod.compiled_module_path);
	}

	void init_optionals(depinfo::DepFormat& depformat) {
		for (auto& depinfo : depformat.sources)
			init_optionals(depinfo);
	}

	auto get_module_names(std::optional<std::vector<depinfo::ModuleDesc>>& from) {
		std::vector<std::string> names;
		for (auto& desc : from.value())
			names.push_back(std::get<std::string>(desc.logical_name));
		return names;
	}

	void check(const std::vector<ood_idx>& ood_indices) {
		if (!submit_previous_results) {
			CHECK(nr_results == ood_indices.size());
		} else {
			CHECK(nr_results == (std::size_t)item_set.items.size());
		}

		vector_map<cppm::scan_item_idx_t, char> expect_is_ood;
		expect_is_ood.resize(item_set.items.size());
		for (auto idx_wrapper : ood_indices)
			expect_is_ood[cppm::scan_item_idx_t { idx_wrapper.idx }] = true;

		for (auto item_idx : all_results.indices()) {
			if (!expect_is_ood[item_idx] && !submit_previous_results)
				continue;
			depinfo::DepInfo& res_info = all_results[item_idx];
			depinfo::DepInfo& exp_info = all_expected[item_idx];

			init_optionals(res_info);

			//CHECK(std::get<std::string>(res_info.input) == input_file);
			CHECK(std::get<std::size_t>(res_info.input) == (std::size_t)item_idx);

			canonicalize(res_info.depends, exp_info.depends);
			CHECK_THAT(res_info.depends.value(), UnorderedEquals(exp_info.depends.value()));

			auto& res_fc = res_info.future_compile.value(), & exp_fc = exp_info.future_compile.value();
			auto res_provides = get_module_names(res_fc.provide);
			auto exp_provides = get_module_names(exp_fc.provide);
			CHECK_THAT(res_provides, UnorderedEquals(exp_provides));
			auto res_requires = get_module_names(res_fc.require);
			auto exp_requires = get_module_names(exp_fc.require);
			CHECK_THAT(res_requires, UnorderedEquals(exp_requires));

			if (submit_previous_results) {
				std::vector<std::size_t> res_imports;
				for (auto imp_idx : module_visitor_result->imports_item[item_idx])
					res_imports.push_back((std::size_t)imp_idx);
				auto& exp_imports = expected_module_imports[(std::size_t)item_idx];
				CHECK_THAT(res_imports, UnorderedEquals(exp_imports));
			}
		}
	}

	void scan_check(const std::vector<ood_idx>& ood_indices) {
		scan();
		check(ood_indices);
	}

	void touch(const char* file_name) {
		fs::last_write_time(tmp_path / file_name, fs::file_time_type::clock::now());
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
	test.create_items("target1", R"(
> a.cpp
#include "a.h"
> b.cpp
#include "b.h"
> c.cpp
	)");
	test.create_items("target2", "", { "b.cpp" });

	using vdb = std::vector<depinfo::DataBlock>;
	depinfo::DepInfo a_cpp_info = { .input = "a.cpp", .depends = vdb{ "a.h", "b.h" } };
	depinfo::DepInfo b_cpp_info = { .input = "b.cpp", .depends = vdb{ "b.h" } };
	depinfo::DepInfo c_cpp_info = { .input = "c.cpp" };
	test.set_expected({ .sources = { a_cpp_info, b_cpp_info, c_cpp_info, b_cpp_info } });

	ood_idx a { 0 }, b { 1 }, c { 2 }, b1 { 3 };

	test.scan_check({ a, b, c, b1 }); // first scan

	test.scan_check({}); // everything up to date

	for (bool submit_previous_results : { false, true })
	{
		test.submit_previous_results = submit_previous_results;

		test.touch("a.h");
		test.scan_check({ a }); // only a.cpp depends on a.h

		test.scan_check({}); // no changes again

		test.touch("b.h");
		test.scan_check({ a, b, b1 }); // both depend on b.h

		test.touch("b.cpp");
		test.scan_check({ b, b1 }); // just b.cpp this time

		test.touch("c.cpp");
		test.scan_check({ c }); // just c.cpp this time

		test.scan_check({}); // no changes again

		test.touch("b.h");
		test.touch("c.cpp");
		test.scan_check({ a, b, c, b1 }); // all three again

		test.scan_check({}); // no changes again

		test.clean();
		test.scan_check({ a, b, c, b1 }); // all items were removed from the DB

		test.scan_check({}); // no changes again

		// todo: test command changed
	}
}

TEST_CASE("test2 - modules", "[scanner]") {
	TempFileScanTest test;

	test.scan_check({}); // nothing to scan

	test.create_deps(R"(
> a.h
import a;
> c.h
#include "a.h"
import b;
	)");
	test.create_items("target1", R"(
> a.cpp
export module a;
import std.core;
> b.cpp
export module b;
#include "a.h"
> c.cpp
#include "c.h"
	)");

	using vdb = std::vector<depinfo::DataBlock>;
	using vmd = std::vector<depinfo::ModuleDesc>;
	using fc = depinfo::FutureDepInfo;
	depinfo::DepInfo a_cpp_info = { .input = "a.cpp", 
		.future_compile = fc {
			.provide = vmd { { .logical_name = "a" } },
			.require = vmd { { .logical_name = "std.core" } }
		}
	};
	depinfo::DepInfo b_cpp_info = { .input = "b.cpp", .depends = vdb{ "a.h" },
		.future_compile = fc { 
			.provide = vmd { { .logical_name = "b" } },
			.require = vmd { { .logical_name = "a" } }
		}
	};
	depinfo::DepInfo c_cpp_info = { .input = "c.cpp", .depends = vdb{ "a.h", "c.h" },
		.future_compile = fc {
			.require = vmd { { .logical_name = "a" }, { .logical_name = "b" } }
		}
	};
	test.set_expected({ .sources = { a_cpp_info, b_cpp_info, c_cpp_info } },
		{ {},{0},{0,1} });

	ood_idx a { 0 }, b { 1 }, c { 2 };

	test.scan_check({ a, b, c }); // first scan

	for (bool submit_previous_results : { false, true })
	{
		test.submit_previous_results = submit_previous_results;
		test.scan_check({}); // everything up to date

		test.touch("a.h");
		test.scan_check({ b,c });

		test.scan_check({});

		test.touch("a.cpp");
		test.scan_check({ a });

		test.scan_check({});

		test.touch("c.h");
		test.scan_check({ c });
	}
}

// todo: test item_root_dir
// todo: test build_start_time

} // namespace scanner_test