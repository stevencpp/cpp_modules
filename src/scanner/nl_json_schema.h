#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <variant>

namespace depinfo { // todo: should be e.g nl_json_schema, but it doesn't compile :(
	using namespace nlohmann;

	template<typename T>
	void put_to(json& j, const char* key, const T& val) {
		j[key] = val;
	}

	template<typename... Ts>
	void to_json(json& j, const std::variant<Ts...>& v) {
		std::visit([&](const auto& val) {
			j = val;
			}, v);
	}

	template<typename T>
	void put_to(json& j, const char* key, const std::optional<T>& opt) {
		if (opt.has_value())
			put_to(j, key, opt.value());
	}

	template<typename T>
	void get_to(const json& j, const char* key, T& val) {
		j.at(key).get_to(val);
	}

	template<typename T>
	bool try_get(const json& j, T& t) {
		// maybe use try/catch here ?
		t = j.get<T>();
		return true;
	}

	static inline bool try_get(const json& j, std::string& s) {
		if (!j.is_string()) return false;
		s = j.get<std::string>();
		return true;
	}

	static inline bool try_fail() {
		throw std::runtime_error("failed to match variant");
		return true;
	}

	template<typename... Ts>
	void from_json(const json& j, std::variant<Ts...>& v) {
		(try_get(j, std::get<Ts>(v)) || ...) || try_fail();
	}

	template<typename T>
	void get_to(const json& j, const char* key, std::optional<T>& opt) {
		auto itr = j.find(key);
		if (itr != j.end())
			opt = itr->get<T>();
		// leave the default value set for the optional
	}

	template<typename ObjT>
	struct RW
	{
	private:
		bool to_json = true;
		json* j = nullptr;
		ObjT* obj = nullptr;
	public:
		RW(json& j, const ObjT& obj) : to_json(true), j(&j), obj(const_cast<ObjT*>(&obj)) {}
		RW(const json& j, ObjT& obj) : to_json(false), j(const_cast<json*>(&j)), obj(&obj) {}

		ObjT* operator->() {
			return obj;
		}

		void json_schema() {}

		template<typename MemberT, typename... MemberTs>
		void json_schema(MemberT&& member, const char* key, MemberTs&&... members) {
			if (to_json) {
				put_to(*j, key, std::forward<MemberT>(member));
			} else {
				get_to(*j, key, std::forward<MemberT>(member));
			}
			json_schema(std::forward<MemberTs>(members)...);
		}
	};
}