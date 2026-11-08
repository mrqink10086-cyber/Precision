#pragma once

#pragma warning(push)
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#pragma warning(disable: 4702)
#include "ModAPI.h"
#include <SimpleIni.h>
#include <toml++/toml.h>

#pragma warning(disable: 4201)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#ifdef NDEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif
#pragma warning(pop)

#include <shared_mutex>

#include "CollisionFilter.h"

using namespace std::literals;

namespace logger = SKSE::log;

namespace util
{
	using SKSE::stl::report_and_fail;
}

namespace std
{
	template <class T>
	struct hash<RE::BSPointerHandle<T>>
	{
		uint32_t operator()(const RE::BSPointerHandle<T>& a_handle) const
		{
			uint32_t nativeHandle = const_cast<RE::BSPointerHandle<T>*>(&a_handle)->native_handle();  // ugh
			return nativeHandle;
		}
	};
}

namespace stl
{
	using namespace SKSE::stl;

	template <class T>
	void write_thunk_call(std::uintptr_t a_src)
	{
		SKSE::AllocTrampoline(14);

		auto& trampoline = SKSE::GetTrampoline();
		T::func = trampoline.write_call<5>(a_src, T::thunk);
	}
}

using Lock = std::shared_mutex;
using ReadLocker = std::shared_lock<Lock>;
using WriteLocker = std::unique_lock<Lock>;

enum class WeaponType
{
	// RE::WEAPON_TYPE
	kHandToHandMelee = 0,
	kOneHandSword = 1,
	kOneHandDagger = 2,
	kOneHandAxe = 3,
	kOneHandMace = 4,
	kTwoHandSword = 5,
	kTwoHandAxe = 6,
	kBow = 7,
	kStaff = 8,
	kCrossbow = 9,

	// added
	kShield = 10
};

#define DLLEXPORT __declspec(dllexport)

[[nodiscard]] inline std::size_t RelocationOffset(
	std::size_t a_se,
	std::size_t a_ae) noexcept
{
	return REL::VariantOffset(a_se, a_ae, 0).offset();
}

[[nodiscard]] inline std::size_t RelocationOffset(
	std::size_t a_se,
	std::size_t a_aePre1799,
	std::size_t a_ae1799) noexcept
{
	return REL::VariantOffset(
		a_se,
		(REL::Module::IsAtLeast(SKSE::RUNTIME_SSE_1_7_99) ? a_ae1799 : a_aePre1799),
		0)
	    .offset();
}

#define RELOCATION_OFFSET(...) RelocationOffset(__VA_ARGS__)

#include "Plugin.h"
