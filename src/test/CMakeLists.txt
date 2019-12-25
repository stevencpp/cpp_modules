cmake_minimum_required(VERSION 3.15)

# ==== test driver ====

find_package(OpenMP)
find_package(fmt CONFIG REQUIRED)
find_package(lmdb CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
#find_package(absl CONFIG REQUIRED)
find_package(range-v3 CONFIG REQUIRED)
find_package(reproc CONFIG REQUIRED)

enable_language(C) # for reproc

#set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(cppm_test
	main.cpp
	scanner_test.cpp
	depinfo.cpp
	msbuild.cpp
	lmdb.cpp
	gen_ninja.cpp
	util.h
	test_config.h
	temp_file_test.h
	system_test.h
	system_test.cpp
)

set_property(TARGET cppm_test PROPERTY CXX_STANDARD 20)
set_property(TARGET cppm_test PROPERTY VS_GLOBAL_LLVMInstallDir "C:\\Program Files\\LLVM")

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	# C++20 mode enables /permissive- which causes https://github.com/abseil/abseil-cpp/issues/357
	set_property(SOURCE lmdb.cpp PROPERTY COMPILE_FLAGS "/std:c++17 /permissive")
endif()

target_link_libraries(cppm_test
	PRIVATE cppm_scanner
	PRIVATE cppm_utils
	PRIVATE fmt::fmt
	PRIVATE lmdb
	#PRIVATE ntdll.lib
	#PRIVATE OpenMP::OpenMP_CXX
	PRIVATE Catch2::Catch2
	#PRIVATE absl::flat_hash_map absl::hashtablez_sampler
	PRIVATE meta concepts range-v3
	PRIVATE reproc
)