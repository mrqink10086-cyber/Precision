#include "../src/TrailHistory.h"

#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			throw std::runtime_error(a_message);
		}
	}

	void TestWarmupOrderAndBoundedGrowth()
	{
		Precision::TrailHistory<int, 4> history;
		for (int sample = 1; sample <= 3; ++sample) {
			history.Push(sample);
			Require(history.size() == static_cast<std::size_t>(sample), "history warmup size changed");
			Require(history.Newest() == sample, "newest sample order changed during warmup");
		}

		history.Push(4);
		Require(history.size() == 4, "history did not retain the fourth warmup sample");
		for (int sample = 5; sample <= 100000; ++sample) {
			history.Push(sample);
			Require(history.size() == 4, "history grew past its capacity");
		}

		Require(history.Newest(0) == 100000, "latest history sample is not first");
		Require(history.Newest(1) == 99999, "second latest history sample order changed");
		Require(history.Newest(2) == 99998, "third latest history sample order changed");
		Require(history.Newest(3) == 99997, "fourth latest history sample order changed");
	}
}

int main()
{
	try {
		TestWarmupOrderAndBoundedGrowth();
		std::cout << "trail history tests passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "trail history test failure: " << exception.what() << '\n';
		return 1;
	}
}
