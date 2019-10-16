#include <catch2/catch.hpp>
#include "temp_file_test.h"
#include "lmdb_wrapper.h"
#include "span.hpp"

using namespace Catch::Matchers;

TEST_CASE("lmdb - multiple spans", "[lmdb]") {
	TempFileTest test;
	mdb::mdb_env env;
	test.all_files_created.insert("scanner.mdb");
	test.all_files_created.insert("scanner.mdb-lock");
	env.set_maxdbs(5);
	env.open((test.tmp_path / "scanner.mdb").string().c_str(), mdb::flags::env::nosubdir);
	auto txn = env.txn_read_write();
	using key_t = uint32_t;
	struct value_t {
		uint32_t dummy = 42;
		tcb::span<uint32_t> a;
		tcb::span<uint32_t> b;
	};
	auto dbi = txn.open_db<key_t, value_t>("db", mdb::flags::open_db::create);
	std::vector<uint32_t> b = { 5, 6, 7, 8, 9, 10 };
	dbi.put(1, { 43, {}, b });
	auto val = dbi.get(1);
	CHECK(val.dummy == 43);
	CHECK(val.a.size() == 0);
	std::vector<uint32_t> b1;
	for (uint32_t v : val.b)
		b1.push_back(v);
	CHECK_THAT(b1, UnorderedEquals(b));
	txn.commit();
}