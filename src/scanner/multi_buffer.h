#pragma once

#include <vector>
#include <string>
#include <string_view>
#include "span.hpp"

// store a vector of strings in a single string buffer and return it as a vector of string_views
template<typename size_type = std::size_t>
class multi_string_buffer {
private:
	bool can_realloc = true;
	std::string buf;
	std::vector<std::size_t> ofs;
public:
	multi_string_buffer() {
		ofs.push_back(0);
	}
	void reserve(std::size_t size) {
		buf.reserve(size);
	}
	size_type add(std::string_view str) {
		// todo: use a custom allocator to guarantee this doesn't reallocate when it shouldn't
		if (!can_realloc && (buf.size() + str.size() >= buf.capacity()))
			throw std::runtime_error("must not reallocate the buffer after viewing it");
		buf += str;
		ofs.push_back(buf.size());
		return size_type { ofs.size() - 2 };
	}
	std::string_view get(size_type idx) {
		can_realloc = false;
		if (idx >= ofs.size())
			throw std::runtime_error("invalid index");
		std::size_t start = ofs[idx];
		std::size_t len = ofs[idx + 1] - start;
		return { &buf[start], len };
	}
	auto to_vector() {
		vector_map<size_type, std::string_view> ret;
		can_realloc = false;
		if (buf.empty()) return ret;
		ret.resize(size_type { ofs.size() - 1 });
		const char* start = &buf[0];
		for (std::size_t i = 1; i < ofs.size(); ++i) {
			const char* end = &buf.front() + ofs[i]; // note: ofs[i] could be == buf.size()
			ret[size_type { i - 1 }] = { start, (std::size_t)(end - start) };
			start = end;
		}
		return ret;
	}
};

// store a vector of vectors of T in a single vector of T and return it as a vector of span of T
// while also reordering the input vectors
template<typename size_type, typename T>
class reordered_multi_vector_buffer {
private:
	bool can_add = true;
	std::vector<T> buf;
	struct vec_info { std::size_t ofs; size_type final_idx; };
	std::vector<vec_info> vecs;
	size_type max_idx = {};
public:
	void new_vector(size_type idx) {
		if (!can_add)
			throw std::runtime_error("must not add more vectors after viewing some");
		vecs.push_back({ buf.size(), idx });
		if (idx > max_idx) max_idx = idx;
	}
	void add(const T& item) {
		if (!can_add)
			throw std::runtime_error("must not add more elements after viewing some");
		if (vecs.empty())
			throw std::runtime_error("must call new_vector first");
		buf.push_back(item);
	}
	auto to_vectors() {
		vector_map<size_type, tcb::span<T>> ret;
		can_add = false;
		ret.resize(size_type((std::size_t)size()));
		if (buf.empty()) return ret;
		T* start = &buf.front();
		for (std::size_t i = 0; i < vecs.size() - 1; ++i) {
			T* end = &buf.front() + vecs[i + 1].ofs; // note: the ofs could be == buf.size()
			ret[vecs[i].final_idx] = { start, end };
			start = end;
		}
		T* end = &buf.front() + buf.size();
		ret[vecs.back().final_idx] = { start, end };
		return ret;
	}
	size_type size() const {
		return max_idx + 1;
	}
};