cmake_minimum_required(VERSION 3.13)
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

target_link_libraries(C D)

add_library(D STATIC
	D1.cpp
	D2.cpp
	D3.cpp
	D4.cpp
)

set_property(TARGET A B C D PROPERTY CXX_STANDARD 17)

target_cpp_modules(A B C D)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT A)