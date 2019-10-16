#include "util.h"
#include <fmt/core.h>
#include <fmt/color.h>
#include <reproc/reproc.h>

#include <string_view>
#include <vector>
#include <thread>
#include <mutex>

void CmdArgs::init() {
	// todo: needs to be different for *nix
	arg_vec.push_back("cmd");
	arg_vec.push_back("/C");
}

int64_t run_cmd(const CmdArgs& args) {
	reproc_t process;
	std::vector<const char*> argv;
	argv.reserve(args.arg_vec.size());
	for (auto& str : args.arg_vec)
		argv.push_back(&str[0]);
	argv.push_back(nullptr);

	REPROC_ERROR err = REPROC_SUCCESS;
	err = reproc_start(&process, &argv[0], nullptr, nullptr);
	if (err != REPROC_SUCCESS)
		return -1;

	constexpr std::size_t read_buf_size = 32 * 1024;

	std::mutex print_lock;

	auto print_loop = [&process, &print_lock](REPROC_STREAM stream) {
		std::vector<uint8_t> buf;
		buf.resize(read_buf_size);
		fmt::memory_buffer fmt_buf;
		fmt_buf.reserve(read_buf_size + 256);
		bool got_newline = true;
		while (true) {
			unsigned int bytes_read = 0;
			REPROC_ERROR err = reproc_read(&process, stream, &buf[0], buf.size(), &bytes_read);
			if (err != REPROC_SUCCESS)
				break;
			std::string_view str { (const char*)buf.data(), (std::size_t)bytes_read };
			fmt_buf.clear();
			while (!str.empty()) {
				std::string_view line = str.substr(0, str.find_first_of("\r\n"));
				if (got_newline) fmt::format_to(fmt_buf, "> ");
				fmt::format_to(fmt_buf, "{}", line);
				str.remove_prefix(line.size());
				got_newline = false;
				while (!str.empty() && (str[0] == '\r' || str[0] == '\n')) {
					got_newline = true;
					fmt::format_to(fmt_buf, "{}", str[0]);
					str.remove_prefix(1);
				}
			}
			std::lock_guard guard { print_lock };
			str = std::string_view { fmt_buf.data(), fmt_buf.size() };
			if (stream == REPROC_STREAM_ERR)
				fmt::print(fmt::fg(fmt::color::red), "{}", str);// fmt_buf);
			else
				fmt::print("{}", str);
		}
	};

	std::thread error_thread(print_loop, REPROC_STREAM_ERR);
	print_loop(REPROC_STREAM_OUT);
	error_thread.join();

	err = reproc_wait(&process, REPROC_INFINITE);
	int64_t ret = -1;
	if (err == REPROC_SUCCESS)
		ret = reproc_exit_status(&process);
	reproc_destroy(&process);
	return ret;
}