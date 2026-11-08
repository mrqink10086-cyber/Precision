#pragma once

#include <toml++/toml.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace CollisionConfig
{
	struct ParsedCollision
	{
		std::string_view nodeName;
		std::optional<uint8_t> ID;
		bool noRecoil = false;
		bool noTrail = false;
		bool trailUseTrueLength = false;
		bool weaponTip = false;
		float damageMult = 1.f;
		float duration = 0.f;
		std::optional<float> durationMult;
		std::optional<float> delay;
		std::optional<float> radius;
		std::optional<float> radiusMult;
		std::optional<float> length;
		std::optional<float> lengthMult;
		std::optional<std::array<float, 3>> rotation;
		std::optional<std::array<float, 3>> translation;
		std::optional<float> scale;
		std::optional<std::array<float, 3>> groundShake;
	};

	template <class T>
	bool ReadOptionalNode(const toml::node* a_node, std::string_view a_name, std::optional<T>& a_out, std::string& a_error)
	{
		if (!a_node) {
			return true;
		}

		auto value = a_node->value<T>();
		if (!value) {
			a_error = "Collision field '" + std::string(a_name) + "' has an invalid type or value";
			return false;
		}

		a_out = *value;
		return true;
	}

	template <class T>
	bool ReadOptional(const toml::table& a_table, std::string_view a_key, std::optional<T>& a_out, std::string& a_error)
	{
		return ReadOptionalNode(a_table.get(a_key), a_key, a_out, a_error);
	}

	inline bool ReadVector(const toml::table& a_parent, std::string_view a_key, const std::array<std::string_view, 3>& a_components, std::optional<std::array<float, 3>>& a_out, std::string& a_error)
	{
		const auto* node = a_parent.get(a_key);
		if (!node) {
			return true;
		}

		const auto* table = node->as_table();
		if (!table) {
			a_error = "Collision field '" + std::string(a_key) + "' must be a table";
			return false;
		}

		std::array<float, 3> values{};
		for (std::size_t index = 0; index < a_components.size(); ++index) {
			std::optional<float> component;
			const auto* componentNode = table->get(a_components[index]);
			const std::string fieldName = std::string(a_key) + "." + std::string(a_components[index]);
			if (!ReadOptionalNode(componentNode, fieldName, component, a_error)) {
				return false;
			}
			if (component) {
				values[index] = *component;
			}
		}

		a_out = values;
		return true;
	}

	inline bool Parse(const toml::table& a_table, ParsedCollision& a_out, std::string& a_error)
	{
		ParsedCollision parsed;
		auto nodeName = a_table["NodeName"].value<std::string_view>();
		if (!nodeName) {
			a_error = "Collision field 'NodeName' is required and must be a string";
			return false;
		}
		parsed.nodeName = *nodeName;

		std::optional<uint8_t> ID;
		std::optional<bool> noRecoil;
		std::optional<bool> noTrail;
		std::optional<bool> trailUseTrueLength;
		std::optional<bool> weaponTip;
		std::optional<float> damageMult;
		std::optional<float> duration;
		if (!ReadOptional(a_table, "ID", ID, a_error) ||
			!ReadOptional(a_table, "NoRecoil", noRecoil, a_error) ||
			!ReadOptional(a_table, "NoTrail", noTrail, a_error) ||
			!ReadOptional(a_table, "TrailUseTrueLength", trailUseTrueLength, a_error) ||
			!ReadOptional(a_table, "WeaponTip", weaponTip, a_error) ||
			!ReadOptional(a_table, "DamageMult", damageMult, a_error) ||
			!ReadOptional(a_table, "Duration", duration, a_error) ||
			!ReadOptional(a_table, "DurationMult", parsed.durationMult, a_error) ||
			!ReadOptional(a_table, "Delay", parsed.delay, a_error) ||
			!ReadOptional(a_table, "Radius", parsed.radius, a_error) ||
			!ReadOptional(a_table, "RadiusMult", parsed.radiusMult, a_error) ||
			!ReadOptional(a_table, "Length", parsed.length, a_error) ||
			!ReadOptional(a_table, "LengthMult", parsed.lengthMult, a_error) ||
			!ReadOptional(a_table, "Scale", parsed.scale, a_error) ||
			!ReadVector(a_table, "Rotation", { "x", "y", "z" }, parsed.rotation, a_error) ||
			!ReadVector(a_table, "Translation", { "x", "y", "z" }, parsed.translation, a_error)) {
			return false;
		}

		if (ID) {
			parsed.ID = *ID;
		}
		if (noRecoil) {
			parsed.noRecoil = *noRecoil;
		}
		if (noTrail) {
			parsed.noTrail = *noTrail;
		}
		if (trailUseTrueLength) {
			parsed.trailUseTrueLength = *trailUseTrueLength;
		}
		if (weaponTip) {
			parsed.weaponTip = *weaponTip;
		}
		if (damageMult) {
			parsed.damageMult = *damageMult;
		}
		if (duration) {
			parsed.duration = *duration;
		}

		const auto* groundShakeNode = a_table.get("GroundShake");
		if (groundShakeNode) {
			const auto* groundShakeTable = groundShakeNode->as_table();
			if (!groundShakeTable) {
				a_error = "Collision field 'GroundShake' must be a table";
				return false;
			}

			std::array<float, 3> values{};
			constexpr std::array<std::string_view, 3> fields{ "Strength", "Duration", "Frequency" };
			for (std::size_t index = 0; index < fields.size(); ++index) {
				std::optional<float> component;
				const std::string fieldName = "GroundShake." + std::string(fields[index]);
				if (!ReadOptionalNode(groundShakeTable->get(fields[index]), fieldName, component, a_error)) {
					return false;
				}
				if (component) {
					values[index] = *component;
				}
			}
			parsed.groundShake = values;
		}

		a_out = std::move(parsed);
		return true;
	}
}
