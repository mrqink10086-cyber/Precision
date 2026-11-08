#include "../src/RuntimeAlgorithms.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;
	using Microseconds = std::chrono::duration<double, std::micro>;
	using OldStringMap = std::unordered_map<std::string, std::string>;
	using NewStringMap = std::unordered_map<std::string, std::string, RuntimeAlgorithms::TransparentStringHash, std::equal_to<>>;

	constexpr int kWarmups = 2;
	constexpr int kSamples = 7;
	std::uint64_t g_checksumSink = 0;

	void Consume(std::uint64_t a_value) noexcept
	{
		g_checksumSink ^= a_value + 0x9E3779B97F4A7C15ULL + (g_checksumSink << 6) + (g_checksumSink >> 2);
	}

	template <class Measure>
	double MedianTime(Measure&& a_measure, std::uint64_t& a_combinedChecksum)
	{
		for (int warmup = 0; warmup < kWarmups; ++warmup) {
			const auto result = a_measure();
			Consume(result.second);
			a_combinedChecksum ^= result.second;
		}

		std::vector<double> samples;
		samples.reserve(kSamples);
		for (int sample = 0; sample < kSamples; ++sample) {
			const auto result = a_measure();
			samples.push_back(result.first);
			Consume(result.second);
			a_combinedChecksum ^= result.second;
		}
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	struct BenchJob
	{
		std::uint32_t id;
		bool expired;
	};

	using JobQueue = std::vector<std::unique_ptr<BenchJob>>;
	using JobFlags = std::vector<std::vector<unsigned char>>;
	using JobFixtures = std::vector<JobQueue>;

	JobFlags MakeJobFlags(std::size_t a_size, std::size_t a_repetitions, unsigned a_expiredPercent, std::mt19937& a_random)
	{
		JobFlags flags(a_repetitions, std::vector<unsigned char>(a_size, 0));
		const std::size_t expiredCount = a_size * a_expiredPercent / 100;
		for (auto& queueFlags : flags) {
			std::fill_n(queueFlags.begin(), expiredCount, static_cast<unsigned char>(1));
			std::shuffle(queueFlags.begin(), queueFlags.end(), a_random);
		}
		return flags;
	}

	JobFixtures MakeJobFixtures(const JobFlags& a_flags)
	{
		JobFixtures fixtures;
		fixtures.reserve(a_flags.size());
		for (const auto& queueFlags : a_flags) {
			JobQueue queue;
			queue.reserve(queueFlags.size());
			for (std::size_t id = 0; id < queueFlags.size(); ++id) {
				queue.push_back(std::make_unique<BenchJob>(BenchJob{ static_cast<std::uint32_t>(id), queueFlags[id] != 0 }));
			}
			fixtures.push_back(std::move(queue));
		}
		return fixtures;
	}

	std::pair<double, std::uint64_t> MeasureJobKernel(JobFixtures& a_fixtures, bool a_useNew)
	{
		std::uint64_t inputChecksum = 0;
		for (const auto& queue : a_fixtures) {
			for (const auto& job : queue) {
				inputChecksum = inputChecksum * 1099511628211ULL + job->id + (job->expired ? 1ULL : 0ULL);
			}
		}

		const auto start = Clock::now();
		for (auto& queue : a_fixtures) {
			if (a_useNew) {
				RuntimeAlgorithms::ProcessRetained(queue, [](const auto& a_job) { return a_job->expired; });
			} else {
				for (auto it = queue.begin(); it != queue.end();) {
					if ((*it)->expired) {
						it = queue.erase(it);
					} else {
						++it;
					}
				}
			}
		}
		const auto end = Clock::now();

		std::uint64_t checksum = 0;
		for (const auto& queue : a_fixtures) {
			for (const auto& job : queue) {
				checksum += (static_cast<std::uint64_t>(job->id) + 1) * 0x100000001B3ULL;
			}
		}
		return { Microseconds(end - start).count(), checksum ^ inputChecksum };
	}

	void BenchmarkJobCompaction()
	{
		std::cout << "jobs: median microseconds per batch; fixture construction/checksum excluded; seed=0xA11CE55\n";
		std::cout << "  size expired% erase_us compact_us checksum\n";
		std::mt19937 random(0xA11CE55U);
		for (const std::size_t size : { 32U, 256U, 4096U }) {
			const std::size_t repetitions = std::max<std::size_t>(20, 30000 / size);
			for (const unsigned expiredPercent : { 50U, 100U }) {
				std::vector<double> eraseSamples;
				std::vector<double> compactSamples;
				eraseSamples.reserve(kSamples);
				compactSamples.reserve(kSamples);
				std::uint64_t checksum = 0;
				for (int iteration = 0; iteration < kWarmups + kSamples; ++iteration) {
					const auto flags = MakeJobFlags(size, repetitions, expiredPercent, random);
					auto eraseFixtures = MakeJobFixtures(flags);
					auto compactFixtures = MakeJobFixtures(flags);
					auto eraseResult = MeasureJobKernel(eraseFixtures, false);
					auto compactResult = MeasureJobKernel(compactFixtures, true);
					if (eraseResult.second != compactResult.second) {
						throw std::runtime_error("job benchmark paths produced different runtime checksums");
					}
					Consume(eraseResult.second);
					Consume(compactResult.second);
					checksum ^= eraseResult.second + 0x9E3779B97F4A7C15ULL + static_cast<std::uint64_t>(iteration);
					if (iteration >= kWarmups) {
						eraseSamples.push_back(eraseResult.first);
						compactSamples.push_back(compactResult.first);
					}
				}
				std::sort(eraseSamples.begin(), eraseSamples.end());
				std::sort(compactSamples.begin(), compactSamples.end());
				std::cout << "  " << size << ' ' << expiredPercent << ' ' << eraseSamples[eraseSamples.size() / 2] << ' ' << compactSamples[compactSamples.size() / 2] << ' ' << checksum << '\n';
			}
		}
	}

	std::vector<std::string_view> LegacyTokenize(std::string_view a_text, char a_delimiter)
	{
		std::size_t start = 0;
		std::size_t end = a_text.find_first_of(a_delimiter);
		std::vector<std::string_view> output;
		while (end <= std::string_view::npos) {
			auto token = a_text.substr(start, end - start);
			if (!token.empty()) {
				output.push_back(token);
			}
			if (end == std::string_view::npos) {
				break;
			}
			start = end + 1;
			end = a_text.find_first_of(a_delimiter, start);
		}
		return output;
	}

	std::uint64_t HashToken(std::string_view a_token) noexcept
	{
		std::uint64_t hash = 1469598103934665603ULL;
		for (const unsigned char byte : a_token) {
			hash = (hash ^ byte) * 1099511628211ULL;
		}
		return hash;
	}

	std::string MakeTokenFixture(std::mt19937& a_random)
	{
		std::string text;
		text.reserve(6000);
		for (int token = 0; token < 420; ++token) {
			if (token % 17 == 0) {
				text.push_back('|');
			}
			const int length = 2 + static_cast<int>(a_random() % 11);
			for (int character = 0; character < length; ++character) {
				text.push_back(static_cast<char>('a' + a_random() % 26));
			}
			if (token % 13 == 0) {
				text.push_back(' ');
			}
			text.push_back('|');
		}
		return text;
	}

	std::pair<double, std::uint64_t> MeasureVectorTokenize(std::string_view a_text, std::size_t a_repetitions)
	{
		std::uint64_t checksum = 0;
		const auto start = Clock::now();
		for (std::size_t repetition = 0; repetition < a_repetitions; ++repetition) {
			const auto tokens = LegacyTokenize(a_text, '|');
			for (const auto token : tokens) {
				checksum += HashToken(token);
			}
		}
		const auto end = Clock::now();
		return { Microseconds(end - start).count(), checksum };
	}

	std::pair<double, std::uint64_t> MeasureRangeTokenize(std::string_view a_text, std::size_t a_repetitions)
	{
		std::uint64_t checksum = 0;
		const auto start = Clock::now();
		for (std::size_t repetition = 0; repetition < a_repetitions; ++repetition) {
			for (std::string_view token : RuntimeAlgorithms::TokenRange(a_text, '|')) {
				checksum += HashToken(token);
			}
		}
		const auto end = Clock::now();
		return { Microseconds(end - start).count(), checksum };
	}

	void BenchmarkTokenFixture(std::string_view a_label, std::string_view a_text, std::size_t a_repetitions)
	{
		std::uint64_t vectorChecksum = 0;
		std::uint64_t rangeChecksum = 0;
		const double vectorUs = MedianTime([&]() { return MeasureVectorTokenize(a_text, a_repetitions); }, vectorChecksum);
		const double rangeUs = MedianTime([&]() { return MeasureRangeTokenize(a_text, a_repetitions); }, rangeChecksum);
		if (vectorChecksum != rangeChecksum) {
			throw std::runtime_error("tokenization benchmark paths produced different runtime checksums");
		}
		const auto tokenCount = LegacyTokenize(a_text, '|').size();
		std::cout << "tokens " << a_label << ": " << tokenCount << " tokens, median microseconds per " << a_repetitions << " parses; vector_us=" << vectorUs << " range_us=" << rangeUs << " checksum=" << vectorChecksum << '\n';
	}

	void BenchmarkTokenization()
	{
		constexpr std::string_view representative = "Node(0x000001A2)|ID|NoRecoil|ModernStaggerLock(17)";
		BenchmarkTokenFixture("representative", representative, 5000);

		std::mt19937 random(0x70C3A11U);
		const auto text = MakeTokenFixture(random);
		std::cout << "  large fixture seed=0x70C3A11\n";
		BenchmarkTokenFixture("synthetic", text, 120);
	}

	std::string MakeJsonValue(std::size_t a_index, std::size_t a_size, std::mt19937& a_random)
	{
		std::string value = "{\"entry\":" + std::to_string(a_index) + ",\"data\":\"";
		while (value.size() + 2 < a_size) {
			value.push_back(static_cast<char>('a' + a_random() % 26));
		}
		value += "\"}";
		return value;
	}

	OldStringMap MakeOldPayloadMap(std::size_t a_keyCount, std::size_t a_payloadSize, std::mt19937& a_random)
	{
		OldStringMap values;
		values.reserve(a_keyCount);
		for (std::size_t index = 0; index < a_keyCount; ++index) {
			const std::string key = index == 0 ? "ModernStaggerLock" : "ExtraPayloadKey_" + std::to_string(index);
			values.emplace(key, MakeJsonValue(index, a_payloadSize, a_random));
		}
		return values;
	}

	NewStringMap MakeNewPayloadMap(const OldStringMap& a_source)
	{
		NewStringMap values;
		values.reserve(a_source.size());
		for (const auto& [key, value] : a_source) {
			values.emplace(key, value);
		}
		return values;
	}

	std::pair<double, std::uint64_t> MeasureOldPayloadCopies(const OldStringMap& a_source, std::size_t a_repetitions, std::string_view a_lookupKey)
	{
		const auto expected = a_source.find(std::string(a_lookupKey))->second;
		std::uint64_t checksum = 0;
		const auto start = Clock::now();
		for (std::size_t repetition = 0; repetition < a_repetitions; ++repetition) {
			auto byValueArgument = a_source;
			auto storedMap = byValueArgument;
			auto found = storedMap.find(std::string(a_lookupKey));
			if (found == storedMap.end() || found->second != expected) {
				throw std::runtime_error("old payload-map benchmark lookup missed");
			}
			checksum += found->second.size() + static_cast<unsigned char>(found->second.front());
		}
		const auto end = Clock::now();
		return { Microseconds(end - start).count(), checksum };
	}

	std::pair<double, std::uint64_t> MeasureMovedPayloadMap(const NewStringMap& a_source, std::size_t a_repetitions, std::string_view a_lookupKey)
	{
		const auto expected = a_source.find(a_lookupKey)->second;
		std::uint64_t checksum = 0;
		const auto start = Clock::now();
		for (std::size_t repetition = 0; repetition < a_repetitions; ++repetition) {
			auto byValueArgument = a_source;
			auto storedMap = std::move(byValueArgument);
			auto found = storedMap.find(a_lookupKey);
			if (found == storedMap.end() || found->second != expected) {
				throw std::runtime_error("moved payload-map benchmark lookup missed");
			}
			checksum += found->second.size() + static_cast<unsigned char>(found->second.front());
		}
		const auto end = Clock::now();
		return { Microseconds(end - start).count(), checksum };
	}

	void BenchmarkPayloadMaps()
	{
		std::cout << "payload maps: median microseconds per batch; fixture construction excluded, whole-map pass-by-value copies included; 17-byte ModernStaggerLock lookup; seed=0xBADC0DE\n";
		std::cout << "  keys value_bytes repetitions old_2copy_us copy_move_view_us checksum\n";
		std::mt19937 random(0xBADC0DEU);
		constexpr std::string_view lookupKey = "ModernStaggerLock";
		for (const std::size_t keyCount : { 1U, 4U }) {
			for (const std::size_t payloadSize : { 48U, 2048U }) {
				const auto oldSource = MakeOldPayloadMap(keyCount, payloadSize, random);
				const auto newSource = MakeNewPayloadMap(oldSource);
				const std::size_t repetitions = payloadSize > 1000 ? 100 : 180;
				std::uint64_t oldChecksum = 0;
				std::uint64_t newChecksum = 0;
				const double oldUs = MedianTime([&]() { return MeasureOldPayloadCopies(oldSource, repetitions, lookupKey); }, oldChecksum);
				const double newUs = MedianTime([&]() { return MeasureMovedPayloadMap(newSource, repetitions, lookupKey); }, newChecksum);
				if (oldChecksum != newChecksum) {
					throw std::runtime_error("payload-map benchmark paths produced different runtime checksums");
				}
				std::cout << "  " << keyCount << ' ' << payloadSize << ' ' << repetitions << ' ' << oldUs << ' ' << newUs << ' ' << oldChecksum << '\n';
			}
		}
	}
}

int main()
{
	try {
		std::cout << "C++20 runtime microbenchmarks; median of " << kSamples << " samples after " << kWarmups << " warmups (microseconds)\n";
		BenchmarkJobCompaction();
		BenchmarkTokenization();
		BenchmarkPayloadMaps();
		std::cout << "runtime checksum sink=" << g_checksumSink << '\n';
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "runtime benchmark failure: " << exception.what() << '\n';
		return 1;
	}
}
