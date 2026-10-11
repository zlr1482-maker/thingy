#include "menu.h"

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace bf2dev {
namespace {

struct Feature {
    const char* id;
    const char* label;
    const char* category;
    bool toggle;
    bool value;
    bool wired;
    const char* frostAsset;
};

std::array<Feature, 12> g_features{{
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
}};

constexpr std::array<const char*, 6> kCategories{{
    "Session", "Heroes / Spawn", "Combat", "Abilities", "Visual / Diagnostics", "Loadout"
}};

bool g_initialized = false;
bool g_open = false;
std::string g_status = "Private dev menu ready.";

bool EnvEnabled(const char* name) {
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value) return false;
    std::string v(value);
    free(value);
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

void Dispatch(Feature& feature) {
    if (!IsPrivateTestEnabled()) {
        g_status = "Blocked: private test mode is not enabled.";
        return;
    }

    if (!feature.wired) {
        g_status = std::string(feature.label) + " is provided by the selected Frosty profile or is pending a validated local runtime handler.";
        return;
    }

    g_status = std::string("Executed ") + feature.label;
}

void DrawFeature(Feature& feature) {
    ImGui::PushID(feature.id);

    if (feature.toggle) {
        const bool previous = feature.value;
        if (ImGui::Checkbox(feature.label, &feature.value) && previous != feature.value) {
            Dispatch(feature);
            if (!feature.wired) feature.value = previous;
        }
    } else {
        if (ImGui::Button(feature.label, ImVec2(-1.0f, 0.0f))) {
            Dispatch(feature);
        }
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(feature.wired ? "Runtime handler: wired" : "Runtime source: Frosty profile / pending local handler");
        ImGui::Separator();
        ImGui::TextWrapped("Frosty reference: %s", feature.frostAsset);
        ImGui::EndTooltip();
    }

    ImGui::PopID();
}

} // namespace

bool IsPrivateTestEnabled() {
    return EnvEnabled("BF2_PRIVATE_DEV_MENU") || EnvEnabled("KYBER_DEV_MODE");
}

void InitializeMenu(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    if (g_initialized) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);
    g_initialized = true;
}

void ShutdownMenu() {
    if (!g_initialized) return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_initialized = false;
    g_open = false;
}

void ToggleMenu() {
    if (!IsPrivateTestEnabled()) return;
    g_open = !g_open;
}

bool IsMenuOpen() { return g_open; }

void RenderMenu() {
    if (!g_initialized || !g_open || !IsPrivateTestEnabled()) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(570.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Battlefront II Private Dev Menu", &g_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "PRIVATE TEST MODE ENABLED");
        ImGui::TextDisabled("Insert toggles this menu. No external service, console, or network bridge is used.");
        ImGui::Separator();

        if (ImGui::BeginTabBar("bf2dev-tabs")) {
            for (const char* category : kCategories) {
                if (ImGui::BeginTabItem(category)) {
                    for (auto& feature : g_features) {
                        if (std::string_view(feature.category) == category) {
                            DrawFeature(feature);
                        }
                    }
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        ImGui::TextWrapped("%s", g_status.c_str());
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

LRESULT HandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled) {
    handled = false;
    if (!g_initialized) return 0;

    if (msg == WM_KEYUP && wParam == VK_INSERT) {
        ToggleMenu();
        handled = IsPrivateTestEnabled();
        return 0;
    }

    if (g_open && IsPrivateTestEnabled() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        handled = true;
        return 1;
    }

    return 0;
}

} // namespace bf2dev
