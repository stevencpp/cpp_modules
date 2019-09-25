#pragma once

#include <string>
#include <variant>
#include <vector>
#include <optional>

#include "default_comparisons.h"

namespace depinfo
{
	// a vector whose items are unique
	template<typename T>
	using unique_vector = std::vector<T>;

	struct RawDataBlock
	{
		// Storage size of code-units' integers
		// maybe string_enum<"raw8", "raw16"> in c++20 ?
		std::string format;

		// Integer representation of binary values (min: 1)
		std::vector<int> code_units;

		// Readable version of the sequence (min: 1)
		// (purely for human consumption; no semantic meaning)
		std::optional< std::string > readable_name;

		DEFAULT_COMPARISONS(RawDataBlock);
	};

#if 1 // extension
	using IndexedDataBlock = std::pair< std::size_t, std::variant<std::string, RawDataBlock> >;

	// A binary sequence. (for string - min_length: 1)
	using DataBlock = std::variant<std::string, std::size_t, RawDataBlock, IndexedDataBlock>;
#else
	// A binary sequence. (for string - min_length: 1)
	using DataBlock = std::variant<std::string, RawDataBlock>;
#endif

	struct ModuleDesc
	{
		// 
		std::optional< DataBlock > source_path;

		// 
		std::optional< DataBlock > compiled_module_path;

		//
		DataBlock logical_name;

		DEFAULT_COMPARISONS(ModuleDesc);
	};

	// Files output by a future rule for this source using the same flags
	struct FutureDepInfo
	{
		// Files output by a future rule for this source using the same flags
		std::optional< unique_vector<DataBlock> > output; // changed from outputs

		// Modules provided by a future compile rule for this source using the same flags
		std::optional< unique_vector<ModuleDesc> > provide; // changed from provides

		// Modules required by a future compile rule for this source using the same flags
		std::optional< unique_vector<ModuleDesc> > require; // changed from requires (keyword in C++20)

		DEFAULT_COMPARISONS(FutureDepInfo);
	};

	// Dependency information for a source file
	struct DepInfo
	{
		// 
		DataBlock input;

		// Files that will be output by this execution
		std::optional< unique_vector<DataBlock> > outputs;

		// Paths read during this execution
		std::optional< unique_vector<DataBlock> > depends;

		//
		std::optional< FutureDepInfo > future_compile;

		DEFAULT_COMPARISONS(DepInfo);
	};

	// SG15 TR depformat
	struct DepFormat {
		// The version of the output specification
		int version = 1;

		// The revision of the output specification
		std::optional< int > revision = 0;

		//
		DataBlock work_directory;

		// (min: 1)
		std::vector<DepInfo> sources;

		DEFAULT_COMPARISONS(DepFormat);
	};
}