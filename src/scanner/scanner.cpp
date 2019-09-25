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
#include "strong_id.h"
#include "multi_buffer.h"

namespace cppm {

#if 0
using item_id_t = int;
using file_id_t = int;
using scan_item_idx_t = int;
using log_file_id_t = int;
#else
//DECL_STRONG_ID(item_id_t)
DECL_STRONG_ID_INV(file_id_t, 0) // 0 is the invalid file id
DECL_STRONG_ID(scan_item_idx_t)
DECL_STRONG_ID(unique_deps_idx_t)
DECL_STRONG_ID(to_stat_idx_t)
DECL_STRONG_ID(file_table_idx_t)
DECL_STRONG_ID_INV(db_target_id, 0)
#endif
using cmd_hash_t = std::size_t;
using item_id_t = std::pair<file_id_t, db_target_id>;

std::string concat_u8_path(std::string_view path, std::string_view filename) {
	return (std::filesystem::u8path(path) / filename).string();
}

enum class ood_state {
	unknown,
	command_changed,
	new_file,
	file_changed,
	deps_changed,
	up_to_date
};

struct DB {
	mdb::mdb_env env;
	mdb::mdb_txn_rw txn_rw;

	void open(std::string_view db_path, std::string_view db_file_name) {
		using namespace mdb::flags;
		//env.set_map_size(...) ?
		env.set_maxdbs(5);
		env.open(concat_u8_path(db_path, db_file_name).c_str(), env::nosubdir);
	}

	struct directory_entry {};
	struct file_entry {
		file_time_t last_write_time;
		file_time_t last_stat_time;
	};

	mdb::path_store<file_id_t, directory_entry, file_entry> path_store;

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
		auto last_stat_time = file_time_t_now();

		path_store.update_file_data(files_to_stat, [&](to_stat_idx_t idx) {
			return file_entry { lwts[files_to_stat[idx]], last_stat_time };
		});

		path_store.commit_changes(txn_rw);
	}

	template<typename size_type, typename... Vs>
	static void resize_all_to(size_type size, Vs&&... vs) {
		(vs.resize(size), ...);
	}

	struct item_entry {
		cmd_hash_t cmd_hash;
		file_time_t last_successful_scan;
		tcb::span<file_id_t> file_deps;
		tcb::span<item_id_t> item_deps;
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

		void resize(scan_item_idx_t size) {
			DB::resize_all_to(size, file_id, cmd_hash, last_successful_scan, file_deps, item_deps);
		}
	};

	auto get_target_ids(span_map<target_idx_t, std::string_view> targets, bool add_if_not_found) {
		vector_map<target_idx_t, db_target_id> target_ids;
		target_ids.resize(targets.size());

		auto db = txn_rw.open_db<db_target_id, std::string_view>("targets", mdb::flags::open_db::integer_keys);
		static_assert(std::is_constructible_v<uint32_t, db_target_id>, "not convertible");
#if 0
		std::unordered_map<std::string_view, db_target_id> map;
		auto db_max_target_id = db.get_max_key(); // 0 if the DB empty
		map.reserve((std::size_t)(db_max_target_id));
		for (auto&& [id, target] : db)
			map[target] = id;

		auto max_target_id = db_max_target_id;
		for (auto i : targets.indices()) {
			if (auto itr = map.find(targets[i]); itr != map.end()) {
				target_ids[i] = itr->second;
			} else {
				target_ids[i] = ++max_target_id; // first id = 1
			}
		}

		// put invalidates the string views in the map, so do this at the end
		for (auto i : targets.indices())
			if(target_ids[i] > db_max_target_id)
				db.put(target_ids[i], targets[i]);
#else
		std::unordered_map<std::string_view, target_idx_t> map;
		map.reserve((std::size_t)targets.size());
		for (auto i : targets.indices())
			map[targets[i]] = i;

		db_target_id max_target_id = {};
		for (auto&& [id, target] : db) {
			if (auto itr = map.find(target); itr != map.end())
				target_ids[itr->second] = id;
			if (id > max_target_id) max_target_id = id;
		}

		if (add_if_not_found) {
			for (auto i : targets.indices())
				if (!target_ids[i].is_valid())
					db.put((target_ids[i] = ++max_target_id), targets[i]);
		}
#endif

		return target_ids;
	}

	auto get_item_data(span_map<target_idx_t, const db_target_id> target_ids, std::string_view item_root_path,
		span_map<scan_item_idx_t, const ScanItemView> items) 
	{
		item_data data;
		data.file_id = get_item_file_ids(item_root_path, items);
		data.db_max_file_id = path_store.db_max_file_id;
		data.max_file_id = path_store.max_file_id;

		data.resize(items.size()); // todo: can we not allocate all this if the DB is empty ?

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
			} catch (mdb::key_not_found_exception&) {
				// if e.g the scanner was interrupted/crashed then
				// the file may be in the DB but not the item, ignore this
			}
		}
		return data;
	}

	void update_items(
		const vector_map<scan_item_idx_t, ood_state>& item_ood,
		span_map<target_idx_t, const db_target_id> target_ids,
		span_map<scan_item_idx_t, const ScanItemView> items,
		const vector_map<scan_item_idx_t, file_id_t>& item_file_ids,
		const vector_map<cmd_idx_t, cmd_hash_t>& cmd_hashes,
		std::string_view item_root_path,
		const vector_map<file_table_idx_t, std::string_view>& dep_file_table,
		const vector_map<scan_item_idx_t, bool>& got_result,
		const vector_map<scan_item_idx_t, tcb::span<file_table_idx_t>>& file_deps,
		const vector_map<scan_item_idx_t, tcb::span<file_table_idx_t>>& item_deps)
	{
		auto dep_file_ids = path_store.get_file_ids(txn_rw, item_root_path, dep_file_table);
		path_store.commit_changes(txn_rw);

		// todo: reordering not needed here, a multi_vector_buffer should suffice
		reordered_multi_vector_buffer<scan_item_idx_t, file_id_t> file_deps_buffer;
		reordered_multi_vector_buffer<scan_item_idx_t, item_id_t> item_deps_buffer;

		for (auto i : file_deps.indices()) {
			file_deps_buffer.new_vector(i);
			for(auto file_table_idx : file_deps[i])
				file_deps_buffer.add(dep_file_ids[file_table_idx]);
		}

		for (auto i : item_deps.indices()) {
			item_deps_buffer.new_vector(i);
			// todo: target ?
			/*for (auto file_table_idx : item_deps[i])
				item_deps_buffer.add(dep_file_ids[file_table_idx]);*/
		}

		auto file_deps_view = file_deps_buffer.to_vectors();
		auto item_deps_view = item_deps_buffer.to_vectors();

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
				file_deps_view[i],
				item_deps_view[i]
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
		file_data data;
		data.resize(files.size());
		path_store.get_file_data(files, [&](unique_deps_idx_t idx, const file_entry& f) {
			data.last_write_time[idx] = f.last_write_time;
			data.last_stat_time[idx] = f.last_stat_time;
		});
		return data;
	}

	void get_file_paths(std::string_view item_root_path,
		span_map<to_stat_idx_t, const file_id_t> deps_to_stat, 
		/*inout:*/ vector_map<file_id_t, std::string_view>& file_paths)
	{
		// todo: parallelize this ?
		for (auto i = to_stat_idx_t { 0 }; i < deps_to_stat.size(); ++i) {
			if (file_paths[deps_to_stat[i]].empty())
				file_paths[deps_to_stat[i]] = path_store.get_file_path(deps_to_stat[i]);
		}
	}

	struct db_header {
		constexpr static int current_version = 1;
		int version = current_version;
	};

	void read_write_transaction() {
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
		txn_rw.commit();
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
		auto path = std::filesystem::u8path(file_path); // todo: proper UTF-8
		if (root_path != "" && !path.is_absolute())
			path = std::filesystem::u8path(root_path) / path;
		return path;
	}

	auto get_unique_deps(const vector_map<scan_item_idx_t, tcb::span<file_id_t>>& all_file_deps,
		const vector_map<scan_item_idx_t, file_id_t>& all_item_file_ids, file_id_t max_file_id)
	{
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
		span_map<to_stat_idx_t, const file_id_t> deps_to_stat, file_id_t max_file_id)
	{
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
		// todo: generate an optimal stat plan:
		// - if more than # files in same directory - stat the directory instead (can we do % of nr files in dir ?)
		// - try to stat the whole dir, 

		// resize to max file id
		//std::filesystem::current_path(std::filesystem::u8path(item_root_path));
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

	bool generate_compilation_database(
		bool commands_contain_item_path,
		span_map<cmd_idx_t, std::string_view> commands,
		std::string_view item_root_path,
		span_map<scan_item_idx_t, const ScanItemView> items,
		const vector_map<scan_item_idx_t, ood_state>& item_ood,
		const std::filesystem::path& comp_db_path)
	{
		auto json_array = nlohmann::json();
		for (auto i : item_ood.indices()) {
			if (item_ood[i] != ood_state::up_to_date) {
				auto json = nlohmann::json();
				json["directory"] = nullptr;
				auto path = get_rooted_path(item_root_path, items[i].path).string();
				json["file"] = path;
				std::string cmd = (std::string)commands[items[i].command_idx];
				if (!commands_contain_item_path) cmd += fmt::format(" \"{}\"", path);
				json["command"] = std::move(cmd);
				json_array.push_back(json);
			}
		}

		if (json_array.empty())
			return false;

		std::ofstream db_out(comp_db_path);
		if (!db_out)
			throw std::runtime_error("failed to open compilation database");
		db_out << json_array.dump();
		db_out.close();
		return true;
	}

	// note: all of the input paths are required to be normalized already
	std::string scan(Scanner::Type tool_type, std::string_view tool_path, 
		std::string_view db_path, std::string_view int_dir, std::string_view item_root_path,
		bool commands_contain_item_path, span_map<cmd_idx_t, std::string_view> commands,
		span_map<target_idx_t, std::string_view> targets, 
		span_map<scan_item_idx_t, const ScanItemView> items,
		file_time_t build_start_time, bool concurrent_targets, bool file_tracker_running,
		DepInfoObserver * observer)
	{
		std::string ret;

		// todo: we may not need to recompute some of this if we can detect that the environment stays constant
		// todo: do this while reading data from the DB, use an eager future
		auto cmd_hashes = get_cmd_hashes(commands);

		// todo: estimate db size if it doesn't use a sparse file ?

		db.open(db_path, "scanner.mdb");

		db.read_write_transaction();
		auto target_ids = db.get_target_ids(targets, true);
		auto item_data = db.get_item_data(target_ids, item_root_path, items); // returns new file/item ids for files/items not in the db yet
		// todo: if concurrent_targets == false, it should be more efficient to assume all files are deps ?
		auto unique_deps = get_unique_deps(item_data.file_deps, item_data.file_id, item_data.max_file_id);
		auto file_data = db.get_file_data(unique_deps);
		auto [real_lwt, deps_to_stat] = remove_deps_already_stated(unique_deps,
			file_data.last_write_time, file_data.last_stat_time, build_start_time, file_tracker_running, item_data.max_file_id);
		auto file_paths = get_file_paths(item_root_path, items, item_data.file_id, deps_to_stat, item_data.max_file_id);
		// todo: if the log is empty then we can stat items and compute hashes in parallel with scanning
		get_file_ood(item_root_path, deps_to_stat, file_paths, /*inout: */real_lwt);
		auto item_ood = get_item_ood(items, item_data, cmd_hashes, real_lwt, /*just for logging*/file_paths); // maybe do the cmd_hashes check later, close txn faster ?
		// note: the deps in item_data becomes invalid once we start writing to the db
		db.update_file_last_write_times(deps_to_stat, real_lwt); // also adds new files
		// changes to:
		// files:
		// - new last write times / last stat time
		// - new files from items
		db.commit_transaction();

		multi_string_buffer<file_table_idx_t> file_table_buf;
		reordered_multi_vector_buffer<scan_item_idx_t, file_table_idx_t> file_deps_buf, item_deps_buf;
		vector_map<scan_item_idx_t, bool> got_result;
		got_result.resize(items.size());

		// todo: load/store the minimized source files for clang-scan-deps from the DB
		// todo: would this be faster with a named pipe ? or sending directly to stdin ?
		auto comp_db_path = concat_u8_path(int_dir, "pp_commands.json");
		bool got_ood_items = generate_compilation_database(commands_contain_item_path, commands, 
			item_root_path, items, item_ood, comp_db_path);

		if (got_ood_items) {
			// code page 65001 is UTF-8, use that for the paths
			std::string cmd = fmt::format("chcp 65001 & \"{}\" --compilation-database=\"{}\"", tool_path, comp_db_path);
			FILE* cmd_out = _popen(cmd.c_str(), "r");
			if (!cmd_out)
				throw std::runtime_error("failed to execute scanner tool");

#if 0
			auto json_depformat = nlohmann::json::parse(cmd_out);
			fclose(cmd_out);
#endif

			std::vector<file_table_idx_t> file_table_indexed_lookup;
			std::unordered_map<std::string, file_table_idx_t> file_table_lookup;

			auto get_file_table_idx = [&](const std::string& str) {
				auto [itr, inserted] = file_table_lookup.try_emplace(str, file_table_idx_t {});
				if (inserted)
					itr->second = file_table_buf.add(str);
				return itr->second;
			};

#if 0
			auto get_file_table_idx_2 = [&](std::size_t idx, const std::string& str) {
				auto ft_idx = get_file_table_idx(str);
				if (file_table_indexed_lookup.size() < idx + 1)
					file_table_indexed_lookup.resize(idx + 1); // todo: reserve ?
				file_table_indexed_lookup[idx] = ft_idx;
				return ft_idx;
			};

			auto get_file_table_idx_3 = [&](std::size_t idx) {
				return file_table_indexed_lookup[idx];
			};
#endif

#if 0
			for (auto& json_depinfo : json_depformat["sources"]) {
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
			}
#else
			std::unordered_map<std::string, scan_item_idx_t> item_lookup;
			for (auto i : items.indices()) {
				// note: this doesn't work if there are multiple items with the same file path
				item_lookup[get_rooted_path(item_root_path, items[i].path).string()] = i;
			}
			constexpr int buf_size = 2 * 1024 * 1024;
			std::string line;
			line.resize(buf_size);
			auto starts_with = [](std::string_view a, std::string_view b) {
				return (a.substr(0, b.size()) == b);
			};
			auto subview = [](const std::string& str, std::size_t ofs) {
				std::string_view ret { str };
				ret.remove_prefix(ofs);
				return ret;
			};
			int nr_lines = 0;
			std::string current_file;
			while (fgets(&line[0], buf_size, cmd_out) != NULL) {
				// skip the first line: active code page ..
				if (nr_lines++ == 0) continue;
				line.resize(line.find_first_of("\n\r"));
				//fmt::print("{}\n", line);
				if (starts_with(line, ":::: ")) {
					if (current_file != "") observer->item_finished();
					current_file = line.substr(5);
					auto itr = item_lookup.find(current_file);
					if (itr == item_lookup.end())
						throw std::runtime_error("unknown scan item");
					scan_item_idx_t item_idx = itr->second;
					file_deps_buf.new_vector(item_idx);
					item_deps_buf.new_vector(item_idx);
					got_result[item_idx] = true;
					observer->results_for_item((std::size_t)item_idx);
				} else if (starts_with(line, ":exp ")) {
					observer->export_module(subview(line, 5));
				} else if (starts_with(line, ":imp ")) {
					observer->import_module(subview(line, 5));
					// todo: import header unit ?
				} else {
					if (line != "" && line != current_file) {
						file_deps_buf.add(get_file_table_idx(line));
						// todo: header or other deps ? check extension ? :-/
						observer->include_header(line);
						//observer->other_file_dep();
					}
				}
				line.resize(buf_size);
			}
			if (current_file != "") observer->item_finished();
			fclose(cmd_out);
#endif
		}

		// todo: add an option to do the DB operations while the scanner is still running
		// todo: reestime db size ?
		db.read_write_transaction();
		db.update_items(item_ood, target_ids, items, item_data.file_id, cmd_hashes, 
			item_root_path, file_table_buf.to_vector(), got_result, 
			file_deps_buf.to_vectors(), item_deps_buf.to_vectors());
		// changes to:
		// files:
		// - new files from deps (added with last_write_time/last_stat_time = 0)
		// items:
		// - new cmd hashes
		// - last_successful_scan = now ? todo: errors ?
		// - deps changed
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
		auto target_ids = db.get_target_ids(targets, false);
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

std::string Scanner::scan(const Config & cc)
{
	if (cc.items.empty())
		return "";

	auto start = std::chrono::high_resolution_clock::now();

	Config c = cc;
	if (c.tool_path.empty()) throw std::runtime_error("must provide a tool path");
	if (c.db_path.empty()) throw std::runtime_error("must provide a db path");
	if (c.int_dir.empty()) throw std::runtime_error("must provide an intermediate path");
	if (c.targets.empty()) throw std::runtime_error("must provide at least one target");

	DepInfoObserver dummy;
	if (c.observer == nullptr)
		c.observer = &dummy;
	if (c.build_start_time == 0)
		c.build_start_time = file_time_t_now();

	auto ret = impl->scan(c.tool_type, c.tool_path, c.db_path, c.int_dir, c.item_root_path, 
		c.commands_contain_item_path, c.commands, c.targets, to_span_map<scan_item_idx_t>(c.items),
		c.build_start_time, c.concurrent_targets, c.file_tracker_running,
		c.observer);

	auto end = std::chrono::high_resolution_clock::now();
	fmt::print("elapsed: {} ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
	return ret;
}

void Scanner::clean(const Config & c) {
	if (c.items.empty())
		return;

	if (c.db_path.empty()) throw std::runtime_error("must provide a db path");
	if (c.targets.empty()) throw std::runtime_error("must provide at least one target");

	impl->clean(c.db_path, c.item_root_path, c.targets, to_span_map<scan_item_idx_t>(c.items));
}

} // namespace cppm