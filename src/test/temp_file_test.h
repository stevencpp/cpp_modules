#pragma once

#include <catch2/catch.hpp>
#include <fmt/core.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>

#ifndef _WIN32
#include <stdlib.h> // for mkdtemp
#endif

namespace fs = std::filesystem;

struct TempFileTest {
public:
	fs::path current_path;
	fs::path tmp_path;
	std::string tmp_path_str;
	std::unordered_set<std::string> all_files_created;
	std::unordered_set<std::string> all_dirs_created;

	template<typename F>
	void create_files(std::string_view file_def, F&& file_visitor) {
		std::ofstream fout;
		while (!file_def.empty()) {
			std::string_view line = file_def.substr(0, file_def.find_first_of("\n\r"));
			if (line.starts_with(">")) {
				if (fout) fout.close();
				std::string_view file = line.substr(2); // todo: allow arbitrary whitespace after >
				fout.open(tmp_path / file);
				file_visitor((std::string)file);
				all_files_created.insert((std::string)file);
			} else if (fout.is_open()) {
				fout << line << "\n";
			}
			if (file_def.size() == line.size())
				break;
			file_def.remove_prefix(line.size() + 1); // UB if file_def doesn't have at least size + 1 elems
		};
	}

	void create_files(std::string_view file_def) {
		create_files(file_def, [](const std::string&){});
	}

	fs::path create_dir(fs::path name) {
		auto path = tmp_path / name;
		fs::create_directories(path);
		// we only need to add the first component to the set of dirs created
		// because that whole subtree will be removed in the destructor
		for (auto& component : name) {
			all_dirs_created.insert(component.string());
			break;
		}
		return path;
	}
public:

	TempFileTest() {
		current_path = fs::current_path();
		
		// note: std::tmpnam has a deprecation warning 
#ifdef _WIN32
		char tmp_dir_name[256];
		REQUIRE(tmpnam_s(tmp_dir_name) == 0);
		tmp_path = fs::temp_directory_path() / "cppm" / fs::path { tmp_dir_name }.filename();
		REQUIRE(fs::create_directories(tmp_path));
#else
		char tmp_dir_name[256] = "/tmp/cppm_XXXXXX";
		REQUIRE(nullptr != mkdtemp(tmp_dir_name));
		tmp_path = fs::path { tmp_dir_name };
#endif
		tmp_path = std::filesystem::canonical(tmp_path);
		
		tmp_path_str = tmp_path.string();
	}

	~TempFileTest() {
		// note: can't remove tmp_path if something chdir'd into it
		fs::current_path(current_path);
		for (auto& file : all_files_created)
			fs::remove(tmp_path / file);
		for (auto& dir : all_dirs_created)
			fs::remove_all(tmp_path / dir);
		fs::remove(tmp_path);
	}
};