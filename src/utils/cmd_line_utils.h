#include <functional>
#include <string>
#include <string_view>

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

} // namespace cppm