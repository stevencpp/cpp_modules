#pragma once

#include <filesystem>

namespace cppm {

using file_time_t = unsigned long long; // todo: move this back to impl ?

file_time_t file_time_t_now();
file_time_t get_last_write_time(const std::filesystem::path&);

} // namespace cppm