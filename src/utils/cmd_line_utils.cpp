#include "cmd_line_utils.h"

#include <codecvt>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <mutex>

#include <fmt/core.h>
#include <fmt/color.h>

#include <reproc/reproc.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

namespace cppm {

template<typename C, typename N, typename W>
auto choose_literal(N&& narrow, W&& wide) {
	if constexpr (std::is_same_v<C, char>)
		return std::string_view { narrow };
	else if constexpr (std::is_same_v<C, wchar_t>)
		return std::wstring_view { wide };
}

#define LITERAL(T,x) choose_literal<T>(x, L##x)

template<typename T> 
void detail::ApplyCommandLineFromFile::init(int argc, T* argv[]) {
	// if the first argument is a #, ignore the command line file
#if _WIN32
	if (argc >= 2 && argv[1] == LITERAL(T, "#")) {
		this->argc--;
		if constexpr (std::is_same_v<T, wchar_t>) {
			argv_w = &argv[1];
			argv_w[0] = argv[0];
		} else {
			argv_a = &argv[1];
			argv_a[0] = argv[0];
		}
		return;
	}

	std::wifstream fin("command_line.txt");
	if (!fin)
		return;

	std::wstring command_line = L"exec";
	std::wstring line;
	auto starts_with = [](std::wstring_view a, std::wstring_view b) {
		return (a.substr(0, b.size()) == b);
	};
	while (std::getline(fin, line)) {
		if (starts_with(line, L"#"))
			continue;
		command_line += L" " + line;
	}

	int nArgs = 0;
	LPWSTR* szArglist = CommandLineToArgvW(command_line.c_str(), &nArgs);
	if (NULL == szArglist) {
		printf("CommandLineToArgvW failed\n");
		return;
	}
	this->argc = nArgs;
	this->argv_w = szArglist;

	allocated_w = true;
#endif
}

detail::ApplyCommandLineFromFile::ApplyCommandLineFromFile(int argc, wchar_t** argv)
	: argc(argc), argv_w(argv)
{
	init(argc, argv);
}

detail::ApplyCommandLineFromFile::ApplyCommandLineFromFile(int argc, char* argv[])
	: argc(argc), argv_a(argv)
{
	init(argc, argv);
}

void detail::ApplyCommandLineFromFile::to_argv_a()
{
#if _WIN32
	if (!argv_w)
		return;

	allocated_a = true;

	argv_a = new char* [argc];

	for (int i = 0; i < argc; ++i) {
		int bufSize = WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, NULL, 0, NULL, NULL);
		argv_a[i] = new char[bufSize];
		WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, argv_a[i], bufSize, NULL, NULL);
	}
#endif
}

detail::ApplyCommandLineFromFile::~ApplyCommandLineFromFile()
{
#if _WIN32
	if (allocated_w) {
		LocalFree(argv_w);
	}
	if (allocated_a) {
		for (int i = 0; i < argc; ++i)
			delete[] argv_a[i];
		delete[] argv_a;
	}
#endif
}

std::string executable_path() {
	char buf[1024];
#ifdef _WIN32
	DWORD const ret = GetModuleFileNameA(NULL, buf, sizeof(buf));
	return (!ret || ret == sizeof(buf)) ? "" : buf;
#else
	return ((readlink("/proc/self/exe", buf, sizeof(buf)) <= 0) ? "" : buf);
#endif
}

std::string find_command_line_argument(std::string_view command_line, std::string_view starts_with)
{
	if (command_line.empty())
		return "";

	// todo: make this work quoted string properly
	std::size_t start = command_line.find(starts_with);
	if (start == std::string_view::npos)
		return "";
	start += starts_with.size();
	std::size_t end = command_line.find(" ", start);
	if (end == std::string_view::npos)
		end = command_line.size();
	return { &command_line[start], end - start };

#if 0
	std::wstring_convert<std::codecvt<wchar_t, char, std::mbstate_t>, wchar_t> conv;
	std::wstring w_cmd = conv.from_bytes(command_line.data(), command_line.data() + command_line.size());

	int nArgs = 0;
	LPWSTR* szArglist = CommandLineToArgvW(w_cmd.c_str(), &nArgs);
	if (NULL == szArglist) {
		throw std::runtime_error("CommandLineToArgvW failed\n");
	}

	LocalFree(szArglist);
#endif
}

std::string get_command_line_argument(std::string_view command_line, int idx)
{
	// todo: for now this assumes idx = 0 and the first argument doesn't contain spaces
	return (std::string)command_line.substr(0, command_line.find(" "));
}

void CmdArgs::init() {
	// todo: needs to be different for *nix
	arg_vec.push_back("cmd");
	arg_vec.push_back("/C");
}

static auto to_argv(const CmdArgs& args) {
	std::vector<const char*> argv;
	argv.reserve(args.arg_vec.size());
	for (auto& str : args.arg_vec)
		argv.push_back(&str[0]);
	argv.push_back(nullptr);
	return argv;
}

int64_t run_cmd(const CmdArgs& args) {
	reproc_t process;
	auto argv = to_argv(args);
	
	REPROC_ERROR err = REPROC_SUCCESS;
	err = reproc_start(&process, &argv[0], nullptr, nullptr);
	if (err != REPROC_SUCCESS)
		return -1;

	std::mutex print_lock;

	auto print_loop = [&process, &print_lock](REPROC_STREAM stream) {
		constexpr std::size_t read_buf_size = 32 * 1024;
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
				// todo: maybe find a cleaner way to do this
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

int64_t run_cmd_read_lines(const CmdArgs& args,
	const std::function<bool(std::string_view)>& stdout_callback,
	const std::function<bool(std::string_view)>& stderr_callback)
{
	reproc_t process;
	auto argv = to_argv(args);

	REPROC_ERROR err = REPROC_SUCCESS;
	err = reproc_start(&process, &argv[0], nullptr, nullptr);
	if (err != REPROC_SUCCESS)
		return -1;

	std::mutex callback_lock;

	auto print_loop = [&process, &callback_lock](
		REPROC_STREAM stream,
		const std::function<bool(std::string_view)>& callback)
	{
		constexpr std::size_t read_buf_size = 32 * 1024;
		std::vector<uint8_t> buf;
		buf.resize(read_buf_size);
		std::vector<char> line_buf;
		line_buf.reserve(read_buf_size);
		bool got_newline = true;
		while (true) {
			unsigned int bytes_read = 0;
			REPROC_ERROR err = reproc_read(&process, stream, &buf[0], buf.size(), &bytes_read);
			if (err != REPROC_SUCCESS)
				break;
			std::string_view str { (const char*)buf.data(), (std::size_t)bytes_read };
			while (!str.empty()) {
				auto newline_pos = str.find_first_of("\r\n");
				bool found_newline = (newline_pos != std::string_view::npos);
				std::string_view line_part = str.substr(0, newline_pos);
				std::string_view full_line = line_part;
				if (!line_buf.empty()) {
					// if last time we didn't find a newline in the buffer
					// then this is a continuation of the same line
					line_buf.insert(line_buf.end(), line_part.begin(), line_part.end());
					full_line = { line_buf.data(), line_buf.size() };
				}
				if (found_newline) {
					if (std::lock_guard guard { callback_lock }; !callback(full_line))
						return;
					line_buf.clear();
				}
				str.remove_prefix(line_part.size());
				// todo: the following might ignore some empty lines.
				while (!str.empty() && (str[0] == '\r' || str[0] == '\n'))
					str.remove_prefix(1);
			}
		}
		if (!line_buf.empty()) {
			// the last line ended without a newline
			std::lock_guard guard { callback_lock }; 
			callback({ line_buf.data(), line_buf.size() });
		}
	};

	std::thread error_thread(print_loop, REPROC_STREAM_ERR, stderr_callback);
	print_loop(REPROC_STREAM_OUT, stdout_callback);
	error_thread.join();

	err = reproc_wait(&process, REPROC_INFINITE);
	int64_t ret = -1;
	if (err == REPROC_SUCCESS)
		ret = reproc_exit_status(&process);
	reproc_destroy(&process);
	return ret;
}

} // namespace cppm