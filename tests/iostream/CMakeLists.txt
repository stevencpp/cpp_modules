cmake_minimum_required(VERSION 3.16)
project(test_iostream LANGUAGES CXX)

set(NUM_FILES 25 CACHE STRING "")

set(USE_HEADERS OFF)
set(USE_PCH OFF)
set(USE_HEADER_UNITS OFF)
set(USE_MODULES OFF)

if(USE STREQUAL "HEADERS") # include iostream as a regular header
	set(USE_HEADERS ON)
elseif(USE STREQUAL "PCH") # use precompiled headers for iostream
	set(USE_PCH ON)
elseif(USE STREQUAL "HEADER_UNITS") # import iostream as a header unit
	set(USE_HEADER_UNITS ON)
elseif(USE STREQUAL "MODULES") # import a named module for iostream
	set(USE_MODULES ON)
else()
	message(FATAL_ERROR "-DUSE must be either PCH/HEADER_UNITS/MODULES/HEADERS")
endif()

message(STATUS "vars: ${USE_HEADERS} ${USE_PCH} ${USE_HEADER_UNITS} ${USE_MODULES}")

add_executable(A main.cpp)

set(BINDIR "${CMAKE_CURRENT_BINARY_DIR}")
foreach(index RANGE 1 ${NUM_FILES})
	file(COPY template.cpp DESTINATION "${BINDIR}")
	file(RENAME "${BINDIR}/template.cpp" "${BINDIR}/A${index}.cpp")
	target_sources(A PRIVATE "${BINDIR}/A${index}.cpp")
endforeach()

if(USE_HEADER_UNITS OR USE_MODULES)
	find_package(cpp_modules REQUIRED)
	
	target_cpp_modules(A)

	if(USE_HEADER_UNITS)
		find_file(iostream_path "iostream")
		#message(STATUS "iostream_path: ${iostream_path}")
		target_cpp_header_units(A "${iostream_path}")
		target_compile_definitions(A PRIVATE USE_HEADER_UNITS)
	endif()

	if(USE_MODULES)
		target_sources(A PRIVATE "iostream.cpp")
		target_compile_definitions(A PRIVATE USE_MODULES)
	endif()
endif()

if(USE_PCH)
	target_precompile_headers(A PRIVATE "<iostream>")
	target_compile_definitions(A PRIVATE USE_PCH)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT A)
