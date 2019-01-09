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

required_find_path(CPPM_TARGETS_PATH cpp_modules.targets
	HINTS "${CMAKE_CURRENT_LIST_DIR}/../etc" DOC "path containing cpp_modules.targets and its dependencies")

function(target_cpp_modules targets)
	foreach(target ${ARGV})
		set_property(TARGET ${target} PROPERTY CXX_STANDARD 17)
		#the following doesn't set EnableModules and so /module:stdifcdir is also not set:
		#target_compile_options(${target} PRIVATE "/experimental:module") 
		#so use a property sheet instead to set EnableModules:
		set_property(TARGET ${target} PROPERTY VS_USER_PROPS ${CPPM_TARGETS_PATH}/cpp_modules.props)

		target_link_libraries(${target}
			${CPPM_TARGETS_PATH}/cpp_modules.targets
		)
	endforeach()
endfunction()