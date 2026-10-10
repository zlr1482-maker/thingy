#pragma once

// Benign launcher/process metadata for STAR WARS Battlefront II (2017).
// This file intentionally contains no memory offsets, hooks, anti-cheat
// bypasses, stealth logic, or write primitives.

namespace battlefront2_profile {

inline constexpr const char* kGameKey     = "battlefront2";
inline constexpr const char* kDisplayName = "STAR WARS Battlefront II";
inline constexpr const char* kExeName     = "starwarsbattlefrontii.exe";
inline constexpr const char* kSteamUrl    = "steam://rungameid/1237950";
inline constexpr const char* kEngine      = "Frostbite 3";
inline constexpr const char* kDescription = "DICE / Electronic Arts";

inline constexpr const wchar_t* kProbeModules[] = {
    L"starwarsbattlefrontii.exe",
    L"kernel32.dll",
    L"ntdll.dll",
    L"user32.dll",
    L"d3d11.dll",
    L"dxgi.dll",
    L"d3d12.dll",
    L"vcruntime140.dll",
    nullptr
};

} // namespace battlefront2_profile
