#include "scanner.h"
#include "depinfo_cs.h"

#include <vector>
#include<vcclr.h>

// in C++17 we need to use std::codecvt_utf8<wchar_t> which is deprecated
// but in c++20 std::codecv<char8_t, wchar_t> will work
#pragma warning(disable:4996)
#include <codecvt>

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Runtime::InteropServices;

namespace cppm_cs {

public ref struct ScanItem {
	String^ path;
	uint32_t command_idx = 0;
	uint32_t target_idx = 0;
};

#if 0
ref struct PinnedSet {
	array<GCHandle>^ handles;
	int poz = 0;

	PinnedSet(std::size_t nr) {
		handles = gcnew array<GCHandle>(nr);
	}
	std::wstring_view to_sv(String^ str) {
		/*if (System::String::IsNullOrEmpty(str)) {
			return { L"empty", 5 };
		}*/
		GCHandle handle = GCHandle::Alloc(str, GCHandleType::Pinned);
		handles[poz++] = handle;
		IntPtr ptr = handle.AddrOfPinnedObject();
		return { (wchar_t*)ptr.ToPointer(), (std::size_t)str->Length };
	}
	~PinnedSet() {
		for (int i = 0; i < poz; ++i)
			handles[i].Free();
	}
};
#endif

#if 0
struct PinnedSet {
	gcroot<array<GCHandle>^> handles;
	int poz = 0;

	void resize(std::size_t nr) {
		handles = gcnew array<GCHandle>(nr);
	}

	std::wstring_view to_sv(String^ str) {
		/*if (System::String::IsNullOrEmpty(str)) {
			return { L"empty", 5 };
		}*/
		GCHandle handle = GCHandle::Alloc(str, GCHandleType::Pinned);
		handles[poz++] = handle;
		IntPtr ptr = handle.AddrOfPinnedObject();
		return { (wchar_t*)ptr.ToPointer(), (std::size_t)str->Length };
	}
	~PinnedSet() {
		for (int i = 0; i < poz; ++i)
			handles[i].Free();
	}
};
#endif

struct UCS2_to_UTF8_Converter
{
	std::size_t nr_strings = 0;
	std::size_t total_len = 0;

	// the buffer that will store all of the coverted strings
	std::vector<char> buf;
	char* buf_poz = nullptr, * buf_end = nullptr, * buf_mid = nullptr;
	std::codecvt_utf8<wchar_t> conv;

	UCS2_to_UTF8_Converter& reserve(String^ str) {
		if (buf_poz != nullptr)
			throw new std::runtime_error("cannot call reserve after finish_reserve");
		nr_strings++;
		total_len += str->Length;
		return *this;
	}

	void finish_reserve() {
		if (buf_poz != nullptr)
			throw new std::runtime_error("finish_reserve must be called only once");
		if (total_len == 0)
			return;
		constexpr int max_bytes_per_codepoint_utf8 = 4;
		buf.resize(total_len * max_bytes_per_codepoint_utf8);
		buf_poz = &buf[0];
		buf_end = buf_poz + buf.size();
	}

	std::string_view to_sv(String^ str) {
		if (buf_poz == nullptr)
			throw new std::runtime_error("must call finish_reserve first");
		pin_ptr<const wchar_t> wstr = PtrToStringChars(str);
		const wchar_t* wstr_mid = nullptr;
		conv.out(std::mbstate_t {}, wstr, wstr + str->Length, wstr_mid,
			buf_poz, buf_end, buf_mid);
		if ((wstr_mid - wstr) != str->Length)
			throw new std::runtime_error("failed to convert string to UTF-8");
		std::string_view sv_out { buf_poz, std::size_t(buf_mid - buf_poz) };
		buf_poz = buf_mid;
		return sv_out;
	}
};

public ref class DepInfoObserver abstract {
public:
	virtual void on_result(uint32_t item_idx, depinfo_cs::DepInfo^ dep_info, bool out_of_date) = 0;
};

struct DepInfoForwarder : cppm::DepInfoObserver {
	gcroot<List<String^>^> string_table = gcnew List<String^>();
	gcroot<depinfo_cs::DepInfo^> cur_dep_info = nullptr;
	gcroot<cppm_cs::DepInfoObserver^> forward_to;
	cppm::scan_item_idx_t cur_item_idx = {};
	bool cur_item_is_out_of_date = false;
	gcroot<List<ScanItem^>^> items;

	DepInfoForwarder(gcroot<List<ScanItem^>^> items, gcroot<cppm_cs::DepInfoObserver^> forward_to) : 
		items(items), forward_to(forward_to) {}

	String^ get_data(DataBlockView db) {
		String^ ret = nullptr;
		if (auto sv = std::get_if<std::string_view>(&db)) {
			ret = gcnew String(((std::string)*sv).c_str());
		} else if(auto isv = std::get_if<IndexedStringView>(&db)) {
			ret = gcnew String(((std::string)isv->sv).c_str());
			List<String^>^ st = string_table;
			while (((std::size_t)st->Count) <= isv->idx)
				st->Add(nullptr);
			st[isv->idx] = ret;
		} else if(auto idx = std::get_if<std::size_t>(&db)) {
			List<String^>^ st = string_table;
			ret = st[*idx];
		} else if(auto rdb = std::get_if<RawDataBlockView>(&db)) {
			throw gcnew System::Exception("raw datablocks are not supported");
		} else if(auto irdb = std::get_if<IndexedRawDataBlockView>(&db)) {
			throw gcnew System::Exception("raw datablocks are not supported");
		}
		return ret;
	}

	template<typename T>
	T^ ens(T^% t) {
		if (!t) t = gcnew T();
		return t;
	}

	auto get_future_compile() {
		return ens(cur_dep_info->future_compile);
	}

	auto make_named_module(DataBlockView name) {
		auto ret = gcnew depinfo_cs::ModuleDesc();
		ret->logical_name = get_data(name);
		return ret;
	}

	auto make_header_unit(DataBlockView path) {
		auto ret = gcnew depinfo_cs::ModuleDesc();
		ret->source_path = get_data(path);
		return ret;
	}

	void results_for_item(cppm::scan_item_idx_t item_idx, bool out_of_date) override {
		cur_item_idx = item_idx;
		cur_item_is_out_of_date = out_of_date;
		cur_dep_info = gcnew depinfo_cs::DepInfo();
		List<ScanItem^>^ a_items = items;
		cur_dep_info->input = gcnew String(a_items[(std::size_t)item_idx]->path);
	}

	void export_module(DataBlockView name) override {
		ens(get_future_compile()->provides)->Add(
			make_named_module(name)
		);
	}

	void import_module(DataBlockView name) override {
		ens(get_future_compile()->requires)->Add(
			make_named_module(name)
		);
	}

	void include_header(DataBlockView path) override {
		ens(cur_dep_info->depends)->Add(get_data(path));
	}

	void import_header(DataBlockView path) override {
		ens(get_future_compile()->requires)->Add(
			make_header_unit(path)
		);
	}

	void other_file_dep(DataBlockView path) override {
		ens(cur_dep_info->depends)->Add(get_data(path));
	}

	void item_finished() override {
		forward_to->on_result((uint32_t)cur_item_idx, cur_dep_info, cur_item_is_out_of_date);
		cur_dep_info = nullptr;
	}
};

public ref struct ScanItemSet {
	String^ item_root_path = "";
	bool commands_contain_item_path = false;
	List<String^>^ commands = gcnew List<String^>();
	List<String^>^ targets = gcnew List<String^>();
	List<ScanItem^>^ items = gcnew List<ScanItem^>();
};

public ref class Scanner {

	auto convert_commands(UCS2_to_UTF8_Converter& conv, List<String^>^ commands) {
		vector_map<cppm::cmd_idx_t, std::string_view> v_commands;
		v_commands.reserve(cppm::cmd_idx_t { commands->Count });
		for each (String ^ command in commands)
			v_commands.push_back(conv.to_sv(command));
		return v_commands;
	}

	auto convert_targets(UCS2_to_UTF8_Converter& conv, List<String^>^ targets) {
		vector_map<cppm::target_idx_t, std::string_view> v_targets;
		v_targets.reserve(cppm::target_idx_t { targets->Count });
		for each (String ^ name in targets)
			v_targets.push_back(conv.to_sv(name));
		return v_targets;
	}

	auto convert_items(UCS2_to_UTF8_Converter& conv, List<ScanItem^>^ items) {
		vector_map<cppm::scan_item_idx_t, cppm::ScanItemView> v_items;
		v_items.reserve(cppm::scan_item_idx_t { items->Count });
		for each (ScanItem ^ item in items)
			v_items.push_back(cppm::ScanItemView {
				conv.to_sv(item->path),
				cppm::cmd_idx_t { (std::size_t) item->command_idx },
				cppm::target_idx_t { (std::size_t) item->target_idx }
			});
		return v_items;
	}

	void reserve(UCS2_to_UTF8_Converter& conv, ScanItemSet^ scan_items) {
		conv.reserve(scan_items->item_root_path);
		for each (String ^ name in scan_items->targets)
			conv.reserve(name);
		for each (String ^ command in scan_items->commands)
			conv.reserve(command);
		for each (ScanItem ^ item in scan_items->items)
			conv.reserve(item->path);
	}

	cppm::ScanItemSetOwnedView convert(UCS2_to_UTF8_Converter& conv, ScanItemSet^ scan_items) {
		cppm::ScanItemSetOwnedView ret;
		ret.item_root_path = conv.to_sv(scan_items->item_root_path);
		ret.commands_contain_item_path = scan_items->commands_contain_item_path;
		// todo: are these conversions slow ? maybe do some threading here ?
		ret.commands = convert_commands(conv, scan_items->commands);
		ret.targets = convert_targets(conv, scan_items->targets);
		ret.items = convert_items(conv, scan_items->items);
		return ret;
	}

public:
	enum class Type {
		CLANG_SCAN_DEPS = (int)cppm::Scanner::Type::CLANG_SCAN_DEPS
	};

	String^ scan(Type tool_type, String^ tool_path, String^ db_path, String^ int_dir, 
		ScanItemSet^ scan_items, bool concurrent_targets, bool file_tracker_running, 
		DepInfoObserver^ observer, bool submit_previous_results)
	{
		// let the converter allocate enough space for all of the strings at once
		UCS2_to_UTF8_Converter conv;
		conv.reserve(tool_path).reserve(db_path).reserve(int_dir);
		reserve(conv, scan_items);
		conv.finish_reserve();

		DepInfoForwarder forwader(scan_items->items, observer);

		cppm::Scanner::ConfigView config;
		config.tool_type = static_cast<cppm::Scanner::Type>(tool_type);
		config.tool_path = conv.to_sv(tool_path);
		config.db_path = conv.to_sv(db_path);
		config.int_dir = conv.to_sv(int_dir);
		auto owned_view = convert(conv, scan_items);
		config.item_set = cppm::ScanItemSetView::from(owned_view);
		config.concurrent_targets = concurrent_targets;
		config.file_tracker_running = file_tracker_running;
		config.observer = &forwader;
		config.submit_previous_results = submit_previous_results;

		cppm::Scanner scanner;
		try {
			scanner.scan(config);
		} catch (std::exception & e) {
			throw gcnew System::Exception(gcnew String(e.what()));
		}
		std::string ret = "v9";
		return gcnew String(ret.c_str());
	}

	String^ clean(String^ db_path, ScanItemSet^ scan_items)
	{
		UCS2_to_UTF8_Converter conv;
		conv.reserve(db_path);
		reserve(conv, scan_items);
		conv.finish_reserve();

		cppm::Scanner::ConfigView config;
		config.db_path = conv.to_sv(db_path);
		auto owned_view = convert(conv, scan_items);
		config.item_set = cppm::ScanItemSetView::from(owned_view);

		cppm::Scanner scanner;
		try {
			scanner.clean(config);
		} catch (std::exception & e) {
			throw gcnew System::Exception(gcnew String(e.what()));
		}

		std::string ret = "v4";
		return gcnew String(ret.c_str());
	}
};

} // namespace cppm_cs