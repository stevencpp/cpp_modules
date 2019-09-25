#pragma once

#include <vector>
#include <string_view>
#include <memory>
#include "span.hpp"

#include "depinfo.h"
#include "strong_id.h"
#include "file_time.h"

namespace cppm {

DECL_STRONG_ID(target_idx_t)
DECL_STRONG_ID(cmd_idx_t)

struct ScanItemView {
	std::string_view path; // UTF_8 : todo: use std::u8string_view everywhere
	cmd_idx_t command_idx = {};
	target_idx_t target_idx = {};
};

struct DepInfoObserver {
	struct RawDataBlockView {
		std::string_view format;
		tcb::span<int> code_units;
		std::string_view readable_name;
	};
	struct IndexedStringView {
		std::size_t idx;
		std::string_view sv;
	};
	struct IndexedRawDataBlockView {
		std::size_t idx;
		RawDataBlockView data;
	};
	using DataBlockView = std::variant<std::string_view, IndexedStringView, IndexedRawDataBlockView, std::size_t, RawDataBlockView>;
	
	virtual void on_result(std::size_t for_item_idx, std::string_view depinfo_json) {}

	virtual void results_for_item(std::size_t item_idx) {}
	virtual void export_module(DataBlockView name) {}
	virtual void import_module(DataBlockView name) {}
	virtual void include_header(DataBlockView path) {}
	virtual void import_header(DataBlockView path) {}
	virtual void other_file_dep(DataBlockView path) {}
	virtual void item_finished() {}
};

struct ScannerImpl;

class Scanner {
	std::unique_ptr<ScannerImpl> impl;
public:
	enum class Type {
		CLANG_SCAN_DEPS
	};

	// note: all of the input strings should be UTF-8
	struct Config
	{
		// the type of scanner used:
		Type tool_type = Type::CLANG_SCAN_DEPS;
		// the path to the scanner tool to execute:
		std::string_view tool_path;
		// where to persist information for incremental scans:
		// (this is intended to be the same for all targets)
		std::string_view db_path;
		// where to store other intermediate files unique to each (concurrently) scanned targets:
		std::string_view int_dir;
		// if not "" then relative item paths will be based on this:
		std::string_view item_root_path;
		// there may be far fewer unique commands than items:
		span_map<cmd_idx_t, std::string_view> commands;
		// if false: the item paths will be appeneded to the commands:
		bool commands_contain_item_path = false;
		// names used to distinguish mutiple configurations for the same file
		span_map<target_idx_t, std::string_view> targets;
		// what to scan:
		// note: source files other than header units may appear in multiple targets
		tcb::span<ScanItemView> items;
		// used to speed up multiple invocations during the same build:
		file_time_t build_start_time = 0;
		// if false: must not invoke the tool on the same DB concurrently:
		// if true: can use the same DB as long as all concurrent invocations are scanning different items // todo: too restrictive ?
		bool concurrent_targets = true;
		// if true: assume the stat times in the DB are up to date:
		bool file_tracker_running = false;
		// scan results are sent to this observer:
		DepInfoObserver* observer = nullptr;
	};

	Scanner();
	~Scanner();

	std::string scan(const Config & config);

	void clean(const Config& config);
};

} // namespace cppm