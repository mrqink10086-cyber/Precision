#pragma once

#include <cstddef>
#include <functional>
#include <iterator>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace RuntimeAlgorithms
{
	template <class T, class Mutex, class Visitor>
	void ProcessAndClearLocked(std::vector<T>& a_items, Mutex& a_mutex, Visitor a_visit)
	{
		std::unique_lock lock(a_mutex);
		for (auto& item : a_items) {
			a_visit(item);
		}
		a_items.clear();
	}

	template <class T, class Predicate>
	void ProcessRetained(std::vector<T>& a_items, Predicate a_remove)
	{
		std::size_t retained = 0;
		std::size_t read = 0;
		try {
			for (; read < a_items.size(); ++read) {
				if (a_remove(a_items[read])) {
					// Release completed work before running the next item, as vector::erase did.
					a_items[read].reset();
				} else {
					if (retained != read) {
						a_items[retained] = std::move(a_items[read]);
					}
					++retained;
				}
			}
		} catch (...) {
			for (; read < a_items.size(); ++read) {
				if (retained != read) {
					a_items[retained] = std::move(a_items[read]);
				}
				++retained;
			}
			a_items.resize(retained);
			throw;
		}
		a_items.resize(retained);
	}

	class TokenRange
	{
	public:
		class Iterator
		{
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = std::string_view;
			using difference_type = std::ptrdiff_t;
			using reference = std::string_view;
			using pointer = void;

			Iterator() = default;
			Iterator(std::string_view a_text, char a_delimiter) :
				remaining_(a_text), delimiter_(a_delimiter) { Advance(); }

			std::string_view operator*() const noexcept { return current_; }
			Iterator& operator++() noexcept
			{
				Advance();
				return *this;
			}
			Iterator operator++(int) noexcept
			{
				auto previous = *this;
				++*this;
				return previous;
			}
			bool operator==(const Iterator&) const = default;
			bool operator==(std::default_sentinel_t) const noexcept { return current_.empty(); }

		private:
			void Advance() noexcept
			{
				while (!remaining_.empty()) {
					const auto end = remaining_.find(delimiter_);
					current_ = remaining_.substr(0, end);
					if (end == std::string_view::npos) {
						remaining_ = {};
					} else {
						remaining_.remove_prefix(end + 1);
					}
					if (!current_.empty()) {
						return;
					}
				}
				current_ = {};
			}

			std::string_view remaining_;
			std::string_view current_;
			char delimiter_ = 0;
		};

		TokenRange(std::string_view a_text, char a_delimiter) noexcept :
			text_(a_text), delimiter_(a_delimiter) {}
		Iterator begin() const noexcept { return Iterator(text_, delimiter_); }
		std::default_sentinel_t end() const noexcept { return {}; }
		bool empty() const noexcept { return begin() == end(); }

	private:
		std::string_view text_;
		char delimiter_;
	};

	struct TransparentStringHash
	{
		using is_transparent = void;
		std::size_t operator()(std::string_view a_text) const noexcept { return std::hash<std::string_view>{}(a_text); }
	};
}
