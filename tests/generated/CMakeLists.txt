cmake_minimum_required(VERSION 3.15)
project(test_generated LANGUAGES CXX)

find_package(cpp_modules REQUIRED)

add_executable(gen gen.cpp)

add_custom_command(
	OUTPUT B.h B.cpp C.m.cpp
	COMMAND gen
	DEPENDS gen
)

add_executable(A
	A.cpp
	"${CMAKE_CURRENT_BINARY_DIR}/B.cpp"
	"${CMAKE_CURRENT_BINARY_DIR}/B.h"
	"${CMAKE_CURRENT_BINARY_DIR}/C.m.cpp"
)

target_include_directories(A PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

target_cpp_modules(A gen)
target_cpp_header_units(A "${CMAKE_CURRENT_BINARY_DIR}/B.h")

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT A)