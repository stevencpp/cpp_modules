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
	if constexpr (is_braces_constructible<type, any_type, any_type, any_type, any_type, any_type, any_type>{}) {
		auto&& [p1, p2, p3, p4, p5, p6] = (type&)object;
		return std::tuple(p1, p2, p3, p4, p5, p6);
	} else if constexpr (is_braces_constructible<type, any_type, any_type, any_type, any_type, any_type>{}) {
		auto&& [p1, p2, p3, p4, p5] = (type&)object;
		return std::tuple(p1, p2, p3, p4, p5);
	} else if constexpr (is_braces_constructible<type, any_type, any_type, any_type, any_type>{}) {
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

template< class T > // todo: should be standard in C++20
struct type_identity {
	using type = T;
};

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

template<typename... TypeIdent, std::size_t... Is>
constexpr std::size_t get_total_non_view_size_bytes(std::index_sequence<Is...>, TypeIdent... type_idents)
{
	auto get_single = [&](auto type_ident, auto idx_c) -> std::size_t {
		using T = typename decltype(type_ident)::type;
		constexpr std::size_t idx = decltype(idx_c)::value;
		if constexpr (is_view_v<T>) {
			if constexpr (idx == sizeof...(Is) - 1)
				return 0;
			return sizeof(stored_size_type);
		} else {
			return sizeof(T);
		}
	};
	return (get_single(type_idents, std::integral_constant<std::size_t, Is>{}) + ...);
}

template<typename T>
constexpr std::size_t get_view_size_bytes(const T& elem) {
	if constexpr (is_string_view_v<T>)
		return elem.size();
	else if constexpr (is_span_v<T>)
		return elem.size() * sizeof(typename T::element_type);
	return 0;
}

template<typename... Ts, std::size_t... Is>
std::size_t get_aggregate_size(const std::tuple<Ts...>& tup, std::index_sequence<Is...>)
{
	return std::apply([&](auto&... elems) {
		constexpr std::size_t non_view_size = get_total_non_view_size_bytes(
			std::index_sequence<Is...>{}, type_identity<Ts> {} ...);
		std::size_t total_size = non_view_size + (get_view_size_bytes(elems) + ...);
		return total_size;
	}, tup);
}

template<bool check_size, typename... Ts, std::size_t... Is>
MDB_val to_val_from_aggregate(const std::tuple<Ts...> &tup, std::index_sequence<Is...> idxs, 
	char *buf, std::size_t buf_size)
{
	std::size_t ofs = 0;
	auto copy = [&](auto& elem, auto idx_c) {
		using T = std::decay_t<decltype(elem)>;
		constexpr std::size_t idx = decltype(idx_c)::value;
		if constexpr (is_view_v<T>) {
			stored_size_type size = get_view_size_bytes(elem);
			if constexpr (idx != sizeof...(Is) - 1) {
				memcpy(&buf[ofs], &size, sizeof(stored_size_type));
				ofs += sizeof(stored_size_type);
			}
			memcpy(&buf[ofs], elem.data(), size);
			ofs += size;
		} else {
			memcpy(&buf[ofs], &elem, sizeof(T));
			ofs += sizeof(T);
		}
	};

	if constexpr (check_size) {
		// check if the input data is valid, todo: disable this in release ?
		std::size_t total_size = get_aggregate_size(tup, idxs);
		if (total_size >= buf_size)
			throw std::invalid_argument("not enough buffer space");
	}

	std::apply([&](auto&... elems) {
		(copy(elems, std::integral_constant<std::size_t, Is>{}), ...);
	}, tup);

	return MDB_val { ofs, buf };
}

#if 0 // bug in VS 16.4.0 Preview 2
template<bool check_size = true, typename T>
MDB_val to_val(const T& t, char *buf, int buf_size) {
	if constexpr (std::is_convertible_v<T, std::string_view>) {
		auto sv = std::string_view { t };
		return { sv.size(), (void*)sv.data() };
	} else if constexpr (contains_a_view<T>()) { // it tries to compile this branch even when if constexpr (false)
		auto tup = to_tuple(t);
		auto idxs = std::make_index_sequence<std::tuple_size_v<decltype(tup)>> {};
		return to_val_from_aggregate<check_size>(tup, idxs, buf, buf_size);
	} else {
		return { sizeof(T), (void*)&t };
	}
}
#else
template<bool check_size = true, typename T>
std::enable_if_t<
	std::is_convertible_v<T, std::string_view>, 
MDB_val> to_val(const T& t, char* buf, int buf_size) {
	auto sv = std::string_view { t };
	return { sv.size(), (void*)sv.data() };
}

template<bool check_size = true, typename T>
std::enable_if_t<
	contains_a_view<T>(),
MDB_val> to_val(const T& t, char* buf, int buf_size) {
	static_assert(!std::is_same_v<T, uint32_t>, "must not be key");
	auto tup = to_tuple(t);
	auto idxs = std::make_index_sequence<std::tuple_size_v<decltype(tup)>> {};
	return to_val_from_aggregate<check_size>(tup, idxs, buf, buf_size);
}

template<bool check_size = true, typename T>
MDB_val to_val(const T& t, char* buf, int buf_size, std::enable_if_t<
	!std::is_convertible_v<T, std::string_view> && !contains_a_view<T>(),
void> * v = nullptr) {
	return { sizeof(T), (void*)&t };
}
#endif

template<typename T>
std::size_t get_val_size(const T& t) {
	if constexpr (std::is_convertible_v<T, std::string_view>) {
		auto sv = std::string_view { t };
		return sv.size();
	} else if constexpr (contains_a_view<T>()) {
		auto tup = to_tuple(t);
		auto idxs = std::make_index_sequence<std::tuple_size_v<decltype(tup)>> {};
		return get_aggregate_size(tup, idxs);
	} else {
		return sizeof(T);
	}
}

template<typename A, typename... Ts, std::size_t... Is>
decltype(auto) from_val_to_aggregate(MDB_val val, std::tuple<Ts...> tup, std::index_sequence<Is...>) {
	auto get_view_size_without_last = [&](auto& elem, auto idx_c) -> std::size_t {
		using T = std::decay_t<decltype(elem)>;
		constexpr std::size_t idx = decltype(idx_c)::value;
		if constexpr (is_view_v<T> && idx != sizeof...(Is) - 1)
			return get_view_size_bytes(elem);
		return 0;
	};

	auto data_ptr = (char*)val.mv_data;
	auto copy_to = [&](auto & elem, auto idx_c) {
		using T = std::decay_t<decltype(elem)>;
		constexpr std::size_t idx = decltype(idx_c)::value;
		if constexpr (is_view_v<T>) {
			std::size_t size_bytes = 0;
			if constexpr (idx == sizeof...(Is) - 1) {
				size_bytes = val.mv_size - (data_ptr - (char*)val.mv_data);
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
		// check if the input data is valid, todo: disable this in release ?
		constexpr std::size_t non_view_size = get_total_non_view_size_bytes(std::index_sequence<Is...>{}, type_identity<Ts> {} ...);
		std::size_t size_without_first_view = non_view_size + 
			(get_view_size_without_last(elems, std::integral_constant<std::size_t, Is>{}) + ...);
		if (size_without_first_view > val.mv_size)
			throw std::runtime_error("size mismatch");
		// copy the data from val into tup
		(copy_to(elems, std::integral_constant<std::size_t, Is>{}), ...);
		// then convert tup to the aggregate type
		return A { elems... };
	}, tup);
}

template<typename T>
decltype(auto) from_val(MDB_val val) {
	if constexpr (std::is_same_v<T, std::string_view>) {
		return std::string_view { (const char*)val.mv_data, val.mv_size };
	} else if constexpr (contains_a_view<T>()) {
		auto tup = to_tuple(T {});
		auto idxs = std::make_index_sequence<std::tuple_size_v<decltype(tup)>> {};
		return from_val_to_aggregate<T>(val, tup, idxs);
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