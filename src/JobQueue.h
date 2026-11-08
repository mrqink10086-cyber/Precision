#pragma once

#include "RuntimeAlgorithms.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

template <class Job>
class JobQueue
{
public:
	explicit JobQueue(const std::atomic<std::uint64_t>& a_epoch) :
		_epoch(a_epoch)
	{}

	template <class T, class... Args>
	void Push(Args&&... a_args)
	{
		static_assert(std::is_base_of_v<Job, T>);
		const auto epoch = _epoch.load(std::memory_order_acquire);
		if (epoch & 1) {
			return;
		}
		std::lock_guard lock(_mutex);
		if (_epoch.load(std::memory_order_acquire) == epoch) {
			_items.push_back(std::make_unique<T>(std::forward<Args>(a_args)...));
		}
	}

	template <class Predicate>
	void Process(Predicate a_remove)
	{
		const auto epoch = _epoch.load(std::memory_order_acquire);
		if (epoch & 1) {
			return;
		}
		std::lock_guard lock(_mutex);
		if (_epoch.load(std::memory_order_acquire) == epoch) {
			RuntimeAlgorithms::ProcessRetained(_items, std::move(a_remove));
		}
	}

	void Clear()
	{
		std::lock_guard lock(_mutex);
		_items.clear();
	}

private:
	// Odd epochs suspend work; changed epochs reject callers waiting across a reset.
	const std::atomic<std::uint64_t>& _epoch;
	std::mutex _mutex;
	std::vector<std::unique_ptr<Job>> _items;
};
