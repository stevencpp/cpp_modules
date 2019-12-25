cmake_minimum_required(VERSION 3.15)
project(test_header_units LANGUAGES CXX)

find_package(cpp_modules REQUIRED)

add_executable(A
	A.cpp
	B.cpp
	C.m.cpp
	D.m.cpp
	F.cpp
)

target_cpp_modules(A)

target_cpp_header_units(A
	A.hpp
	B.hpp
	C.hpp
	D1.hpp
	E.hpp
	F.hpp
)

target_include_directories(A PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT A)