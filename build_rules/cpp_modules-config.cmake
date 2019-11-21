cmake_minimum_required(VERSION 3.13)

macro(required_find_common var name)
	if(NOT ${var})
		message(FATAL_ERROR "${type} ${name} not found")
	endif()
endmacro()
macro(required_find_path var name)
	find_path(${var} ${name} ${ARGN})
	required_find_common(${var} ${name})
endmacro()
macro(required_find_library var name)
	find_library(${var} ${name} ${ARGN})
	required_find_common(${var} ${name})
endmacro()
macro(required_find_program var name)
	find_program(${var} ${name} ${ARGN})
	required_find_common(${var} ${name})
endmacro()

if(NOT DEFINED CPPM_SCANNER_PATH)
	required_find_program(CPPM_SCANNER_PATH clang-scan-deps
		HINTS "C:/Program Files/LLVM/bin" "/usr/local/cpp_modules/bin"
		DOC "path to the patched clang-scan-deps executable")
endif()
message(STATUS "using the scanner at '${CPPM_SCANNER_PATH}'")

if(CMAKE_GENERATOR MATCHES "Visual Studio")
	required_find_path(CPPM_TARGETS_PATH cpp_modules.targets
		HINTS "${CMAKE_CURRENT_LIST_DIR}/../etc" DOC "path containing cpp_modules.targets and its dependencies")
endif()
if(CMAKE_GENERATOR MATCHES "Ninja")
	set(CMAKE_MAKE_PROGRAM "${CMAKE_CURRENT_LIST_DIR}/../etc/ninja" CACHE FILEPATH "" FORCE)
	file(WRITE "${CMAKE_BINARY_DIR}/scanner_config.txt" "tool_path ${CPPM_SCANNER_PATH}")
endif()

function(target_cpp_modules targets)
	foreach(target ${ARGV})
		set_property(TARGET ${target} PROPERTY CXX_STANDARD 20)
		set_property(TARGET ${target} PROPERTY CXX_STANDARD_REQUIRED ON)
	endforeach()
	
	if(CMAKE_GENERATOR MATCHES "Visual Studio")
		foreach(target ${ARGV})
			#the following doesn't set EnableModules and so /module:stdifcdir is also not set:
			#target_compile_options(${target} PRIVATE "/experimental:module") 
			#so use a property sheet instead to set EnableModules:
			set_property(TARGET ${target} PROPERTY VS_USER_PROPS "${CPPM_TARGETS_PATH}/cpp_modules.props")
			set_property(TARGET ${target} PROPERTY VS_GLOBAL_LLVMInstallDir "C:\\Program Files\\LLVM")
			set_property(TARGET ${target} PROPERTY VS_GLOBAL_CppM_ClangScanDepsPath "${CPPM_SCANNER_PATH}")

			target_link_libraries(${target}
				${CPPM_TARGETS_PATH}/cpp_modules.targets
			)
		endforeach()
	
		add_library(_CPPM_ALL_BUILD EXCLUDE_FROM_ALL ${CPPM_TARGETS_PATH}/dummy.cpp)
		set_property(TARGET _CPPM_ALL_BUILD PROPERTY EXCLUDE_FROM_DEFAULT_BUILD TRUE)
		add_dependencies(_CPPM_ALL_BUILD ${ARGV})
		target_link_libraries(_CPPM_ALL_BUILD ${CPPM_TARGETS_PATH}/cpp_modules.targets)
	endif()
endfunction()

function(target_cpp_legacy_headers target)
	set(headers ${ARGN})
	target_sources(${target} PRIVATE ${headers})
	list(TRANSFORM headers PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
	set_property(TARGET ${target} PROPERTY VS_GLOBAL_CppM_Legacy_Headers ${headers})
endfunction()