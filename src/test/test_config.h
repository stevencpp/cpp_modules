#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

struct ConfigString;

struct TestConfig {

	std::unordered_map<std::string, ConfigString*> strings;

	static TestConfig* instance() {
		static TestConfig test_config;
		return &test_config;
	}
};

struct ConfigVar {
	const char* name;
	const char* description;
	std::vector<ConfigString*> alts;
};

struct ConfigString : public std::string, ConfigVar {

	ConfigString(const char* name, const std::string& default_value, const char* description = "") 
		: ConfigVar { name, description }, std::string(default_value)
	{
		auto [itr,inserted] = TestConfig::instance()->strings.try_emplace(name, this);
		if (!inserted)
			itr->second->alts.push_back(this);
	}
	std::string& str() {
		return *static_cast<std::string*>(this);
	}
};