[![Build status](https://ci.appveyor.com/api/projects/status/4gexbm58k239kd5n/branch/master?svg=true)](https://ci.appveyor.com/project/stevencpp/cpp-modules/branch/master)

# cpp_modules

### 1. What is this ?

As a proof of concept, this build customization provides partial C++ modules support for MSBuild. Once enabled (either from the IDE or using a CMake generator), it scans the source files for modules and builds them in the right order with the right flags. It can build the modules with either MSVC or Clang, using the toolchain selector from the IDE. Scanning and building are per-source incremental.

### 2. Usage Example

```
find_package(cpp_modules REQUIRED)
add_executable(A A.cpp)
target_cpp_modules(A)
```

See `/tests/` for more CMake examples.

From the IDE the customization can be added to an existing MSBuild project via Project->Build Customizations->Find Existing and selecting the installed `cpp_modules.targets` file.

### 3. How does this work ?

The build customization overrides the MSBuild target which builds the sources with a custom C# task. The incremental scanning is done by a C++ component with a C++/CLI wrapper DLL referenced by the task. A patched clang-scan-deps is used to extract the dependencies. Information for incremental scanning (targets, paths, file write times, item command hashes, dependencies, scan results) is persisted in an LMDB database. The task generates manifest files for Ninja to execute the build commands.

### 4. How to build and install it ?

- get the latest preview of Visual Studio 2019 and enable the C++ Modules component in the installer
- to build modules with clang-cl, get a [snapshot build of llvm](https://llvm.org/builds/)
- `vcpkg install fmt:x86-windows lmdb:x86-windows nlohmann-json:x86-windows catch2:x86-windows range-v3:x86-windows`
- download a [patched clang-scan-deps.exe](https://drive.google.com/uc?export=download&id=1xHRCY_eF5uPrW2kNMj1LyXeCCfFFVNia) to `C:\Program Files\LLVM\bin`
- generate Win32 VS solutions for this repo with CMake and the vcpkg toolchain file
- build and install the configuration to `C:\Program Files\cpp_modules`