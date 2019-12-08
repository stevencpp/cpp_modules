cmake_minimum_required(VERSION 3.15)

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(lmdb CONFIG REQUIRED)
find_package(reproc CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)

include(FetchContent)

FetchContent_Declare(
  ninja
  GIT_REPOSITORY https://github.com/stevencpp/ninja.git
  GIT_TAG        7d1bc08f61a6a29183c79dce87e720d51c2dd419
)

FetchContent_MakeAvailable(ninja)

#set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ==== main library ====
add_library(cppm_scanner
	scanner.cpp
	scanner.h
	depinfo.h
	lmdb_wrapper.h
	lmdb_wrapper_impl.h
	lmdb_string_store.h
	lmdb_path_store.h
	strong_id.h
	multi_buffer.h
	nl_json_depinfo.h
	nl_json_schema.h
	default_comparisons.h
	file_time.h
	file_time.cpp
	span.hpp
	trace.h
	module_cmdgen.cpp
	module_cmdgen.h
)

target_link_libraries(cppm_scanner
	PRIVATE nlohmann_json nlohmann_json::nlohmann_json
	PRIVATE fmt::fmt
	PRIVATE lmdb
	PRIVATE absl::flat_hash_map absl::hashtablez_sampler
	PRIVATE cppm_utils
)

target_include_directories(cppm_scanner
	INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
#target_compile_options(cppm_scanner PRIVATE "/permissive-") # commented because MSVC bugs :(
endif()

set_property(TARGET cppm_scanner PROPERTY CXX_STANDARD 17)
set_property(TARGET cppm_scanner PROPERTY VS_GLOBAL_LLVMInstallDir "C:\\Program Files\\LLVM")

target_link_libraries(libninja PRIVATE cppm_scanner PRIVATE cppm_utils PRIVATE fmt::fmt)
target_link_libraries(ninja PRIVATE cppm_scanner)
target_compile_definitions(libninja PRIVATE "WIN32_LEAN_AND_MEAN")

# ==== executable tool ====
add_executable(cppm_scanner_tool
	tool_main.cpp
	gen_ninja.cpp
	gen_ninja.h
)

target_link_libraries(cppm_scanner_tool
	PRIVATE cppm_scanner
	PRIVATE cppm_utils
)

target_link_libraries(cppm_scanner_tool
	PRIVATE nlohmann_json nlohmann_json::nlohmann_json
	PRIVATE fmt::fmt
)

set_property(TARGET cppm_scanner_tool PROPERTY CXX_STANDARD 17)

# ==== C# wrapper ====
if(MSVC)

add_library(cppm_scanner_cs SHARED
	wrapper_cs.cpp
	dll_main.cpp
	depinfo_cs.h
)

target_link_libraries(cppm_scanner_cs
	PRIVATE cppm_scanner
)

set_property(TARGET cppm_scanner_cs PROPERTY COMMON_LANGUAGE_RUNTIME "")
set_property(TARGET cppm_scanner_cs PROPERTY CXX_STANDARD 17)
#set_property(TARGET cppm_scanner_cs PROPERTY DOTNET_TARGET_FRAMEWORK_VERSION "v4.8")

# copy all the DLLs
install(TARGETS cppm_scanner_cs DESTINATION bin)
endif(MSVC)

install(FILES "$<TARGET_FILE:fmt::fmt>" DESTINATION bin)
install(FILES "$<TARGET_FILE:lmdb>" DESTINATION bin)
install(FILES "$<TARGET_FILE:reproc>" DESTINATION bin)
install(TARGETS ninja DESTINATION bin)