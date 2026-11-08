#pragma once

#include <array>
#include <cstddef>

namespace Precision
{
	template <class T, std::size_t Capacity>
	class TrailHistory
	{
		static_assert(Capacity > 0);

	public:
		void Push(const T& a_value)
		{
			const std::size_t lastIndex = size_ < Capacity ? size_ : Capacity - 1;
			for (std::size_t index = lastIndex; index > 0; --index) {
				values_[index] = values_[index - 1];
			}
			values_[0] = a_value;

			if (size_ < Capacity) {
				++size_;
			}
		}

		[[nodiscard]] std::size_t size() const noexcept { return size_; }

		[[nodiscard]] const T& Newest(std::size_t a_index = 0) const noexcept { return values_[a_index]; }

	private:
		std::array<T, Capacity> values_{};
		std::size_t size_ = 0;
	};
}
