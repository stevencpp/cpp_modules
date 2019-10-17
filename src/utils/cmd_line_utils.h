#include <functional>
#include <string>
#include <string_view>
#include <string>
#include <vector>
#include <fmt/format.h>


namespace cppm {

namespace detail {

struct ApplyCommandLineFromFile {
	bool allocated_a = false, allocated_w = false;
	int argc; char** argv_a = nullptr; wchar_t** argv_w = nullptr;
	ApplyCommandLineFromFile(int argc, wchar_t* argv[]);
	ApplyCommandLineFromFile(int argc, char* argv[]);
	template<typename T>
	void init(int argc, T* argv[]);
	void to_argv_a();
	~ApplyCommandLineFromFile();
};

struct ExecutablePath {
	ExecutablePath();
};

} // namespace cppm::detail

template<typename T, typename ArgsFunc>
decltype(auto) apply_command_line_from_file(int argc, T* argv[], ArgsFunc&& args_func) {
	auto aclff = detail::ApplyCommandLineFromFile { argc, argv };
	if constexpr (std::is_invocable_v<ArgsFunc, int, wchar_t**>) {
		return args_func(aclff.argc, aclff.argv_w);
	} else {
		aclff.to_argv_a();
		return args_func(aclff.argc, aclff.argv_a);
	}
}

std::string executable_path();

std::string find_command_line_argument(std::string_view command_line, std::string_view starts_with);
std::string get_command_line_argument(std::string_view command_line, int idx);

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

int64_t run_cmd_read_lines(const CmdArgs& args,
	const std::function<bool(std::string_view)>& stdout_callback,
	const std::function<bool(std::string_view)>& stderr_callback);

} // namespace cppm