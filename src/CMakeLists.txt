cmake_minimum_required(VERSION 3.15)

include(CPack)

set(CPPM_SCANNER_PATH "" CACHE FILEPATH "path to the patched clang-scan-deps executable")
if(NOT EXISTS "${CPPM_SCANNER_PATH}")
	message(FATAL_ERROR "CPPM_SCANNER_PATH ('${CPPM_SCANNER_PATH}') must point to the patched clang-scan-deps executable")
endif()
message(STATUS "using the scanner at '${CPPM_SCANNER_PATH}'")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	if(WIN32)
		set(CMAKE_INSTALL_PREFIX "$ENV{PROGRAMFILES}/cpp_modules" CACHE PATH "..." FORCE)
	else()
		set(CMAKE_INSTALL_PREFIX "/usr/local/cpp_modules" CACHE PATH "..." FORCE)
	endif()
endif()

if(WIN32)
	set(CPACK_VERBATIM_VARIABLES ON CACHE BOOL "..." FORCE)
	set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64" CACHE STRING "..." FORCE)
	set(CPACK_PACKAGE_INSTALL_DIRECTORY "cpp_modules" CACHE PATH "..." FORCE)
	message(STATUS "CPACK_NSIS_INSTALL_ROOT is '${CPACK_NSIS_INSTALL_ROOT}'")
else()

endif()

if(WIN32)
	install(FILES 
		../build_rules/cpp_modules.props
		../build_rules/cpp_modules.targets
		../build_rules/cpp_modules.cs
		../build_rules/cpp_modules.xml
		../build_rules/dummy.cpp
		DESTINATION etc
	)
	
	install(FILES
		../build_rules/Newtonsoft.Json.dll
		DESTINATION bin
	)
endif()

install(FILES ../build_rules/cpp_modules-config.cmake DESTINATION cmake)
install(PROGRAMS "${CPPM_SCANNER_PATH}" DESTINATION bin)

add_subdirectory(utils)
add_subdirectory(scanner)
add_subdirectory(test)
#add_subdirectory(ninja)