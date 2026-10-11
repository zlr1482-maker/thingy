# Battlefront II Private Dev Menu

Standalone internal D3D11/ImGui menu for **STAR WARS Battlefront II (2017)**. It is independent of Xen and contains no injector, driver, anti-cheat bypass, module hiding, protection-evasion, or anti-detection code.

## Current state

The internal menu shell is implemented and checks that it is running inside `starwarsbattlefrontii.exe` before initializing its renderer integration.

The following menu categories are present:

- Session
- Heroes / Spawn
- Combat
- Abilities
- Visual / Diagnostics
- Loadout

The entries are mapped to the Frosty assets recovered from the supplied `.fbmod` and `.fbproject` files. Runtime gameplay handlers are intentionally marked **pending** until each Frostbite/Kyber call is identified and validated. A pending toggle restores its previous value instead of pretending that the feature worked.

## Quiet private-test behavior

The test build is deliberately low-noise without trying to evade security software.

- The menu starts hidden.
- No console window is created.
- No localhost listener or external HTTP bridge is required.
- ImGui `.ini` and log files are disabled.
- The UI only opens after private-test mode has been explicitly enabled and the operator presses **Insert**.
- When the menu is closed, it does not consume game input through ImGui.
- Renderer/UI resources are released during shutdown.

These behaviors are for clean testing and operator ergonomics. The project intentionally does **not** hide the DLL, unlink module lists, spoof signatures, bypass anti-cheat, suppress security telemetry, or provide manual-map/driver-based loading.

## Private test guard

The DLL remains dormant unless one of these environment variables is enabled before Battlefront II starts:

```text
BF2_PRIVATE_DEV_MENU=1
KYBER_DEV_MODE=1
```

This is an explicit operator-controlled guard. It is not anti-cheat detection.

## Building

Use a 64-bit Visual Studio developer environment with CMake and Git available.

```powershell
cd Battlefront2Internal
cmake -S . -B build -A x64
cmake --build build --config Release
```

The output library is named `Battlefront2DevMenu.dll`.

ImGui and MinHook are fetched by CMake at configure time.

## Loading

Load the DLL through the private/Kyber test environment you already use. No DLL injector is included in this project.

Press **Insert** to show or hide the menu after private-test mode is enabled.

## Frosty-backed features

The preferred gameplay implementation is Frosty. Features supplied through the selected Frosty profile are treated as launch-time game-data changes rather than pretending they are live runtime toggles.

Menu rendering and game behavior are deliberately separated. `src/menu.cpp` only dispatches a runtime feature after the private-test guard passes. A feature remains `wired = false` until its real local handler has been verified.

The focused Frosty references include:

```text
UI/InGame/InGameMenu/InGameMenuMPScreen
Gameplay/GameModes/Shared/PF_GameMode_Controller
SchematicChannels/ForceStartLobby_Schematic
```

The Frosty dev-pack profiles should remain mutually exclusive where they replace the same game-mode or menu assets.
