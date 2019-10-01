add_executable(cpp_modules main.cpp)

set_property(TARGET cpp_modules PROPERTY CXX_STANDARD 17)

target_link_libraries(cpp_modules)

install(TARGETS cpp_modules
	RUNTIME DESTINATION bin
)

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
	
	install(FILES
		../build_rules/cpp_modules-config.cmake
		DESTINATION cmake
	)
endif()

add_subdirectory(scanner)
add_subdirectory(test)
#add_subdirectory(ninja)