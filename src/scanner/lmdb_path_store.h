#pragma once

#include "lmdb_wrapper.h"

#include "strong_id.h"

//#include <absl/container/flat_hash_map.h>

namespace mdb {

template<
	typename file_id_t,
	typename directory_entry,
	typename file_entry
>
struct path_store {
#if 0
	// based on https://www.openldap.org/lists/openldap-technical/201504/msg00195.html
	// and https://stackoverflow.com/questions/29800538/implementing-persistent-associative-array-on-top-of-lmdb

	struct path_element_view {
		file_id_t id;
		file_id_t parent_id;
		std::string_view name;
		bool is_file;
	};

	using key_t = path_element_view;

	//using value_t = std::variant < directory_entry, file_entry >;
	using value_t = std::string_view;
#endif

	using key_t = file_id_t;

	using data_t = std::variant < directory_entry, file_entry >;

	struct value_t {
		file_id_t parent_id;
		std::string_view name;
		data_t data; // todo: store the minimum amount for this
	};

	struct entry_t {
		key_t key;
		value_t value;
		enum status_t : char {
			unchanged = 0,
			new_entry = 1,
			updated = 2,
		};
		status_t status;
	};
	vector_map<file_id_t, entry_t> all_entries;
	vector_map<file_id_t, std::string> db_paths;

	file_id_t db_max_file_id = {};
	file_id_t max_file_id = {};

	std::list<std::string> new_elements;

	char get_preferred_separator() {
		// todo: maybe use std::filesystem::path::preferred_separator and convert it to char ?
		return '\\';
	}

	template<typename path_idx_t>
	auto get_file_ids(mdb::mdb_txn<false> & txn_rw, std::string_view item_root_path, 
		const vector_map<path_idx_t, std::string_view> & paths)
	{
		vector_map<path_idx_t, file_id_t> file_ids;
		file_ids.resize(paths.size());

		auto p_item_root_path = std::filesystem::canonical(item_root_path);
		char separator = get_preferred_separator();

		// if the a daemon is running to keep things in memory then it should be faster to persist a hash table
		// but otherwise if the file cache is cold then it should be faster to load a compressed file tree

		auto db = txn_rw.open_db<key_t, value_t>("paths", mdb::flags::open_db::integer_keys); // todo: automatically detect integer_keys

#if 0
		std::vector<key_t> path_keys;
		struct path_entry {
			key_t key;
			value_t value;
			std::size_t hash;
		};
		std::vector<path_entry> path_entries;

		std::vector<std::size_t> path_hashes;

		using lookup_key_t = std::size_t;
		struct hasher : public std::hash<lookup_key_t> {
			hasher(int x) {}
		};

		struct equality : public std::equal_to<lookup_key_t> {
			equality(int x) {}
		};

		using lookup_value_t = std::size_t;

		struct path_reference {
			std::size_t idx;
			std::vector<key_t>& path_keys;
			std::size_t hash;
		};

		std::unordered_map<std::size_t, unsigned int, hasher, equality> file_lookup { 0, hasher(2), equality(2) };
		std::vector<file_entry> file_entries;
#endif

		std::unordered_map<std::string, file_id_t> path_lookup;
		// todo: compute item hashes in parallel with this

		// todo: when this is called again to look up item deps,
		// don't read everything again from the DB, just any new files
		// that might've been added by other processes while scanning
		bool first = true;
		file_id_t max_path_id = {};
		for (auto&& [key, val] : db) {
			static_assert(std::is_same_v<decltype(key), file_id_t&>, "key must be a copy of path_element_view");
			//static_assert(std::is_same_v<decltype(key), path_element_view>, "key must be a copy of path_element_view");
			//static_assert(std::is_same_v<decltype(val), value_t&>, "val must be an l-value ref to the data");
			static_assert(std::is_same_v<decltype(val), value_t>, "val must be a copy of value_t");

			if (first) {
				max_path_id = val.parent_id;
				db_paths.resize(max_path_id + 1);
				all_entries.reserve(max_path_id + 1 + (uint32_t)(paths.size()));
				all_entries.resize(max_path_id + 1);
				first = false;
				continue;
			}
#if 0
			std::hash<std::string_view> hash_func;
			if (path_hashes.size() <= key) path_hashes.resize(key + 1); // todo: avoid reallocations
			path_hashes[key] = path_hashes[val.parent_id] ^ hash_func(key.name);
			// insert into file_lookup with a hasher that just returns the precomputed hash
			// and an equality that is always false because we know the key is not already in the map
			//path_lookup[ path_hashes[key] ] = key;
#endif
			if (!val.parent_id.is_valid()) {
				db_paths[key] = val.name;
			} else {
				db_paths[key] = db_paths[val.parent_id] + separator;
				db_paths[key] += val.name;
			}
			path_lookup[db_paths[key]] = key;

			all_entries[key] = { key, val, entry_t::unchanged };
		}

		if (first) {
			all_entries.reserve(id_cast<file_id_t>(paths.size()));
			all_entries.push_back({}); // id=0 is reserved
		}

		db_max_file_id = max_path_id + 1;

		std::vector<std::size_t> separator_positions;

		std::filesystem::path p;
		for (auto i : paths.indices()) {
			p = paths[i];
			if (p.is_relative()) {
				// assume that relative paths are already canonical
				p = p_item_root_path;
				p /= paths[i];
				p.make_preferred();
			} else {
				// this is an expensive call (I/O):
				// todo: hash every prefix of the path in non-canonical form
				// (allow multiple entries in the hash map to point to the same path id)
				// so that hopefully next time we can avoid this call 
				p = std::filesystem::canonical(paths[i]);
			}
			auto p_str = p.string(); // todo: remove this allocation
			auto p_view = std::string_view { p_str };

			if (auto itr = path_lookup.find(p_str); itr != path_lookup.end()) {
				file_ids[i] = itr->second;
			} else {
				// find path separator positions
				std::size_t ofs = 0;
				separator_positions.clear();
				while (true) {
					ofs = p_view.find_first_of("/\\", ofs);
					if (ofs == std::string_view::npos)
						break;
					separator_positions.push_back(ofs);
					ofs++; // skip the separator
				}

				// find the offset of the first prefix of path that's not already in the DB
				// todo: this can be done with binary search ?
				file_id_t parent_id = {};
				std::size_t idx = 0;
				for (auto pos : separator_positions) {
					auto prefix = p_str.substr(0, pos); // todo: optimize the lookup
					auto itr = path_lookup.find(prefix);
					if (itr == path_lookup.end())
						break;
					parent_id = itr->second;
					idx++;
				}

				// queue new path entries to be inserted into the DB later
				while (true) {
					bool at_start = (idx == 0);
					bool at_end = (idx == separator_positions.size());
					std::size_t ofs = at_end ? p_str.size() : separator_positions[idx];
					std::size_t last_ofs = at_start ? 0 : separator_positions[idx - 1] + 1;

					auto tmp_element = std::string_view { &p_view[last_ofs], ofs - last_ofs };
					// todo: optimize this storage
					std::string_view element = new_elements.emplace_back((std::string)tmp_element);

					file_id_t new_id = ++max_path_id;

					data_t data = directory_entry {};
					if (at_end) data = file_entry {};

					all_entries.push_back({ new_id, value_t { parent_id, element, data }, entry_t::new_entry});

					auto prefix = p_str.substr(0, ofs); // todo: optimize the lookup
					path_lookup[prefix] = new_id;

					if (at_end)	break;
					parent_id = new_id;
					++idx;
				}

				file_ids[i] = max_path_id;
			}
		}

		max_file_id = max_path_id + 1;

		return file_ids;
	}

	template<typename idx_t, typename F>
	void get_file_data(const vector_map<idx_t, file_id_t>& files, F&& data_func) {
		for (auto idx : files.indices()) {
			auto file_id = files[idx];
			if (file_id >= all_entries.size())
				throw std::invalid_argument("bad file id");
			if (file_id >= db_max_file_id) // no data for new files
				continue;
			auto& entry = all_entries[file_id];
			if (auto f_entry = std::get_if<file_entry>(&entry.value.data))
				data_func(idx, *f_entry);
		}
	}

	std::string_view get_file_path(file_id_t file_id) {
		if(file_id > all_entries.size()) 
			return "";
		return db_paths[file_id];
#if 0
		auto path_id = file_id;
		while (path_id.is_valid()) {
			path_id = all_entries[path_id].value.parent_id;
		}
#endif
	}

	template<typename idx_t, typename F>
	void update_file_data(const span_map<idx_t, file_id_t> files, F&& data_func) {
		for (auto idx : files.indices()) {
			auto file_id = files[idx];
			if (file_id >= all_entries.size())
				throw std::invalid_argument("bad file id");
			auto& entry = all_entries[file_id];
			if (auto f_entry = std::get_if<file_entry>(&entry.value.data)) {
				// todo: don't update the DB if it's the same value ?
				*f_entry = data_func(idx);
				if (entry.status != entry_t::new_entry)
					entry.status = entry_t::updated;
			}
		}
	}

	void commit_changes(mdb::mdb_txn<false> & txn_rw) {
		if (all_entries.empty())
			return;

		auto db = txn_rw.open_db<key_t, value_t>("paths", mdb::flags::open_db::integer_keys); // todo: integer_keys

		db.put(key_t {}, value_t { all_entries.size() - 1, "", {} });

		for (auto& entry : all_entries) {
			if (entry.status == entry_t::unchanged)
				continue;
			auto flags = mdb::flags::put::none;
			if (entry.status == entry_t::new_entry)
				flags = mdb::flags::put::append;
			db.put(entry.key, entry.value, flags);
			entry.status = entry_t::unchanged;
		}
	}
};

} // namespace mdb