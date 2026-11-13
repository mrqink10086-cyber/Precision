#pragma once

#include <cstddef>

#include <RE/N/NiBound.h>
#include <RE/N/NiTransform.h>

namespace ModelBounds
{
	[[nodiscard]] inline RE::NiBound Transform(RE::NiBound a_bound, const RE::NiTransform& a_local, const RE::NiTransform& a_parent)
	{
		a_bound.center = a_local * a_bound.center;
		a_bound.radius *= a_local.scale;
		a_bound.center = a_parent * a_bound.center;
		a_bound.radius *= a_parent.scale;
		return a_bound;
	}
}
