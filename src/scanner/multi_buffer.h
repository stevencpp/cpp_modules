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
	std::string buf; // todo: use vector<char> because std::string uses SBO
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

class stable_multi_string_buffer
{
private:
	constexpr static int chunk_size = 1024 * 1024;

	std::vector< std::vector<char> > buffers; // using vector<char> because it's not allowed to use SBO
	char* insert_position = nullptr;
	std::size_t remaining_in_current_buffer = 0;
public:
	// copies str into the buffer and returns a stable string_view
	std::string_view copy(std::string_view str) {
		if (str.size() > remaining_in_current_buffer) {
			if (str.size() > chunk_size)
				throw std::invalid_argument("strings must be smaller than the chunk size");
			auto &new_buf = buffers.emplace_back(chunk_size);
			insert_position = &new_buf[0];
			remaining_in_current_buffer = chunk_size;
		}

		memcpy(insert_position, str.data(), str.size());
		remaining_in_current_buffer -= str.size();
		auto ret = std::string_view { insert_position, str.size() };
		insert_position += str.size();
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
	void reserve(std::size_t nr_vecs, std::size_t nr_elems) {
		vecs.reserve(nr_vecs);
		buf.reserve(nr_elems);
	}

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

	template<typename SpanVisitor>
	void visit(SpanVisitor&& visitor) {
		can_add = false;
		if (buf.empty()) return;
		T* start = &buf.front();
		for (std::size_t i = 0; i < vecs.size() - 1; ++i) {
			T* end = &buf.front() + vecs[i + 1].ofs; // note: the ofs could be == buf.size()
			visitor(vecs[i].final_idx, tcb::span<T> { start, end });
			start = end;
		}
		T* end = &buf.front() + buf.size();
		visitor(vecs.back().final_idx, tcb::span<T> { start, end });
	}

	auto to_vectors() {
		vector_map<size_type, tcb::span<T>> ret;
		can_add = false;
		ret.resize(size());
		if (buf.empty()) return ret;
		visit([&](size_type idx, tcb::span<T> vec) {
			ret[idx] = vec;
		});
		return ret;
	}

	void to_vectors(/*inout: */span_map<size_type, tcb::span<T>> vecs) {
		if (vecs.size() < size())
			throw std::logic_error("reordered_multi_vector_buffer: size mismatch");
		visit([&](size_type idx, tcb::span<T> vec) {
			vecs[idx] = vec;
		});
	}

	auto get_last_span() {
		can_add = false;
		if (buf.empty() || vecs.empty())
			return tcb::span<T> {};
		T* start = &buf.front() + vecs.back().ofs;
		T* end = &buf.front() + buf.size();
		return tcb::span<T> { start, end };
	}

	size_type size() const {
		return max_idx + 1;
	}

	std::vector<T> move_buffer() {
		vecs.clear();
		max_idx = size_type {};
		can_add = true;
		return std::exchange(buf, {});
	}
};