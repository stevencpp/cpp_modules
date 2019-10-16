#pragma once

#include "lmdb_wrapper.h"

namespace mdb {

template <
	typename id_t
>
struct string_id_store
{
	const char* db_name = nullptr;
	string_id_store(const char* db_name) : db_name(db_name) {}

	template<typename idx_t>
	auto get_ids(mdb::mdb_txn<false>& txn_rw, span_map<idx_t, std::string_view> strings, bool add_if_not_found)
	{
		vector_map<idx_t, id_t> ids;
		ids.resize(strings.size());

		auto db = txn_rw.open_db<id_t, std::string_view>(db_name, mdb::flags::open_db::integer_keys);
		static_assert(std::is_constructible_v<uint32_t, id_t>, "not convertible");
#if 0
		std::unordered_map<std::string_view, id_t> map;
		auto db_max_id = db.get_max_key(); // 0 if the DB empty
		map.reserve((std::size_t)(db_max_id));
		for (auto&& [id, str] : db)
			map[str] = id;

		auto max_id = db_max_id;
		for (auto i : strings.indices()) {
			if (auto itr = map.find(strings[i]); itr != map.end()) {
				ids[i] = itr->second;
			} else {
				ids[i] = ++max_id; // first id = 1
			}
		}

		// put invalidates the string views in the map, so do this at the end
		for (auto i : strings.indices())
			if (ids[i] > db_max_id)
				db.put(ids[i], strings[i]);
#else
		std::unordered_map<std::string_view, idx_t> map;
		map.reserve((std::size_t)strings.size());
		for (auto i : strings.indices())
			map[strings[i]] = i;

		id_t max_id = {};
		for (auto&& [id, str] : db) {
			if (auto itr = map.find(str); itr != map.end())
				ids[itr->second] = id;
			if (id > max_id) max_id = id;
		}

		if (add_if_not_found) {
			for (auto i : strings.indices())
				if (!ids[i].is_valid())
					db.put((ids[i] = ++max_id), strings[i]);
		}
#endif

		return ids;
	}

	auto get_all_strings(mdb::mdb_txn<false>& txn_rw) {
		vector_map<id_t, std::string_view> ret;
		auto db = txn_rw.open_db<id_t, std::string_view>(db_name, mdb::flags::open_db::integer_keys);
		ret.resize(db.get_max_key() + 1);
		for (auto&& [id, str] : db)
			ret[id] = str;
		// todo:
		return ret;
	}

	void commit_changes(mdb::mdb_txn<false>& txn_rw) {

	}
};

} // namespace mdb