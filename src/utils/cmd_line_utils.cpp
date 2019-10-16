#include "cmd_line_utils.h"

#include <codecvt>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <string>
#endif

namespace cppm {

template<typename C>
auto choose_literal(char * narrow, wchar_t * wide) {
	if constexpr (std::is_same_v<C, char>)
		return std::string_view { narrow };
	else if constexpr (std::is_same_v<C, wchar_t>)
		return std::wstring_view { wide };
}

#define LITERAL(T,x) choose_literal<T>(x, L##x)

template<typename T> 
void detail::ApplyCommandLineFromFile::init(int argc, T* argv[]) {
	// if the first argument is a #, ignore the command line file
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
	if (!argv_w)
		return;

	allocated_a = true;

	argv_a = new char* [argc];

	for (int i = 0; i < argc; ++i) {
		int bufSize = WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, NULL, 0, NULL, NULL);
		argv_a[i] = new char[bufSize];
		WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, argv_a[i], bufSize, NULL, NULL);
	}
}

detail::ApplyCommandLineFromFile::~ApplyCommandLineFromFile()
{
	if (allocated_w) {
		LocalFree(argv_w);
	}
	if (allocated_a) {
		for (int i = 0; i < argc; ++i)
			delete[] argv_a[i];
		delete[] argv_a;
	}
}

std::string executable_path() {
	char buf[1024];
	DWORD const ret = GetModuleFileNameA(NULL, buf, sizeof(buf));
	return (!ret || ret == sizeof(buf)) ? "" : buf;
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

} // namespace cppm