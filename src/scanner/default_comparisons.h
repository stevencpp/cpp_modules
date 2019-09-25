#pragma once

#if 0 //__cpp_concepts -- todo: disabled because the current implementation doesn't add the < operator to RawDataBlock
// todo: __cpp_lib_spaceship / __cpp_lib_three_way_comparison don't seem to work, using __cpp_concepts for now
#include <compare> // for <==>
#define DEFAULT_COMPARISONS(CLASS) auto operator<=>(const CLASS&) const = default;
#else
// adapted from https://www.reddit.com/r/cpp/comments/4yp7fv/c17_structured_bindings_convert_struct_to_a_tuple/
// and https://www.fluentcpp.com/2019/04/09/how-to-emulate-the-spaceship-operator-before-c20-with-crtp/
#include <tuple>
#include <type_traits>

namespace comparison_detail {

template <class T, class... TArgs> decltype(void(T { std::declval<TArgs>()... }), std::true_type {}) test_is_braces_constructible(int);
template <class, class...> std::false_type test_is_braces_constructible(...);
template <class T, class... TArgs> using is_braces_constructible = decltype(test_is_braces_constructible<T, TArgs...>(0));

struct any_type {
	// added the const because of std::optional, based on
	// https://github.com/apolukhin/magic_get/commit/1b138a4bd76b1118c433ed5623f0447f64cf057d
	template<class T>
	constexpr operator T() const {
		return T {}; // this fixes warnings on clang
	} // non explicit
};

template<class T>
auto to_tie(T&& object) noexcept {
	using type = std::decay_t<T>;
	if constexpr (is_braces_constructible<type, any_type, any_type, any_type, any_type>{}) {
		// the (type&) casts on needed with clang + MSVC stdlib
		// https://stackoverflow.com/questions/53721714/why-does-structured-binding-not-work-as-expected-on-struct
		auto&& [p1, p2, p3, p4] = (type&)object;
		return std::tie(p1, p2, p3, p4);
	} else if constexpr (is_braces_constructible<type, any_type, any_type, any_type>{}) {
		auto&& [p1, p2, p3] = (type&)object;
		return std::tie(p1, p2, p3);
	} else if constexpr (is_braces_constructible<type, any_type, any_type>{}) {
		auto&& [p1, p2] = (type&)object;
		return std::tie(p1, p2);
	} else if constexpr (is_braces_constructible<type, any_type>{}) {
		auto&& [p1] = (type&)object;
		return std::tie(p1);
	} else {
		return std::tie();
	}
}

} // comparison_detail

#define DEFAULT_COMPARISONS(CLASS) \
	auto Tie() const { \
		return comparison_detail::to_tie(*this); \
	} \
	[[nodiscard]] constexpr bool operator==(const CLASS& other) const \
	{ \
		return Tie() == other.Tie(); \
	} \
	[[nodiscard]] constexpr bool operator!=(const CLASS& other) const \
	{ \
		return Tie() != other.Tie(); \
	} \
	[[nodiscard]] constexpr bool operator<(const CLASS& other) const \
	{ \
		return Tie() < other.Tie(); \
	} \
	[[nodiscard]] constexpr bool operator>(const CLASS& other) const \
	{ \
		return Tie() > other.Tie(); \
	} \
	[[nodiscard]] constexpr bool operator>=(const CLASS& other) const \
	{ \
		return Tie() >= other.Tie(); \
	} \
	[[nodiscard]] constexpr bool operator<=(const CLASS& other) const \
	{ \
		return Tie() <= other.Tie(); \
	}
#endif