#pragma once

#include "depinfo.h"
#include "nl_json_schema.h"

namespace depinfo {
	//using namespace nl_json_schema;

	static inline void to_from_json(RW<RawDataBlock> rd) {
		rd.json_schema(
			rd->format, "format",
			rd->code_units, "code_units",
			rd->readable_name, "readable_name"
		);
	}

	static inline void to_from_json(RW<ModuleDesc> m) {
		m.json_schema(
			m->source_path, "source_path",
			m->compiled_module_path, "compiled_module_path",
			m->logical_name, "logical_name"
		);
	}

	static inline void to_from_json(RW<FutureDepInfo> f) {
		f.json_schema(
			f->output, "output",
			f->provide, "provide",
			f->require, "require"
		);
	}

	static inline void to_from_json(RW<DepInfo> i) {
		i.json_schema(
			i->input, "input",
			i->outputs, "outputs",
			i->depends, "depends",
			i->future_compile, "future_compile"
		);
	}

	static inline void to_from_json(RW<DepFormat> f) {
		f.json_schema(
			f->version, "version",
			f->revision, "revision",
			f->work_directory, "work_directory",
			f->sources, "sources"
		);
	}

#if __cpp_concepts
	template<typename T>
	concept ToFromAble = requires(json & j, const T & t) {
		to_from_json(RW { j, t });
	}&& requires(const json& j, T& t) {
		to_from_json(RW { j, t });
	};

	template<ToFromAble T>
	void to_json(json& j, const T& t) {
		to_from_json(RW { j, t });
	}

	template<ToFromAble T>
	void from_json(const json& j, T& t) {
		to_from_json(RW { j, t });
	}
#else
#define TO_FROM_JSON(CLASS) \
	void from_json(const json& j, CLASS& t) { to_from_json(RW { j, t }); } \
	void to_json(json& j, const CLASS& t) { to_from_json(RW { j, t }); }

	TO_FROM_JSON(RawDataBlock);
	TO_FROM_JSON(ModuleDesc);
	TO_FROM_JSON(FutureDepInfo);
	TO_FROM_JSON(DepInfo);
	TO_FROM_JSON(DepFormat);
#endif

}