cmake_minimum_required(VERSION 3.15)
project(test_dag LANGUAGES CXX)

find_package(cpp_modules REQUIRED)

add_executable(A
	A1.cpp
	A2.cpp
	A3.cpp
	A4.cpp
)

target_link_libraries(A B C)

add_library(B STATIC
	B1.cpp
	B2.cpp
	B3.cpp
	B4.cpp
)

target_link_libraries(B C D)

add_library(C STATIC
	C1.cpp
	C2.cpp
	C3.cpp
	C4.cpp
)

target_cpp_legacy_headers(C
	C5.h
)

target_link_libraries(C D)

add_library(D STATIC
	D1.cpp
	D2.cpp
	D3.cpp
	D4.cpp
)

if(CMAKE_GENERATOR_TOOLSET STREQUAL "ClangCl")
	target_sources(D PRIVATE std_core.cpp)
endif()

target_cpp_legacy_headers(D
	D5.h
	D6.h
)

foreach(T A B C D)
	target_include_directories(${T} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

target_cpp_modules(A B C D)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT A)