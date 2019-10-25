#pragma once

#include <lmdb.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "lmdb_wrapper_impl.h"

namespace mdb {

namespace flags {
	enum env : unsigned int {
		//none = 0,
		nosubdir = MDB_NOSUBDIR,
		writemap = MDB_WRITEMAP,
		nomeminit = MDB_NOMEMINIT,
		fixedmap = MDB_FIXEDMAP,
	};

	enum open_db : unsigned int {
		//none = 0,
		create = MDB_CREATE,
		integer_keys = MDB_INTEGERKEY,
	};

	enum put : unsigned int {
		none = 0,
		append = MDB_APPEND,
		reserve = MDB_RESERVE,
	};
}

template<bool read_only, typename Key, typename Value>
struct mdb_dbi;

struct key_not_found_exception : public std::exception {};

template<bool read_only, typename Key, typename Value>
struct mdb_dbi_get {
	decltype(auto) get(const Key& k) {
		using namespace impl;
		auto parent = static_cast<mdb_dbi<read_only, Key, Value>*>(this);
		constexpr int max_key_len = 2048; // todo: use the value that lmdb was compiled with
		char key_buf[max_key_len];
		MDB_val key = to_val(k, key_buf, max_key_len);
		MDB_val val;
		int ret = mdb_get(parent->txn, parent->dbi, &key, &val);
		if (ret == MDB_NOTFOUND)
			throw key_not_found_exception {};
		handle_mdb_error(ret, "failed to get data");
		return from_val<Value>(val);
	}
};

template<bool read_only, typename Key, typename Value>
struct mdb_dbi_put {};

template<typename Key, typename Value>
struct mdb_dbi_put<false, Key, Value> { // only for read/write transactions
	void put(const Key& k, const Value& v, flags::put put_flags = {}) {
		using namespace impl;
		auto parent = static_cast<mdb_dbi<false, Key, Value>*>(this);
		// if key or value are aggregates that contain views then their contents need to be copied to a buffer first
		constexpr int max_key_len = 2048; // todo: use the value that lmdb was compiled with
		char key_buf[max_key_len];
		MDB_val key = to_val(k, key_buf, max_key_len);

		if constexpr (contains_a_view<Value>()) {
			MDB_val val;
			val.mv_size = get_val_size(v);
			int ret = mdb_put(parent->txn, parent->dbi, &key, &val, put_flags + flags::put::reserve);
			handle_mdb_error(ret, "failed to put aggregate");
			to_val<false>(v, (char*)val.mv_data, val.mv_size);
		} else {
			constexpr int max_val_len = 2048;
			char val_buf[max_val_len];
			MDB_val val = to_val(v, val_buf, max_val_len);
			int ret = mdb_put(parent->txn, parent->dbi, &key, &val, put_flags);
			handle_mdb_error(ret, "failed to put data");
		}
	}
};

template<bool read_only, typename Key, typename Value>
struct mdb_dbi_del {};

template<typename Key, typename Value>
struct mdb_dbi_del<false, Key, Value> { // only for read/write transactions
	void del(const Key& k) {
		using namespace impl;
		auto parent = static_cast<mdb_dbi<false, Key, Value>*>(this);
		constexpr int max_key_len = 2048; // todo: use the value that lmdb was compiled with
		char key_buf[max_key_len];
		MDB_val key = to_val(k, key_buf, max_key_len);
		int ret = mdb_del(parent->txn, parent->dbi, &key, nullptr);
		if (ret == MDB_NOTFOUND)
			throw key_not_found_exception {};
		handle_mdb_error(ret, "failed to get data");
	}
};

template<bool read_only, typename Key, typename Value>
struct mdb_dbi : public
	mdb_dbi_get<read_only, Key, Value>,
	mdb_dbi_put<read_only, Key, Value>,
	mdb_dbi_del<read_only, Key, Value>
{
private:
	MDB_txn* txn = nullptr;
	decltype(impl::make_mdb_dbi(nullptr, nullptr, 0)) dbi = 0;
	friend struct mdb_dbi_get< read_only, Key, Value >;
	friend struct mdb_dbi_put< read_only, Key, Value >;
	friend struct mdb_dbi_del< read_only, Key, Value >;

	struct iterator_end {};

	struct iterator {
		decltype(impl::make_mdb_cursor(nullptr, 0)) cursor;
		MDB_val key = {}, val = {};

		decltype(auto) operator * () {
			using namespace impl;
			//the following don't work if e.g Key = string_view and Value = large_object:
			//return std::pair { from_val<Key>(key), from_val<Value>(val) }; // makes a copy of value
			//return std::tie(from_val<Key>(key), from_val<Value>(val)); // reference to temporary string_view
			//return std::forward_as_tuple(from_val<Key>(key), from_val<Value>(val)); // same
			struct kvp {
				decltype(from_val<Key>(key)) k;
				decltype(from_val<Value>(val)) v;
			};
			return kvp { from_val<Key>(key), from_val<Value>(val) };
		}
		bool operator != (const iterator_end&) const {
			return key.mv_data != nullptr;
		}
		iterator & operator++() {
			int ret = mdb_cursor_get(cursor.get(), &key, &val, MDB_NEXT);
			if (ret == MDB_NOTFOUND) key.mv_data = nullptr; // not an error if we've reached the end
			else impl::handle_mdb_error(ret, "failed to get next key value pair");
			return *this;
		}
	};
public:
	mdb_dbi(MDB_txn * txn, const char *name, unsigned int open_flags) :
		txn(txn), dbi { impl::make_mdb_dbi(txn, name, open_flags) } {}

	iterator begin() {
		iterator itr { impl::make_mdb_cursor(txn, dbi) };
		int ret = mdb_cursor_get(itr.cursor.get(), &itr.key, &itr.val, MDB_NEXT);
		if (ret != MDB_NOTFOUND) // not an error if the DB is empty
			impl::handle_mdb_error(ret, "failed to get next key value pair");
		return itr;
	}

	iterator_end end() {
		return {};
	}

	Key get_max_key() {
		auto cursor = impl::make_mdb_cursor(txn, dbi);
		MDB_val key, val;
		int ret = mdb_cursor_get(cursor.get(), &key, &val, MDB_LAST);
		if (ret == MDB_NOTFOUND) // not an error if the DB is empty
			return {};
		impl::handle_mdb_error(ret, "failed to get next key value pair");
		return impl::from_val<Key>(key);
	}
#if 0
	iterator key_range(Key min_key) {

	}

	iterator key_range(Key min_key, Key max_key) {

	}
#endif
};

template<bool read_only>
struct mdb_txn {
private:
	static MDB_txn* make_txn(MDB_env *env) {
		MDB_txn* txn = nullptr;
		int ret = mdb_txn_begin(env, nullptr, (read_only ? MDB_RDONLY : 0), &txn);
		impl::handle_mdb_error(ret, "failed to begin lmdb transaction");
		return txn;
	}
	static void dummy(MDB_txn*) {}

	std::unique_ptr<MDB_txn, void(*)(MDB_txn*)> txn = { (MDB_txn*)nullptr, &dummy };

public:
	MDB_txn* get() { return txn.get(); }
	mdb_txn() {}
	mdb_txn(MDB_env* env) : txn { make_txn(env), &mdb_txn_abort } {}
	void commit() {
		int ret = mdb_txn_commit(txn.get());
		txn.release(); // don't call txn_abort after this
		impl::handle_mdb_error(ret, "failed to commit transaction");
	}

	template<typename Key = std::string_view, typename Value = std::string_view>
	auto open_db(const char* name, flags::open_db open_flags = {}) {
		// todo: this doesn't work for strong ids
		constexpr bool is_integral = std::is_integral_v<Key>;
		return mdb_dbi<read_only, Key, Value> { txn.get(), name, open_flags |
			flags::open_db::create | (is_integral ? flags::open_db::integer_keys : 0)
		};
	}

	template<typename Key = std::string_view, typename Value = std::string_view>
	auto open_db(flags::open_db open_flags = {}) {
		return open_db<Key, Value>(nullptr, open_flags);
	}
};

struct mdb_txn_rw : public mdb_txn<false> {
	using base = mdb_txn<false>;
	using base::base;
};

struct mdb_txn_ro : public mdb_txn<true> {
	using base = mdb_txn<true>;
	using base::base;
};

struct mdb_env {
private:
	decltype(impl::make_mdb_env()) env = impl::make_mdb_env();
public:
	MDB_env* get() { return env.get(); }
	void open(const char* path, flags::env env_flags) {
		mdb_mode_t mode = 0;
#ifndef _WIN32
		mode = S_IRUSR | S_IWUSR;
#endif
		int ret = mdb_env_open(env.get(), path, env_flags, mode);
		impl::handle_mdb_error(ret, "failed to open lmdb env");
	}

	void set_map_size(std::size_t size) {
		int ret = mdb_env_set_mapsize(env.get(), size);
		impl::handle_mdb_error(ret, "failed to set map size");
	}

	void set_maxdbs(std::size_t nr) {
		int ret = mdb_env_set_maxdbs(env.get(), (MDB_dbi)nr);
		impl::handle_mdb_error(ret, "failed to set maxdbs size");
	}

	auto txn_read_write() {
		return mdb_txn_rw { env.get() };
	}
	auto txn_read_only() {
		return mdb_txn_ro { env.get() };
	}
};

class key_range {

};

} // namespace mdb