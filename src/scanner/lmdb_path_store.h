#pragma once

#include "lmdb_wrapper.h"

#include "strong_id.h"

#include <filesystem>

#define USE_ABSL
#ifdef USE_ABSL
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <absl/container/flat_hash_map.h>
#else
#include <unordered_map>
#endif
#include "multi_buffer.h"

namespace mdb {

namespace fs = std::filesystem;

template <
	typename file_id_t,
	typename file_entry
>
struct path_store {
	// note: these paths are not canonical in the std::canonical sense
	// i.e they may contain "~" abbreviations and symlinks
	// but the separators are normalized and "."/".."s are removed
	stable_multi_string_buffer normal_paths;
	// multiple equivalent paths may be assigned the same id
#ifdef USE_ABSL
	absl::flat_hash_map<std::string_view, file_id_t> normal_path_to_id;
#else
	std::unordered_map<std::string_view, file_id_t> normal_path_to_id;
#endif
	vector_map<file_id_t, file_id_t> parents;
	vector_map<file_id_t, std::string_view> id_to_canonical_path;
	using ref_t = typename decltype(normal_paths)::ref_t;
	vector_map<file_id_t, ref_t> id_to_normal_path_ref = { {} }; // id = 0 is invalid

	std::string current_path;
	std::vector<std::size_t> current_path_separator_positions;
	file_id_t current_path_id = {};

	constexpr bool is_separator(char c) noexcept {
#ifdef _WIN32
		return c == '/' || c == '\\';
#else
		return c == '/';
#endif
	}

	constexpr static int max_num_components = 256;
	void update_current_path(std::string_view path = "") {
		if (path.empty()) {
			current_path = fs::current_path().string();
			assert(!current_path.empty());
		} else {
			current_path = (std::string)path;
		}

		if (is_relative(current_path))
			throw std::invalid_argument("the current path must be absolute");

		std::string_view normal_path = normalize(current_path);
		current_path = (std::string)normal_path;
		normal_paths.free_last_alloc(normal_path.size());

		current_path_separator_positions.clear();
		current_path_separator_positions.push_back(0);
		std::size_t poz = 0;
		for (char &c : current_path) {
			if (is_separator(c)) {
				current_path_separator_positions.push_back(poz + 1);
				c = preferred_separator;
			}
			poz++;
		}

		current_path_id = try_add(current_path);
	}

	constexpr static char preferred_separator = '/';

	bool is_relative(std::string_view path) {
		if (path.empty())
			return true;
		if (path[0] == '/')
			return false;
#ifdef _WIN32
		if (path.size() >= 2 && path[1] == ':')
			return false;
#endif
		return true;
	}

	// just allocate the buffer to be used later with multithreading
	tcb::span<char> init_buffer_for_path(std::string_view path) {
		std::size_t size = is_relative(path) ? path.size() : 
			current_path.size() + 1 + path.size();
		return { normal_paths.alloc(size), size };
	}

	// Return an allocated buffer to store the normalized path
	// and a pointer into that buffer (possibly after the absolute path)
	// where the normalized path should be written.
	// If path is a relative path then the current path will be written
	// before the normalized path. The component positions array and
	// the current component index will be updated accordingly.
	std::pair<char*, std::string_view> init_buffer_for_path(std::string_view path,
		char **components, int *p_current_component)
	{
		if (!is_relative(path)) {
			char* buf = normal_paths.alloc(path.size());
			char* ptr = buf;
			// the root slash should not be treated as an empty component
			if (path[0] == '/')
				*ptr++ = '/';
			return { ptr, { buf, path.size() } };
		}
		
		std::size_t size = current_path.size() + 1 + path.size();
		char* buf = normal_paths.alloc(size);
		memcpy(buf, current_path.data(), current_path.size());
		
		buf[current_path.size()] = preferred_separator;
		int& current_component = *p_current_component;
		for (std::size_t poz : current_path_separator_positions)
			components[current_component++] = &buf[poz];

		return { &buf[current_path.size() + 1], { buf, size } };
	}

	std::string_view normalize(std::string_view path) {
		char* components[max_num_components];
		int current_component = 0;

		auto [out, buf] = init_buffer_for_path(path, components, &current_component);

		const char* in = &path[0];
		const char* in_end = in + path.size();
		components[current_component] = out;
		std::size_t component_size = 0;
		// todo: windows network paths

		while (in != in_end) {
			if (is_separator(*in)) {
				char*& c = components[current_component];
				if (component_size == 2 && c[0] == '.' && c[1] == '.') {
					if (current_component == 0) // should we just ignore this ?
						throw std::invalid_argument(fmt::format("invalid path {}", path));
					out = components[--current_component];
				} else if (component_size == 1 && c[0] == '.') {
					out = components[current_component];
				} else if (component_size > 0) {
					++current_component;
					if (current_component == max_num_components)
						throw std::invalid_argument(fmt::format("path has too many components {}", path));
					*out++ = preferred_separator;
					components[current_component] = out;
				}
				component_size = 0;
				++in;
			} else {
			#ifdef _WIN32
				// note: ninja doesn't do this, so things should usually work without it
				// but e.g the scanner can return paths with a lower/upper case
				// drive letter depending on the environment variables
				// todo: maybe fix the scanner instead, or only toupper the drive letter ?
				// todo: unicode ?
				*out++ = toupper(*in++);
			#else
				*out++ = *in++;
			#endif
				++component_size;
			}
		}

		char*& c = components[current_component];
		if (component_size == 2 && c[0] == '.' && c[1] == '.') {
			if (current_component == 0) // should we just ignore this ?
				throw std::invalid_argument(fmt::format("invalid path {}", path));
			char* last_out = components[--current_component];
			if (last_out == buf.data())
				out -= 2; // "/.." -> "/" ;; "C:/.." -> "C:/"
			else
				out = last_out - 1; // "/x/y/.." -> "/x"
		} else if (component_size == 1 && c[0] == '.') {
			// the absolute path introduces at least one other component
			assert(components[current_component] != buf.data());
			out = components[current_component] - 1; // remove trailing /.
		} else if (component_size == 0) {
			--out; // remove trailing /
		}

		auto new_size = (std::size_t)(out - buf.data());
		normal_paths.shrink_last_alloc(buf.size() - new_size);
		return { buf.data(), new_size };
	}

	file_id_t db_max_id = {};
	file_id_t next_id = db_max_id + 1;

	file_id_t try_add(std::string_view path) {
		if (path.empty()) // todo: assume false
			return current_path_id;

		if (path.find("~") != std::string_view::npos)
			throw std::invalid_argument(fmt::format("paths with ~ are not supported: {}", path));

		std::string_view normal_path = normalize(path);
		
		auto [itr, inserted] = normal_path_to_id.try_emplace(normal_path, next_id);
		if (inserted) {
			id_to_normal_path_ref.push_back(normal_paths.get_last_alloc_reference(normal_path));
			++next_id;
		} else {
			// todo: add an RAII class for this
			normal_paths.free_last_alloc(normal_path.size());
		}
		return itr->second;
	}

	auto open_paths_db(mdb::mdb_txn<false>& txn_rw) {
		return txn_rw.open_db<uint32_t, std::string_view>("paths");
	}

	void commit_path_changes(mdb::mdb_txn<false>& txn_rw) {
		auto db = open_paths_db(txn_rw);

		if (id_to_normal_path_ref.empty())
			return;

		assert(next_id >= db_max_id + 1);
		if (next_id == db_max_id + 1) // no changes
			return;

		db.put(0, { (char*)id_to_normal_path_ref.data(),
			(std::size_t)id_to_normal_path_ref.size() * sizeof(ref_t) });

		// todo: only write the chunks where there were changes
		uint32_t idx = 1, last_idx = (uint32_t)normal_paths.get_buffers().size();
		for(auto &buffer : normal_paths.get_buffers()) {
			if (idx != last_idx)
				db.put(idx, { buffer.data(), buffer.size() });
			else
				db.put(idx, { buffer.data(), normal_paths.get_allocated_in_current_buffer() });
			++idx;
		}
	}

	void read_paths(mdb::mdb_txn<false>& txn_rw, std::string_view item_root_path) {
		auto db = open_paths_db(txn_rw);

		normal_paths.clear();
		id_to_normal_path_ref.clear();
		id_to_normal_path_ref.push_back({}); // id = 0 is invalid
		db_max_id.invalidate();
		normal_path_to_id.clear();

		for (auto [idx, value] : db) {
			if (idx == 0) {
				id_to_normal_path_ref.resize(file_id_t { value.size() / sizeof(ref_t) });
				memcpy(id_to_normal_path_ref.data(), value.data(), value.size());
			} else {
				normal_paths.copy(value);
			}
		}
		if (!id_to_normal_path_ref.empty()) {
			db_max_id = id_to_normal_path_ref.size() - 1;
			next_id = db_max_id + 1;
		}

		for (auto id = file_id_t { 1 }; id < id_to_normal_path_ref.size(); ++id) {
			auto path = normal_paths.get_alloc(id_to_normal_path_ref[id]);
			normal_path_to_id[path] = id;
		}

		update_current_path(item_root_path);
	}

	template<typename path_idx_t>
	auto get_file_ids(mdb::mdb_txn<false>& txn_rw, std::string_view item_root_path,
		const vector_map<path_idx_t, std::string_view>& paths) {
		vector_map<path_idx_t, file_id_t> file_ids;
		file_ids.resize(paths.size());
		read_paths(txn_rw, item_root_path);
		// todo: use multiple threads here to do the normalize/hash the paths
		for (auto idx : paths.indices())
			file_ids[idx] = try_add(paths[idx]);
		return file_ids;
	}

	std::string_view get_file_path(file_id_t file_id) {
		return normal_paths.get_alloc(id_to_normal_path_ref[file_id]);
	}

	auto open_file_data_db(mdb::mdb_txn<false>& txn_rw) {
		return txn_rw.open_db<file_id_t, file_entry>("file_data");
	}

	// note: this doesn't work if called after update_file_data
	template<typename idx_t, typename F>
	void get_file_data(mdb::mdb_txn<false>& txn_rw, const vector_map<idx_t, file_id_t>& files, F&& data_func) {
		auto db = open_file_data_db(txn_rw);

		for (auto idx : files.indices()) {
			auto file_id = files[idx];
			if (file_id > db_max_id) // there's no data to fetch for new entires
				continue;
			// update_file_data is not called for e.g new headers found while scanning
			try { // todo: don't use exceptions for this
				data_func(idx, db.get(file_id));
			} catch(mdb::key_not_found_exception &) {}
		}
	}

	std::vector<std::pair<file_id_t, file_entry>> new_file_data;

	// note: this should only be called once
	template<typename idx_t, typename F>
	void update_file_data(const span_map<idx_t, file_id_t> files, F&& data_func) {
		new_file_data.reserve((std::size_t)next_id);
		for (auto idx : files.indices()) {
			auto file_id = files[idx];
			new_file_data.push_back({ file_id, data_func(idx) });
		}
	}

	void commit_file_data_changes(mdb::mdb_txn<false>& txn_rw) {
		auto db = open_file_data_db(txn_rw);

		for (auto& [file_id, entry] : new_file_data)
			db.put(file_id, entry);
	}

	void commit_changes(mdb::mdb_txn<false>& txn_rw) {
		commit_path_changes(txn_rw);
		commit_file_data_changes(txn_rw);
	}

	void print_paths(mdb::mdb_txn<false>& txn_rw) {
		read_paths(txn_rw, "");
		for (auto id = file_id_t { 1 }; id < id_to_normal_path_ref.size(); ++id) {
			auto path = normal_paths.get_alloc(id_to_normal_path_ref[id]);
			fmt::print("{} - {}\n", id, path);
		}
	}

	void print_file_data(mdb::mdb_txn<false>& txn_rw) {
		auto db = open_file_data_db(txn_rw);
		// todo:
	}

	// todo: this should work with a read-only txn as well
	void print(mdb::mdb_txn<false>& txn_rw) {
		print_paths(txn_rw);
		print_file_data(txn_rw);
	}
};

} // namespace mdb