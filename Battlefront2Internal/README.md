# Battlefront II Private Dev Menu

Standalone internal D3D11/ImGui menu for **STAR WARS Battlefront II (2017)**. It is independent of Xen and contains no injector, driver, anti-cheat bypass, or protection-evasion code.

## Current state

The internal menu shell is implemented and checks that it is running inside `starwarsbattlefrontii.exe` before installing its renderer hooks.

The following menu categories are present:

- Session
- Heroes / Spawn
- Combat
- Abilities
- Visual / Diagnostics
- Loadout

The entries are mapped to the Frosty assets recovered from the supplied `.fbmod` and `.fbproject` files. Runtime gameplay handlers are intentionally marked **pending** until each Frostbite/Kyber call is identified and validated. A pending toggle restores its previous value instead of pretending that the feature worked.

## Private test guard

The DLL does nothing unless one of these environment variables is enabled before Battlefront II starts:

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

Press **Insert** to show or hide the menu.

## Runtime handler policy

Menu rendering and game behavior are deliberately separated. `src/menu.cpp` only dispatches a feature after the private-test guard passes. A feature remains `wired = false` until its real Frostbite/Kyber implementation has been verified.

The first recommended runtime handler to implement is `force_start_lobby`, because the supplied Frosty project narrows it to:

```text
UI/InGame/InGameMenu/InGameMenuMPScreen
Gameplay/GameModes/Shared/PF_GameMode_Controller
SchematicChannels/ForceStartLobby_Schematic
```

After that, the smaller hero-spawn/game-starter package is the next clean target.

## Related Kyber plugin scaffold

`../Battlefront2DevMenu/` contains a Kyber Lua plugin scaffold and a more detailed Frosty feature registry. It is kept separate from this renderer so the runtime implementation can use Kyber's supported scripting/events where possible while the internal UI remains independent.
