#include <fstream>
#include <string>

void write_file(std::string name, std::string contents) {
	std::ofstream fout { name };
	fout << contents;
}

int main() {
	write_file("B.h", "void foo();");
	write_file("B.cpp", "void foo() {}");
	write_file("C.m.cpp", "export module C;\nexport void bar() {}");
	return 0;
}
