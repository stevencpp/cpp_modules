cmake_minimum_required(VERSION 3.15)

# ==== misc. utilities ====

add_library(cppm_utils
	cmd_line_utils.cpp
	cmd_line_utils.h
)

target_include_directories(cppm_utils
	INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

set_property(TARGET cppm_utils PROPERTY CXX_STANDARD 17)
set_property(TARGET cppm_utils PROPERTY VS_GLOBAL_LLVMInstallDir "C:\\Program Files\\LLVM")