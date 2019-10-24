#include "scanner.h"

#pragma warning(disable:4275) // non dll-interface class 'std::runtime_error' used as base for dll-interface class 'fmt::v6::format_error'
#include <fmt/format.h>

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <optional>
#include <nlohmann/json.hpp>

#include "lmdb_wrapper.h"
#include "lmdb_path_store.h"
#include "lmdb_string_store.h"
#include "strong_id.h"
#include "multi_buffer.h"
#include "trace.h"
#include "cmd_line_utils.h"

namespace cppm {

DECL_STRONG_ID_INV(file_id_t, 0); // 0 is the invalid file id
DECL_STRONG_ID(unique_deps_idx_t);
DECL_STRONG_ID(to_stat_idx_t);
DECL_STRONG_ID_INV(db_target_id, 0);
DECL_STRONG_ID_INV(module_id_t, 0);

using cmd_hash_t = std::size_t; // todo: probably needs a bigger hash to avoid collisions ? 
using item_id_t = std::pair<file_id_t, db_target_id>;

namespace fs = std::filesystem;

std::string concat_u8_path(std::string_view path, std::string_view filename) {
	return (fs::u8path(path) / filename).string();
}

struct DB {
	mdb::mdb_env env;
	mdb::mdb_txn_rw txn_rw;

	void open(std::string_view db_path, std::string_view db_file_name) {
		TRACE();
		using namespace mdb::flags;
		constexpr int MB = 1024 * 1024;
		env.set_map_size(10 * MB);
		env.set_maxdbs(5);
		env.open(concat_u8_path(db_path, db_file_name).c_str(), env::nosubdir);
	}

	struct directory_entry {};
	struct file_entry {
		file_time_t last_write_time;
		file_time_t last_stat_time;
	};

	mdb::string_id_store<db_target_id> target_store { "targets" };
	mdb::path_store<file_id_t, directory_entry, file_entry> path_store;
	mdb::string_id_store<module_id_t> module_store { "modules" };

	auto get_item_file_ids(std::string_view item_root_path, span_map<scan_item_idx_t, const ScanItemView> items)
	{
		vector_map<scan_item_idx_t, std::string_view> paths;
		paths.reserve(items.size());
		for (auto& item : items)
			paths.push_back(item.path);
		return path_store.get_file_ids(txn_rw, item_root_path, paths);
	}

	void update_file_last_write_times(span_map<to_stat_idx_t, file_id_t> files_to_stat,
		const vector_map<file_id_t, file_time_t>& lwts)
	{
		TRACE();
		// todo: if the file didn't change, could we avoid updating all the file records ?
		auto last_stat_time = file_time_t_now();

		path_store.update_file_data(files_to_stat, [&](to_stat_idx_t idx) {
			return file_entry { lwts[files_to_stat[idx]], last_stat_time };
		});
	}

	template<typename size_type, typename... Vs>
	static void resize_all_to(size_type size, Vs&&... vs) {
		(vs.resize(size), ...);
	}

	auto get_target_ids(span_map<target_idx_t, std::string_view> targets) {
		TRACE();
		return target_store.get_ids(txn_rw, targets);
	}

	struct item_entry {
		cmd_hash_t cmd_hash;
		file_time_t last_successful_scan;
		tcb::span<file_id_t> file_deps;
		tcb::span<item_id_t> item_deps;
		module_id_t exports;
		tcb::span<module_id_t> imports;
	};

	// todo: make the DB independent of particular strong indexes used by the caller   
	struct item_data {
		// the source file corresponding to a given item (if it changes, the item is out of date)
		vector_map<scan_item_idx_t, file_id_t> file_id;
		vector_map<scan_item_idx_t, cmd_hash_t> cmd_hash;
		vector_map<scan_item_idx_t, file_time_t> last_successful_scan;
		// for a given item: if any of the files in file_deps changed then the item is out of date
		vector_map<scan_item_idx_t, tcb::span<file_id_t>> file_deps;
		// for a given item: if any of the items in item_deps are out of date then the item is out of date as well
		// note: this works recursively so that for e.g importable header units we can compress the log more
		vector_map<scan_item_idx_t, tcb::span<item_id_t>> item_deps;
		// note: the deps are invalidated on the first mdb_put operation
		// "Values returned from the database are valid only until a subsequent update operation, or the end of the transaction."
		file_id_t db_max_file_id = {}; // the largest file id in the database + 1
		file_id_t max_file_id = {}; // includes new files not already in the database
		vector_map<scan_item_idx_t, module_id_t> exports;
		vector_map<scan_item_idx_t, tcb::span<module_id_t>> imports;
		module_id_t db_max_module_id = {}; // the largest module id the database + 1

		void resize(scan_item_idx_t size) {
			DB::resize_all_to(size, file_id, cmd_hash, last_successful_scan, file_deps, item_deps, exports, imports);
		}
	};
	auto get_item_data(span_map<target_idx_t, const db_target_id> target_ids, std::string_view item_root_path,
		span_map<scan_item_idx_t, const ScanItemView> items) 
	{
		TRACE();
		item_data data;
		data.file_id = get_item_file_ids(item_root_path, items);
		data.db_max_file_id = path_store.db_max_file_id + 1;
		data.max_file_id = path_store.max_file_id + 1;

		data.resize(items.size()); // todo: can we not allocate all this if the DB is empty ?

		// todo: maybe make file_id the key and allow duplicates >
		auto db = txn_rw.open_db<item_id_t, item_entry>("items");

		for (auto i : items.indices()) {
			try {
				auto file_id = data.file_id[i];
				if (file_id >= data.db_max_file_id) // nothing to do here for new files
					continue;
				auto entry = db.get({ file_id, target_ids[items[i].target_idx] });
				// what about data.target ?
				data.cmd_hash[i] = entry.cmd_hash;
				data.last_successful_scan[i] = entry.last_successful_scan;
				data.file_deps[i] = entry.file_deps;
				data.item_deps[i] = entry.item_deps;
				data.exports[i] = entry.exports;
				data.imports[i] = entry.imports;
			} catch (mdb::key_not_found_exception&) {
				// if e.g the scanner was interrupted/crashed then
				// the file may be in the DB but not the item, ignore this
			}
		}
		return data;
	}

	void update_items( // todo: break this up
		const vector_map<scan_item_idx_t, ood_state>& item_ood,
		span_map<target_idx_t, const db_target_id> target_ids,
		span_map<scan_item_idx_t, const ScanItemView> items,
		const vector_map<scan_item_idx_t, file_id_t>& item_file_ids,
		const vector_map<cmd_idx_t, cmd_hash_t>& cmd_hashes,
		span_map<scan_item_idx_t, char> got_result,
		span_map<scan_item_idx_t, tcb::span<file_id_t>> file_deps,
		span_map<scan_item_idx_t, tcb::span<item_id_t>> item_deps,
		span_map<scan_item_idx_t, module_id_t> exports,
		span_map<scan_item_idx_t, tcb::span<module_id_t>> imports)
	{
		TRACE();
		module_store.commit_changes(txn_rw);
		target_store.commit_changes(txn_rw); // todo: order matters to be able to use append ?
		path_store.commit_changes(txn_rw);

		auto last_successful_scan = file_time_t_now();

		auto db = txn_rw.open_db<item_id_t, item_entry>("items");
		
		for (auto i : items.indices()) {
			if (!got_result[i])
				continue;
			if (item_ood[i] == ood_state::up_to_date)
				continue;
			// if item is new
			db.put(item_id_t {
				item_file_ids[i],
				target_ids[items[i].target_idx],
			}, item_entry {
				cmd_hashes[items[i].command_idx],
				last_successful_scan,
				file_deps[i],
				{}, // todo: item_deps[i]
				exports[i],
				imports[i]
			});
		}
	}

	void remove_items(span_map<target_idx_t, db_target_id> targets,
		span_map<scan_item_idx_t, const ScanItemView> items,
		const vector_map<scan_item_idx_t, file_id_t>& item_file_ids)
	{
		auto db = txn_rw.open_db<item_id_t, item_entry>("items");

		for (auto i : items.indices()) {
			auto file_id = item_file_ids[i];
			auto target_id = targets[items[i].target_idx];
			if(target_id.is_valid())
				db.del(item_id_t { file_id, target_id });
		}
	}

	struct file_data {
		// note: this is only needed if we don't want to re-stat the files
		// e.g if the scanner is run multiple times during the same build
		// or if a file tracker daemon is keeping these values up to date
		vector_map<unique_deps_idx_t, file_time_t> last_write_time;
		vector_map<unique_deps_idx_t, file_time_t> last_stat_time;
		// todo: maybe hash ? - we already need to read all the files to scan them so ..
		void resize(unique_deps_idx_t size) {
			DB::resize_all_to(size, last_write_time, last_stat_time);
		}
	};
	auto get_file_data(const vector_map<unique_deps_idx_t, file_id_t>& files) {
		TRACE();
		file_data data;
		data.resize(files.size());
		path_store.get_file_data(files, [&](unique_deps_idx_t idx, const file_entry& f) {
			data.last_write_time[idx] = f.last_write_time;
			data.last_stat_time[idx] = f.last_stat_time;
		});
		return data;
	}

	void get_file_paths(std::string_view item_root_path,
		tcb::span<const file_id_t> deps_to_stat, 
		/*inout:*/ vector_map<file_id_t, std::string_view>& file_paths)
	{
		// todo: parallelize this ?
		for (auto dep_file_id : deps_to_stat) {
			if (file_paths[dep_file_id].empty()) // don't use the DB if it's already set
				file_paths[dep_file_id] = path_store.get_file_path(dep_file_id);
		}
	}

	auto get_all_module_names() {
		return module_store.get_all_strings(txn_rw);
	}

	struct db_header {
		constexpr static int current_version = 1;
		int version = current_version;
	};

	void read_write_transaction() {
		TRACE();
		txn_rw = env.txn_read_write();

		auto dbi = txn_rw.open_db<uint64_t, db_header>("header");
		db_header h;
		try {
			h = dbi.get(1);
		} catch (mdb::key_not_found_exception&) {
			dbi.put(1, db_header {});
		}
		if (h.version != db_header::current_version)
			throw std::runtime_error("db version mismatch");
	}

	void commit_transaction() {
		TRACE();
		// todo: this is slow, maybe use MDB_MAPASYNC ?
		txn_rw.commit();
	}

	// this needs to be called before using try_add_* or get_*
	void init_stores() {
		TRACE();
		module_store.init(txn_rw, {});
	}

	// note: the input string must be valid until the changes are committed later
	module_id_t try_add_module(std::string_view name) {
		return module_store.try_add(name);
	}

	// note: the input string must be valid until the changes are committed later
	file_id_t try_add_file(std::string_view path) {
		return path_store.try_add(path);
	}

	std::string_view get_module_name(module_id_t id) {
		return module_store.get(id);
	}
};

struct ScannerImpl {
	DB db;

	ScannerImpl() {
		// todo: launch threads early, hoping to hide some of the startup overhead ?
	}

	cmd_hash_t get_cmd_hash(std::string_view cmd) {
		std::hash<std::string_view> hfn;
		return hfn(cmd);
	}

	auto get_rooted_path(std::string_view root_path, std::string_view file_path) {
		auto path = fs::u8path(file_path); // todo: proper UTF-8
		if (root_path != "" && !path.is_absolute())
			path = fs::u8path(root_path) / path;
		return path;
	}

	auto get_unique_deps(const vector_map<scan_item_idx_t, tcb::span<file_id_t>>& all_file_deps,
		const vector_map<scan_item_idx_t, file_id_t>& all_item_file_ids, file_id_t max_file_id)
	{
		TRACE();
		// todo: maybe store the unique deps for each target ?
		vector_map<unique_deps_idx_t, file_id_t> unique_deps;
		unique_deps.reserve(id_cast<unique_deps_idx_t>(max_file_id));
		vector_map<file_id_t, char> file_visited;
		file_visited.resize(max_file_id);

		auto add = [&](file_id_t id) {
			if (file_visited[id]) return;
			file_visited[id] = true;
			unique_deps.push_back(id);
		};

		for (auto file_id : all_item_file_ids)
			add(file_id);

		for (auto& file_deps : all_file_deps)
			for (auto file_id : file_deps)
				add(file_id);

		// note: item_deps doesn't add any unique file deps not already added here
		return unique_deps;
	}

	auto get_file_paths(std::string_view item_root_path,
		span_map<scan_item_idx_t, const ScanItemView> items, const vector_map<scan_item_idx_t, file_id_t>& item_file_ids,
		tcb::span<const file_id_t> deps_to_stat, file_id_t max_file_id)
	{
		TRACE();
		vector_map<file_id_t, std::string_view> file_paths; // keyed by file_id
		file_paths.resize(max_file_id);

		// for the items we don't need to retrieve the file path from the DB
		for (auto i = scan_item_idx_t { 0 }; i < items.size(); ++i)
			file_paths[item_file_ids[i]] = items[i].path;

		// todo: the file paths are only use for stat-ing the files
		// but it's more efficient to stat the dirs instead
		// so add a db operation that gets the unique set of parent paths instead
		db.get_file_paths(item_root_path, deps_to_stat, /*inout:*/ file_paths);

		return file_paths;
	}

	auto remove_deps_already_stated(vector_map<unique_deps_idx_t, file_id_t>& unique_deps,
		const vector_map<unique_deps_idx_t, file_time_t>& db_last_write_time,
		const vector_map<unique_deps_idx_t, file_time_t>& db_last_stat_time,
		file_time_t build_start_time, bool file_tracker_running, file_id_t max_file_id)
	{
		TRACE();
		struct ret_t {
			vector_map<file_id_t, file_time_t> real_lwts; // last write time
			span_map<to_stat_idx_t, file_id_t> deps_to_stat;
		} ret;
		ret.real_lwts.resize(max_file_id);
		auto idx = unique_deps_idx_t { 0 };
		auto itr_end = std::remove_if(unique_deps.begin(), unique_deps.end(), [&](file_id_t file_id) {
			auto i = idx++;
			file_id_t dep_id = unique_deps[i];
			if (file_tracker_running || db_last_stat_time[i] > build_start_time) {
				ret.real_lwts[dep_id] = db_last_write_time[i];
				return true; // this was already stat-ed during this build, no need to stat it again
			}
			return false;
		});
		if (!unique_deps.empty()) {
			file_id_t* start = &unique_deps.front(), * end = start + (itr_end - unique_deps.begin());
			ret.deps_to_stat = span_map<to_stat_idx_t, file_id_t> { start, end };
		}
		return ret;
	}

	void get_file_ood(std::string_view item_root_path,
		span_map<to_stat_idx_t, file_id_t> deps_to_stat,
		const vector_map<file_id_t, std::string_view>& file_paths,
		vector_map<file_id_t, file_time_t>& real_last_write_time)
	{
		TRACE();
		// todo: generate an optimal stat plan:
		// - if more than # files in same directory - stat the directory instead (can we do % of nr files in dir ?)
		// - try to stat the whole dir, 

		// resize to max file id
		//fs::current_path(fs::u8path(item_root_path));
		// todo: we need a huge # of threads for get_last_write_time, but maybe that's
		// unnecessary thread launch overhead if ~all of the files have been stat-ed already in the current build
//#pragma omp parallel for
		for (auto i = to_stat_idx_t { 0 }; i < deps_to_stat.size(); ++i) {
			file_id_t dep_id = deps_to_stat[i];
			real_last_write_time[dep_id] = get_last_write_time(
				get_rooted_path(item_root_path, file_paths[dep_id])
			);
		}
	}

	auto get_cmd_hashes(span_map<cmd_idx_t, std::string_view> commands)
	{
		TRACE();
		vector_map<cmd_idx_t, cmd_hash_t> cmd_hashes;
		cmd_hashes.resize(commands.size());

	//#pragma omp parallel for
		for(auto i : commands.indices())
			cmd_hashes[i] = get_cmd_hash(commands[i]);
		return cmd_hashes;
	}

	constexpr static bool log_ood = false;

	auto get_item_ood(
		span_map<scan_item_idx_t, const ScanItemView> items,
		const DB::item_data& item_data,
		const vector_map<cmd_idx_t, cmd_hash_t>& cmd_hashes,
		const vector_map<file_id_t, file_time_t>& real_lwt,
		const vector_map<file_id_t, std::string_view>& file_paths /* just for logging*/)
	{
		vector_map<scan_item_idx_t, ood_state> item_ood;
		item_ood.resize(item_data.file_id.size());

		auto get_ood_state = [&](scan_item_idx_t i) {
			file_id_t item_file_id = item_data.file_id[i];
			if (item_file_id >= item_data.db_max_file_id) {
				if constexpr (log_ood) fmt::print("{} is out of date because: it's a new file\n", items[i].path);
				return ood_state::new_file;
			}
			if (real_lwt[item_file_id] > item_data.last_successful_scan[i]) {
				if constexpr (log_ood) fmt::print("{} is out of date because: it changed\n", items[i].path);
				return ood_state::file_changed;
			}
			if (item_data.cmd_hash[i] != cmd_hashes[items[i].command_idx]) {
				if constexpr (log_ood) fmt::print("{} is out of date because: its command changed\n", items[i].path);
				return ood_state::command_changed;
			}
			for (auto dep_id : item_data.file_deps[i]) {
				if (real_lwt[dep_id] > item_data.last_successful_scan[i]) {
					if constexpr (log_ood) fmt::print("{} is out of date because: {} changed\n", items[i].path, file_paths[dep_id]);
					return ood_state::deps_changed;
				}
			}
			// todo: dep_items
			if constexpr (log_ood) fmt::print("{} is up to date\n", items[i].path);
			return ood_state::up_to_date;
		};

//#pragma omp parallel for
		for (auto i : item_ood.indices()) {
			item_ood[i] = get_ood_state(i);
		}

		return item_ood;
	}

	auto partition_items_by_ood(const vector_map<scan_item_idx_t, ood_state>& item_ood) {
		struct ret_t {
			std::vector<scan_item_idx_t> ood; // out of date
			std::vector<scan_item_idx_t> utd; // up to date
		} ret;
		ret.ood.reserve((std::size_t)item_ood.size());
		ret.utd.reserve((std::size_t)item_ood.size());
		for (auto i : item_ood.indices())
			if (item_ood[i] != ood_state::up_to_date)
				ret.ood.push_back(i);
			else
				ret.utd.push_back(i);
		return ret;
	}

	void generate_compilation_database(
		bool commands_contain_item_path,
		span_map<cmd_idx_t, std::string_view> commands,
		std::string_view item_root_path,
		span_map<scan_item_idx_t, const ScanItemView> items,
		const std::vector<scan_item_idx_t>& ood_items,
		const fs::path& comp_db_path)
	{
		if (ood_items.empty())
			return;
		TRACE();

		auto json_array = nlohmann::json();
		for (auto i : ood_items) {
			auto json = nlohmann::json();
			json["directory"] = item_root_path;
			auto path = get_rooted_path(item_root_path, items[i].path).string();
			json["file"] = path;
			std::string cmd = (std::string)commands[items[i].command_idx];
			if (!commands_contain_item_path) cmd += fmt::format(" \"{}\"", path);
			json["command"] = std::move(cmd);
			json_array.push_back(json);
		}

		std::ofstream db_out(comp_db_path);
		if (!db_out)
			throw std::runtime_error("failed to open compilation database");
		db_out << json_array.dump();
		db_out.close();
	}

	struct scanner_data {
		vector_map<scan_item_idx_t, char> got_result;
		stable_multi_string_buffer read_lines;
		reordered_multi_vector_buffer<scan_item_idx_t, file_id_t> file_deps_buf;
		reordered_multi_vector_buffer<scan_item_idx_t, item_id_t> item_deps_buf;
		reordered_multi_vector_buffer<scan_item_idx_t, module_id_t> imports_buf;
	};
	auto execute_scanner(std::string_view tool_path, std::string_view comp_db_path,
		std::string_view item_root_path, span_map<scan_item_idx_t, const ScanItemView> items,
		const std::vector<scan_item_idx_t>& ood_items, DepInfoObserver* observer,
		/*inout: */span_map<scan_item_idx_t, tcb::span<file_id_t>> file_deps,
		/*inout: */span_map<scan_item_idx_t, tcb::span<item_id_t>> item_deps,
		/*inout: */span_map<scan_item_idx_t, module_id_t> exports,
		/*inout: */span_map<scan_item_idx_t, tcb::span<module_id_t>> imports)
	{
		TRACE();
		scanner_data data;
		// todo: reserve memory for the other buffers here
		data.got_result.resize(items.size());

		if (ood_items.empty())
			return data;

		std::unordered_map<std::string, scan_item_idx_t> item_lookup;
		for (auto i : items.indices()) {
			// note: this doesn't work if there are multiple items with the same file path
			item_lookup[get_rooted_path(item_root_path, items[i].path).string()] = i;
		}
		auto starts_with = [](std::string_view a, std::string_view b) {
			return (a.substr(0, b.size()) == b);
		};
		int nr_lines = 0;
		std::string current_file;
		scan_item_idx_t current_item_idx = {};

		// code page 65001 is UTF-8, use that for the paths - todo: test this
		CmdArgs cmd { "chcp 65001 & \"{}\" --compilation-database=\"{}\"", tool_path, comp_db_path };
		auto ret = run_cmd_read_lines(cmd, [&](std::string_view line) {
			// todo: use the stable buffer inside run_cmd_read_lines to avoid this copy
			// todo: store only a single copy of each file/module name
			line = data.read_lines.copy(line);
#if 0
			auto input_file = json_depinfo["input"];
			auto item_idx = scan_item_idx_t { json_depinfo["_id"].get<std::size_t>() }; // note: otherwise we need to look at the outputs

			file_deps_buf.new_vector(item_idx);
			for (auto& json_dep_file : json_depinfo["depends"]) {
				auto dep_file = json_dep_file.get<std::string>();
				file_deps_buf.add(get_file_table_idx(dep_file));
			}
			item_deps_buf.new_vector(item_idx);
			for (auto& json_module : json_depinfo["future_compile"]["requires"]) {
				auto src_path = json_module["source_path"].get<std::string>();
				item_deps_buf.add(get_file_table_idx(src_path));
			}
#endif

			// skip the first line: active code page ..
			if (nr_lines++ == 0) return true;
			//fmt::print("{}\n", line);
			if (starts_with(line, ":::: ")) {
				if (observer && current_file != "") observer->item_finished();
				current_file = line.substr(5);
				auto itr = item_lookup.find(current_file);
				if (itr == item_lookup.end())
					throw std::runtime_error("unknown scan item");
				current_item_idx = itr->second;
				data.file_deps_buf.new_vector(current_item_idx);
				data.item_deps_buf.new_vector(current_item_idx);
				data.imports_buf.new_vector(current_item_idx);
				data.got_result[current_item_idx] = true;
				if(observer) observer->results_for_item(current_item_idx, /*out_of_date=*/true);
			} else if (starts_with(line, ":exp ")) {
				auto name = line.substr(5);
				exports[current_item_idx] = db.try_add_module(name);
				if(observer) observer->export_module(name);
			} else if (starts_with(line, ":imp ")) {
				auto name = line.substr(5);
				data.imports_buf.add(db.try_add_module(name));
				if(observer) observer->import_module(name);
				// todo: import header unit ?
			} else {
				if (line != "" && line != current_file) {
					data.file_deps_buf.add(db.try_add_file(line));
					// todo: header or other deps ? check extension ? :-/
					if (observer) observer->include_header(line);
					//if(observer) observer->other_file_dep();
				}
			}
			return true;
		}, [](std::string_view err_line) {
			fmt::print("ERR: {}\n", err_line);
			return true;
		});

		if(ret != 0)
			throw std::runtime_error("failed to execute scanner tool");

		if (observer && current_file != "") observer->item_finished();

		data.file_deps_buf.to_vectors(file_deps);
		data.item_deps_buf.to_vectors(item_deps);
		data.imports_buf.to_vectors(imports);

		return data;
	}

	bool init_module_visitor(ModuleVisitor* module_visitor,
		span_map<scan_item_idx_t, const ScanItemView> items,
		span_map<scan_item_idx_t, const module_id_t> exports,
		span_map<scan_item_idx_t, const tcb::span<module_id_t>> imports)
	{
		if (exports.empty())
			return true;
		TRACE();
		vector_map<module_id_t, scan_item_idx_t> exported_by;
		reordered_multi_vector_buffer<scan_item_idx_t, scan_item_idx_t> imports_item_buf;

		module_id_t max_module_id = *std::max_element(exports.begin(), exports.end());
		exported_by.resize(max_module_id + 1);
		for (auto& idx : exported_by)
			idx.invalidate();
		module_visitor->has_export.resize(exports.size());
		for (auto idx : exports.indices()) {
			if (exports[idx].is_valid())
				exported_by[exports[idx]] = idx;
			module_visitor->has_export[idx] = (exports[idx].is_valid());
		}

		for (auto idx : imports.indices()) {
			imports_item_buf.new_vector(idx);
			for (module_id_t id : imports[idx]) {
				auto exp_idx = exported_by[id];
				if (!exp_idx.is_valid()) {
					std::string_view name = db.get_module_name(id);
					if (name == "std.core")
						continue;
					fmt::print(stderr, "'{}' imports '{}' which is not exported by any TU\n",
						items[exp_idx].path, name);
					module_visitor->missing_imports = true;
					return false;
				}
				imports_item_buf.add(exp_idx);
			}
		}
		module_visitor->imports_item = imports_item_buf.to_vectors();
		module_visitor->imports_item_buf = imports_item_buf.move_buffer();
		return true;
	}

	void submit_up_to_date_items(tcb::span<scan_item_idx_t> utd_items, DB::item_data & item_data, 
		span_map<file_id_t, std::string_view> file_paths, DepInfoObserver* observer)
	{
		if (!observer)
			return;
		TRACE();
		auto module_names = db.get_all_module_names();
		for (auto i : utd_items) {
			observer->results_for_item(i, /*out_of_date=*/false);
			// todo: store headers and other deps separately ?
			for (auto file_id : item_data.file_deps[i])
				observer->other_file_dep(file_paths[file_id]);
			// todo: imported headers ?
			if(item_data.exports[i].is_valid())
				observer->export_module(module_names[item_data.exports[i]]);
			for (auto module_id : item_data.imports[i])
				observer->import_module(module_names[module_id]);
			observer->item_finished();
		}
	}

	// note: all of the input paths are required to be normalized already
	std::string scan(Scanner::Type tool_type, std::string_view tool_path, 
		std::string_view db_path, std::string_view int_dir, std::string_view item_root_path,
		bool commands_contain_item_path, span_map<cmd_idx_t, std::string_view> commands,
		span_map<target_idx_t, std::string_view> targets, 
		span_map<scan_item_idx_t, const ScanItemView> items,
		file_time_t build_start_time, bool concurrent_targets, bool file_tracker_running,
		DepInfoObserver * observer, bool submit_previous_results, ModuleVisitor * module_visitor)
	{
		TRACE();

		std::string ret;

		// todo: we may not need to recompute some of this if we can detect that the environment stays constant
		// todo: do this while reading data from the DB, use an eager future
		auto cmd_hashes = get_cmd_hashes(commands);

		// todo: estimate db size if it doesn't use a sparse file ?

		db.open(db_path, "scanner.mdb");

		// todo: maybe we could somehow do a read only transaction here
		// and then switch to a read-write transaction (possibly reading more) only if needed ?
		db.read_write_transaction();
		auto target_ids = db.get_target_ids(targets);
		auto item_data = db.get_item_data(target_ids, item_root_path, items); // returns new file/item ids for files/items not in the db yet
		// todo: if concurrent_targets == false, it might be more efficient to assume all files are deps ?
		auto unique_deps = get_unique_deps(item_data.file_deps, item_data.file_id, item_data.max_file_id);
		auto file_data = db.get_file_data(unique_deps);
		auto [real_lwt, deps_to_stat] = remove_deps_already_stated(unique_deps,
			file_data.last_write_time, file_data.last_stat_time, build_start_time, file_tracker_running, item_data.max_file_id);
		auto need_paths_for = deps_to_stat.to_span();
		if (observer && submit_previous_results) need_paths_for = unique_deps.to_span();
		auto file_paths = get_file_paths(item_root_path, items, item_data.file_id, need_paths_for, item_data.max_file_id);
		// todo: if the log is empty then we can stat items and compute hashes in parallel with scanning
		get_file_ood(item_root_path, deps_to_stat, file_paths, /*inout: */real_lwt);
		auto item_ood = get_item_ood(items, item_data, cmd_hashes, real_lwt, /*just for logging*/file_paths); // maybe do the cmd_hashes check later, close txn faster ?
		auto [ood_items, utd_items] = partition_items_by_ood(item_ood);
		// todo: the notification and the write could happen in parallel with scanning
		// note: the observer must not launch another scan on the same DB while we're holding the transaction lock
		if(observer && submit_previous_results) submit_up_to_date_items(utd_items, item_data, file_paths, observer);
		// note: the following might add new files but doesn't commit any changes yet
		db.update_file_last_write_times(deps_to_stat, real_lwt);
		if(module_visitor || ood_items.size() > 0)
			db.init_stores(); // used by execute_scanner and init_module_visitor

		// todo: load/store the minimized source files for clang-scan-deps from the DB
		// todo: would this be faster with a named pipe ? or sending directly to stdin ?
		auto comp_db_path = concat_u8_path(int_dir, "pp_commands.json");
		generate_compilation_database(commands_contain_item_path, commands,
			item_root_path, items, ood_items, comp_db_path);
		auto data = execute_scanner(tool_path, comp_db_path, item_root_path, items, ood_items, observer,
			/*inout: */item_data.file_deps, item_data.item_deps, item_data.exports, item_data.imports);

		if (module_visitor) init_module_visitor(module_visitor, items, item_data.exports, item_data.imports);

		// note: the deps/modules in item_data become invalid once we start writing to the db
		// note: the hash maps for path/module_store become invalid as well and they're used while scanning

		// todo: maybe break this function up ?
		db.update_items(item_ood, target_ids, items, item_data.file_id, cmd_hashes, 
			data.got_result, item_data.file_deps, item_data.item_deps, item_data.exports, item_data.imports);
		// changes to:
		// targets:
		// - new ids
		// files:
		// - new last write times / last stat time
		// - new files from items
		// - new files from deps (added with last_write_time/last_stat_time = 0)
		// items:
		// - new cmd hashes
		// - last_successful_scan = now ? todo: errors ?
		// - deps,export/import changed
		// modules:
		// - new ids
		db.commit_transaction();

		// todo: maybe run stat on the non-ood output files while the scanner is running
		// to get the OS to cache them and reduce the overhead of ninja restatting them afterwards ?

		// todo: maybe cleanup things that were removed ?

		//fmt::print("\n");
		// output information for the module maps
		return ret;
	}

	void clean(std::string_view db_path, std::string_view item_root_path,
		span_map<target_idx_t, std::string_view> targets,
		span_map<scan_item_idx_t, const ScanItemView> items)
	{
		db.open(db_path, "scanner.mdb");

		db.read_write_transaction();
		auto target_ids = db.get_target_ids(targets);
		auto item_file_ids = db.get_item_file_ids(item_root_path, items);
		db.remove_items(target_ids, items, item_file_ids); 
		db.commit_transaction();
		// note: the files are never removed, only the items, is that ok ?
		// todo: how about target ids ? should they ever get removed ?
	}
};

Scanner::Scanner() : impl(std::make_unique<ScannerImpl>()) {

}

Scanner::~Scanner() {}

std::string Scanner::scan(const ConfigView & cc)
{
	if (cc.item_set.items.empty())
		return "";

	ConfigView c = cc;
	auto& ci = c.item_set;
	if (c.tool_path.empty()) throw std::runtime_error("must provide a tool path");
	if (c.db_path.empty()) throw std::runtime_error("must provide a db path");
	if (c.int_dir.empty()) throw std::runtime_error("must provide an intermediate path");
	if (ci.targets.empty()) throw std::runtime_error("must provide at least one target");

	if (c.build_start_time == 0)
		c.build_start_time = file_time_t_now();

	if (c.module_visitor != nullptr && c.submit_previous_results == false)
		throw std::runtime_error("previous results are needed for the module visitor");

	auto ret = impl->scan(c.tool_type, c.tool_path, c.db_path, c.int_dir, ci.item_root_path,
		ci.commands_contain_item_path, ci.commands, ci.targets, ci.items,
		c.build_start_time, c.concurrent_targets, c.file_tracker_running,
		c.observer, c.submit_previous_results, c.module_visitor);

	return ret;
}

void Scanner::clean(const ConfigView & c) {
	auto& ci = c.item_set;
	if (ci.items.empty())
		return;

	if (c.db_path.empty()) throw std::runtime_error("must provide a db path");
	if (ci.targets.empty()) throw std::runtime_error("must provide at least one target");

	impl->clean(c.db_path, ci.item_root_path, ci.targets, ci.items);
}

ScanItemSet scan_item_set_from_comp_db(std::string_view comp_db_path, std::string_view item_root_path)
{
	TRACE();
	ScanItemSet item_set;
	item_set.item_root_path = item_root_path;
	item_set.targets = { "x" }; // todo: add an argument
	std::ifstream fin(comp_db_path);
	auto json_db = nlohmann::json::parse(fin);
	for (auto& json_item : json_db) {
		item_set.items.push_back({
			/*.path =*/ json_item["file"],
			/*.command_idx =*/ item_set.commands.size(),
			/*.target_idx =*/ item_set.targets.indices().front()
		});
		item_set.commands.push_back(json_item["command"]);
	}
	item_set.commands_contain_item_path = true;
	return item_set;
}

} // namespace cppm