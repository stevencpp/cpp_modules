cmake_minimum_required(VERSION 3.13)
project(test_concurrent LANGUAGES CXX)

find_package(cpp_modules REQUIRED)

add_executable(A A.cpp)
target_link_libraries(A B1 B2 B3 B4 B5 B6 B7 B8)

add_library(B1 STATIC B1.cpp)
target_link_libraries(B1 C)
add_library(B2 STATIC B2.cpp)
target_link_libraries(B2 C)
add_library(B3 STATIC B3.cpp)
target_link_libraries(B3 C)
add_library(B4 STATIC B4.cpp)
target_link_libraries(B4 C)
add_library(B5 STATIC B5.cpp)
target_link_libraries(B5 C)
add_library(B6 STATIC B6.cpp)
target_link_libraries(B6 C)
add_library(B7 STATIC B7.cpp)
target_link_libraries(B7 C)
add_library(B8 STATIC B8.cpp)
target_link_libraries(B8 C)

add_library(C STATIC C.cpp)

target_cpp_modules(A B1 B2 B3 B4 B5 B6 B7 B8 C)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT A)