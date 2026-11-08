#include "../src/CallbackRegistry.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace
{
	std::atomic<bool> countAllocations = false;
	std::atomic<std::size_t> allocationCount = 0;

	using Registry = CallbackRegistry<int, std::function<void()>>;

	void Require(bool a_condition)
	{
		if (!a_condition) {
			std::abort();
		}
	}

	void TestSelfRemovalAndReentry()
	{
		Registry registry;
		int selfCalls = 0;
		int addedCalls = 0;
		Require(registry.Add(1, [&] {
			++selfCalls;
			Require(registry.Remove(1));
			Require(!registry.Add(1, [] {}));
			Require(registry.Add(2, [&] { ++addedCalls; }));
		}));

		registry.ForEach([](auto& a_callback) {
			a_callback();
			return true;
		});
		Require(selfCalls == 1);
		Require(registry.Size() == 1);

		registry.ForEach([](auto& a_callback) {
			a_callback();
			return true;
		});
		Require(addedCalls == 1);
	}

	void TestRemoveFutureCallbackInBatch()
	{
		std::vector<int> iterationOrder;
		Registry probe;
		for (int i = 0; i < 16; ++i) {
			Require(probe.Add(i, [&, i] { iterationOrder.push_back(i); }));
		}
		probe.ForEach([](auto& a_callback) {
			a_callback();
			return true;
		});
		Require(iterationOrder.size() == 16);

		const int removerKey = iterationOrder.front();
		const int futureKey = iterationOrder.back();
		bool futureCalled = false;
		Registry registry;
		for (int i = 0; i < 16; ++i) {
			if (i == removerKey) {
				Require(registry.Add(i, [&] { Require(registry.Remove(futureKey)); }));
			} else if (i == futureKey) {
				Require(registry.Add(i, [&] { futureCalled = true; }));
			} else {
				Require(registry.Add(i, [] {}));
			}
		}
		registry.ForEach([](auto& a_callback) {
			a_callback();
			return true;
		});
		Require(!futureCalled);
		Require(registry.Size() == 15);
	}

	void TestCrossRegistryReentrantRemoval()
	{
		Registry hitCallbacks;
		Registry contactCallbacks;
		int hitCalls = 0;
		int contactCalls = 0;
		Require(hitCallbacks.Add(1, [&] {
			++hitCalls;
			contactCallbacks.ForEach([](auto& a_callback) {
				a_callback();
				return true;
			});
		}));
		Require(contactCallbacks.Add(2, [&] {
			++contactCalls;
			Require(hitCallbacks.Remove(1));
		}));

		hitCallbacks.ForEach([](auto& a_callback) {
			a_callback();
			return true;
		});
		Require(hitCalls == 1);
		Require(contactCalls == 1);
		Require(hitCallbacks.Size() == 0);
	}

	void TestStatefulCallbackPersists()
	{
		CallbackRegistry<int, std::function<int()>> registry;
		Require(registry.Add(1, [count = 0]() mutable { return ++count; }));
		std::vector<int> values;
		for (int i = 0; i < 2; ++i) {
			registry.ForEach([&](auto& a_callback) {
				values.push_back(a_callback());
				return true;
			});
		}
		Require((values == std::vector<int>{ 1, 2 }));
	}

	void TestCapturedSnapshotMembership()
	{
		Registry registry;
		bool firstCalled = false;
		bool laterCalled = false;
		Require(registry.Add(1, [&] { firstCalled = true; }));
		const auto captured = registry.Capture();
		Require(!captured.Empty());
		Require(registry.Add(2, [&] { laterCalled = true; }));
		Require(registry.Remove(1));

		registry.ForEach(captured, [](auto& a_callback) {
			a_callback();
			return true;
		});
		Require(!firstCalled);
		Require(!laterCalled);

		registry.ForEach([](auto& a_callback) {
			a_callback();
			return true;
		});
		Require(laterCalled);
	}

	void TestDispatchDoesNotAllocateAfterRegistration()
	{
		Registry registry;
		int calls = 0;
		Require(registry.Add(1, [&] { ++calls; }));
		auto dispatch = [&] {
			registry.ForEach([](auto& a_callback) {
				a_callback();
				return true;
			});
		};
		dispatch();
		allocationCount.store(0, std::memory_order_relaxed);
		countAllocations.store(true, std::memory_order_release);
		for (int i = 0; i < 100; ++i) {
			dispatch();
		}
		countAllocations.store(false, std::memory_order_release);
		Require(allocationCount.load(std::memory_order_relaxed) == 0);
		Require(calls == 101);
	}

	struct DestructionState
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool callbackEntered = false;
		bool releaseCallback = false;
		bool destructorEntered = false;
		bool releaseDestructor = false;
	};

	struct BlockingCapture
	{
		explicit BlockingCapture(std::shared_ptr<DestructionState> a_state) :
			state(std::move(a_state)) {}
		~BlockingCapture()
		{
			std::unique_lock lock(state->mutex);
			state->destructorEntered = true;
			state->condition.notify_all();
			state->condition.wait(lock, [&] { return state->releaseDestructor; });
		}
		std::shared_ptr<DestructionState> state;
	};

	void TestExternalRemoveWaitsForInvocationAndDestruction()
	{
		Registry registry;
		auto state = std::make_shared<DestructionState>();
		auto capture = std::make_shared<BlockingCapture>(state);
		std::weak_ptr<BlockingCapture> weakCapture = capture;
		Require(registry.Add(1, [capture = std::move(capture), state] {
			(void)capture;
			std::unique_lock lock(state->mutex);
			state->callbackEntered = true;
			state->condition.notify_all();
			state->condition.wait(lock, [&] { return state->releaseCallback; });
		}));
		Require(weakCapture.use_count() == 1);

		std::thread dispatcher([&] {
			registry.ForEach([](auto& a_callback) {
				a_callback();
				return true;
			});
		});
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] { return state->callbackEntered; });
		}

		std::atomic<bool> removeStarted = false;
		std::atomic<bool> removeReturned = false;
		std::thread remover([&] {
			removeStarted.store(true, std::memory_order_release);
			Require(registry.Remove(1));
			removeReturned.store(true, std::memory_order_release);
		});
		while (!removeStarted.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		Require(!removeReturned.load(std::memory_order_acquire));

		{
			std::lock_guard lock(state->mutex);
			state->releaseCallback = true;
		}
		state->condition.notify_all();
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] { return state->destructorEntered; });
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		Require(!removeReturned.load(std::memory_order_acquire));

		{
			std::lock_guard lock(state->mutex);
			state->releaseDestructor = true;
		}
		state->condition.notify_all();
		dispatcher.join();
		remover.join();
		Require(removeReturned.load(std::memory_order_acquire));
		Require(weakCapture.expired());

		int callsAfterRemove = 0;
		registry.ForEach([&](auto&) {
			++callsAfterRemove;
			return true;
		});
		Require(callsAfterRemove == 0);
	}

	struct ReentrantCapture
	{
		explicit ReentrantCapture(Registry& a_registry) :
			registry(a_registry) {}
		~ReentrantCapture()
		{
			Require(registry.Add(3, [] {}));
			Require(registry.Remove(2));
			Require(registry.Remove(1));
		}
		Registry& registry;
	};

	void TestCaptureDestructorReentry()
	{
		Registry registry;
		Require(registry.Add(2, [] {}));
		auto capture = std::make_shared<ReentrantCapture>(registry);
		std::weak_ptr<ReentrantCapture> weakCapture = capture;
		Require(registry.Add(1, [capture = std::move(capture)] { (void)capture; }));
		Require(weakCapture.use_count() == 1);
		Require(registry.Remove(1));
		Require(weakCapture.expired());
		Require(registry.Size() == 1);
	}

	void TestCaptureDestructorReentryAfterCallbackRemoval()
	{
		Registry registry;
		Require(registry.Add(2, [] {}));
		auto capture = std::make_shared<ReentrantCapture>(registry);
		std::weak_ptr<ReentrantCapture> weakCapture = capture;
		Require(registry.Add(1, [capture = std::move(capture), &registry] {
			(void)capture;
			Require(registry.Remove(1));
		}));
		registry.ForEach([](auto& a_callback) {
			a_callback();
			return true;
		});
		Require(weakCapture.expired());
		Require(registry.Size() == 1);
	}

	void TestExceptionsReleaseInvocationAccounting()
	{
		Registry registry;
		Require(registry.Add(1, [] { throw 42; }));
		try {
			registry.ForEach([](auto& a_callback) {
				a_callback();
				return true;
			});
			Require(false);
		} catch (int value) {
			Require(value == 42);
		}
		Require(registry.Remove(1));
		Require(registry.Size() == 0);
	}

	void TestConcurrentAddRemoveAndDispatch()
	{
		Registry registry;
		std::atomic<int> calls = 0;
		std::atomic<bool> done = false;
		Require(registry.Add(1, [&] { calls.fetch_add(1, std::memory_order_relaxed); }));
		std::thread dispatcher([&] {
			while (!done.load(std::memory_order_acquire)) {
				registry.ForEach([](auto& a_callback) {
					a_callback();
					return true;
				});
			}
		});
		for (int i = 0; i < 1000; ++i) {
			Require(registry.Remove(1));
			Require(registry.Add(1, [&] { calls.fetch_add(1, std::memory_order_relaxed); }));
		}
		done.store(true, std::memory_order_release);
		dispatcher.join();
		Require(registry.Remove(1));
		Require(calls.load(std::memory_order_relaxed) > 0);
	}
}

void* operator new(std::size_t a_size)
{
	if (countAllocations.load(std::memory_order_acquire)) {
		allocationCount.fetch_add(1, std::memory_order_relaxed);
	}
	if (auto memory = std::malloc(a_size == 0 ? 1 : a_size)) {
		return memory;
	}
	throw std::bad_alloc{};
}

void* operator new[](std::size_t a_size)
{
	if (countAllocations.load(std::memory_order_acquire)) {
		allocationCount.fetch_add(1, std::memory_order_relaxed);
	}
	if (auto memory = std::malloc(a_size == 0 ? 1 : a_size)) {
		return memory;
	}
	throw std::bad_alloc{};
}

void operator delete(void* a_memory) noexcept { std::free(a_memory); }
void operator delete(void* a_memory, std::size_t) noexcept { std::free(a_memory); }
void operator delete[](void* a_memory) noexcept { std::free(a_memory); }
void operator delete[](void* a_memory, std::size_t) noexcept { std::free(a_memory); }

int main()
{
	TestSelfRemovalAndReentry();
	TestRemoveFutureCallbackInBatch();
	TestCrossRegistryReentrantRemoval();
	TestStatefulCallbackPersists();
	TestCapturedSnapshotMembership();
	TestDispatchDoesNotAllocateAfterRegistration();
	TestExternalRemoveWaitsForInvocationAndDestruction();
	TestCaptureDestructorReentry();
	TestCaptureDestructorReentryAfterCallbackRemoval();
	TestExceptionsReleaseInvocationAccounting();
	TestConcurrentAddRemoveAndDispatch();
}
