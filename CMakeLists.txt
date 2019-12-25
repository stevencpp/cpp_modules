cmake_minimum_required(VERSION 3.15)

set(CMAKE_VS_INCLUDE_INSTALL_TO_DEFAULT_BUILD 1)
project(cpp_modules VERSION 0.0.1 LANGUAGES CXX)

if(NOT CMAKE_TOOLCHAIN_FILE)
	message(FATAL_ERROR "The dependencies need to be installed with a vcpkg toolchain file, see README.md.")
endif()

option(BOOTSTRAP OFF)
if(BOOTSTRAP)
	find_package(cpp_modules REQUIRED)
endif()

add_subdirectory(src)