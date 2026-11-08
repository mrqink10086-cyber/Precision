#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CallbackRegistryDetail
{
	inline thread_local std::size_t callbackContextDepth = 0;

	class CallbackContext
	{
	public:
		CallbackContext() noexcept { ++callbackContextDepth; }
		~CallbackContext() { --callbackContextDepth; }

		CallbackContext(const CallbackContext&) = delete;
		CallbackContext& operator=(const CallbackContext&) = delete;
	};

	inline bool IsInCallbackContext() noexcept
	{
		return callbackContextDepth != 0;
	}
}

template <class Key, class Callback, class Hash = std::hash<Key>, class KeyEqual = std::equal_to<Key>>
class CallbackRegistry
{
	struct Entry
	{
		explicit Entry(Callback&& a_callback) :
			callback(std::make_unique<Callback>(std::move(a_callback))) {}

		std::unique_ptr<Callback> callback;
		std::condition_variable retiredCondition;
		std::size_t inFlight = 0;
		bool active = true;
		bool retiring = false;
		bool retired = false;
	};

	using SnapshotEntries = std::vector<std::shared_ptr<Entry>>;

	struct Invocation
	{
		Invocation(CallbackRegistry* a_registry, std::shared_ptr<Entry> a_entry, Callback* a_callback) :
			registry(a_registry), entry(std::move(a_entry)), callback(a_callback) {}

		CallbackRegistry* registry;
		std::shared_ptr<Entry> entry;
		Callback* callback;

		~Invocation()
		{
			if (registry) {
				registry->CompleteInvocation(entry);
			}
		}

		explicit operator bool() const noexcept { return registry != nullptr; }

		Invocation(const Invocation&) = delete;
		Invocation& operator=(const Invocation&) = delete;
		Invocation(Invocation&& a_other) noexcept :
			registry(std::exchange(a_other.registry, nullptr)), entry(std::move(a_other.entry)), callback(std::exchange(a_other.callback, nullptr)) {}
	};

public:
	class Snapshot
	{
	public:
		Snapshot() = default;
		[[nodiscard]] bool Empty() const noexcept { return !entries || entries->empty(); }

	private:
		friend class CallbackRegistry;
		Snapshot(const CallbackRegistry* a_owner, std::shared_ptr<const SnapshotEntries> a_entries) :
			owner(a_owner), entries(std::move(a_entries)) {}

		const CallbackRegistry* owner = nullptr;
		std::shared_ptr<const SnapshotEntries> entries;
	};

	bool Add(Key a_key, Callback a_callback)
	{
		auto entry = std::make_shared<Entry>(std::move(a_callback));
		{
			std::lock_guard lock(_mutex);
			if (_entries.contains(a_key)) {
				return false;
			}

			auto [found, inserted] = _entries.emplace(std::move(a_key), entry);
			if (!inserted) {
				return false;
			}
			try {
				_snapshot = BuildSnapshotLocked();
			} catch (...) {
				_entries.erase(found);
				throw;
			}
		}
		return true;
	}

	bool Remove(const Key& a_key)
	{
		std::shared_ptr<Entry> entry;
		std::unique_ptr<Callback> callbackToDestroy;
		const bool reentrant = CallbackRegistryDetail::IsInCallbackContext();

		{
			std::unique_lock lock(_mutex);
			auto found = _entries.find(a_key);
			if (found == _entries.end()) {
				return false;
			}

			entry = found->second;
			std::shared_ptr<const SnapshotEntries> nextSnapshot;
			if (entry->active) {
				nextSnapshot = BuildSnapshotLocked(entry.get());
			}
			entry->active = false;
			if (nextSnapshot) {
				_snapshot = std::move(nextSnapshot);
			}
			callbackToDestroy = BeginRetirementLocked(entry);
		}

		if (callbackToDestroy) {
			FinishRetirement(entry, std::move(callbackToDestroy));
		}

		if (reentrant) {
			return true;
		}

		std::unique_lock lock(_mutex);
		entry->retiredCondition.wait(lock, [&] { return entry->retired; });
		return true;
	}

	[[nodiscard]] Snapshot Capture() const
	{
		std::lock_guard lock(_mutex);
		return Snapshot{ this, _snapshot };
	}

	template <class Function>
	void ForEach(Function&& a_function)
	{
		const auto snapshot = Capture();
		ForEach(snapshot, std::forward<Function>(a_function));
	}

	template <class Function>
	void ForEach(const Snapshot& a_snapshot, Function&& a_function)
	{
		if (a_snapshot.owner != this || !a_snapshot.entries) {
			return;
		}

		for (const auto& entry : *a_snapshot.entries) {
			auto invocation = BeginInvocation(entry);
			if (!invocation) {
				continue;
			}

			CallbackRegistryDetail::CallbackContext callbackContext;
			if (!std::invoke(a_function, *invocation.callback)) {
				break;
			}
		}
	}

	[[nodiscard]] std::size_t Size() const
	{
		const auto snapshot = Capture();
		return snapshot.entries ? snapshot.entries->size() : 0;
	}

private:
	std::shared_ptr<const SnapshotEntries> BuildSnapshotLocked(const Entry* a_excludedEntry = nullptr) const
	{
		auto entries = std::make_shared<SnapshotEntries>();
		entries->reserve(_entries.size());
		for (const auto& [key, entry] : _entries) {
			(void)key;
			if (entry.get() != a_excludedEntry && entry->active && !entry->retiring && !entry->retired && entry->callback) {
				entries->push_back(entry);
			}
		}
		return entries;
	}

	std::unique_ptr<Callback> BeginRetirementLocked(const std::shared_ptr<Entry>& a_entry)
	{
		if (a_entry->active || a_entry->inFlight != 0 || a_entry->retiring || a_entry->retired) {
			return {};
		}

		a_entry->retiring = true;
		return std::move(a_entry->callback);
	}

	Invocation BeginInvocation(const std::shared_ptr<Entry>& a_entry)
	{
		std::lock_guard lock(_mutex);
		if (!a_entry->active || a_entry->retiring || a_entry->retired || !a_entry->callback) {
			return { nullptr, {}, nullptr };
		}

		++a_entry->inFlight;
		return { this, a_entry, a_entry->callback.get() };
	}

	void CompleteInvocation(const std::shared_ptr<Entry>& a_entry)
	{
		std::unique_ptr<Callback> callbackToDestroy;
		{
			std::lock_guard lock(_mutex);
			--a_entry->inFlight;
			callbackToDestroy = BeginRetirementLocked(a_entry);
		}

		if (callbackToDestroy) {
			FinishRetirement(a_entry, std::move(callbackToDestroy));
		}
	}

	void FinishRetirement(const std::shared_ptr<Entry>& a_entry, std::unique_ptr<Callback> a_callback)
	{
		{
			CallbackRegistryDetail::CallbackContext callbackContext;
			a_callback.reset();
		}

		{
			std::lock_guard lock(_mutex);
			for (auto found = _entries.begin(); found != _entries.end(); ++found) {
				if (found->second == a_entry) {
					_entries.erase(found);
					break;
				}
			}
			a_entry->retired = true;
			a_entry->retiredCondition.notify_all();
		}
	}

	mutable std::mutex _mutex;
	std::unordered_map<Key, std::shared_ptr<Entry>, Hash, KeyEqual> _entries;
	std::shared_ptr<const SnapshotEntries> _snapshot = std::make_shared<const SnapshotEntries>();
};
