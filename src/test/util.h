#pragma once

#include <chrono>
#include <iostream>

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