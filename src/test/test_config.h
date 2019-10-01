#pragma once

#include <string>
#include <string_view>

struct ConfigString;

struct TestConfig {

	std::vector<ConfigString*> strings;

	static TestConfig* instance() {
		static TestConfig test_config;
		return &test_config;
	}
};

struct ConfigVar {
	const char* name;
	const char* description;
};

struct ConfigString : public std::string, ConfigVar {

	ConfigString(const char* name, const std::string& default_value, const char* description = "") 
		: ConfigVar { name, description }, std::string(default_value)
	{
		TestConfig::instance()->strings.push_back(this);
	}
	std::string& str() {
		return *static_cast<std::string*>(this);
	}
};