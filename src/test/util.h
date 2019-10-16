#pragma once

// for timer
#include <chrono>
#include <iostream>

// for run_cmd
#include <string>
#include <vector>
#include <fmt/format.h>

// for chdir_guard
#include <filesystem>

struct timer
{
	std::chrono::high_resolution_clock::time_point ts;

	void start() {
		ts = std::chrono::high_resolution_clock::now();
	}

	auto stop() {
		auto te = std::chrono::high_resolution_clock::now();
		auto nr_ms = std::chrono::duration_cast<std::chrono::milliseconds>(te - ts).count();
		std::cout << "elapsed: " << nr_ms << "ms\n";
		return nr_ms;
	}
};

struct CmdArgs {
	std::vector<std::string> arg_vec;

	CmdArgs() {
		init();
	}

	template<typename... Args>
	CmdArgs(std::string_view format_string, Args&&... args) {
		init();
		append(format_string, std::forward<Args>(args)...);
	}

	void init();

	template<typename... Args>
	void append(std::string_view format_string, Args&&... args) {
		std::string str = fmt::format(format_string, std::forward<Args>(args)...);
		std::string cur;
		for (size_t i = 0; i < str.size(); i++) {
			char c = str[i];
			if (c == ' ') {
				if (!cur.empty()) {
					arg_vec.push_back(cur);
					cur.clear();
				}
			} else if (c == '\"') {
				i++;
				while (str[i] != '\"') { cur += str[i]; i++; }
			} else {
				cur += c;
			}
		}
		if (!cur.empty())
			arg_vec.push_back(cur);
	}
};

int64_t run_cmd(const CmdArgs& args);

namespace fs = std::filesystem;

auto make_chdir_guard() {
	struct chdir_guard {
		fs::path current;
		chdir_guard() : current(fs::current_path()) {}
		~chdir_guard() { fs::current_path(current); }
	};
	return chdir_guard {};
}