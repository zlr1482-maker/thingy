# Battlefront II Frosty Dev Menu Profiles

This directory documents the Frosty-backed implementation path for the private/Kyber test menu. It is independent of Xen and does not include an injector, anti-cheat bypass, or protection-evasion code.

## Important

Pick exactly one menu/game-start profile at a time. The three menu packages below overlap on core Frostbite assets such as `UI/InGame/InGameMenu/InGameMenuMPScreen` and `Gameplay/GameModes/Shared/PF_GameMode_Controller`, so enabling more than one lets Frosty load order decide which version wins.

### Full Debug Menu

Use `Hero Debug Menu 3.5.fbmod` plus `starcardloadouts0918.fbmod`.

This is the broadest supplied in-game debug menu and advertises hero switching, respawn controls, early spawning, intro movement, timer skipping, spawn-screen removal, allegiance controls, and spectator/debug behavior.

### Lean Hero / Game Starter

Use `Hero spawn menu + Game starter + Emote toggle.fbmod` plus `starcardloadouts0918.fbmod`.

This is the closest match to a compact private-test menu. The supplied package advertises hero spawn selection, F1 respawn, H to start the pre-round phase, and an emote-triggered test state.

### Force Start Only

Use `Force start lobby.fbmod` plus `starcardloadouts0918.fbmod`.

This is the smallest profile. It adds a Force Start Lobby control to the in-game pause menu and touches only the in-game menu, shared game-mode controller, and `ForceStartLobby_Schematic` for that action.

## Excluded

The Frosty pack intentionally excludes the anti-cheat-labeled package and the wallhack package. Those are not part of this implementation path.
