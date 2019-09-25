#pragma once

#include <lmdb.h>

//#include "unique_resource.h"
#include <memory>
#include <string_view>
#include <stdexcept>
#pragma warning(disable:4275) // non dll-interface class 'std::runtime_error' used as base for dll-interface class 'fmt::v6::format_error'
#include <fmt/format.h>
#include <tuple>
#include "span.hpp"

namespace mdb {

namespace impl {

static inline void handle_mdb_error(int err, const char* context) {
	if (err != 0) {
		auto msg = fmt::format("{} because: {}", context, mdb_strerror(err));
		//fmt::print(msg);
		throw std::runtime_error(msg);
	}
}

static inline auto make_mdb_dbi(MDB_txn* txn, const char* name, unsigned int flags) {
	MDB_dbi dbi;
	int ret = mdb_dbi_open(txn, name, flags, &dbi);
	//MDB_env* env = mdb_txn_env(txn); // txn may be closed before the dbi is closed
	handle_mdb_error(ret, "failed to open lmdb database");
	return dbi; // todo: the docs say this doesn't really need to be closed ?
}

static inline auto make_mdb_cursor(MDB_txn* txn, MDB_dbi dbi) {
	MDB_cursor* cursor;
	int ret = mdb_cursor_open(txn, dbi, &cursor);
	handle_mdb_error(ret, "failed to open cursor");
	return std::unique_ptr<MDB_cursor, void(*)(MDB_cursor*)> { cursor, mdb_cursor_close };
}

static inline auto make_mdb_env() {
	MDB_env* env = nullptr;
	int ret = mdb_env_create(&env);
	handle_mdb_error(ret, "failed to create lmdb environment");
	return std::unique_ptr<MDB_env, void(*)(MDB_env*)> { env, mdb_env_close };
}

// from https://www.reddit.com/r/cpp/comments/4yp7fv/c17_structured_bindings_convert_struct_to_a_tuple/
template <class T, class... TArgs> decltype(void(T { std::declval<TArgs>()... }), std::true_type {}) test_is_braces_constructible(int);
template <class, class...> std::false_type test_is_braces_constructible(...);
template <class T, class... TArgs> using is_braces_constructible = decltype(test_is_braces_constructible<T, TArgs...>(0));

struct any_type {
	// added the const because of std::optional, based on
	// https://github.com/apolukhin/magic_get/commit/1b138a4bd76b1118c433ed5623f0447f64cf057d
	template<class T>
	constexpr operator T() const; // non explicit
};

template<class T>
auto to_tuple(T&& object) noexcept {
	using type = std::decay_t<T>;
	if constexpr (is_braces_constructible<type, any_type, any_type, any_type, any_type>{}) {
		auto&& [p1, p2, p3, p4] = (type&)object;
		return std::tuple(p1, p2, p3, p4);
	} else if constexpr (is_braces_constructible<type, any_type, any_type, any_type>{}) {
		auto&& [p1, p2, p3] = (type&)object;
		return std::tuple(p1, p2, p3);
	} else if constexpr (is_braces_constructible<type, any_type, any_type>{}) {
		auto&& [p1, p2] = (type&)object;
		return std::tuple(p1, p2);
	} else if constexpr (is_braces_constructible<type, any_type>{}) {
		auto&& [p1] = (type&)object;
		return std::tuple(p1);
	} else {
		return std::tuple();
	}
}

template<typename T>
struct is_string_view : std::false_type {};
template <>
struct is_string_view<std::string_view> : std::true_type {};
template<typename T>
constexpr bool is_string_view_v = is_string_view<T>::value;

template<typename T>
struct is_span : std::false_type {};
template <typename T>
struct is_span<tcb::span<T>> : std::true_type {};
template<typename T>
constexpr bool is_span_v = is_span<T>::value;

template<typename T>
using is_view = std::disjunction< is_string_view<T>, is_span<T> >;
template<typename T>
constexpr bool is_view_v = is_view<T>::value;

template <typename Tuple>
struct has_view;
template <typename... Us>
struct has_view<std::tuple<Us...>> : std::disjunction<is_view<Us>...> {};

template<typename T>
constexpr bool contains_a_view() {
	if constexpr (std::is_aggregate_v<T>)
		return has_view< decltype(to_tuple(T {})) > ::value;
	return false;
}

using stored_size_type = uint32_t;

template<typename... TypeIdent>
constexpr std::size_t get_total_non_view_size_bytes(TypeIdent... type_idents) {
	bool first_view = true;
	auto get_single = [&](auto type_ident) -> std::size_t {
		using T = typename decltype(type_ident)::type;
		if constexpr (is_view_v<T>) {
			if (!first_view)
				return sizeof(stored_size_type);
			first_view = false;
			return 0;
		} else {
			return sizeof(T);
		}
	};
	return (get_single(type_idents) + ...);
}

template<typename T>
constexpr std::size_t get_view_size_bytes(const T& elem) {
	if constexpr (is_string_view_v<T>)
		return elem.size();
	else if constexpr (is_span_v<T>)
		return elem.size() * sizeof(typename T::element_type);
	return 0;
}

template<typename... Ts>
MDB_val to_val_from_aggregate(const std::tuple<Ts...> &tup, char *buf, std::size_t buf_size) {
	bool first_view = true;
	std::size_t ofs = 0;
	auto copy = [&](auto& elem) {
		using T = std::decay_t<decltype(elem)>;
		if constexpr (is_view_v<T>) {
			auto size = get_view_size_bytes(elem);
			if (!first_view) {
				memcpy(&buf[ofs], &size, sizeof(stored_size_type));
				ofs += sizeof(stored_size_type);
			} else {
				first_view = false;
			}
			memcpy(&buf[ofs], elem.data(), size);
			ofs += size;
		} else {
			memcpy(&buf[ofs], &elem, sizeof(T));
			ofs += sizeof(T);
		}
	};

	std::apply([&](auto&... elems) {
		constexpr std::size_t non_view_size = get_total_non_view_size_bytes(type_identity<Ts> {} ...);
		std::size_t total_size = non_view_size + (get_view_size_bytes(elems) + ...);
		if (total_size >= buf_size)
			throw std::invalid_argument("not enough buffer space");
		(copy(elems), ...);
	}, tup);

	return MDB_val { ofs, buf };
}

template<typename T>
MDB_val to_val(const T& t, char *buf, int buf_size) {
	if constexpr (std::is_convertible_v<T, std::string_view>) {
		auto sv = std::string_view { t };
		return { sv.size(), (void*)sv.data() };
	} else if constexpr (contains_a_view<T>()) {
		return to_val_from_aggregate(to_tuple(t), buf, buf_size);
	} else {
		return { sizeof(T), (void*)&t };
	}
}

template< class T > // todo: should be standard in C++20
struct type_identity {
	using type = T;
};

template<typename A, typename... Ts>
decltype(auto) from_val_to_aggregate(MDB_val val, std::tuple<Ts...> tup) {
	bool first_view = true; // todo: find a constexpr way to do this
	auto get_view_size_without_first = [&](auto& elem) -> std::size_t {
		using T = std::decay_t<decltype(elem)>;
		if constexpr (is_view_v<T>) {
			if (!first_view)
				return get_view_size_bytes(elem);
			first_view = false;	
		}
		return 0;
	};

	auto data_ptr = (char*)val.mv_data;
	auto copy_to = [&](auto & elem, std::size_t size_without_first_view) {
		using T = std::decay_t<decltype(elem)>;
		if constexpr (is_view_v<T>) {
			std::size_t size_bytes = 0;
			if (first_view) {
				size_bytes = val.mv_size - size_without_first_view;
				first_view = false;
			} else {
				stored_size_type szb; // todo: maybe use a variable length binary integer ?
				memcpy(&szb, data_ptr, sizeof(stored_size_type)); // todo: std::bless
				size_bytes = szb;
				data_ptr += sizeof(stored_size_type);
			}
			if constexpr (is_string_view_v<T>) {
				elem = T { data_ptr, size_bytes };
			} else if constexpr (is_span_v<T>) {
				using TT = typename T::element_type;
				elem = T { reinterpret_cast<TT*>(data_ptr), (std::size_t)(size_bytes / sizeof(TT)) };
			}
			data_ptr += size_bytes;
		} else {
			memcpy(&elem, data_ptr, sizeof(T)); // todo: maybe use elem = std::bless<T>(data_ptr); in c++2x ?
			data_ptr += sizeof(T);
		}
	};
	
	return std::apply([&](auto&... elems) {
		constexpr std::size_t non_view_size = get_total_non_view_size_bytes(type_identity<Ts> {} ...);
		// find the size of the string inside the aggregate
		std::size_t size_without_first_view = non_view_size + (get_view_size_without_first(elems) + ...);
		if (size_without_first_view > val.mv_size)
			throw std::runtime_error("size mismatch");
		// copy the data from val into tup
		first_view = true;
		(copy_to(elems, size_without_first_view), ...);
		// then convert tup to the aggregate type
		return A { elems... };
	}, tup);
}

template<typename T>
decltype(auto) from_val(MDB_val val) {
	if constexpr (std::is_same_v<T, std::string_view>) {
		return std::string_view { (const char*)val.mv_data, val.mv_size };
	} else if constexpr (contains_a_view<T>()) {
		return from_val_to_aggregate<T>(val, to_tuple(T {}));
	} else {
		if (val.mv_size != sizeof(T)) {
			throw std::runtime_error("size mismatch");
		}
		// todo: use std::bless/start_lifetime_as<T> to remove the UB ?
		return *reinterpret_cast<T*>(val.mv_data); // l-value reference to the data
	}
}

} // namespace impl

} // namespace mdb