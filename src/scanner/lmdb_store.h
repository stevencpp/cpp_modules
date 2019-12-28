#pragma once

#include "lmdb_wrapper.h"
#include "strong_id.h"

#include <vector>

namespace mdb {

template <
	typename id_t,
	typename entry_t
>
struct id_store {
	const char* db_name = nullptr;
	id_t db_max_id = {};
	id_t next_id = db_max_id + 1;

	id_store(const char* db_name) : db_name(db_name) {}

	auto open_db(mdb::mdb_txn<false>& txn_rw) {
		return txn_rw.open_db<id_t, entry_t>(db_name);
	}

	// note: this doesn't work if called after update_data
	template<typename idx_t, typename F>
	void get_data(mdb::mdb_txn<false>& txn_rw, const vector_map<idx_t, id_t>& ids, F&& data_func) {
		auto db = open_db(txn_rw);

		for (auto idx : ids.indices()) {
			auto id = ids[idx];
			if (id > db_max_id) // there's no data to fetch for new entires
				continue;
			try { // todo: don't use exceptions for this
				data_func(idx, db.get(id));
			}
			catch (mdb::key_not_found_exception&) {}
		}
	}

	std::vector<std::pair<id_t, entry_t>> new_data;

	// note: this should only be called once
	template<typename idx_t, typename F>
	void update_data(const span_map<idx_t, id_t> ids, F&& data_func) {
		new_data.reserve((std::size_t)next_id);
		for (auto idx : ids.indices())
			new_data.push_back({ ids[idx], data_func(idx) });
	}

	void commit_changes(mdb::mdb_txn<false>& txn_rw) {
		auto db = open_db(txn_rw);

		for (auto& [id, entry] : new_data)
			db.put(id, entry);
	}

	// todo: this should work with a read-only txn as well
	void print_data(mdb::mdb_txn<false>& txn_rw) {
		auto db = open_db(txn_rw);
		// todo:
	}
};

}