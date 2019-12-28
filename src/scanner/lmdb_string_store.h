#pragma once

#include "lmdb_wrapper.h"

#include "strong_id.h"
#include <unordered_map>

namespace mdb {

template <
	typename id_t
>
struct string_id_store
{
	const char* db_name = nullptr;
	std::unordered_map<std::string_view, id_t> map;
	vector_map<id_t, std::string_view> reverse_map;
	id_t db_max_id = {}; // the max id that is already present in the DB
	id_t next_id = db_max_id + 1; // the next id that will be used when inserting new strings
	bool is_initialized = false;

	string_id_store(const char* db_name) : db_name(db_name) {}

	auto open_db(mdb::mdb_txn<false>& txn_rw) {
		return txn_rw.open_db<id_t, std::string_view>(db_name, mdb::flags::open_db::integer_keys);
	}

	void init(mdb::mdb_txn<false>& txn_rw, id_t expected_size) {
		if (is_initialized)
			return;

		auto db = open_db(txn_rw);

		// todo: maybe persist the hash map into the DB ? 

		bool first = true;
		for (auto&& [id, str] : db) {
			if (first) {
				// note: db.get_max_key() might require a disk seek,
				// so instead store the max key in the first element
				if (str.size() != sizeof(id_t))
					throw std::runtime_error("db format error");
				memcpy(&db_max_id, str.data(), sizeof(id_t));
				next_id = db_max_id + 1;
				id_t size_to_reserve = expected_size + db_max_id + 1;
				map.reserve((std::size_t)size_to_reserve);
				reverse_map.reserve(size_to_reserve);
				first = false;
			}
			map[str] = id;
			reverse_map.resize(id + 1);
			reverse_map[id] = str;
		}
		is_initialized = true;
	}

	template<typename idx_t>
	auto get_ids(mdb::mdb_txn<false>& txn_rw, span_map<idx_t, std::string_view> strings)
	{
		vector_map<idx_t, id_t> ids;
		ids.resize(strings.size());

		init(txn_rw, id_cast<id_t>(strings.size()));

		for (auto i : strings.indices())
			ids[i] = try_add(strings[i]);

		return ids;
	}

	// note: the input string must be valid until the changes are committed later
	id_t try_add(std::string_view str) {
		auto [itr, inserted] = map.try_emplace(str, next_id);
		if (inserted) {
			reverse_map.resize(next_id + 1);
			reverse_map[next_id] = str;
			++next_id;
		}
		return itr->second;
	}

	std::string_view get(id_t id) {
		return reverse_map[id];
	}

	const vector_map<id_t, std::string_view>& get_all_strings(mdb::mdb_txn<false>& txn_rw) {
		init(txn_rw, {});
		return reverse_map;
	}

	void commit_changes(mdb::mdb_txn<false>& txn_rw) {
		// nothing to do if there were no changes
		if (next_id == db_max_id + 1)
			return;

		auto db = open_db(txn_rw);

		char buf[sizeof(id_t)];
		id_t new_max_id = next_id - 1;
		memcpy(buf, &new_max_id, sizeof(id_t));
		db.put({}, std::string_view { buf, sizeof(id_t) });

		// note: put invalidates the strings that were read from the DB
		// but this only uses the new strings so it's fine
		for (auto id : reverse_map.indices()) {
			auto str = reverse_map[id];
			if(id > db_max_id)
				db.put(id, str, mdb::flags::put::append);
		}
	}

	// todo: this should work with a read-only txn as well
	void print(mdb::mdb_txn<false>& txn_rw) {
		auto db = open_db(txn_rw);
		bool first = true;
		for (auto&& [id, str] : db) {
			if (first) { first = false; continue; }
			fmt::print("{} - {}\n", id, str);
		}
	}
};

} // namespace mdb