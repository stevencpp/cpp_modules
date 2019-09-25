#pragma once

#include<vcclr.h>

namespace depinfo_cs
{
	using namespace System;
	using namespace System::Collections::Generic;
	using namespace System::Runtime::Serialization;

	using DataBlock = String;

	public ref struct RawDataBlock
	{
		// Storage size of code-units' integers
		String^ format;

		// Integer representation of binary values (min: 1)
		List<int>^ code_units;

		// Readable version of the sequence (min: 1)
		// (purely for human consumption; no semantic meaning)
		[OptionalField]
		String^ readable_name;
	};

	// A binary sequence. (for string - min_length: 1)

	public ref struct ModuleDesc
	{
		// 
		[OptionalField]
		DataBlock^ source_path;

		// 
		[OptionalField]
		DataBlock^ compiled_module_path;

		//
		DataBlock^ logical_name;
	};

	// Files output by a future rule for this source using the same flags
	public ref struct FutureDepInfo
	{
		// Files output by a future rule for this source using the same flags
		[OptionalField]
		List<DataBlock^>^ outputs;

		// Modules provided by a future compile rule for this source using the same flags
		[OptionalField]
		List<ModuleDesc^>^ provides;

		// Modules required by a future compile rule for this source using the same flags
		[OptionalField]
		List<ModuleDesc^>^ requires;
	};

	// Dependency information for a source file
	public ref struct DepInfo
	{
		// 
		DataBlock^ input;

		// Files that will be output by this execution
		[OptionalField]
		List<DataBlock^>^ outputs;

		// Paths read during this execution
		[OptionalField]
		List<DataBlock^>^ depends;

		//
		[OptionalField]
		FutureDepInfo^ future_compile;
	};

	// SG15 TR depformat
	public ref struct DepFormat {
		// The version of the output specification
		int version = 1;

		// The revision of the output specification
		[OptionalField]
		int revision = 0;

		//
		DataBlock^ work_directory;

		// (min: 1)
		List<DepInfo^>^ sources;
	};
}