local FeatureRegistry = {}

FeatureRegistry.categories = {
    "Session",
    "Heroes / Spawn",
    "Combat",
    "Abilities",
    "Visual / Diagnostics",
    "Loadout"
}

FeatureRegistry.features = {
    {
        id = "force_start_lobby",
        label = "Force Start Lobby",
        category = "Session",
        kind = "action",
        privateOnly = true,
        wired = false,
        assets = {
            "UI/InGame/InGameMenu/InGameMenuMPScreen",
            "Gameplay/GameModes/Shared/PF_GameMode_Controller",
            "SchematicChannels/ForceStartLobby_Schematic"
        }
    },
    {
        id = "hero_spawn_menu",
        label = "Hero Spawn Controls",
        category = "Heroes / Spawn",
        kind = "action",
        privateOnly = true,
        wired = false,
        assets = {
            "UI/InGame/Hud/InGameScreens",
            "UI/InGame/Hud/CommoRose/PF_UI_CommoRoses_Combined",
            "UI/InGame/PF_ViewAbilities_InGame",
            "Gameplay/GameModes/Shared/PF_GameMode_Controller"
        }
    },
    {
        id = "spawn_disable",
        label = "Spawn State Controls",
        category = "Heroes / Spawn",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "UI/InGame/InGameMenu/DebugMenuMPScreen",
            "UI/InGame/InGameMenu/DebugMenuSkirmishScreen",
            "Gameplay/GameModes/Shared/PF_GameMode_Controller"
        }
    },
    {
        id = "accurate_deflections",
        label = "Accurate Saber Deflections",
        category = "Combat",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "Gameplay/Equipment/Shared/Projectiles/BlasterProjectile_SaberDeflect",
            "Gameplay/Prefabs/Abilities/Gameplay/PF_DeflectAbility_Gameplay_Anakin",
            "Gameplay/Prefabs/Abilities/Gameplay/PF_DeflectAbility_Gameplay_Luke",
            "Gameplay/Prefabs/Abilities/Gameplay/PF_DeflectAbility_Gameplay_Vader"
        }
    },
    {
        id = "saber_throw_tuning",
        label = "Saber Throw Test Tuning",
        category = "Combat",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "Gameplay/Equipment/shared/projectiles/SaberThrow1hit",
            "Gameplay/Kits/Hero/DarthVader/PF_SaberThrow_DarthVader",
            "Gameplay/Kits/Hero/DarthMaul/PF_SaberThrow_DarthMaul"
        }
    },
    {
        id = "crosshair_zoom_tuning",
        label = "Crosshair / Zoom Test Controls",
        category = "Combat",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "UI/InGame/Hud/Crosshair",
            "Gameplay/Equipment/Weapons/WeaponSway"
        }
    },
    {
        id = "auto_parry_test",
        label = "Auto-Parry State Test",
        category = "Abilities",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "Gameplay/Kits/Hero/PF_Ability_Evade_CharacterState",
            "Gameplay/Kits/Hero/PF_Ability_Evade_CharacterState_Dooku"
        }
    },
    {
        id = "boba_midair_test",
        label = "Boba Mid-Air State Test",
        category = "Abilities",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "Gameplay/Kits/Hero/BobaFett/Prefabs/PF_Jetpack_BobaFett"
        }
    },
    {
        id = "vader_choke_constraint_test",
        label = "Vader Choke Constraint Test",
        category = "Abilities",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "Gameplay/Kits/Hero/DarthVader/PF_ForceChoke_DarthVader_02"
        }
    },
    {
        id = "emote_ability_reset_test",
        label = "Emote Ability Reset Test",
        category = "Abilities",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "Gameplay/Prefabs/Emotes/PF_Ability_Emote",
            "Gameplay/Prefabs/Emotes/PF_Ability_Emote_Hero",
            "Gameplay/Equipment/Emotes/Abilities/TrooperEmoteAbility"
        }
    },
    {
        id = "spotting_diagnostics",
        label = "Spotting Diagnostics",
        category = "Visual / Diagnostics",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "UI/InGame/Spotting/PF_UI_Spotting",
            "UI/InGame/Hud/InWorldMarkers/Above/PF_UI_Above_Soldier"
        }
    },
    {
        id = "starcard_loadouts",
        label = "Star Card Loadout Test UI",
        category = "Loadout",
        kind = "toggle",
        privateOnly = true,
        wired = false,
        assets = {
            "UI/Frontend/Loadout/LoadoutTabsWidget",
            "UI/Frontend/Loadout/NewEditCardGridScreen",
            "UI/Frontend/Loadout/CharacterPreviewWidgetMinimalWithLoadouts"
        }
    }
}

function FeatureRegistry.find(id)
    for _, feature in ipairs(FeatureRegistry.features) do
        if feature.id == id then
            return feature
        end
    end
    return nil
end

function FeatureRegistry.forCategory(category)
    local out = {}
    for _, feature in ipairs(FeatureRegistry.features) do
        if feature.category == category then
            table.insert(out, feature)
        end
    end
    return out
end

return FeatureRegistry
