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

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

namespace depinfo {
	std::ostream& operator <<(std::ostream& os, DataBlock const& value) {
		std::visit(overloaded {
			[&](const std::string& s) {
				os << "'" << s << "'";
			}, [&](auto&) {}
		}, value);
		return os;
	}
}

namespace scanner_test {

ConfigPath clang_scan_deps_path { "clang_scan_deps_path", R"(c:\Program Files\cpp_modules\bin\clang-scan-deps.exe)" };
ConfigString default_command { "scanner_default_command", R"("cl.exe" /c /Zi /nologo /W3 /WX- /diagnostics:column /Od /Ob0 /D WIN32 /D _WINDOWS /D _MBCS /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /GR /std:c++latest /Gd /TP)" };
ConfigPath single_comp_db { "scanner_comp_db", "" };
ConfigPath single_db_path { "scanner_db_path", "" };
ConfigPath single_file_to_touch { "scanner_file_to_touch", "" };
ConfigPath single_item_root_path { "scanner_item_root_path", "" };

struct DepInfoCollector : public cppm::DepInfoObserver {
public:
	constexpr static bool print_results = false;

	vector_map<cppm::scan_item_idx_t, depinfo::DepInfo> all_infos;
	vector_map<cppm::scan_item_idx_t, char> is_ood;
	std::size_t nr_results = 0;
	DepInfoCollector(span_map<cppm::scan_item_idx_t, const cppm::ScanItemView> items) : items(items) {
		all_infos.resize(items.size());
		is_ood.resize(items.size());
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
		is_ood[current_item_idx] = out_of_date;
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

using namespace Catch::Matchers;

inline bool ends_with(std::string_view str, std::string_view with) {
	if (str.size() < with.size())
		return false;
	return str.substr(str.size() - with.size(), with.size()) == with;
}

constexpr std::string_view default_cmd_suffix = "ASDFG";

struct TempFileScanTest : public TempFileTest {
private:
	cppm::ScanItemSet item_set;
	vector_map<cppm::scan_item_idx_t, depinfo::DepInfo> all_results;
	vector_map<cppm::scan_item_idx_t, char> observer_result_is_ood;
	std::size_t nr_results = 0;

	vector_map<cppm::scan_item_idx_t, char> has_previous_result;

	vector_map<cppm::scan_item_idx_t, cppm::Scanner::Result> scanner_results;
	std::unique_ptr<cppm::ModuleVisitor> module_visitor_result;
	
	vector_map< cppm::scan_item_idx_t, char> expect_results;
	vector_map<cppm::scan_item_idx_t, depinfo::DepInfo> all_expected;
	// the set of scan item indices expected to be imported by each item
	std::vector<std::vector<cppm::scan_item_idx_t>> expected_module_imports;
	int scan_counter = 0;
	int current_line = 0; // line from which the last test method was called
public:
	bool submit_previous_results = false;

	void set_expected(depinfo::DepFormat expected, std::vector<std::vector<cppm::scan_item_idx_t>> expected_module_imports = {}) {
		init_optionals(expected);
		// todo: this assumes expected.sources is ordered the same as item_set.items
		for (auto& depinfo : expected.sources)
			all_expected.push_back(std::move(depinfo));
		this->expected_module_imports = std::move(expected_module_imports);
		this->expected_module_imports.resize((std::size_t)all_expected.size());
		has_previous_result.resize(all_expected.size());
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
			.target_idx = target_idx,
			.is_header_unit = ends_with(file, ".h")
		});
		std::string cmd = fmt::format("{} /Fo\"{}.{}.obj\" /D {}", 
			default_command, file, item_set.targets[target_idx], default_cmd_suffix);
		item_set.commands.emplace_back(std::move(cmd));
	}

	void remove_item(cppm::scan_item_idx_t idx) {
		item_set.items.erase(item_set.items.begin() + (std::size_t)idx);
		item_set.commands.erase(item_set.commands.begin() + (std::size_t)idx);
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

	void set_command_suffix(cppm::scan_item_idx_t idx, std::string_view suffix) {
		// by construction, cmd idx == item idx
		std::string& cmd = item_set.commands[id_cast<cppm::cmd_idx_t>(idx)];
		auto d_pos = cmd.rfind("/D ");
		assert(d_pos != std::string::npos);
		auto prev_suffix_pos = d_pos + 3;
		auto prev_suffix_length = cmd.size() - prev_suffix_pos;
		cmd.replace(prev_suffix_pos, prev_suffix_length, suffix);
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

		auto what = fmt::format("scan {}", ++scan_counter);
		
		timer t;
		t.start();
		scanner_results = scanner.scan(config);
		t.stop(what);

		all_results = std::move(collector.all_infos);
		observer_result_is_ood = std::move(collector.is_ood);
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

		for (auto& has : has_previous_result)
			has = false;
	}

	using opt_db_vec = std::optional<std::vector<depinfo::DataBlock>>;

	std::string tmp_relative_path(const depinfo::DataBlock& path) {
		fs::path source_path = std::get<std::string>(path);
		if (source_path.is_absolute())
			source_path = fs::relative(source_path, tmp_path);
		return source_path.string();
	}

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
		for (auto& desc : from.value()) {
			if (desc.source_path.has_value()) // header unit imports
				names.push_back(tmp_relative_path(desc.source_path.value()));
			else
				names.push_back(std::get<std::string>(desc.logical_name));
		}
		return names;
	}

	void check(const std::vector<cppm::scan_item_idx_t>& ood_indices,
		const std::vector<cppm::scan_item_idx_t>& fail_indices = {},
		bool expect_collate_success = true)
	{
		auto to_id_map = [&](auto& vec) {
			vector_map<cppm::scan_item_idx_t, char> id_map;
			id_map.resize(item_set.items.size());
			for (auto idx : vec)
				id_map[idx] = true;
			return id_map;
		};

		auto expect_is_ood = to_id_map(ood_indices);
		auto expect_failed = to_id_map(fail_indices);

		auto expect_result = [&](cppm::scan_item_idx_t idx) {
			if (expect_failed[idx])
				return false;
			// if submit_previous_results then they should be available,
			// unless this is the first scan or last time the scan failed
			if (submit_previous_results && has_previous_result[idx])
				return true;
			// otherwise we only expect to get result for ood items that got scanned
			return (bool)expect_is_ood[idx];
		};

		for (auto item_idx : all_results.indices()) {
			depinfo::DepInfo& exp_info = all_expected[item_idx];
			INFO("checking " << std::get<std::string>(exp_info.input) << " (" << (uint32_t)item_idx << ")");

			cppm::Scanner::Result& res_ret = scanner_results[item_idx];
			bool res_failed = (res_ret.scan == cppm::scan_state::failed);
			bool res_is_ood = (res_ret.ood != cppm::ood_state::up_to_date &&
				res_ret.ood != cppm::ood_state::unknown);
			CHECK(res_failed == (bool)expect_failed[item_idx]);
			CHECK(res_is_ood == (bool)expect_is_ood[item_idx]);

			depinfo::DepInfo& res_info = all_results[item_idx];
			bool got_result = std::holds_alternative<std::size_t>(res_info.input);
			CHECK(got_result == expect_result(item_idx));
			has_previous_result[item_idx] |= (char)got_result;
			if (!got_result)
				continue;
			CHECK(observer_result_is_ood[item_idx] == expect_is_ood[item_idx]);

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
				CHECK(module_visitor_result->collate_success == expect_collate_success);
				if (module_visitor_result->collate_success) {
					std::vector<cppm::scan_item_idx_t> res_imports;
					for (auto imp_idx : module_visitor_result->imports_item[item_idx])
						res_imports.push_back(imp_idx);
					//auto& res_imports = module_visitor_result->imports_item[item_idx];
					auto& exp_imports = expected_module_imports[(std::size_t)item_idx];
					CHECK_THAT(res_imports, UnorderedEquals(exp_imports));
				}
			}
		}
	}

	void scan_check(const std::vector<cppm::scan_item_idx_t>& ood_indices,
		const std::vector<cppm::scan_item_idx_t>& fail_indices = {},
		bool expect_collate_success = true)
	{
		INFO("line " << current_line);
		scan();
		check(ood_indices, fail_indices, expect_collate_success);
	}

	TempFileScanTest& set_line(int line) {
		current_line = line;
		return *this;
	}
};

using vdb = std::vector<depinfo::DataBlock>;
using vmd = std::vector<depinfo::ModuleDesc>;
using fc = depinfo::FutureDepInfo;

#define test test_.set_line(__LINE__)

TEST_CASE("test1", "[scanner]") {
	TempFileScanTest test_;

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

	depinfo::DepInfo a_cpp_info = { .input = "a.cpp", .depends = vdb{ "a.h", "b.h" } };
	depinfo::DepInfo b_cpp_info = { .input = "b.cpp", .depends = vdb{ "b.h" } };
	depinfo::DepInfo c_cpp_info = { .input = "c.cpp" };
	test.set_expected({ .sources = { a_cpp_info, b_cpp_info, c_cpp_info, b_cpp_info } });

	cppm::scan_item_idx_t a { 0 }, b { 1 }, c { 2 }, b1 { 3 };

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

		test.set_command_suffix(a, "TTRTETRRE");
		test.scan_check({ a }); // the command changed for a
		test.set_command_suffix(b, "JFDJD");
		test.set_command_suffix(a, default_cmd_suffix);
		test.scan_check({ a, b });
		test.set_command_suffix(b, default_cmd_suffix);
		test.scan_check({ b });
	}
}

TEST_CASE("test2 - modules", "[scanner]") {
	TempFileScanTest test_;

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
	cppm::scan_item_idx_t a { 0 }, b { 1 }, c { 2 };
	test.set_expected({ .sources = { a_cpp_info, b_cpp_info, c_cpp_info } },
		{ {},{ a },{ a, b } });


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

TEST_CASE("test3 - header units", "[scanner]") {
	TempFileScanTest test_;

	test.scan_check({}); // nothing to scan

	test.create_deps(R"(
> c.h
> d.h
	)");
	test.create_items("target1", R"(
> a.h
import "b.h";
#include "c.h"
> b.h
#ifdef FOO
  #include "c.h"
#else
  #include "d.h"
#endif
> a.cpp
export module a;
import "a.h";
> b.cpp
#define FOO
import "b.h";
> c.cpp
import a;
#define FOO
#include "b.h"
	)");

	#define SCANNER_BUG // todo:

	depinfo::DepInfo a_h_info = { .input = "a.h",
	#ifdef SCANNER_BUG
		.depends = vdb{ "c.h", "d.h" },
	#else
		.depends = vdb{ "c.h" },
	#endif
		.future_compile = fc {
			.require = vmd { {.source_path = "b.h" } }
		}
	};
	depinfo::DepInfo b_h_info = { .input = "b.h",
		.depends = vdb{ "d.h" }
	};
	depinfo::DepInfo a_cpp_info = { .input = "a.cpp",
	#ifdef SCANNER_BUG
		.depends = vdb{ "c.h", "d.h" },
	#endif
		.future_compile = fc {
			.provide = vmd { { .logical_name = "a" } },
		#ifdef SCANNER_BUG
			.require = vmd { { .source_path = "a.h" }, { .source_path = "b.h" } }
		#else
			.require = vmd { { .source_path = "a.h" } }
		#endif
		}
	};
	depinfo::DepInfo b_cpp_info = { .input = "b.cpp",
	#ifdef SCANNER_BUG
		.depends = vdb{ "c.h" },
	#endif
		.future_compile = fc {
			.require = vmd { { .source_path = "b.h" } }
		}
	};
	depinfo::DepInfo c_cpp_info = { .input = "c.cpp",
	#ifdef SCANNER_BUG
		.depends = vdb{ "c.h" },
	#else
		.depends = vdb{ "b.h", "c.h" },
	#endif
		.future_compile = fc {
		#ifdef SCANNER_BUG
			.require = vmd { { .logical_name = "a" }, {.source_path = "b.h" } }
		#else
			.require = vmd { { .logical_name = "a" } }
		#endif
		}
	};
	cppm::scan_item_idx_t ah { 0 }, bh { 1 }, a { 2 }, b { 3 }, c { 4 };

	test.set_expected({ .sources = { a_h_info, b_h_info, a_cpp_info, b_cpp_info, c_cpp_info } },
	#ifdef SCANNER_BUG
		{ { bh },{},{ ah,bh },{ bh },{ bh, a } }
	#else
		{ { bh },{},{ ah },{ bh },{ a } }
	#endif
	);


	test.scan_check({ ah, bh, a, b, c });

	for (bool submit_previous_results : { false, true })
	{
		test.submit_previous_results = submit_previous_results;
		test.scan_check({});

		test.touch("a.h");
		test.scan_check({ ah, a });

		test.touch("b.h");
		test.scan_check({ ah, bh, a, b, c });

		test.touch("c.h");
	#ifdef SCANNER_BUG
		test.scan_check({ ah, a, b, c });
	#else
		test.scan_check({ ah, c });
	#endif

		test.touch("d.h");
	#ifdef SCANNER_BUG
		test.scan_check({ ah, bh, a, b, c });
	#else
		test.scan_check({ ah, bh, a, b });
	#endif
	}
}

TEST_CASE("test4 - error handling", "[scanner]") {
	TempFileScanTest test_;
	SECTION("missing headers / modules") {
		test.create_items("target1", R"(
> a.cpp
#include "a.h"
> b.cpp
import c;
> c.cpp
export module c;
		)");

		depinfo::DepInfo a_info = { .input = "a.cpp", .depends = vdb{ "a.h" }, };
		depinfo::DepInfo b_info = { .input = "b.cpp", .future_compile = fc { .require = vmd { { .logical_name = "c" } } } };
		depinfo::DepInfo c_info = { .input = "c.cpp", .future_compile = fc { .provide = vmd { { .logical_name = "c" } } } };
		cppm::scan_item_idx_t a { 0 }, b { 1 }, c { 2 };
		test.set_expected({ .sources = { a_info, b_info, c_info } }, { {}, { c }, {} });
		test.submit_previous_results = true;

		test.scan_check({ a, b, c }, { a }); // the scan should fail but still return the results for b,c
		// todo: a shouldn't really be ood here:
		test.scan_check({ a }, { a }); // still fails but b,c are no longer out of date
		test.touch("a.h");
		test.scan_check({ a }); // successful scan here
		test.scan_check({});
		test.remove("a.h");
		test.scan_check({ a }, { a }); // failing again

		test.remove_item(c);
		// b should be scanned successfully even though c is missing
		test.scan_check({ a }, { a }, false); // but collate should fail
		test.remove_item(b);
		test.touch("a.h");
		test.scan_check({ a }); // collate success without b
	}

	SECTION("module cycles") {
		test.create_items("target1", R"(
> a.cpp
export module a;
import b;
> b.cpp
export module b;
import a;
		)");

		depinfo::DepInfo a_info = { .input = "a.cpp", .future_compile = fc {
			.provide = vmd { {.logical_name = "a" } },
			.require = vmd { {.logical_name = "b" } }
		} };
		depinfo::DepInfo b_info = { .input = "b.cpp", .future_compile = fc {
			.provide = vmd { {.logical_name = "b" } },
			.require = vmd { {.logical_name = "a" } } 
		} };
		cppm::scan_item_idx_t a { 0 }, b { 1 };
		test.set_expected({ .sources = { a_info, b_info } }, { { b }, { a } });
		test.submit_previous_results = true;
		test.scan_check({ a, b }); // collate success here
		// todo: neither collate nor visit can detect cycles, but ninja should
	}
}

// todo: test item_root_dir
// todo: test build_start_time

} // namespace scanner_test