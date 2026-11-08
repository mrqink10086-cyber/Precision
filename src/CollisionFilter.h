#pragma once

#include <cstdint>

enum class CollisionLayer
{
	kUnidentified = 0,
	kStatic = 1,
	kAnimStatic = 2,
	kTransparent = 3,
	kClutter = 4,
	kWeapon = 5,
	kProjectile = 6,
	kSpell = 7,
	kBiped = 8,
	kTrees = 9,
	kProps = 10,
	kWater = 11,
	kTrigger = 12,
	kTerrain = 13,
	kTrap = 14,
	kNonCollidable = 15,
	kCloudTrap = 16,
	kGround = 17,
	kPortal = 18,
	kDebrisSmall = 19,
	kDebrisLarge = 20,
	kAcousticSpace = 21,
	kActorZone = 22,
	kProjectileZone = 23,
	kGasTrap = 24,
	kShellCasting = 25,
	kTransparentWall = 26,
	kInvisibleWall = 27,
	kTransparentSmallAnim = 28,
	kClutterLarge = 29,
	kCharController = 30,
	kStairHelper = 31,
	kDeadBip = 32,
	kBipedNoCC = 33,
	kAvoidBox = 34,
	kCollisionBox = 35,
	kCameraSphere = 36,
	kDoorDetection = 37,
	kConeProjectile = 38,
	kCamera = 39,
	kItemPicker = 40,
	kLOS = 41,
	kPathingPick = 42,
	kUnused0 = 43,
	kUnused1 = 44,
	kSpellExplosion = 45,
	kDroppingPick = 46,
	kPrecisionAttack = 56,
	kPrecisionBody = 57,
	kPrecisionRecoil = 58
};

namespace CollisionFilter
{
	[[nodiscard]] constexpr bool IsCameraPrecisionPair(std::uint32_t a_filterInfoA, std::uint32_t a_filterInfoB) noexcept
	{
		const auto layerA = static_cast<CollisionLayer>(a_filterInfoA & 0x7f);
		const auto layerB = static_cast<CollisionLayer>(a_filterInfoB & 0x7f);
		const auto isCamera = [](CollisionLayer a_layer) {
			return a_layer == CollisionLayer::kCameraSphere || a_layer == CollisionLayer::kCamera;
		};
		const auto isPrecision = [](CollisionLayer a_layer) {
			return a_layer == CollisionLayer::kPrecisionAttack || a_layer == CollisionLayer::kPrecisionBody || a_layer == CollisionLayer::kPrecisionRecoil;
		};
		return (isCamera(layerA) && isPrecision(layerB)) || (isPrecision(layerA) && isCamera(layerB));
	}
}
