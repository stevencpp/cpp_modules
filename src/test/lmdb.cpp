#include <catch2/catch.hpp>
#include "temp_file_test.h"
#include "lmdb_wrapper.h"
#include "lmdb_string_store.h"
#include "lmdb_path_store.h"
#include "span.hpp"
#include "test_config.h"
#include "util.h"
#include "trace.h"

namespace lmdb_test {

ConfigString scanner_output_file { "scanner_output_file", "" };

using namespace Catch::Matchers;

struct LMDB_Test : public TempFileTest
{
	auto init_env() {
		mdb::mdb_env env;
		all_files_created.insert("scanner.mdb");
		all_files_created.insert("scanner.mdb-lock");
		env.set_maxdbs(5);
		env.open((tmp_path / "scanner.mdb").string().c_str(), mdb::flags::env::nosubdir);
		return env;
	}
};

TEST_CASE("lmdb - multiple spans", "[lmdb]") {
	LMDB_Test test;
	auto env = test.init_env();
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

TEST_CASE("lmdb - string store", "[lmdb]") {
	LMDB_Test test;
	auto env = test.init_env();
	auto txn = env.txn_read_write();
	mdb::string_id_store<uint32_t> string_store { "strings" };
	string_store.init(txn, {});
	for (auto str : { "D3","std.core","D4","D2","std.core","D4","D1","std.core","D2","D3","D4","std.core","std.core" })
		string_store.try_add(str);
	string_store.commit_changes(txn);
	txn.commit();
}

auto read_scanner_output() {
	TRACE();
	std::vector<std::string> ret;
	std::ifstream fin(scanner_output_file);
	std::string line;
	while (std::getline(fin, line)) {
		if (line.empty() || line.substr(0, 4) == "::::") continue;
		ret.push_back(line);
	}
	return ret;
}

auto hash_map_benchmark(const std::vector<std::string> & files) {
	TRACE();
	//std::unordered_map<std::string, int> map;
	std::unordered_map<std::string_view, int> map;
	//map.reserve(10000);
	int i = 0;
	for (auto& file : files) {
		auto [itr, inserted] = map.try_emplace(file, i);
		if (inserted)
			i++;
	}
	std::cout << i << " unique files\n";
	return map;
}

void canonicalize_benchmark(const std::unordered_map<std::string_view, int>& map)
{
	TRACE();
	std::size_t total_size = 0;
	for (auto&& [file, id] : map) {
		total_size += std::filesystem::canonical(file).native().size();
	}
	std::cout << "total canonical length = " << total_size << "\n";
}

TEST_CASE("lmdb - path store", "[lmdb_path_store]") {
	auto files = read_scanner_output();
	/*auto file_map = hash_map_benchmark(files);
	//canonicalize_benchmark(file_map);
	return;*/

	LMDB_Test test;
	auto env = test.init_env();
	auto txn = env.txn_read_write();
	struct dir_entry {};
	struct file_entry {};
	mdb::path_store<uint32_t, dir_entry, file_entry> ps;

	timer t;
	t.start();
	std::unordered_map<std::string, uint32_t> map;
	for (auto& file : files) {
		auto [itr, inserted] = map.try_emplace(file, 0);
		if(inserted)
			itr->second = ps.try_add(file);
	}
	t.stop();
}

} // namespace lmdb_test