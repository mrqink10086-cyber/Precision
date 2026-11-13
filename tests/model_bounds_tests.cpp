#include "ModelBounds.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
	void RequireNear(float a_actual, float a_expected, const char* a_message)
	{
		if (!std::isfinite(a_actual) || std::fabs(a_actual - a_expected) > 0.0001f) {
			std::cerr << a_message << ": expected " << a_expected << ", got " << a_actual << '\n';
			throw std::runtime_error(a_message);
		}
	}

	void RequireBound(const RE::NiBound& a_actual, const RE::NiBound& a_expected)
	{
		RequireNear(a_actual.center.x, a_expected.center.x, "bound center X");
		RequireNear(a_actual.center.y, a_expected.center.y, "bound center Y");
		RequireNear(a_actual.center.z, a_expected.center.z, "bound center Z");
		RequireNear(a_actual.radius, a_expected.radius, "bound radius");
	}

	void TestHalfTurnDoesNotInflateLength()
	{
		const RE::NiBound model{ { 0.f, 100.f, 0.f }, 10.f };
		RE::NiTransform local;
		local.rotate.entry[0][0] = -1.f;
		local.rotate.entry[1][1] = -1.f;
		local.translate = { 0.f, 150.f, 0.f };
		const auto result = ModelBounds::Transform(model, local, RE::NiTransform{});
		const float reach = result.radius + std::fabs(result.center.y);
		std::cout << "Half-turn fixture: expected reach 60, measured " << reach << '\n';
		RequireNear(reach, 60.f, "rotated weapon reach");
		RequireBound(result, { { 0.f, 50.f, 0.f }, 10.f });
		RequireBound(model, { { 0.f, 100.f, 0.f }, 10.f });
	}

	void TestIdentityAndTranslation()
	{
		const RE::NiBound model{ { 1.f, 2.f, 3.f }, 4.f };
		RequireBound(ModelBounds::Transform(model, RE::NiTransform{}, RE::NiTransform{}), model);
		RE::NiTransform local;
		local.scale = 2.f;
		local.translate = { 10.f, 20.f, 30.f };
		RequireBound(ModelBounds::Transform(model, local, RE::NiTransform{}), { { 12.f, 24.f, 36.f }, 8.f });
	}

	void TestLocalAndParentTransformOrder()
	{
		const RE::NiBound model{ { 1.f, 2.f, 3.f }, 4.f };
		RE::NiTransform local;
		local.rotate.entry[0][0] = 0.f;
		local.rotate.entry[0][1] = -1.f;
		local.rotate.entry[1][0] = 1.f;
		local.rotate.entry[1][1] = 0.f;
		local.scale = 2.f;
		local.translate = { 10.f, 20.f, 30.f };
		RE::NiTransform parent;
		parent.rotate.entry[1][1] = 0.f;
		parent.rotate.entry[1][2] = -1.f;
		parent.rotate.entry[2][1] = 1.f;
		parent.rotate.entry[2][2] = 0.f;
		parent.scale = 3.f;
		parent.translate = { -5.f, 7.f, 9.f };
		RequireBound(ModelBounds::Transform(model, local, parent), { { 13.f, -101.f, 75.f }, 24.f });
	}
}

int main()
{
	try {
		TestHalfTurnDoesNotInflateLength();
		TestIdentityAndTranslation();
		TestLocalAndParentTransformOrder();
		std::cout << "Model bounds tests passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "Model bounds test failure: " << exception.what() << '\n';
		return 1;
	}
}
