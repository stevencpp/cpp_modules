cmake_minimum_required(VERSION 3.13)

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(lmdb CONFIG REQUIRED)
#find_package(absl CONFIG REQUIRED)

#set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ==== main library ====
add_library(cppm_scanner
	scanner.cpp
	scanner.h
	depinfo.h
	lmdb_wrapper.h
	lmdb_wrapper_impl.h
	lmdb_path_store.h
	strong_id.h
	multi_buffer.h
	nl_json_depinfo.h
	nl_json_schema.h
	default_comparisons.h
	file_time.h
	file_time.cpp
	span.hpp
)

target_link_libraries(cppm_scanner
	PRIVATE nlohmann_json nlohmann_json::nlohmann_json
	PRIVATE fmt::fmt
	PRIVATE lmdb
	#PRIVATE absl::flat_hash_map absl::hashtablez_sampler
)

target_include_directories(cppm_scanner
	INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

set_property(TARGET cppm_scanner PROPERTY CXX_STANDARD 17)
set_property(TARGET cppm_scanner PROPERTY VS_GLOBAL_LLVMInstallDir "C:\\Program Files\\LLVM")

# ==== executable tool ====
add_executable(cppm_scanner_tool
	tool_main.cpp
)

target_link_libraries(cppm_scanner_tool
	PRIVATE cppm_scanner
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
install(TARGETS cppm_scanner_cs DESTINATION etc)
install(FILES "$<TARGET_FILE:fmt::fmt>" DESTINATION etc)
install(FILES "$<TARGET_FILE:lmdb>" DESTINATION etc)

endif(MSVC)