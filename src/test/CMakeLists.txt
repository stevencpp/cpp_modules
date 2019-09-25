cmake_minimum_required(VERSION 3.13)

# ==== test driver ====

find_package(OpenMP)
find_package(fmt CONFIG REQUIRED)
find_package(lmdb CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
#find_package(absl CONFIG REQUIRED)

#set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(cppm_test
	main.cpp
	scanner_test.cpp
	depinfo.cpp
	util.h
	test_config.h
)

set_property(TARGET cppm_test PROPERTY CXX_STANDARD 20)
set_property(TARGET cppm_test PROPERTY VS_GLOBAL_LLVMInstallDir "C:\\Program Files\\LLVM")

target_link_libraries(cppm_test
	PRIVATE cppm_scanner
	PRIVATE fmt::fmt
	PRIVATE lmdb
	#PRIVATE ntdll.lib
	#PRIVATE OpenMP::OpenMP_CXX
	PRIVATE Catch2::Catch2
	#PRIVATE absl::flat_hash_map absl::hashtablez_sampler
)