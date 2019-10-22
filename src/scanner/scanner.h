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
DECL_STRONG_ID(scan_item_idx_t)

template<typename string_t>
struct ScanItemBase {
	string_t path; // UTF_8
	cmd_idx_t command_idx = {};
	target_idx_t target_idx = {};

	template<typename other_string_t>
	static ScanItemBase<string_t> from(const ScanItemBase<other_string_t> & item) {
		return { item.path, item.command_idx, item.target_idx };
	}

	explicit operator ScanItemBase<std::string_view>() const {
		return { path, command_idx, target_idx };
	}
};

using ScanItem = ScanItemBase<std::string>; // todo: use std::u8string(_view) everywhere
using ScanItemView = ScanItemBase<std::string_view>;

namespace detail {

template<typename T> struct is_view : std::false_type {};
template<> struct is_view<std::string_view> : std::true_type {};
template<> struct is_view<ScanItemView> : std::true_type {};
template<typename T> constexpr bool is_view_v = is_view<T>::value;

template<typename T> struct is_span_map : std::false_type {};
template<typename U, typename V> struct is_span_map < span_map<U, V> >: std::true_type {};
template<typename T> constexpr bool is_span_map_v = is_span_map<T>::value;

// convert between any of the following, when possible
// vector<object>
// vector<view>
// span<object>
// span<view>
// where object could be string/ScanItem
// and view could be string_view/ScanItemView
template<
	template < typename, typename > typename dst_map_t, typename dst_idx_t, typename dst_val_t,
	template < typename, typename > typename src_map_t, typename src_idx_t, typename src_val_t
>
static void conv(dst_map_t<dst_idx_t, dst_val_t>& dst, const src_map_t<src_idx_t, src_val_t>& src) {
	using dst_t = dst_map_t<dst_idx_t, dst_val_t>;
	if constexpr (is_span_map_v<dst_t>) {
		if constexpr (is_view_v<dst_val_t>) {
			static_assert(is_view_v<src_val_t>,
				"cannot create e.g a span<string_view> from a *<string> directly");
			dst = src;
		} else { // !is_view_v<dst_val_t>
			static_assert(!is_view_v<src_val_t>,
				"cannot create e.g a span<string> from a *<string_view>");
			dst = src;
		}
	} else { // is_vector_map<dst_t>
		dst.resize(src.size()); // works for both vector/span
		for (auto i : src.indices()) 
			dst[i] = (dst_val_t)src[i]; // works for string(_view)<->string(_view)
	}
}

} // namespace detail

template<
	typename string_t,
	template < typename, typename > typename map_t
>
struct ScanItemSetBase;

using ScanItemSet = ScanItemSetBase<std::string, vector_map>;
using ScanItemSetOwnedView = ScanItemSetBase<std::string_view, vector_map>;
using ScanItemSetView = ScanItemSetBase<std::string_view, span_map>;

template<
	typename string_t,
	template < typename, typename > typename map_t
>
struct ScanItemSetBase
{
	// if not "" then relative item paths will be based on this:
	string_t item_root_path;
	// there may be far fewer unique commands than items:
	map_t<cmd_idx_t, string_t> commands;
	// if false: the item paths will be appeneded to the commands:
	bool commands_contain_item_path = false;
	// names used to distinguish mutiple configurations for the same file
	map_t<target_idx_t, string_t> targets;
	// what to scan:
	// note: source files other than header units may appear in multiple targets
	map_t<scan_item_idx_t, ScanItemBase<string_t>> items;

	template<
		typename other_string_t,
		template < typename, typename > typename other_map_t
	>
	static auto from(const ScanItemSetBase<other_string_t, other_map_t>& other) {
		ScanItemSetBase ret;
		ret.item_root_path = other.item_root_path;
		detail::conv(ret.commands, other.commands);
		ret.commands_contain_item_path = other.commands_contain_item_path;
		detail::conv(ret.targets, other.targets);
		detail::conv(ret.items, other.items);
		return ret;
	}
};

ScanItemSet scan_item_set_from_comp_db(std::string_view comp_db_path, std::string_view item_root_path = "");

enum class ood_state {
	unknown,
	command_changed,
	new_file,
	file_changed,
	deps_changed,
	up_to_date
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

	// todo: maybe use a separate function_ref for each of these
	// so the user can subscribe to only a subset of them ?
	virtual void results_for_item(scan_item_idx_t item_idx, bool out_of_date) {}
	virtual void export_module(DataBlockView name) {}
	virtual void import_module(DataBlockView name) {}
	virtual void include_header(DataBlockView path) {}
	virtual void import_header(DataBlockView path) {}
	virtual void other_file_dep(DataBlockView path) {}
	virtual void item_finished() {}
};

struct ModuleVisitor {
	std::vector<scan_item_idx_t> imports_item_buf;
	vector_map<scan_item_idx_t, bool> has_export;
	vector_map<scan_item_idx_t, tcb::span<scan_item_idx_t>> imports_item;
	std::vector<scan_item_idx_t> queue;
	std::vector<bool> is_in_queue;
	bool missing_imports = false;

	template<typename F>
	void visit_transitive_imports(scan_item_idx_t root_idx, F&& visitor_func) {
		queue.resize((std::size_t)imports_item.size());
		if (queue.empty())
			return;
		is_in_queue.resize((std::size_t)imports_item.size());
		queue[0] = root_idx;
		is_in_queue[(std::size_t)root_idx] = true;
		std::size_t s = 0, e = 0;
		while (s <= e) {
			auto idx = queue[s++];
			if (idx != root_idx)
				visitor_func(idx);
			for (auto imp_idx : imports_item[idx]) {
				if (!is_in_queue[(std::size_t)imp_idx]) {
					is_in_queue[(std::size_t)imp_idx] = true;
					queue[++e] = imp_idx;
				}
			}
		}
		is_in_queue.clear();
	}
};

struct ScannerImpl;

class Scanner {
	std::unique_ptr<ScannerImpl> impl;
public:
	enum class Type {
		CLANG_SCAN_DEPS
	};

	template<
		typename string_t, // note: all of the input strings should be UTF-8
		template < typename, typename > typename map_t
	>
	struct ConfigBase
	{
		// the type of scanner used:
		Type tool_type = Type::CLANG_SCAN_DEPS;
		// the path to the scanner tool to execute:
		string_t tool_path;
		// where to persist information for incremental scans:
		// (this is intended to be the same for all targets)
		string_t db_path;
		// where to store other intermediate files unique to each (concurrently) scanned targets:
		string_t int_dir;
		// what to scan:
		ScanItemSetBase<string_t, map_t> item_set;
		// used to speed up multiple invocations during the same build:
		file_time_t build_start_time = 0;
		// if false: must not invoke the tool on the same DB concurrently:
		// if true: can use the same DB as long as all concurrent invocations are scanning different items // todo: too restrictive ?
		bool concurrent_targets = true;
		// if true: assume the stat times in the DB are up to date:
		bool file_tracker_running = false;
		// scan results are sent to this observer:
		DepInfoObserver* observer = nullptr;
		// submit scan results to the observer from previous scans for up-to-date items
		bool submit_previous_results = false;
		// initialize this visitor which helps with resolving transitive imports
		// note: requires submit_previous_results = true
		ModuleVisitor* module_visitor = nullptr;

		template<
			typename other_string_t,
			template < typename, typename > typename other_map_t
		>
		static auto from(const ConfigBase<other_string_t, other_map_t>& conf) {
			ConfigBase<string_t, map_t> ret;
			ret.tool_type = conf.tool_type;
			ret.tool_path = conf.tool_path;
			ret.db_path = conf.db_path;
			ret.int_dir = conf.int_dir;
			ret.item_set = ScanItemSetBase<string_t, map_t>::from(conf.item_set);
			ret.build_start_time = conf.build_start_time;
			ret.concurrent_targets = conf.concurrent_targets;
			ret.file_tracker_running = conf.file_tracker_running;
			ret.observer = conf.observer;
			ret.submit_previous_results = conf.submit_previous_results;
			ret.module_visitor = conf.module_visitor;
			return ret;
		}
	};

	using Config = ConfigBase<std::string, vector_map>;
	using ConfigOwnedView = ConfigBase<std::string_view, vector_map>;
	using ConfigView = ConfigBase<std::string_view, span_map>;

	Scanner();
	~Scanner();

	std::string scan(const ConfigView& config);

	void clean(const ConfigView& config);
};

} // namespace cppm