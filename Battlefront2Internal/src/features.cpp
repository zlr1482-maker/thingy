#include "features.h"

#include <string_view>

namespace bf2dev {

std::vector<FeatureDescriptor>& Features() {
    static std::vector<FeatureDescriptor> features{
        {"force_start_lobby", "Force Start Lobby", "Session", false, false, false,
         "SchematicChannels/ForceStartLobby_Schematic"},
        {"hero_spawn_menu", "Hero Spawn Controls", "Heroes / Spawn", false, false, false,
         "Gameplay/GameModes/Shared/PF_GameMode_Controller"},
        {"spawn_state", "Spawn State Controls", "Heroes / Spawn", true, false, false,
         "UI/InGame/InGameMenu/DebugMenuMPScreen"},
        {"accurate_deflections", "Accurate Saber Deflections", "Combat", true, false, false,
         "Gameplay/Equipment/Shared/Projectiles/BlasterProjectile_SaberDeflect"},
        {"saber_throw", "Saber Throw Test Tuning", "Combat", true, false, false,
         "Gameplay/Equipment/shared/projectiles/SaberThrow1hit"},
        {"crosshair_zoom", "Crosshair / Zoom Test Controls", "Combat", true, false, false,
         "UI/InGame/Hud/Crosshair"},
        {"auto_parry", "Auto-Parry State Test", "Abilities", true, false, false,
         "Gameplay/Kits/Hero/PF_Ability_Evade_CharacterState"},
        {"boba_midair", "Boba Mid-Air State Test", "Abilities", true, false, false,
         "Gameplay/Kits/Hero/BobaFett/Prefabs/PF_Jetpack_BobaFett"},
        {"vader_choke", "Vader Choke Constraint Test", "Abilities", true, false, false,
         "Gameplay/Kits/Hero/DarthVader/PF_ForceChoke_DarthVader_02"},
        {"emote_reset", "Emote Ability Reset Test", "Abilities", true, false, false,
         "Gameplay/Prefabs/Emotes/PF_Ability_Emote_Hero"},
        {"spotting_diag", "Spotting Diagnostics", "Visual / Diagnostics", true, false, false,
         "UI/InGame/Spotting/PF_UI_Spotting"},
        {"starcard_loadouts", "Star Card Loadout Test UI", "Loadout", true, false, false,
         "UI/Frontend/Loadout/LoadoutTabsWidget"},
    };
    return features;
}

FeatureResult ExecuteFeature(FeatureDescriptor& feature) {
    if (!feature.wired) {
        return {
            false,
            std::string(feature.label) +
                " is mapped to Frosty data, but its local runtime handler is not wired yet."
        };
    }

    return {true, std::string("Executed ") + feature.label};
}

} // namespace bf2dev
