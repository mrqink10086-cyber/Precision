#include "../src/RuntimeAlgorithms.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	std::atomic<bool> g_countAllocations{ false };
	std::atomic<std::size_t> g_allocationCount{ 0 };

	void Require(bool a_condition, const std::string& a_message)
	{
		if (!a_condition) {
			throw std::runtime_error(a_message);
		}
	}

	std::vector<std::string_view> LegacyTokenize(std::string_view a_text, char a_delimiter)
	{
		std::size_t start = 0;
		std::size_t end = a_text.find_first_of(a_delimiter);
		std::vector<std::string_view> output;

		while (end <= std::string_view::npos) {
			auto substring = a_text.substr(start, end - start);
			if (!substring.empty()) {
				output.emplace_back(substring);
			}

			if (end == std::string_view::npos) {
				break;
			}
			start = end + 1;
			end = a_text.find_first_of(a_delimiter, start);
		}
		return output;
	}

	std::vector<std::string_view> CollectTokens(const RuntimeAlgorithms::TokenRange& a_range)
	{
		std::vector<std::string_view> result;
		for (std::string_view token : a_range) {
			result.push_back(token);
		}
		return result;
	}

	void TestTokenRange()
	{
		using RuntimeAlgorithms::TokenRange;
		static_assert(std::is_same_v<decltype(*std::declval<TokenRange::Iterator>()), std::string_view>);
		static_assert(requires(const std::unordered_map<std::string, std::string, RuntimeAlgorithms::TransparentStringHash, std::equal_to<>>& a_map, std::string_view a_key) {
			a_map.find(a_key);
		});

		const std::vector<std::string> examples{
			"", "|", "||", "a", "a|b", "|a", "a|", "a||b", "a|||b", "  | \t |x y|", "\n|\t"
		};
		for (const auto& text : examples) {
			const auto expected = LegacyTokenize(text, '|');
			const TokenRange actual(text, '|');
			Require(CollectTokens(actual) == expected, "TokenRange differs from the legacy tokenizer");
			Require(actual.empty() == expected.empty(), "TokenRange::empty disagrees with legacy tokenizer");
		}

		const std::string nulText("a\0b|| c\0| ", 10);
		const std::string_view explicitNulView(nulText.data(), nulText.size());
		Require(CollectTokens(TokenRange(explicitNulView, '|')) == LegacyTokenize(explicitNulView, '|'), "TokenRange lost embedded NUL bytes");

		constexpr std::uint32_t seed = 0x51A7E123U;
		std::mt19937 random(seed);
		const std::array<char, 8> alphabet{ 'a', 'Z', ' ', '\t', '\n', '|', ';', '\0' };
		for (int sample = 0; sample < 3000; ++sample) {
			std::string text;
			const auto length = random() % 180;
			text.reserve(length);
			for (std::size_t index = 0; index < length; ++index) {
				text.push_back(alphabet[random() % alphabet.size()]);
			}
			const char delimiter = (random() % 3 == 0) ? '\0' : ((random() % 2 == 0) ? '|' : ';');
			const std::string_view view(text.data(), text.size());
			const auto expected = LegacyTokenize(view, delimiter);
			const TokenRange actual(view, delimiter);
			Require(CollectTokens(actual) == expected, "randomized TokenRange differential failed (seed=0x51A7E123)");
			Require(actual.empty() == expected.empty(), "randomized TokenRange::empty differential failed (seed=0x51A7E123)");
		}

		const std::string noAllocationFixture = "one|two||three| four |five";
		const TokenRange noAllocationRange(noAllocationFixture, '|');
		g_allocationCount.store(0, std::memory_order_relaxed);
		g_countAllocations.store(true, std::memory_order_relaxed);
		std::size_t byteChecksum = 0;
		for (std::string_view token : noAllocationRange) {
			byteChecksum += token.size();
		}
		g_countAllocations.store(false, std::memory_order_relaxed);
		Require(byteChecksum == 21, "TokenRange allocation-check fixture checksum changed");
		Require(g_allocationCount.load(std::memory_order_relaxed) == 0, "TokenRange iteration allocated memory");
	}

	using StringMap = std::unordered_map<std::string, std::string, RuntimeAlgorithms::TransparentStringHash, std::equal_to<>>;

	void TestTransparentStringMap()
	{
		static_assert(std::is_invocable_r_v<std::size_t, RuntimeAlgorithms::TransparentStringHash, std::string_view>);
		static_assert(std::is_same_v<typename StringMap::hasher::is_transparent, void>);

		StringMap values;
		const std::string shortKey = "";
		const std::string longKey = "ModernStaggerLock_17";
		const std::string nulKey("key\0tail", 8);
		const std::string longValue(256, 'v');
		const std::string nulValue("payload\0suffix", 14);

		const std::string_view longKeyView(longKey.data(), longKey.size());
		const std::string_view nulKeyView(nulKey.data(), nulKey.size());
		const std::string_view emptyKeyView(shortKey.data(), shortKey.size());
		Require(RuntimeAlgorithms::TransparentStringHash{}(longKeyView) == RuntimeAlgorithms::TransparentStringHash{}(std::string_view(longKey)), "hash differs for equivalent string and string_view");

		const std::string callerPayload = longValue;
		std::string firstCopy = callerPayload;
		auto [insertedAt, inserted] = values.try_emplace(std::string(longKeyView), std::move(firstCopy));
		Require(inserted, "transparent-map test insertion unexpectedly found an existing key");
		Require(callerPayload == longValue, "moving the first payload copy changed the caller's source");
		Require(insertedAt->second == longValue, "map did not take ownership of the moved payload");
		Require(values.find(longKeyView) == insertedAt, "heterogeneous lookup failed for a long key");
		const std::string replacementSource = "replacement-payload";
		std::string longReplacement = replacementSource;
		values.insert_or_assign(std::string(longKeyView), std::move(longReplacement));
		Require(replacementSource == "replacement-payload" && values.find(longKeyView)->second == replacementSource, "replacement did not preserve caller ownership and replace the mapped value");

		values.try_emplace(std::string(nulKeyView), "initial-nul-value");
		std::string replacementCopy = nulValue;
		values.insert_or_assign(std::string(nulKeyView), std::move(replacementCopy));
		values.try_emplace(std::string(emptyKeyView), "empty-key-value");
		Require(values.find(nulKeyView)->second == nulValue, "embedded-NUL key/value ownership failed");
		Require(values.find(emptyKeyView)->second == "empty-key-value", "empty key insertion/lookup failed");

		std::string mutableKey = "owned-key";
		std::string mutableValue = "owned-value";
		values.try_emplace(std::string(mutableKey), std::string_view(mutableValue));
		mutableKey.assign("overwritten-key");
		mutableValue.assign("overwritten-value");
		Require(values.find(std::string_view("owned-key"))->second == "owned-value", "map retained views instead of owning inserted strings");
	}

	struct Trace
	{
		std::vector<std::string> events;
		std::vector<std::vector<int>> survivorsByFrame;
		std::vector<std::vector<int>> callsByFrame;
		std::vector<int> completionFrame;
		int frame = 0;
	};

	struct TracedJob
	{
		TracedJob(int a_id, int a_runsToComplete, Trace& a_trace) :
			id(a_id), runsToComplete(a_runsToComplete), trace(&a_trace) {}

		~TracedJob()
		{
			trace->events.push_back("F" + std::to_string(trace->frame) + ":D" + std::to_string(id));
		}

		bool Run()
		{
			trace->events.push_back("F" + std::to_string(trace->frame) + ":R" + std::to_string(id));
			++trace->callsByFrame.back()[id];
			return --runsToComplete == 0;
		}

		int id;
		int runsToComplete;
		Trace* trace;
	};

	template <class Pointer, bool UseNew>
	Trace RunScenario(const std::vector<int>& a_runsToComplete, int a_frames, const std::vector<bool>& a_externalOwners = {})
	{
		Trace trace;
		trace.completionFrame.assign(a_runsToComplete.size(), -1);
		trace.events.reserve(a_runsToComplete.size() * static_cast<std::size_t>(a_frames + 2));
		{
			std::vector<Pointer> jobs;
			jobs.reserve(a_runsToComplete.size());
			std::vector<std::shared_ptr<TracedJob>> external;
			for (std::size_t id = 0; id < a_runsToComplete.size(); ++id) {
				Pointer job(new TracedJob(static_cast<int>(id), a_runsToComplete[id], trace));
				if constexpr (std::is_same_v<Pointer, std::shared_ptr<TracedJob>>) {
					if (id < a_externalOwners.size() && a_externalOwners[id]) {
						external.push_back(job);
					}
				}
				jobs.push_back(std::move(job));
			}

			for (int frame = 0; frame < a_frames; ++frame) {
				trace.frame = frame;
				trace.callsByFrame.emplace_back(a_runsToComplete.size(), 0);
				auto remove = [&](const Pointer& a_job) {
					const bool complete = a_job->Run();
					if (complete && trace.completionFrame[a_job->id] < 0) {
						trace.completionFrame[a_job->id] = frame;
					}
					return complete;
				};
				if constexpr (UseNew) {
					RuntimeAlgorithms::ProcessRetained(jobs, remove);
				} else {
					for (auto it = jobs.begin(); it != jobs.end();) {
						if (remove(*it)) {
							it = jobs.erase(it);
						} else {
							++it;
						}
					}
				}
				std::vector<int> survivors;
				survivors.reserve(jobs.size());
				for (const auto& job : jobs) {
					survivors.push_back(job->id);
				}
				trace.survivorsByFrame.push_back(std::move(survivors));
			}

			trace.frame = a_frames;
			jobs.clear();
			external.clear();
		}
		return trace;
	}

	void RequireSameTrace(const Trace& a_old, const Trace& a_new, const std::string& a_case)
	{
		Require(a_new.events == a_old.events, a_case + ": Run/destructor trace differs from erase loop");
		Require(a_new.survivorsByFrame == a_old.survivorsByFrame, a_case + ": survivor identity/order differs from erase loop");
		Require(a_new.callsByFrame == a_old.callsByFrame, a_case + ": per-frame Run counters differ from erase loop");
		Require(a_new.completionFrame == a_old.completionFrame, a_case + ": completion frames differ from erase loop");
	}

	void TestRetentionCompaction()
	{
		const std::vector<std::vector<int>> scenarios{
			{}, { 1, 1, 1, 1 }, { 9, 9, 9 }, { 1, 3, 2, 5, 1, 4 }
		};
		for (const auto& durations : scenarios) {
			const auto oldUnique = RunScenario<std::unique_ptr<TracedJob>, false>(durations, 4);
			const auto newUnique = RunScenario<std::unique_ptr<TracedJob>, true>(durations, 4);
			RequireSameTrace(oldUnique, newUnique, "unique_ptr scenario");
			const auto oldShared = RunScenario<std::shared_ptr<TracedJob>, false>(durations, 4);
			const auto newShared = RunScenario<std::shared_ptr<TracedJob>, true>(durations, 4);
			RequireSameTrace(oldShared, newShared, "shared_ptr scenario");
		}

		const auto immediate = RunScenario<std::unique_ptr<TracedJob>, true>({ 1, 1, 1 }, 1);
		const std::vector<std::string> immediatePrefix{ "F0:R0", "F0:D0", "F0:R1", "F0:D1", "F0:R2", "F0:D2" };
		Require(immediate.events.size() >= immediatePrefix.size() && std::equal(immediatePrefix.begin(), immediatePrefix.end(), immediate.events.begin()), "completed unique_ptr jobs were not destroyed immediately after their predicate");

		const std::vector<bool> externalOwners{ true, false, true, false };
		const auto oldExternal = RunScenario<std::shared_ptr<TracedJob>, false>({ 1, 1, 1, 1 }, 2, externalOwners);
		const auto newExternal = RunScenario<std::shared_ptr<TracedJob>, true>({ 1, 1, 1, 1 }, 2, externalOwners);
		RequireSameTrace(oldExternal, newExternal, "shared_ptr external-owner scenario");
		for (const int id : { 0, 2 }) {
			const std::string releasedAtEnd = "F2:D" + std::to_string(id);
			Require(std::find(newExternal.events.begin(), newExternal.events.end(), releasedAtEnd) != newExternal.events.end(), "shared job with an external owner was destroyed before its final owner released it");
		}

		std::vector<std::unique_ptr<int>> empty;
		RuntimeAlgorithms::ProcessRetained(empty, [](const auto&) { return true; });
		std::vector<std::unique_ptr<int>> unchanged;
		for (int value : { 4, 7, 9 }) {
			unchanged.push_back(std::make_unique<int>(value));
		}
		std::vector<const int*> before;
		for (const auto& item : unchanged) {
			before.push_back(item.get());
		}
		RuntimeAlgorithms::ProcessRetained(unchanged, [](const auto&) { return false; });
		Require(unchanged.size() == before.size(), "no-op compaction changed vector size");
		for (std::size_t index = 0; index < before.size(); ++index) {
			Require(unchanged[index].get() == before[index], "no-op compaction moved or reordered a stable pointer");
		}

		constexpr std::uint32_t seed = 0xC0FFEE42U;
		std::mt19937 random(seed);
		for (int sample = 0; sample < 250; ++sample) {
			const std::size_t count = random() % 48;
			const int frames = 1 + static_cast<int>(random() % 9);
			std::vector<int> durations(count);
			std::vector<bool> external(count, false);
			for (std::size_t id = 0; id < count; ++id) {
				durations[id] = 1 + static_cast<int>(random() % 11);
				external[id] = random() % 4 == 0;
			}
			RequireSameTrace(
				RunScenario<std::unique_ptr<TracedJob>, false>(durations, frames),
				RunScenario<std::unique_ptr<TracedJob>, true>(durations, frames),
				"random unique_ptr differential (seed=0xC0FFEE42)");
			RequireSameTrace(
				RunScenario<std::shared_ptr<TracedJob>, false>(durations, frames, external),
				RunScenario<std::shared_ptr<TracedJob>, true>(durations, frames, external),
				"random shared_ptr differential (seed=0xC0FFEE42)");
		}
	}

	struct PredicateFailure
	{};

	template <class Pointer, bool UseNew>
	Trace RunThrowingResumeScenario()
	{
		Trace trace;
		const std::vector<int> runsToComplete{ 1, 4, 2, 3, 5 };
		trace.completionFrame.assign(runsToComplete.size(), -1);
		{
			std::vector<Pointer> jobs;
			for (std::size_t id = 0; id < runsToComplete.size(); ++id) {
				jobs.emplace_back(new TracedJob(static_cast<int>(id), runsToComplete[id], trace));
			}

			trace.frame = 0;
			trace.callsByFrame.emplace_back(runsToComplete.size(), 0);
			auto throwingPredicate = [&](const Pointer& a_job) {
				const bool complete = a_job->Run();
				if (complete) {
					trace.completionFrame[a_job->id] = trace.frame;
				}
				if (a_job->id == 4) {
					throw PredicateFailure{};
				}
				return complete;
			};

			bool caught = false;
			try {
				if constexpr (UseNew) {
					RuntimeAlgorithms::ProcessRetained(jobs, throwingPredicate);
				} else {
					for (auto it = jobs.begin(); it != jobs.end();) {
						if (throwingPredicate(*it)) {
							it = jobs.erase(it);
						} else {
							++it;
						}
					}
				}
			} catch (const PredicateFailure&) {
				caught = true;
			}
			Require(caught, "throwing retention predicate did not propagate its exception");
			std::vector<int> afterThrow;
			for (const auto& job : jobs) {
				Require(static_cast<bool>(job), "predicate exception left a null pointer in the compacted vector");
				afterThrow.push_back(job->id);
			}
			trace.survivorsByFrame.push_back(std::move(afterThrow));

			trace.frame = 1;
			trace.callsByFrame.emplace_back(runsToComplete.size(), 0);
			auto remove = [&](const Pointer& a_job) {
				const bool complete = a_job->Run();
				if (complete && trace.completionFrame[a_job->id] < 0) {
					trace.completionFrame[a_job->id] = trace.frame;
				}
				return complete;
			};
			if constexpr (UseNew) {
				RuntimeAlgorithms::ProcessRetained(jobs, remove);
			} else {
				for (auto it = jobs.begin(); it != jobs.end();) {
					if (remove(*it)) {
						it = jobs.erase(it);
					} else {
						++it;
					}
				}
			}
			std::vector<int> afterResume;
			for (const auto& job : jobs) {
				Require(static_cast<bool>(job), "resumed pass encountered a null pointer");
				afterResume.push_back(job->id);
			}
			trace.survivorsByFrame.push_back(std::move(afterResume));
			trace.frame = 2;
			jobs.clear();
		}
		return trace;
	}

	void TestPredicateExceptionRecovery()
	{
		RequireSameTrace(
			RunThrowingResumeScenario<std::unique_ptr<TracedJob>, false>(),
			RunThrowingResumeScenario<std::unique_ptr<TracedJob>, true>(),
			"unique_ptr throw-mid-pass/resume differential");
		RequireSameTrace(
			RunThrowingResumeScenario<std::shared_ptr<TracedJob>, false>(),
			RunThrowingResumeScenario<std::shared_ptr<TracedJob>, true>(),
			"shared_ptr throw-mid-pass/resume differential");
	}

	struct DelayedTrace : Trace
	{};

	struct DelayedJob
	{
		DelayedJob(int a_id, float a_delay, DelayedTrace& a_trace) :
			id(a_id), remaining(a_delay), trace(&a_trace) {}
		~DelayedJob() { trace->events.push_back("F" + std::to_string(trace->frame) + ":D" + std::to_string(id)); }

		bool Run(float a_deltaTime)
		{
			trace->events.push_back("F" + std::to_string(trace->frame) + ":R" + std::to_string(id));
			++trace->callsByFrame.back()[id];
			remaining -= a_deltaTime;
			return remaining <= 0.f;
		}

		int id;
		float remaining;
		DelayedTrace* trace;
	};

	template <bool UseNew>
	DelayedTrace RunDelayedScenario(const std::vector<float>& a_delays, const std::vector<float>& a_deltaTimes)
	{
		DelayedTrace trace;
		trace.completionFrame.assign(a_delays.size(), -1);
		{
			std::vector<std::unique_ptr<DelayedJob>> jobs;
			for (std::size_t id = 0; id < a_delays.size(); ++id) {
				jobs.push_back(std::make_unique<DelayedJob>(static_cast<int>(id), a_delays[id], trace));
			}
			for (std::size_t frame = 0; frame < a_deltaTimes.size(); ++frame) {
				trace.frame = static_cast<int>(frame);
				trace.callsByFrame.emplace_back(a_delays.size(), 0);
				auto remove = [&](const std::unique_ptr<DelayedJob>& a_job) {
					const bool complete = a_job->Run(a_deltaTimes[frame]);
					if (complete && trace.completionFrame[a_job->id] < 0) {
						trace.completionFrame[a_job->id] = static_cast<int>(frame);
					}
					return complete;
				};
				if constexpr (UseNew) {
					RuntimeAlgorithms::ProcessRetained(jobs, remove);
				} else {
					for (auto it = jobs.begin(); it != jobs.end();) {
						if (remove(*it)) {
							it = jobs.erase(it);
						} else {
							++it;
						}
					}
				}
				std::vector<int> survivors;
				for (const auto& job : jobs) {
					survivors.push_back(job->id);
				}
				trace.survivorsByFrame.push_back(std::move(survivors));
			}
			trace.frame = static_cast<int>(a_deltaTimes.size());
			jobs.clear();
		}
		return trace;
	}

	void TestDelayedJobFrames()
	{
		const std::vector<float> delays{ 0.05f, 0.2f, 0.3f, 0.7f, 1.2f };
		const std::vector<float> deltaTimes{ 0.1f, 0.13f, 0.22f, 0.4f, 0.17f, 0.5f };
		const auto oldTrace = RunDelayedScenario<false>(delays, deltaTimes);
		const auto newTrace = RunDelayedScenario<true>(delays, deltaTimes);
		RequireSameTrace(oldTrace, newTrace, "delayed Run(dt) frame differential");
		Require(newTrace.completionFrame == std::vector<int>{ 0, 1, 2, 3, 5 }, "delayed jobs completed on unexpected frames");
	}

	void TestLockedVisitAndClear()
	{
		std::mutex queueMutex;
		std::vector<int> pending{ 10, 20 };
		std::mutex barrierMutex;
		std::condition_variable barrier;
		bool visitorActive = false;
		bool producerAttempted = false;
		bool producerWasBlocked = false;
		std::vector<int> firstVisit;
		std::vector<int> nextVisit;

		std::thread producer([&]() {
			{
				std::unique_lock barrierLock(barrierMutex);
				barrier.wait(barrierLock, [&]() { return visitorActive; });
			}

			{
				std::unique_lock queueLock(queueMutex, std::try_to_lock);
				producerWasBlocked = !queueLock.owns_lock();
			}

			{
				std::lock_guard barrierLock(barrierMutex);
				producerAttempted = true;
			}
			barrier.notify_all();

			std::lock_guard queueLock(queueMutex);
			pending.push_back(30);
		});

		RuntimeAlgorithms::ProcessAndClearLocked(pending, queueMutex, [&](int a_event) {
			firstVisit.push_back(a_event);
			if (a_event == 10) {
				std::unique_lock barrierLock(barrierMutex);
				visitorActive = true;
				barrier.notify_all();
				barrier.wait(barrierLock, [&]() { return producerAttempted; });
			}
		});
		producer.join();

		Require(producerWasBlocked, "producer acquired the pending-event mutex during the visitor callback");
		Require(firstVisit == std::vector<int>{ 10, 20 }, "locked visitor did not preserve FIFO order for the original batch");
		Require(pending == std::vector<int>{ 30 }, "event enqueued during visitation was erased with the original batch");

		RuntimeAlgorithms::ProcessAndClearLocked(pending, queueMutex, [&](int a_event) { nextVisit.push_back(a_event); });
		Require(nextVisit == std::vector<int>{ 30 }, "event enqueued during visitation was not consumed exactly on the next pass");
		RuntimeAlgorithms::ProcessAndClearLocked(pending, queueMutex, [&](int a_event) { nextVisit.push_back(a_event); });
		Require(pending.empty(), "locked visit-and-clear left events queued after the next pass");
		Require(nextVisit == std::vector<int>{ 30 }, "empty locked pass visited an event more than once");
	}
}

void* operator new(std::size_t a_size)
{
	if (void* memory = std::malloc(a_size == 0 ? 1 : a_size)) {
		if (g_countAllocations.load(std::memory_order_relaxed)) {
			g_allocationCount.fetch_add(1, std::memory_order_relaxed);
		}
		return memory;
	}
	throw std::bad_alloc();
}

void* operator new[](std::size_t a_size)
{
	return ::operator new(a_size);
}

void operator delete(void* a_memory) noexcept { std::free(a_memory); }
void operator delete[](void* a_memory) noexcept { std::free(a_memory); }
void operator delete(void* a_memory, std::size_t) noexcept { std::free(a_memory); }
void operator delete[](void* a_memory, std::size_t) noexcept { std::free(a_memory); }

int main()
{
	try {
		TestTokenRange();
		TestTransparentStringMap();
		TestRetentionCompaction();
		TestPredicateExceptionRecovery();
		TestDelayedJobFrames();
		TestLockedVisitAndClear();
		std::cout << "runtime tests passed (token seed=0x51A7E123, retention seed=0xC0FFEE42)\n";
		return 0;
	} catch (const std::exception& exception) {
		g_countAllocations.store(false, std::memory_order_relaxed);
		std::cerr << "runtime test failure: " << exception.what() << '\n';
		return 1;
	}
}
