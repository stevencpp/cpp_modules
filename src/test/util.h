#pragma once

// for timer
#include <chrono>
#include <iostream>

// for chdir_guard
#include <filesystem>

struct timer
{
	std::chrono::high_resolution_clock::time_point ts;

	void start() {
		ts = std::chrono::high_resolution_clock::now();
	}

	auto stop(std::string_view what = "") {
		auto te = std::chrono::high_resolution_clock::now();
		auto nr_ms = std::chrono::duration_cast<std::chrono::milliseconds>(te - ts).count();
		if (what.empty())
			std::cout << "elapsed: " << nr_ms << "ms\n";
		else
			std::cout << what << " took: " << nr_ms << "ms\n";
		return nr_ms;
	}
};

namespace fs = std::filesystem;

inline auto make_chdir_guard() {
	struct chdir_guard {
		fs::path current;
		chdir_guard() : current(fs::current_path()) {}
		~chdir_guard() { fs::current_path(current); }
	};
	return chdir_guard {};
}