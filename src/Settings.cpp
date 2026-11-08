#include "Settings.h"
#include <toml++/toml.h>

#include "CollisionConfig.h"
#include "PrecisionHandler.h"
#include "render/DrawHandler.h"

void Settings::Initialize()
{
	logger::info("Initializing...");

	auto dataHandler = RE::TESDataHandler::GetSingleton();
	if (dataHandler) {
		auto recoilMaterialsList = dataHandler->LookupForm<RE::BGSListForm>(0x810, "Precision.esp");
		if (recoilMaterialsList) {
			for (auto& form : recoilMaterialsList->forms) {
				auto materialType = form->As<RE::BGSMaterialType>();
				if (materialType) {
					recoilMaterials.emplace(materialType);
				}
			}
		}

		glob_nemesis = dataHandler->LookupForm<RE::TESGlobal>(0x801, "Precision.esp");
		glob_IFPVFirstPerson = dataHandler->LookupForm<RE::TESGlobal>(0x801, "IFPVDetector.esl");
	}

	defaultBodyPartData = RE::TESForm::LookupByID<RE::BGSBodyPartData>(0x1D);

	logger::info("...success");
}

void Settings::ReadSettings()
{
	constexpr auto path = L"Data/SKSE/Plugins/Precision";
	constexpr auto ext = L".toml";
	constexpr auto basecfg = L"Data/SKSE/Plugins/Precision/Precision_base.toml";

	constexpr auto defaultSettingsPath = L"Data/MCM/Config/Precision/settings.ini";
	constexpr auto mcmPath = L"Data/MCM/Settings/Precision.ini";

	auto dataHandler = RE::TESDataHandler::GetSingleton();

	const auto readCollision = [](const toml::table& a_collisionTable, std::vector<CollisionDefinition>& a_collisionDefs, std::string& a_error) {
		CollisionConfig::ParsedCollision parsed;
		if (!CollisionConfig::Parse(a_collisionTable, parsed, a_error)) {
			return false;
		}

		std::optional<RE::NiTransform> transform;
		if (parsed.rotation || parsed.translation || parsed.scale) {
			transform = RE::NiTransform();

			if (parsed.rotation) {
				RE::NiPoint3 rotationVector{ (*parsed.rotation)[0], (*parsed.rotation)[1], (*parsed.rotation)[2] };
				rotationVector.x = Utils::DegreeToRadian(rotationVector.x);
				rotationVector.y = Utils::DegreeToRadian(rotationVector.y);
				rotationVector.z = Utils::DegreeToRadian(rotationVector.z);
				transform->rotate.SetEulerAnglesXYZ(rotationVector);
			}
			if (parsed.translation) {
				transform->translate = { (*parsed.translation)[0], (*parsed.translation)[1], (*parsed.translation)[2] };
			}
			if (parsed.scale) {
				transform->scale = *parsed.scale;
			}
		}

		std::optional<RE::NiPoint3> groundShake;
		if (parsed.groundShake) {
			groundShake = RE::NiPoint3{ (*parsed.groundShake)[0], (*parsed.groundShake)[1], (*parsed.groundShake)[2] };
		}

		a_collisionDefs.emplace_back(parsed.nodeName, parsed.ID, parsed.noRecoil, parsed.noTrail, parsed.trailUseTrueLength, parsed.weaponTip, parsed.damageMult, parsed.duration, parsed.durationMult, parsed.delay, parsed.radius, parsed.radiusMult, parsed.length, parsed.lengthMult, transform, groundShake, std::nullopt);
		return true;
	};

	const auto readToml = [&](std::filesystem::path path) {
		logger::info("  Reading {}...", path.string());
		try {
			const auto tbl = toml::parse_file(path.c_str());
			const auto readTable = [&](const toml::node& a_node, std::string_view a_context) {
				const auto* table = a_node.as_table();
				if (!table) {
					logger::warn("Skipping invalid {} table in {}", a_context, path.string());
				}
				return table;
			};
			const auto readCollisionArray = [&](const toml::array& a_array, std::vector<CollisionDefinition>& a_collisionDefs, std::string_view a_context) {
				for (const auto& collisionEntry : a_array) {
					const auto* collisionTable = collisionEntry.as_table();
					std::string error;
					if (!collisionTable || !readCollision(*collisionTable, a_collisionDefs, error)) {
						logger::warn("Skipping {} definition with invalid collision in {}: {}", a_context, path.string(), collisionTable ? error : "Collision entry must be a table");
						return false;
					}
				}
				return true;
			};

			auto attackDefinitionsArr = tbl.get_as<toml::array>("AttackDefinitions");
			if (attackDefinitionsArr) {
				for (auto&& elem : *attackDefinitionsArr) {
					const auto* definitionsTable = readTable(elem, "AttackDefinitions"sv);
					if (!definitionsTable) {
						continue;
					}
					const auto& definitionsTbl = *definitionsTable;

					auto formIDs = definitionsTbl["BodyPartDataFormIDs"].as_array();
					if (formIDs) {
						for (auto& formIDEntry : *formIDs) {
							const auto* formIDTbl = formIDEntry.as_table();
							if (!formIDTbl) {
								logger::warn("Skipping body part data entry with invalid FormID table in {}", path.string());
								continue;
							}

							auto formID = (*formIDTbl)["FormID"].value<RE::FormID>();
							auto pluginName = (*formIDTbl)["Plugin"].value<std::string_view>();
							if (!formID || !pluginName) {
								logger::warn("Skipping body part data entry with invalid FormID or Plugin in {}", path.string());
								continue;
							}

							auto bodyPartData = dataHandler->LookupForm<RE::BGSBodyPartData>(*formID, *pluginName);
							if (bodyPartData) {
								auto attacksArr = definitionsTbl["Attacks"].as_array();
								if (attacksArr) {
									for (auto&& attackEntry : *attacksArr) {
										// read attack
										const auto* attackTable = readTable(attackEntry, "Attacks"sv);
										if (!attackTable) {
											continue;
										}
										const auto& attackTbl = *attackTable;

										// read swing event
										AttackDefinition::SwingEvent swingEvent = AttackDefinition::SwingEvent::kWeaponSwing;
										auto swingEventVal = attackTbl["SwingEvent"].value<std::string_view>();
										if (swingEventVal) {
											auto& swingEventSv = *swingEventVal;
											std::string swingEventStr{ swingEventSv };
											std::transform(swingEventStr.begin(), swingEventStr.end(), swingEventStr.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });

											if (swingEventStr == "weaponswing"sv) {
												swingEvent = AttackDefinition::SwingEvent::kWeaponSwing;
											} else if (swingEventStr == "prehitframe"sv) {
												swingEvent = AttackDefinition::SwingEvent::kPreHitFrame;
											} else if (swingEventStr == "castokstart"sv) {
												swingEvent = AttackDefinition::SwingEvent::kCastOKStart;
											} else if (swingEventStr == "castokstop"sv) {
												swingEvent = AttackDefinition::SwingEvent::kCastOKStop;
											}
										}

										// event name
										auto eventNames = attackTbl["EventNames"].as_array();
										if (eventNames) {
											for (auto& eventNameEntry : *eventNames) {
												auto eventName = eventNameEntry.value<std::string_view>();
												if (!eventName) {
													logger::warn("Skipping attack event with invalid EventNames entry in {}", path.string());
													continue;
												}

												// read collision
												auto collisionArr = attackTbl["Collisions"].as_array();
												if (collisionArr) {
													std::vector<CollisionDefinition> collisionDefs{};
													if (!readCollisionArray(*collisionArr, collisionDefs, "attack"sv)) {
														continue;
													}

													switch (swingEvent) {
													case AttackDefinition::SwingEvent::kWeaponSwing:
														attackRaceDefinitions[bodyPartData].emplace(*eventName, AttackDefinition(collisionDefs, swingEvent));
														break;
													case AttackDefinition::SwingEvent::kPreHitFrame:
														attackRaceDefinitionsPreHitFrame[bodyPartData].emplace(*eventName, AttackDefinition(collisionDefs, swingEvent));
														break;
													case AttackDefinition::SwingEvent::kCastOKStart:
														attackRaceDefinitionsCastOKStart[bodyPartData].emplace(*eventName, AttackDefinition(collisionDefs, swingEvent));
														break;
													case AttackDefinition::SwingEvent::kCastOKStop:
														attackRaceDefinitionsCastOKStop[bodyPartData].emplace(*eventName, AttackDefinition(collisionDefs, swingEvent));
														break;
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}

			auto attackAnimationDefinitionsArr = tbl.get_as<toml::array>("AttackAnimationDefinitions");
			if (attackAnimationDefinitionsArr) {
				for (auto&& elem : *attackAnimationDefinitionsArr) {
					const auto* definitionsTable = readTable(elem, "AttackAnimationDefinitions"sv);
					if (!definitionsTable) {
						continue;
					}
					const auto& attackAnimationDefinitionsTbl = *definitionsTable;

					auto projectNames = attackAnimationDefinitionsTbl["ProjectNames"].as_array();
					if (projectNames) {
						for (auto& projectNameEntry : *projectNames) {
							auto projectName = projectNameEntry.value<std::string_view>();
							if (projectName) {
								auto& projectNameSv = *projectName;
								std::string projectNameStr{ projectNameSv };
								std::transform(projectNameStr.begin(), projectNameStr.end(), projectNameStr.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });

								auto attacksArr = attackAnimationDefinitionsTbl["Attacks"].as_array();
								if (attacksArr) {
									for (auto&& attackEntry : *attacksArr) {
										// read attack
										const auto* attackTable = readTable(attackEntry, "Attacks"sv);
										if (!attackTable) {
											continue;
										}
										const auto& attackTbl = *attackTable;

										// animation file path
										auto animationFilePath = attackTbl["AnimationFilePath"].value<std::string_view>();
										if (animationFilePath) {
											auto& animationFilePathSv = *animationFilePath;
											std::string animationFilePathStr{ animationFilePathSv };
											std::transform(animationFilePathStr.begin(), animationFilePathStr.end(), animationFilePathStr.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });
											std::replace(animationFilePathStr.begin(), animationFilePathStr.end(), '\\', '/');

											// read swing event
											AttackDefinition::SwingEvent swingEvent = AttackDefinition::SwingEvent::kWeaponSwing;
											auto swingEventVal = attackTbl["SwingEvent"].value<std::string_view>();
											if (swingEventVal) {
												auto& swingEventSv = *swingEventVal;
												std::string swingEventStr{ swingEventSv };
												std::transform(swingEventStr.begin(), swingEventStr.end(), swingEventStr.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });

												if (swingEventStr == "weaponswing"sv) {
													swingEvent = AttackDefinition::SwingEvent::kWeaponSwing;
												} else if (swingEventStr == "prehitframe"sv) {
													swingEvent = AttackDefinition::SwingEvent::kPreHitFrame;
												} else if (swingEventStr == "castokstart"sv) {
													swingEvent = AttackDefinition::SwingEvent::kCastOKStart;
												} else if (swingEventStr == "castokstop"sv) {
													swingEvent = AttackDefinition::SwingEvent::kCastOKStop;
												}
											}

											// read collision
											auto collisionArr = attackTbl["Collisions"].as_array();
											if (collisionArr) {
												std::vector<CollisionDefinition> collisionDefs{};
												if (!readCollisionArray(*collisionArr, collisionDefs, "animation"sv)) {
													continue;
												}

												switch (swingEvent) {
												case AttackDefinition::SwingEvent::kWeaponSwing:
													attackAnimationDefinitions[projectNameStr.data()].emplace(animationFilePathStr, AttackDefinition(collisionDefs, swingEvent));
													break;
												case AttackDefinition::SwingEvent::kPreHitFrame:
													attackAnimationDefinitionsPreHitFrame[projectNameStr.data()].emplace(animationFilePathStr, AttackDefinition(collisionDefs, swingEvent));
													break;
												case AttackDefinition::SwingEvent::kCastOKStart:
													attackAnimationDefinitionsCastOKStart[projectNameStr.data()].emplace(animationFilePathStr, AttackDefinition(collisionDefs, swingEvent));
													break;
												case AttackDefinition::SwingEvent::kCastOKStop:
													attackAnimationDefinitionsCastOKStop[projectNameStr.data()].emplace(animationFilePathStr, AttackDefinition(collisionDefs, swingEvent));
													break;
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}

			auto trailDefinitionArr = tbl.get_as<toml::array>("TrailDefinition");
			if (trailDefinitionArr) {
				for (auto&& elem : *trailDefinitionArr) {
					const auto* trailTable = readTable(elem, "TrailDefinition"sv);
					if (!trailTable) {
						continue;
					}
					const auto& trailDefinitionTbl = *trailTable;

					// priority
					int32_t priority = 0;
					auto priorityVal = trailDefinitionTbl["Priority"].value<uint32_t>();
					if (priorityVal) {
						priority = *priorityVal;
					}

					// all
					bool bAll = false;
					auto allVal = trailDefinitionTbl["All"].value<bool>();
					if (allVal) {
						bAll = *allVal;
					}

					// conditions
					const auto* conditionsTable = trailDefinitionTbl["Conditions"].as_table();
					if (!conditionsTable) {
						logger::warn("Skipping trail definition with invalid Conditions in {}", path.string());
						continue;
					}
					const auto& trailConditionsTbl = *conditionsTable;

					// weapon names
					std::vector<std::string> weaponNames;
					bool bHasWeaponName = false;
					auto weaponNamesArr = trailConditionsTbl["WeaponNames"].as_array();
					if (weaponNamesArr) {
						for (auto& weaponNameEntry : *weaponNamesArr) {
							auto weaponName = weaponNameEntry.value<std::string_view>();
							if (weaponName) {
								weaponNames.emplace_back(*weaponName);
								bHasWeaponName = true;
							}
						}
					}

					// weapon keywords
					std::vector<std::string> weaponKeywords;
					bool bHasWeaponKeyword = false;
					auto weaponKeywordsArr = trailConditionsTbl["WeaponKeywords"].as_array();
					if (weaponKeywordsArr) {
						for (auto& weaponKeywordEntry : *weaponKeywordsArr) {
							auto weaponKeyword = weaponKeywordEntry.value<std::string_view>();
							if (weaponKeyword) {
								weaponKeywords.emplace_back(*weaponKeyword);
								bHasWeaponKeyword = true;
							}
						}
					}

					// enchantment names
					std::vector<std::string> enchantmentNames;
					bool bHasEnchantmentName = false;
					auto enchantmentNamesArr = trailConditionsTbl["EnchantmentNames"].as_array();
					if (enchantmentNamesArr) {
						for (auto& enchantmentNameEntry : *enchantmentNamesArr) {
							auto enchantmentName = enchantmentNameEntry.value<std::string_view>();
							if (enchantmentName) {
								enchantmentNames.emplace_back(*enchantmentName);
								bHasEnchantmentName = true;
							}
						}
					}

					// effect names
					std::vector<std::string> effectNames;
					bool bHasEffectName = false;
					auto effectNamesArr = trailConditionsTbl["EffectNames"].as_array();
					if (effectNamesArr) {
						for (auto& effectNameEntry : *effectNamesArr) {
							auto effectName = effectNameEntry.value<std::string_view>();
							if (effectName) {
								effectNames.emplace_back(*effectName);
								bHasEffectName = true;
							}
						}
					}

					// effect keywords
					std::vector<std::string> effectKeywords;
					bool bHasEffectKeyword = false;
					auto effectKeywordsArr = trailConditionsTbl["EffectKeywords"].as_array();
					if (effectKeywordsArr) {
						for (auto& effectKeywordEntry : *effectKeywordsArr) {
							auto effectKeyword = effectKeywordEntry.value<std::string_view>();
							if (effectKeyword) {
								effectKeywords.emplace_back(*effectKeyword);
								bHasEffectKeyword = true;
							}
						}
					}

					// effect shaders
					std::vector<RE::TESEffectShader*> effectShaders;
					bool bHasEffectShader = false;
					auto effectShadersArr = trailConditionsTbl["EffectShaders"].as_array();
					if (effectShadersArr) {
						for (auto& effectShadersEntry : *effectShadersArr) {
							const auto* effectShaderTbl = effectShadersEntry.as_table();
							if (!effectShaderTbl) {
								logger::warn("Skipping effect shader entry with invalid table in {}", path.string());
								continue;
							}

							auto formID = (*effectShaderTbl)["FormID"].value<RE::FormID>();
							auto pluginName = (*effectShaderTbl)["Plugin"].value<std::string_view>();
							if (!formID || !pluginName) {
								logger::warn("Skipping effect shader entry with invalid FormID or Plugin in {}", path.string());
								continue;
							}

							auto effectShader = dataHandler->LookupForm<RE::TESEffectShader>(*formID, *pluginName);
							if (effectShader) {
								effectShaders.emplace_back(effectShader);
								bHasEffectShader = true;
							}
						}
					}

					if (bHasWeaponName || bHasWeaponKeyword || bHasEnchantmentName || bHasEffectName || bHasEffectKeyword || bHasEffectShader) {
						TrailOverride trailOverride{};

						// visuals
						const auto* visualsTable = trailDefinitionTbl["Visuals"].as_table();
						if (!visualsTable) {
							logger::warn("Skipping trail definition with invalid Visuals in {}", path.string());
							continue;
						}
						const auto& trailVisualsTbl = *visualsTable;

						// lifetime mult
						trailOverride.lifetimeMult = trailVisualsTbl["LifetimeMult"].value<float>();

						const auto fillColor = [&](const toml::table* a_table, RE::NiColorA& a_outColor) {
							if (a_table) {
								const auto fillComponent = [&](std::string_view a_name, float& a_value) {
									const auto* component = a_table->get(a_name);
									if (!component) {
										return true;
									}
									const auto value = component->value<float>();
									if (!value) {
										logger::warn("Skipping trail definition with invalid Visuals.{} component in {}", a_name, path.string());
										return false;
									}
									a_value = *value;
									return true;
								};
								return fillComponent("r", a_outColor.red) && fillComponent("g", a_outColor.green) && fillComponent("b", a_outColor.blue) && fillComponent("a", a_outColor.alpha);
							}
							return true;
						};

						// base color override
						auto baseColorOverrideTbl = trailVisualsTbl["BaseColorOverride"].as_table();
						if (baseColorOverrideTbl) {
							trailOverride.baseColorOverride = RE::NiColorA();
							if (!fillColor(baseColorOverrideTbl, *trailOverride.baseColorOverride)) {
								continue;
							}
						}

						// base color scale mult
						trailOverride.baseColorScaleMult = trailVisualsTbl["BaseColorScaleMult"].value<float>();

						// trail mesh override
						auto trailMeshOverrideVal = trailVisualsTbl["TrailMeshOverride"].value<std::string_view>();
						if (trailMeshOverrideVal) {
							trailOverride.meshOverride = *trailMeshOverrideVal;
						}

						std::vector<TrailDefinition>* trailDefinitions = nullptr;
						if (bAll) {
							trailDefinitions = &trailDefinitionsAll;
						} else {
							trailDefinitions = &trailDefinitionsAny;
						}

						trailDefinitions->emplace_back(priority, bHasWeaponName ? std::optional(weaponNames) : std::nullopt, bHasWeaponKeyword ? std::optional(weaponKeywords) : std::nullopt, bHasEnchantmentName ? std::optional(enchantmentNames) : std::nullopt, bHasEffectName ? std::optional(effectNames) : std::nullopt, bHasEffectKeyword ? std::optional(effectKeywords) : std::nullopt, bHasEffectShader ? std::optional(effectShaders) : std::nullopt, trailOverride);
					}
				}
			}

			auto attackEventPairArr = tbl.get_as<toml::array>("AttackEventPair");
			if (attackEventPairArr) {
				for (auto&& elem : *attackEventPairArr) {
					const auto* eventTable = readTable(elem, "AttackEventPair"sv);
					if (!eventTable) {
						continue;
					}
					const auto& attackEventPairTbl = *eventTable;
					auto rightEvent = attackEventPairTbl["RightEvent"].value<std::string_view>();
					auto leftEvent = attackEventPairTbl["LeftEvent"].value<std::string_view>();
					if (rightEvent && leftEvent) {
						attackEventPairs.emplace_back(*rightEvent, *leftEvent);
					}
				}
			}

			auto weaponOverrideArr = tbl.get_as<toml::array>("WeaponOverride");
			if (weaponOverrideArr) {
				for (auto&& elem : *weaponOverrideArr) {
					const auto* weaponTable = readTable(elem, "WeaponOverride"sv);
					if (!weaponTable) {
						continue;
					}
					const auto& weaponOverrideTbl = *weaponTable;

					auto weaponEntryTblPtr = weaponOverrideTbl.get_as<toml::table>("Weapon");
					if (weaponEntryTblPtr) {
						auto& weaponEntryTbl = *weaponEntryTblPtr;
						auto formID = weaponEntryTbl["FormID"].value<RE::FormID>();
						auto pluginName = weaponEntryTbl["Plugin"].value<std::string_view>();
						if (!formID || !pluginName) {
							logger::warn("Skipping weapon override with invalid FormID or Plugin in {}", path.string());
							continue;
						}

						auto weapon = dataHandler->LookupForm<RE::TESObjectWEAP>(*formID, *pluginName);
						if (weapon) {
							auto lengthOverride = weaponOverrideTbl["Length"].value<float>();
							if (lengthOverride) {
								weaponLengthOverrides.emplace(weapon, *lengthOverride);
							}
							auto radiusOverride = weaponOverrideTbl["Radius"].value<float>();
							if (radiusOverride) {
								weaponRadiusOverrides.emplace(weapon, *radiusOverride);
							}
						}
					}
				}
			}
		} catch ([[maybe_unused]] const toml::parse_error& e) {
			util::report_and_fail("Failed to load settings. This might be an indication of your game being unstable, try installing SSE Engine Fixes."sv);
		} catch (const std::exception& e) {
			util::report_and_fail(e.what());
		} catch (...) {
			util::report_and_fail("unknown failure"sv);
		}
	};

	logger::info("Reading .toml files...");

	attackRaceDefinitions.clear();
	attackRaceDefinitionsPreHitFrame.clear();
	attackRaceDefinitionsCastOKStart.clear();
	attackRaceDefinitionsCastOKStop.clear();
	attackAnimationDefinitions.clear();
	attackAnimationDefinitionsPreHitFrame.clear();
	attackAnimationDefinitionsCastOKStart.clear();
	attackAnimationDefinitionsCastOKStop.clear();
	trailDefinitionsAny.clear();
	trailDefinitionsAll.clear();
	attackEventPairs.clear();
	weaponLengthOverrides.clear();
	weaponRadiusOverrides.clear();

	auto baseToml = std::filesystem::path(basecfg);
	readToml(baseToml);
	if (std::filesystem::is_directory(path)) {
		for (const auto& file : std::filesystem::directory_iterator(path)) {  // read all toml files in Data/SKSE/Plugins/Precision folder
			if (std::filesystem::is_regular_file(file) && file.path().extension() == ext) {
				auto filePath = file.path();
				if (filePath != basecfg) {
					readToml(filePath);
				}
			}
		}
	}

	// sort trailDefinitions based on priority
	std::sort(trailDefinitionsAny.begin(), trailDefinitionsAny.end(), [](const auto& a, const auto& b) {
		return a.priority > b.priority;
	});
	std::sort(trailDefinitionsAll.begin(), trailDefinitionsAll.end(), [](const auto& a, const auto& b) {
		return a.priority > b.priority;
	});

	logger::info("...success");

	const auto readMCM = [&](std::filesystem::path path) {
		CSimpleIniA mcm;
		mcm.SetUnicode();

		mcm.LoadFile(path.string().c_str());

		// Attack Collisions
		ReadBoolSetting(mcm, "AttackCollisions", "bAttackCollisionsEnabled", bAttackCollisionsEnabled);
		ReadBoolSetting(mcm, "AttackCollisions", "bEnableJumpIframes", bEnableJumpIframes);
		ReadBoolSetting(mcm, "AttackCollisions", "bNoPlayerTeammateAttackCollision", bNoPlayerTeammateAttackCollision);
		ReadBoolSetting(mcm, "AttackCollisions", "bNoNonHostileAttackCollision", bNoNonHostileAttackCollision);
		ReadFloatSetting(mcm, "AttackCollisions", "fCombatStateLingerTime", fCombatStateLingerTime);
		ReadBoolSetting(mcm, "AttackCollisions", "bDisablePhysicalCollisionOnHit", bDisablePhysicalCollisionOnHit);
		ReadFloatSetting(mcm, "AttackCollisions", "fWeaponLengthMult", fWeaponLengthMult);
		ReadFloatSetting(mcm, "AttackCollisions", "fWeaponCapsuleRadius", fWeaponCapsuleRadius);
		ReadFloatSetting(mcm, "AttackCollisions", "fMinWeaponLength", fMinWeaponLength);
		ReadFloatSetting(mcm, "AttackCollisions", "fDefaultCollisionLifetime", fDefaultCollisionLifetime);
		ReadFloatSetting(mcm, "AttackCollisions", "fDefaultCollisionLifetimePowerAttackMult", fDefaultCollisionLifetimePowerAttackMult);
		ReadFloatSetting(mcm, "AttackCollisions", "fHitSameRefCooldown", fHitSameRefCooldown);
		ReadFloatSetting(mcm, "AttackCollisions", "fHitSameMaterialCooldown", fHitSameMaterialCooldown);
		ReadFloatSetting(mcm, "AttackCollisions", "fFirstPersonAttackLengthOffset", fFirstPersonAttackLengthOffset);
		ReadFloatSetting(mcm, "AttackCollisions", "fPlayerAttackLengthMult", fPlayerAttackLengthMult);
		ReadFloatSetting(mcm, "AttackCollisions", "fPlayerAttackRadiusMult", fPlayerAttackRadiusMult);
		ReadFloatSetting(mcm, "AttackCollisions", "fMountedAttackLengthMult", fMountedAttackLengthMult);
		ReadFloatSetting(mcm, "AttackCollisions", "fMountedAttackRadiusMult", fMountedAttackRadiusMult);
		ReadUInt32Setting(mcm, "AttackCollisions", "uSweepAttackMode", (uint32_t&)uSweepAttackMode);
		ReadUInt32Setting(mcm, "AttackCollisions", "uMaxTargetsNoSweepAttack", uMaxTargetsNoSweepAttack);
		ReadUInt32Setting(mcm, "AttackCollisions", "uMaxTargetsSweepAttack", uMaxTargetsSweepAttack);
		ReadFloatSetting(mcm, "AttackCollisions", "fSweepAttackDiminishingReturnsFactor", fSweepAttackDiminishingReturnsFactor);
		ReadFloatSetting(mcm, "AttackCollisions", "fGroundFeetDistanceThreshold", fGroundFeetDistanceThreshold);

		// Trails
		ReadBoolSetting(mcm, "Trails", "bDisplayTrails", bDisplayTrails);
		ReadBoolSetting(mcm, "Trails", "bTrailUseAttackCollisionLength", bTrailUseAttackCollisionLength);
		ReadFloatSetting(mcm, "Trails", "fTrailSegmentLifetime", fTrailSegmentLifetime);
		ReadFloatSetting(mcm, "Trails", "fTrailFadeOutTime", fTrailFadeOutTime);
		ReadUInt32Setting(mcm, "Trails", "uTrailSegmentsPerSecond", uTrailSegmentsPerSecond);
		ReadFloatSetting(mcm, "Trails", "fTrailDefaultBaseColorR", fTrailDefaultBaseColorR);
		ReadFloatSetting(mcm, "Trails", "fTrailDefaultBaseColorG", fTrailDefaultBaseColorG);
		ReadFloatSetting(mcm, "Trails", "fTrailDefaultBaseColorB", fTrailDefaultBaseColorB);
		ReadFloatSetting(mcm, "Trails", "fTrailDefaultBaseColorA", fTrailDefaultBaseColorA);
		ReadFloatSetting(mcm, "Trails", "fTrailBaseColorScaleMult", fTrailBaseColorScaleMult);

		// Hitstop
		ReadBoolSetting(mcm, "Hitstop", "bEnableHitstop", bEnableHitstop);
		ReadBoolSetting(mcm, "Hitstop", "bEnableNPCHitStop", bEnableNPCHitStop);
		ReadBoolSetting(mcm, "Hitstop", "bApplyHitstopToTarget", bApplyHitstopToTarget);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopDurationNPC", fHitstopDurationNPC);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopDurationOther", fHitstopDurationOther);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopSlowdownTimeMultiplier", fHitstopSlowdownTimeMultiplier);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopDurationPowerAttackMultiplier", fHitstopDurationPowerAttackMultiplier);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopDurationTwoHandedMultiplier", fHitstopDurationTwoHandedMultiplier);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopDurationBlockedHitMultiplier", fHitstopDurationBlockedHitMultiplier);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopDurationDiminishingReturnsFactor", fHitstopDurationDiminishingReturnsFactor);

		ReadBoolSetting(mcm, "Hitstop", "bEnableHitstopCameraShake", bEnableHitstopCameraShake);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopCameraShakeStrengthNPC", fHitstopCameraShakeStrengthNPC);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopCameraShakeStrengthOther", fHitstopCameraShakeStrengthOther);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopCameraShakeDurationNPC", fHitstopCameraShakeDurationNPC);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopCameraShakeDurationOther", fHitstopCameraShakeDurationOther);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopCameraShakeFrequency", fHitstopCameraShakeFrequency);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopCameraShakePowerAttackMultiplier", fHitstopCameraShakePowerAttackMultiplier);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopCameraShakeTwoHandedMultiplier", fHitstopCameraShakeTwoHandedMultiplier);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopCameraShakeBlockedHitMultiplier", fHitstopCameraShakeBlockedHitMultiplier);
		ReadFloatSetting(mcm, "Hitstop", "fHitstopCameraShakeDurationDiminishingReturnsFactor", fHitstopCameraShakeDurationDiminishingReturnsFactor);

		// Recoil
		ReadBoolSetting(mcm, "Recoil", "bRecoilPlayer", bRecoilPlayer);
		ReadBoolSetting(mcm, "Recoil", "bRecoilNPC", bRecoilNPC);
		ReadBoolSetting(mcm, "Recoil", "bRecoilPowerAttack", bRecoilPowerAttack);
		ReadBoolSetting(mcm, "Recoil", "bUseVanillaRecoil", bUseVanillaRecoil);
		ReadBoolSetting(mcm, "Recoil", "bRemoveRecoilOnHitframe", bRemoveRecoilOnHitframe);
		ReadFloatSetting(mcm, "Recoil", "fRecoilCollisionLength", fRecoilCollisionLength);

		ReadBoolSetting(mcm, "Recoil", "bEnableRecoilCameraShake", bEnableRecoilCameraShake);
		ReadFloatSetting(mcm, "Recoil", "fRecoilCameraShakeStrength", fRecoilCameraShakeStrength);
		ReadFloatSetting(mcm, "Recoil", "fRecoilCameraShakeDuration", fRecoilCameraShakeDuration);
		ReadFloatSetting(mcm, "Recoil", "fRecoilCameraShakeFrequency", fRecoilCameraShakeFrequency);

		// Hit Impulse
		ReadBoolSetting(mcm, "HitImpulse", "bApplyImpulseOnHit", bApplyImpulseOnHit);
		ReadBoolSetting(mcm, "HitImpulse", "bApplyImpulseOnKill", bApplyImpulseOnKill);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseBaseMult", fHitImpulseBaseMult);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseBlockMult", fHitImpulseBlockMult);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulsePowerAttackMult", fHitImpulsePowerAttackMult);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseRagdollMult", fHitImpulseRagdollMult);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseKillMult", fHitImpulseKillMult);

		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseBaseStrength", fHitImpulseBaseStrength);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseProportionalStrength", fHitImpulseProportionalStrength);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseMassExponent", fHitImpulseMassExponent);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseMinStrength", fHitImpulseMinStrength);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseMaxStrength", fHitImpulseMaxStrength);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseMaxVelocity", fHitImpulseMaxVelocity);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseDownwardsMultiplier", fHitImpulseDownwardsMultiplier);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseDecayMult1", fHitImpulseDecayMult1);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseDecayMult2", fHitImpulseDecayMult2);
		ReadFloatSetting(mcm, "HitImpulse", "fHitImpulseDecayMult3", fHitImpulseDecayMult3);

		// Miscellaneous
		ReadFloatSetting(mcm, "Miscellaneous", "fActiveActorDistance", fActiveActorDistance);
		ReadBoolSetting(mcm, "Miscellaneous", "bHookAIWeaponReach", bHookAIWeaponReach);
		ReadFloatSetting(mcm, "Miscellaneous", "fAIWeaponReachOffset", fAIWeaponReachOffset);
		ReadBoolSetting(mcm, "Miscellaneous", "bDisableCharacterBumper", bDisableCharacterBumper);
		ReadBoolSetting(mcm, "Miscellaneous", "bUseRagdollCollisionWhenAllowed", bUseRagdollCollisionWhenAllowed);

		// Debug
		ReadBoolSetting(mcm, "Debug", "bDebug", bDebug);
		ReadBoolSetting(mcm, "Debug", "bDisplayWeaponCapsule", bDisplayWeaponCapsule);
		ReadBoolSetting(mcm, "Debug", "bDisplayHitNodeCollisions", bDisplayHitNodeCollisions);
		ReadBoolSetting(mcm, "Debug", "bDisplayHitLocations", bDisplayHitLocations);
		ReadBoolSetting(mcm, "Debug", "bDisplayIframeHits", bDisplayIframeHits);
		ReadBoolSetting(mcm, "Debug", "bDisplayRecoilCollisions", bDisplayRecoilCollisions);
		ReadBoolSetting(mcm, "Debug", "bDisplaySkeletonColliders", bDisplaySkeletonColliders);
		ReadUInt32Setting(mcm, "Debug", "uToggleKey", (uint32_t&)uToggleKey);
		ReadUInt32Setting(mcm, "Debug", "uReloadSettingsKey", (uint32_t&)uReloadSettingsKey);
	};

	logger::info("Reading MCM .ini...");

	readMCM(defaultSettingsPath);  // read the default ini first
	readMCM(mcmPath);

	logger::info("...success");

	DrawHandler::GetSingleton()->OnSettingsUpdated();
}

void Settings::OnPostLoadGame()
{
	UpdateGlobals();
}

void Settings::UpdateGlobals()
{
	if (glob_nemesis && glob_nemesis->value == 0) {
		auto playerCharacter = RE::PlayerCharacter::GetSingleton();
		RE::BSTSmartPointer<RE::BSAnimationGraphManager> animationGraphManagerPtr;
		playerCharacter->GetAnimationGraphManager(animationGraphManagerPtr);
		if (animationGraphManagerPtr) {
			RE::BShkbAnimationGraph* animationGraph = animationGraphManagerPtr->graphs[0].get();
			if (animationGraph) {
				bool dummy;
				glob_nemesis->value = animationGraph->GetGraphVariableBool("Collision_Installed", dummy);
			}
		}
	}
}

void Settings::ReadBoolSetting(CSimpleIniA& a_ini, const char* a_sectionName, const char* a_settingName, bool& a_setting)
{
	const char* bFound = nullptr;
	bFound = a_ini.GetValue(a_sectionName, a_settingName);
	if (bFound) {
		a_setting = a_ini.GetBoolValue(a_sectionName, a_settingName);
	}
}

void Settings::ReadFloatSetting(CSimpleIniA& a_ini, const char* a_sectionName, const char* a_settingName, float& a_setting)
{
	const char* bFound = nullptr;
	bFound = a_ini.GetValue(a_sectionName, a_settingName);
	if (bFound) {
		a_setting = static_cast<float>(a_ini.GetDoubleValue(a_sectionName, a_settingName));
	}
}

void Settings::ReadUInt32Setting(CSimpleIniA& a_ini, const char* a_sectionName, const char* a_settingName, uint32_t& a_setting)
{
	const char* bFound = nullptr;
	bFound = a_ini.GetValue(a_sectionName, a_settingName);
	if (bFound) {
		a_setting = static_cast<uint32_t>(a_ini.GetLongValue(a_sectionName, a_settingName));
	}
}
