#pragma once

#include <vector>
#include "span.hpp"

// todo: allow using 64-bit ids if needed
template<uint32_t invalid_id>
struct strong_int {
	uint32_t val = 0;
	strong_int() {}
	template<typename T, typename = std::enable_if_t< std::is_integral_v<T> > >
	explicit strong_int(T val) : val((uint32_t)val) {}
	explicit operator uint32_t() const {
		return val;
	}
	explicit operator uint64_t() const {
		return val;
	}
	bool is_valid() const {
		return val != invalid_id;
	}
	void invalidate() {
		val = invalid_id;
	}
};
// todo: there's probably a nicer way to do this
#define DECL_STRONG_ID_INV(id, invalid_id) struct id : public strong_int<invalid_id> { \
	using strong_int::strong_int; \
	using strong_int::operator=; \
	bool operator<(const id& i) const { return val < i.val; } \
	bool operator>(const id& i) const { return val > i.val; } \
	bool operator<=(const id& i) const { return val <= i.val; } \
	bool operator>=(const id& i) const { return val >= i.val; } \
	bool operator==(const id& i) const { return val == i.val; } \
	bool operator!=(const id& i) const { return val != i.val; } \
	id& operator++() { val++; return *this; } \
	id operator++(int) { auto tmp = id { val++ }; return tmp; } \
	id operator+(uint32_t v) const { return id { val + v }; } \
	id operator-(uint32_t v) const { return id { val - v }; } \
};

#define DECL_STRONG_ID(id) DECL_STRONG_ID_INV(id, std::numeric_limits<uint32_t>::max())

template<typename T, typename U>
T id_cast(U&& id) {
	return T { (uint32_t)id };
}

template<typename Key>
struct indices_range {
	struct iterator {
		Key k;
		void operator++() {
			++k;
		}
		Key operator*() const {
			return k;
		}
		bool operator!=(iterator i) const {
			return k != i.k;
		}
	};
	Key _begin, _end;

	iterator begin() const {
		return { _begin };
	}

	iterator end() const {
		return { _end };
	}

	Key front() {
		return _begin;
	}
};

template<typename Key, typename Value>
struct vector_map : public std::vector<Value> {
	using base = std::vector<Value>;
	using base::base;
	using size_type = typename base::size_type;
	decltype(auto) operator[](const Key& key) {
		return base::operator[]((size_type)key);
	}
	decltype(auto) operator[](const Key& key) const {
		return base::operator[]((size_type)key);
	}
	Key size() const {
		return (Key)base::size();
	}
	indices_range<Key> indices() const {
		return { Key { 0 }, size() };
	}
	void resize(Key new_size) {
		base::resize((size_type)new_size);
	}
	void reserve(Key new_capacity) {
		base::reserve((size_type)new_capacity);
	}
};

template<typename Key, typename Value>
struct span_map : public tcb::span<Value> {
	using base = tcb::span<Value>;
	//using base::base;
	using base::operator=;
	explicit span_map(tcb::span<Value> s) : base(s) {}
	explicit span_map(Value* start, Value* end) : base(start, end) {}
	explicit span_map() {}
	using MutableValue = std::remove_const_t<Value>;
	span_map(vector_map<Key, MutableValue>& v) : base(v.data(), (std::size_t)v.size()) {}
	template<typename V = Value, typename = std::enable_if_t< std::is_same_v<V, Value> && std::is_const_v<Value> >>
	span_map(const span_map<Key, V> & s) : base(s.data(), (std::size_t)s.size()) {}
	span_map(const span_map<Key, MutableValue> & s) : base(s.data(), (std::size_t)s.size()) {}

	Value& operator[](const Key& key) {
		return base::operator[]((std::size_t)key);
	}
	const Value& operator[](const Key& key) const {
		return base::operator[]((std::size_t)key);
	}
	Key size() const {
		return (Key)base::size();
	}
	indices_range<Key> indices() const {
		return { Key { 0 }, size() };
	}
};

template<typename Key, typename Value>
auto to_span_map(tcb::span<Value> s) {
	return span_map < Key, Value > { s };
}