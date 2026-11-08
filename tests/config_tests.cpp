#include "../src/CollisionConfig.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	void Require(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			throw std::runtime_error(std::string(a_message));
		}
	}

	bool ParseCollisionTable(const toml::table& a_table, CollisionConfig::ParsedCollision& a_out, std::string& a_error)
	{
		return CollisionConfig::Parse(a_table, a_out, a_error);
	}

	void TestDefaultsAndValidFields()
	{
		CollisionConfig::ParsedCollision parsed;
		std::string error;
		const auto minimalTable = toml::parse("NodeName = 'WEAPON'");
		Require(ParseCollisionTable(minimalTable, parsed, error), "valid minimal collision was rejected");
		Require(parsed.nodeName == "WEAPON", "NodeName was not preserved");
		Require(!parsed.ID && !parsed.noRecoil && !parsed.noTrail && !parsed.trailUseTrueLength && !parsed.weaponTip, "optional flags or ID changed their defaults");
		Require(parsed.damageMult == 1.f && parsed.duration == 0.f, "numeric defaults changed");
		Require(!parsed.durationMult && !parsed.delay && !parsed.radius && !parsed.radiusMult && !parsed.length && !parsed.lengthMult, "missing optional dimensions were materialized");
		Require(!parsed.rotation && !parsed.translation && !parsed.scale && !parsed.groundShake, "missing optional vectors were materialized");

		const auto fullTable = toml::parse("NodeName='WEAPON'\nID=12\nNoRecoil=true\nNoTrail=true\nTrailUseTrueLength=true\nWeaponTip=true\nDamageMult=1.5\nDuration=2.5\nDurationMult=2.0\nDelay=0.25\nRadius=4.0\nRadiusMult=1.25\nLength=7.0\nLengthMult=0.5\nRotation={x=10.0,z=30.0}\nTranslation={y=12.0}\nScale=0.0\nGroundShake={Strength=2.0,Frequency=4.0}");
		Require(ParseCollisionTable(fullTable, parsed, error), "valid collision fields were rejected");
		Require(parsed.ID == 12 && parsed.noRecoil && parsed.noTrail && parsed.trailUseTrueLength && parsed.weaponTip, "valid flags or ID were not parsed");
		Require(parsed.damageMult == 1.5f && parsed.duration == 2.5f && parsed.durationMult == 2.f && parsed.delay == 0.25f, "valid scalar values were not parsed");
		Require(parsed.radius == 4.f && parsed.radiusMult == 1.25f && parsed.length == 7.f && parsed.lengthMult == 0.5f, "valid dimensions were not parsed");
		Require(parsed.rotation == std::array<float, 3>{ 10.f, 0.f, 30.f }, "missing rotation axes did not default to zero");
		Require(parsed.translation == std::array<float, 3>{ 0.f, 12.f, 0.f }, "missing translation axes did not default to zero");
		Require(parsed.scale == 0.f, "explicit zero scale was treated as absent");
		Require(parsed.groundShake == std::array<float, 3>{ 2.f, 0.f, 4.f }, "missing ground shake values did not default to zero");

		const auto integerTable = toml::parse("NodeName='WEAPON'\nDamageMult=2\nDuration=3\nRadius=4");
		Require(ParseCollisionTable(integerTable, parsed, error), "integer TOML values were rejected for float fields");
		Require(parsed.damageMult == 2.f && parsed.duration == 3.f && parsed.radius == 4.f, "integer TOML values changed while parsing float fields");
	}

	void TestInvalidCollisionFields()
	{
		const std::vector<std::pair<std::string, std::string>> invalidCases{
			{ "", "NodeName" },
			{ "NodeName=42", "NodeName" },
			{ "NodeName='WEAPON'\nID=256", "ID" },
			{ "NodeName='WEAPON'\nNoRecoil='true'", "NoRecoil" },
			{ "NodeName='WEAPON'\nNoTrail='true'", "NoTrail" },
			{ "NodeName='WEAPON'\nTrailUseTrueLength='0'", "TrailUseTrueLength" },
			{ "NodeName='WEAPON'\nWeaponTip='yes'", "WeaponTip" },
			{ "NodeName='WEAPON'\nDamageMult='1.0'", "DamageMult" },
			{ "NodeName='WEAPON'\nDuration='2.0'", "Duration" },
			{ "NodeName='WEAPON'\nDurationMult='2.0'", "DurationMult" },
			{ "NodeName='WEAPON'\nDelay='0.2'", "Delay" },
			{ "NodeName='WEAPON'\nRadius='4'", "Radius" },
			{ "NodeName='WEAPON'\nRadiusMult='1.2'", "RadiusMult" },
			{ "NodeName='WEAPON'\nLength='7'", "Length" },
			{ "NodeName='WEAPON'\nLengthMult='0.5'", "LengthMult" },
			{ "NodeName='WEAPON'\nRotation='0,0,0'", "Rotation" },
			{ "NodeName='WEAPON'\nRotation={x='0'}", "Rotation.x" },
			{ "NodeName='WEAPON'\nTranslation=1", "Translation" },
			{ "NodeName='WEAPON'\nTranslation={z='0'}", "Translation.z" },
			{ "NodeName='WEAPON'\nScale='1'", "Scale" },
			{ "NodeName='WEAPON'\nGroundShake=1", "GroundShake" },
			{ "NodeName='WEAPON'\nGroundShake={Strength='1'}", "GroundShake.Strength" },
			{ "NodeName='WEAPON'\nGroundShake={Duration='1'}", "GroundShake.Duration" },
			{ "NodeName='WEAPON'\nGroundShake={Frequency='1'}", "GroundShake.Frequency" }
		};

		for (const auto& [source, field] : invalidCases) {
			CollisionConfig::ParsedCollision parsed;
			std::string error;
			const std::string document = source;
			bool accepted = false;
			try {
				const auto table = toml::parse(document);
				accepted = ParseCollisionTable(table, parsed, error);
			} catch (const toml::parse_error&) {
				throw std::runtime_error("invalid-field fixture was not syntactically valid TOML: " + field);
			}
			Require(!accepted, "invalid collision field was accepted: " + field);
			Require(error.find(field) != std::string::npos, "validation diagnostic omitted the field name: " + field);
		}
	}
}

int main()
{
	try {
		TestDefaultsAndValidFields();
		TestInvalidCollisionFields();
		std::cout << "All config tests passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
