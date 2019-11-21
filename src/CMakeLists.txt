cmake_minimum_required(VERSION 3.15)

add_executable(cpp_modules main.cpp)

set_property(TARGET cpp_modules PROPERTY CXX_STANDARD 17)

target_link_libraries(cpp_modules)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(WIN32)
    set(CMAKE_INSTALL_PREFIX "C:/Program Files/cpp_modules" CACHE PATH "..." FORCE)
  else()
    set(CMAKE_INSTALL_PREFIX "/usr/local/cpp_modules" CACHE PATH "..." FORCE)
  endif()
endif()

if(WIN32)
	install(FILES 
		../build_rules/cpp_modules.props
		../build_rules/cpp_modules.targets
		../build_rules/cpp_modules.cs
		../build_rules/cpp_modules.xml
		../build_rules/Newtonsoft.Json.dll
		../build_rules/dummy.cpp
		DESTINATION etc
	)
endif()

install(FILES ../build_rules/cpp_modules-config.cmake DESTINATION cmake)

add_subdirectory(utils)
add_subdirectory(scanner)
add_subdirectory(test)
#add_subdirectory(ninja)