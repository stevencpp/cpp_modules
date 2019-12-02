#include <catch2/catch.hpp>
#include "temp_file_test.h"
#include "lmdb_wrapper.h"
#include "lmdb_string_store.h"
#include "lmdb_path_store.h"
#include "span.hpp"
#include "test_config.h"
#include "util.h"
#include "trace.h"

#include <absl/container/flat_hash_map.h>

namespace lmdb_test {

ConfigPath scanner_output_file { "scanner_output_file", "" };

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

TEST_CASE("lmdb - path store", "[lmdb]") {
	LMDB_Test test;
	auto env = test.init_env();
	auto txn = env.txn_read_write();
	struct file_entry {};
	mdb::path_store<uint32_t, file_entry> ps;
	
	test.create_dir("a/.b/c/d");

	auto try_add = [&](const fs::path& p) {
		return ps.try_add(p.string());
	};

	fs::path cur_path;
	auto set_cur_path = [&](std::string_view rel_path) {
		cur_path = test.tmp_path;
		if (!rel_path.empty()) cur_path /= rel_path;
		ps.update_current_path(cur_path.string());
	};
	set_cur_path("");

	auto check = [&](auto file_id, std::string rel_path) {
		INFO("rel path is " << rel_path);
		CHECK(file_id == try_add(rel_path));
		CHECK(file_id == try_add(cur_path / rel_path));
	#ifdef _WIN32
		std::replace(rel_path.begin(), rel_path.end(), '/', '\\');
		CHECK(file_id == try_add(rel_path));
		CHECK(file_id == try_add(cur_path / rel_path));
	#endif
	};

	auto id = try_add("");
	CHECK(ps.current_path_id == id);
	check(id, "");
	check(id, ".");
	check(id, "a/..");
	check(id, "a/./../");
	check(id, "a/.b/../..");
	check(id, "a/.b/c//../../../");
	check(id, "a//../a/.b/c/../../.b/../../.");

	auto id_a = try_add("a");
	check(id_a, "a");
	check(id_a, "./a");
	check(id_a, "a//.b////..");
	check(id_a, "a/.b/./c/..///..");
	check(id_a, "a/.b/..//../a/../a/");
	check(id_a, "a/./.b/c/d/../../..");
	
	set_cur_path("a");
	CHECK(ps.current_path_id == id_a);
	check(id_a, "");
	check(id_a, "../a");
	check(id_a, "..//a/.b/./../");

	auto id_b = try_add(".b");
	check(id_b, "./../a/.b//");
	set_cur_path("a/.b");
	CHECK(ps.current_path_id == id_b);
	check(id_b, "");
	check(id_b, ".");
	check(id_b, "../../a/.b/c/../.");

	// todo: change_dir("C:") check(id, "../x"); etc.
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

static bool IsPathSeparator(char c) {
#ifdef _WIN32
	return c == '/' || c == '\\';
#else
	return c == '/';
#endif
}

bool CanonicalizePath(char* path, size_t* len, uint64_t* slash_bits,
	std::string* err) {
	// WARNING: this function is performance-critical; please benchmark
	// any changes you make to it.
	//METRIC_RECORD("canonicalize path");
	if (*len == 0) {
		*err = "empty path";
		return false;
	}

	const int kMaxPathComponents = 60;
	char* components[kMaxPathComponents];
	int component_count = 0;

	char* start = path;
	char* dst = start;
	const char* src = start;
	const char* end = start + *len;

	if (IsPathSeparator(*src)) {
#ifdef _WIN32

		// network path starts with //
		if (*len > 1 && IsPathSeparator(*(src + 1))) {
			src += 2;
			dst += 2;
		} else {
			++src;
			++dst;
		}
#else
		++src;
		++dst;
#endif
	}

	while (src < end) {
		if (*src == '.') {
			if (src + 1 == end || IsPathSeparator(src[1])) {
				// '.' component; eliminate.
				src += 2;
				continue;
			} else if (src[1] == '.' && (src + 2 == end || IsPathSeparator(src[2]))) {
				// '..' component.  Back up if possible.
				if (component_count > 0) {
					dst = components[component_count - 1];
					src += 3;
					--component_count;
				} else {
					*dst++ = *src++;
					*dst++ = *src++;
					*dst++ = *src++;
				}
				continue;
			}
		}

		if (IsPathSeparator(*src)) {
			src++;
			continue;
		}

		if (component_count == kMaxPathComponents) {
			*err = "path has too many components";
			return false;
			//Fatal("path has too many components : %s", path);
		}
		components[component_count] = dst;
		++component_count;

		while (src != end && !IsPathSeparator(*src))
			*dst++ = *src++;
		*dst++ = *src++;  // Copy '/' or final \0 character as well.
	}

	if (dst == start) {
		*dst++ = '.';
		*dst++ = '\0';
	}

	*len = dst - start - 1;
#if 0
#ifdef _WIN32
	uint64_t bits = 0;
	uint64_t bits_mask = 1;

	for (char* c = start; c < start + *len; ++c) {
		switch (*c) {
		case '\\':
			bits |= bits_mask;
			*c = '/';
			//NINJA_FALLTHROUGH;
		case '/':
			bits_mask <<= 1;
		}
	}

	*slash_bits = bits;
#else
	* slash_bits = 0;
#endif
#endif
	return true;
}

auto hash_map_benchmark(std::vector<std::string> & files) {
	TRACE();
	//files.resize(files.size() / 100);
	//std::unordered_map<std::string, int> map;
	//std::unordered_map<std::string_view, int> map;
	absl::flat_hash_map<std::string_view, int> map;
	//map.reserve(10000);
	int i = 0;
	std::string err;
	for (auto& file : files) {
#if 1
		std::size_t len = file.size();
		uint64_t slash_bits = 0;
		if (!CanonicalizePath(file.data(), &len, &slash_bits, &err)) {
			fmt::print("failed to canonicalize: {}\n", err);
			return map;
		}
		file.resize(len);
#endif
		auto [itr, inserted] = map.try_emplace(file, i);
		if (inserted)
			i++;
	}
	std::cout << i << " unique files out of " << files.size() << "\n";
#if 0
	TRACE();
	int j = 0;
	for (auto& file : files) {
#if 0
		std::size_t len = file.size();
		uint64_t slash_bits = 0;
		if (!CanonicalizePath(file.data(), &len, &slash_bits, &err)) {
			fmt::print("failed to canonicalize: {}\n", err);
			return map;
		}
		file.resize(len);
#endif
		auto [itr, inserted] = map.try_emplace(file, i);
		if (!inserted)
			j++;
	}
	std::cout << j << " insert attempts\n";
#endif
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

TEST_CASE("lmdb - path store - benchmark", "[lmdb_path_store_benchmark]") {
	auto files = read_scanner_output();
	auto file_map = hash_map_benchmark(files);
	//canonicalize_benchmark(file_map);
	//return;

	LMDB_Test test;
	auto env = test.init_env();
	auto txn = env.txn_read_write();
	struct file_entry {};
	mdb::path_store<uint32_t, file_entry> ps;
	ps.update_current_path();

#if 0
	for (int i = 0; i < 10; i++) {
		timer t;
		t.start();
		uint32_t total = 0;
		std::unordered_map<std::string, uint32_t> map;
		for (auto& file : files) {
			auto [itr, inserted] = map.try_emplace(file, 0);
			if (inserted)
				itr->second = ps.try_add(file);
		}
		t.stop();
	}
#endif

#if 0
	for (int i = 0; i < 10; i++) {
		timer t;
		t.start();
		for (auto& file : files) {
			ps.try_add(file);
		}
		t.stop();
		std::cout << (uint32_t)ps.next_id << "\n";
	}
#endif

#if 1
	{
		TRACE_BLOCK("path_store: try_add");
		for (auto& [file, id] : file_map) {
			id = ps.try_add(file);
		}
		std::cout << "buffer size " << ps.normal_paths.size() << "\n";
	}
#endif
}

} // namespace lmdb_test