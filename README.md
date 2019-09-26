[![Build status](https://ci.appveyor.com/api/projects/status/4gexbm58k239kd5n/branch/master?svg=true)](https://ci.appveyor.com/project/stevencpp/cpp-modules/branch/master)

# cpp_modules

### 1. What is this ?

As a proof of concept, this build customization provides partial C++ modules support for MSBuild. Once enabled (either from the IDE or using a CMake generator), it scans the source files for modules and builds them in the right order with the right flags.

### 2. Usage Example

```
find_package(cpp_modules REQUIRED)
add_executable(A A.cpp)
target_cpp_modules(A)
```

See `/tests/` for more CMake examples.

From the IDE the customization can be added to an existing MSBuild project via Project->Build Customizations->Find Existing and selecting the installed `cpp_modules.targets` file.

### 3. How does this work ?

A patched clang-scan-deps is used to extract the dependencies. Information for incremental scanning is persisted in an LMDB database. Ninja is used to execute the build commands.

### 4. How to build and install it ?

- get the latest preview of Visual Studio 2019 and enable the C++ Modules component in the installer
- `vcpkg install fmt:x86-windows lmdb:x86-windows nlohmann-json:x86-windows catch2:x86-windows`
- install a patched clang-scan-deps.exe to `C:\Program Files\LLVM`
- generate Win32 VS solutions for this repo with CMake and the vcpkg toolchain file
- build (the tool will automatically be installed to `C:\Program Files\cpp_modules` after the build finishes)