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

struct ConfigString : public ConfigVar {
	std::string var;

	ConfigString(const char* name, const std::string& default_value, const char* description = "") 
		: ConfigVar { name, description }, var(default_value)
	{
		TestConfig::instance()->strings.push_back(this);
	}

	operator const char* () {
		return var.c_str();
	}
	operator std::string_view() {
		return var;
	}
	operator std::string& () {
		return var;
	}
	bool empty() const {
		return var.empty();
	}
};