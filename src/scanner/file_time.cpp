#include "file_time.h"

//#define WIN32_LEAN_AND_MEAN
//#include <windows.h>
#include <chrono>

namespace cppm {

template<typename T>
file_time_t time_point_to_file_time_t(T&& tp) {
	return (file_time_t)std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

file_time_t file_time_t_now() {
	//auto tp_now = std::chrono::system_clock::now();
	auto tp_now = std::filesystem::file_time_type::clock::now(); // todo: workaround until we get a clock_cast
	return time_point_to_file_time_t(tp_now);
}

file_time_t get_last_write_time(const std::filesystem::path& path) {
	try {
		auto filetime = std::filesystem::last_write_time(path);
#if 0
		// https://developercommunity.visualstudio.com/content/problem/251213/stdfilesystemfile-time-type-does-not-allow-easy-co.html

		FILETIME ft;
		memcpy(&ft, &filetime, sizeof(FILETIME));
		SYSTEMTIME  stSystemTime;
		if (FileTimeToSystemTime(&ft, &stSystemTime)) {
			... ?
		}
#endif

		return time_point_to_file_time_t(filetime);
	} catch (std::filesystem::filesystem_error&) {
		// todo: if the file just doesn't exist anymore that's fine, just remove it from the DB
		// report other filesystem errors
		// note: this will be compared later to the last item scan time
		return std::numeric_limits<file_time_t>::max();
	}
}

} // namespace cppm