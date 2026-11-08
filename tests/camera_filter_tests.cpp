#include "CollisionFilter.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

static_assert(CollisionFilter::IsCameraPrecisionPair(39, 56));
static_assert(CollisionFilter::IsCameraPrecisionPair(58, 36));
static_assert(!CollisionFilter::IsCameraPrecisionPair(39, 1));
static_assert(!CollisionFilter::IsCameraPrecisionPair(30, 56));

int main()
{
	constexpr std::array<std::array<std::uint32_t, 2>, 12> excludedPairs{ { { 36, 56 }, { 36, 57 }, { 36, 58 }, { 39, 56 }, { 39, 57 }, { 39, 58 },
		{ 56, 36 }, { 57, 36 }, { 58, 36 }, { 56, 39 }, { 57, 39 }, { 58, 39 } } };
	constexpr std::array<std::uint32_t, 3> groups{ 0, 1, 0xffff };
	constexpr std::array<std::uint32_t, 4> flags{ 0, 0x80, 0x4000, 0xff80 };
	std::size_t checks = 0;
	for (std::uint32_t layerA = 0; layerA < 128; ++layerA) {
		for (std::uint32_t layerB = 0; layerB < 128; ++layerB) {
			bool expected = false;
			for (const auto& pair : excludedPairs) {
				expected = expected || (pair[0] == layerA && pair[1] == layerB);
			}
			for (auto groupA : groups) {
				for (auto groupB : groups) {
					for (auto flagA : flags) {
						for (auto flagB : flags) {
							const auto filterA = (groupA << 16) | flagA | layerA;
							const auto filterB = (groupB << 16) | flagB | layerB;
							if (CollisionFilter::IsCameraPrecisionPair(filterA, filterB) != expected) {
								std::cerr << "Unexpected camera policy for " << filterA << ", " << filterB << '\n';
								return EXIT_FAILURE;
							}
							++checks;
						}
					}
				}
			}
		}
	}
	std::cout << "Camera filter policy passed " << checks << " combinations\n";
	return EXIT_SUCCESS;
}
