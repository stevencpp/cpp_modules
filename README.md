![](https://github.com/stevencpp/cpp_modules/workflows/CI/badge.svg)

# cpp_modules

This repository is a collection of experimental tools/libraries that can be used to build C++ modules with CMake's Ninja or MSBuild generators and Clang as the compiler on Linux or MSVC/ClangCl/Clang as the compiler on Windows. The tools use a patched clang-scan-deps to scan for module dependencies and the results are stored in an LMDB embedded store for incremental builds. Binary installers are available for Windows and Linux.

## Example

A.m.cpp:
```c++
module;
#include <stdio.h>
export module A;
export void foo() {
	puts("hello ");
}
```
B.h
```c++
#include <stdio.h>
inline void bar() {
	puts("world!");
}
```
main.cpp:
```c++
import A;

#ifdef __clang__
import <B.h>;
#else
#include <B.h>
#endif

int main() {
	foo();
	bar();
	return 0;
}
```
CMakeLists.txt:
```cmake
cmake_minimum_required(VERSION 3.15)
project(test LANGUAGES CXX)

find_package(cpp_modules REQUIRED)

add_executable(test main.cpp A.m.cpp)
target_include_directories(test PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_cpp_modules(test)
target_cpp_header_units(test B.h)
```
how to build it (after installing the cpp_modules CMake extension):
* with ninja / clang-9 on Ubuntu
```bash
export CXX=clang++-9; export CC=clang-9;
export CXXFLAGS="-stdlib=libc++"; export LDFLAGS="-stdlib=libc++ -fuse-ld=lld";
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ../
cmake --build . --parallel
```
* with ninja / msvc (in a Developer Command Prompt or after running vcvarsall.bat)
``` bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ../
cmake --build . --parallel
```
* with msbuild / msvc (in a Developer Command Prompt or after running vcvarsall.bat)
```bash
mkdir build && cd build
cmake -G "Visual Studio 16 2019" -A "X64" ../
cmake --build . --parallel --config Debug
```
* with ninja / clang-cl (in a Developer Command Prompt or after running vcvarsall.bat)
```bash
mkdir build && cd build
set PATH=%PATH%;C:\Program Files\LLVM\bin
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang-cl.exe ../
cmake --build . --parallel
```
See `/tests/` for examples with more modules, header units, generated headers/sources.

## Installing on Windows

Prerequisites:

* CMake 3.15.4+
* Visual Studio 2019 16.4+ with the C++ standard library modules component enabled in the installer
* Clang 9+ for building with Clang/ClangCl, e.g a [snapshot build](https://llvm.org/builds/)

Download the binary installer from ... and run `cpp_modules-0.0.1-win32.exe /S /D=C:\Program Files\cpp_modules`.

## Installing on Linux

Prerequisities:

* CMake 3.15.4+
* Clang 9+

Download the binary installer from ... and run `sudo ./cpp_modules-0.0.1-Linux.sh --skip-license` which will install the files to `/usr/local/cpp_modules`. Currently it'll likely need some help to find the standard library headers, so if e.g you installed the `clang-9` Ubuntu package then:
```bash
sudo mkdir -p /usr/local/cpp_modules/lib/clang/10.0.0
sudo ln -s /usr/lib/llvm-9/lib/clang/9*/include /usr/local/cpp_modules/lib/clang/10.0.0/include
```

## How does this work ?

The general approach here is to get CMake to build C++ modules without changing CMake. This requires more elaborate customizations for the generated build systems, but the advantage is that the MSBuild customization can be used to build modules without CMake, and while the Ninja fork currently contains some CMake specific code, it should be easier to get it to work with other Ninja generators.

For the MSBuild generator a build customization overrides the MSBuild target which builds the sources with a custom C# task. The incremental scanning is done by a C++ library with a C++/CLI wrapper DLL referenced by the task. The task generates manifest files for Ninja to execute the build commands.

For the Ninja generator a Ninja fork is used instead of the one already installed on the system. Targets that generate headers are built first, then everything else is scanned with the same C++ incremental scanner library, and then the build graph and the commands are modified directly. The Ninja internals are otherwise mostly untouched, so it might be possible to factor this into a kind of plugin for Ninja.

## Known Issues

None of the compilers implement C++20 modules completely yet, so you can expect some bugs.

At the moment clang-scan-deps only really supports clang, the same version that it was built with, but it seems to work with clang-cl/msvc as long as the module dependency information doesn't depend on the compiler that will build the modules. On linux it has some difficulty finding the path to standard library headers, but a workaround with adding a symlink to them seems to work. The patch for clang-scan-deps disables building BMIs for header units while scanning for dependencies, but then the header units are treated as regular headers while scanning so it doesn't quite have proper isolation semantics yet.

The ninja extension is currently faster and better tested than the MSBuild extension.

Since currently the targets that generate headers are built before scanning, those targets cannot use modules.