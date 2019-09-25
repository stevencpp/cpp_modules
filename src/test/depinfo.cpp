#include <catch2/catch.hpp>
#include "nl_json_depinfo.h"

using namespace depinfo;

TEST_CASE("depinfo serialize/deserialize", "[depinfo]") {
	SECTION("no optionals") {
		std::string str = R"( { 
			"version" : 1,
			"work_directory" : "",
			"sources" : []
		} )";
		auto j_in = json::parse(str);
		auto format = j_in.get<DepFormat>();
		CHECK(format == DepFormat { .version = 1, .revision = 0, .work_directory = "", .sources = {} });
		json j_out = format;
		//CHECK(j_out == j_in); // todo: revision - optional default value
		//CHECK(j_in.dump() == j_out.dump());
	}
	SECTION("bit more") {
		std::string str = R"( { 
			"version" : 3, "revision" : 2, "work_directory" : "c:/home/work", "sources" : [
				{ "input" : "a.cpp" }, { "input" : "b.cpp" }
			]
		} )";
		auto format = json::parse(str).get<DepFormat>();
		CHECK(format == DepFormat { .version = 3, .revision = 2, .work_directory = "c:/home/work", .sources = {
			{.input = "a.cpp" }, {.input = "b.cpp"}
		} });
	}
	SECTION("with modules") {
		std::string str = R"( { "version" : 1,  "work_directory" : "", "sources" : [
			{
				"input" : "a.cpp",
				"outputs" : [ "a.obj" ],
				"depends" : ["a.h"],
				"future_compile" : {
					"output" : [ "a.bmi" ],
					"provide" : [ { "logical_name" : "a" } ],
					"require" : [ { "logical_name" : "b" }, { "logical_name" : "c" } ]
				}
			}
		] } )";
		using vdb = std::vector<DataBlock>;
		using vmd = std::vector<ModuleDesc>;
		using fdi = FutureDepInfo;
		auto j_in = json::parse(str);
		auto format = j_in.get<DepFormat>();
		CHECK(format == DepFormat { .sources = {
			{
				.input = "a.cpp",
				.outputs = vdb{ "a.obj" },
				.depends = vdb{ "a.h" },
				.future_compile = fdi{
					.output = vdb{ "a.bmi" },
					.provide = vmd{ { .logical_name = "a" } },
					.require = vmd{ { .logical_name = "b" }, { .logical_name = "c"} }
				}
			}
		} });
		json j_out = format;
		//CHECK(j_out == j_in); // todo: ..
	}
	SECTION("raw and indexed") {
		std::string str = R"( { "version" : 1, "work_directory" : "", "sources" : [
			{ "input" : "a.cpp", "depends" : [ 
				{ "format" : "raw8", "code_units" : [5,6,7,8] },
				"a.h",
				[1, "long/path/to/b.h" ],
				[2, "long/path/to/c.h" ],
				[3, { "format" : "raw8", "code_units" : [8,9,10] }]
			]},	{ "input" : "b.cpp", "depends" : [
				1, 2, "d.h", 3
			]}
		] } )";
	}
}