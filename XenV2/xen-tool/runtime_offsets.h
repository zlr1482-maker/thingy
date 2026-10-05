// runtime_offsets.h — fields populated from xen-api/data/offsets/<game>.json
//
// All UE-engine-internal offsets (UObject.class @+0x10, UStruct.SuperStruct
// @+0x40, vtable slots, AActor.bHidden flags byte, ULevel.Actors array,
// shim-assembly displacements) stay literal in main.cpp because they're
// stable across UE 4/5 minor patches.
//
// Per-game class field offsets — the ones that move when the game ships an
// update — live here.  ApplyOffsetsForGame() reads the JSON blob and
// populates one of these structs.  Any code in main.cpp that previously
// did `pawn + 0x1448` now does `pawn + g_offsets.isle.characterBase.customizerData`.
#pragma once
#include <cstdint>
#include <string>
#include "xen_api_client.h"

namespace runtime_offsets {

// All defaults are 0.  Reads at offset 0 hit the vtable pointer of the
// containing object, which is meaningless data — this guarantees that if
// the API hasn't populated a field, we read garbage rather than silently
// reusing a baked-in offset that may no longer match the live build.
//
// The cheat menu blocks Launch + Attach until xen-api returns a 200 with
// a populated blob, so this state should never be observed unless someone
// bypasses the gate.
struct IsleCharacterBase {
    uint32_t playerState           = 0;
    uint32_t eatingChunkPiece      = 0;
    uint32_t steamIdFString        = 0;
    uint32_t playerNameFString     = 0;   // ATICharacterBase.PlayerName — Isle stores it on the actor, not on APlayerState
    uint32_t abilitySystemComp     = 0;
    uint32_t corpseStateEnterTime  = 0;
    uint32_t elderReplicationStacks= 0;
    uint32_t customizerData        = 0;
    uint32_t speciesType           = 0;
    uint32_t growth                = 0;
    uint32_t eligiblePrimeElder    = 0;
    uint32_t nutrientsValues       = 0;
};

struct IslePlayerController {
    uint32_t playerState            = 0;
    uint32_t bIsAdminCred           = 0;
    uint32_t bIsDevCred             = 0;
    uint32_t bPreparingToEntumb     = 0;
    uint32_t temporaryEntumbMutData = 0;
    uint32_t temporaryEntumbClass   = 0;
    uint32_t temporarySkinData      = 0;
};

struct IslePlayerCameraManager {
    uint32_t ptrFromPC          = 0;
    uint32_t cameraCachePrivate = 0;
    uint32_t povOffset          = 0;
};

struct IslePlayerState {
    uint32_t playerNamePrivate = 0;
    uint32_t steamIdFString    = 0;
};

struct IsleGameInstance {
    uint32_t zoneName = 0;
};

struct IsleAttributeSet {
    uint32_t healthSlot         = 0;
    uint32_t maxHealthSlot      = 0;
    uint32_t staminaSlot        = 0;
    uint32_t currentValueOffset = 0;
};

struct Isle {
    IsleCharacterBase       characterBase;
    IslePlayerController    playerController;
    IslePlayerCameraManager playerCameraManager;
    IslePlayerState         playerState;
    IsleGameInstance        gameInstance;
    IsleAttributeSet        attributeSet;
};

struct BobPlayerState {
    uint32_t steamId       = 0;
    uint32_t controllerCID = 0;
};

struct BobPawn  { uint32_t userFov     = 0; };
struct BobCamera{ uint32_t fieldOfView = 0; uint32_t defaultFov = 0; };
struct BobCarcass {
    uint32_t carcassDinoType = 0;
    uint32_t rancidness      = 0;
};
struct BobDinosaur {
    uint32_t speciesType     = 0;
    uint32_t nutrientsValues = 0;
};

struct Bob {
    BobPlayerState playerState;
    BobPawn        pawn;
    BobCamera      camera;
    BobCarcass     carcass;
    BobDinosaur    dinosaur;
};

struct All {
    Isle isle;
    Bob  bob;
};

// Helper: parse a hex field on a sub-object, falling back to the existing
// default when missing.  This way a partial JSON blob doesn't reset
// fields the API didn't ship for this build.
inline void ApplyHex(uint32_t& slot, const std::string& obj, const std::string& name) {
    auto v = xen_api::JsonHexField(obj, name);
    if (v) slot = (uint32_t)v;
}

inline void ApplyIsle(All& all, const std::string& body) {
    auto isleBlock = xen_api::JsonSubObject(body, "isle");
    if (!isleBlock) return;

    auto& I = all.isle;
    if (auto cb = xen_api::JsonSubObject(*isleBlock, "characterBase")) {
        ApplyHex(I.characterBase.playerState,            *cb, "playerState");
        ApplyHex(I.characterBase.eatingChunkPiece,       *cb, "eatingChunkPiece");
        ApplyHex(I.characterBase.steamIdFString,         *cb, "steamIdFString");
        ApplyHex(I.characterBase.playerNameFString,      *cb, "playerNameFString");
        ApplyHex(I.characterBase.abilitySystemComp,      *cb, "abilitySystemComp");
        ApplyHex(I.characterBase.corpseStateEnterTime,   *cb, "corpseStateEnterTime");
        ApplyHex(I.characterBase.elderReplicationStacks, *cb, "elderReplicationStacks");
        ApplyHex(I.characterBase.customizerData,         *cb, "customizerData");
        ApplyHex(I.characterBase.speciesType,            *cb, "speciesType");
        ApplyHex(I.characterBase.growth,                 *cb, "growth");
        ApplyHex(I.characterBase.eligiblePrimeElder,     *cb, "eligiblePrimeElder");
        ApplyHex(I.characterBase.nutrientsValues,        *cb, "nutrientsValues");
    }
    if (auto pc = xen_api::JsonSubObject(*isleBlock, "playerController")) {
        ApplyHex(I.playerController.playerState,             *pc, "playerState");
        ApplyHex(I.playerController.bIsAdminCred,            *pc, "bIsAdminCred");
        ApplyHex(I.playerController.bIsDevCred,              *pc, "bIsDevCred");
        ApplyHex(I.playerController.bPreparingToEntumb,      *pc, "bPreparingToEntumb");
        ApplyHex(I.playerController.temporaryEntumbMutData,  *pc, "temporaryEntumbMutData");
        ApplyHex(I.playerController.temporaryEntumbClass,    *pc, "temporaryEntumbClass");
        ApplyHex(I.playerController.temporarySkinData,       *pc, "temporarySkinData");
    }
    if (auto cm = xen_api::JsonSubObject(*isleBlock, "playerCameraManager")) {
        ApplyHex(I.playerCameraManager.ptrFromPC,          *cm, "ptrFromPC");
        ApplyHex(I.playerCameraManager.cameraCachePrivate, *cm, "cameraCachePrivate");
        ApplyHex(I.playerCameraManager.povOffset,          *cm, "povOffset");
    }
    if (auto ps = xen_api::JsonSubObject(*isleBlock, "playerState")) {
        ApplyHex(I.playerState.playerNamePrivate, *ps, "playerNamePrivate");
        ApplyHex(I.playerState.steamIdFString,    *ps, "steamIdFString");
    }
    if (auto gi = xen_api::JsonSubObject(*isleBlock, "gameInstance")) {
        ApplyHex(I.gameInstance.zoneName, *gi, "zoneName");
    }
    if (auto at = xen_api::JsonSubObject(*isleBlock, "attributeSet")) {
        ApplyHex(I.attributeSet.healthSlot,         *at, "healthSlot");
        ApplyHex(I.attributeSet.maxHealthSlot,      *at, "maxHealthSlot");
        ApplyHex(I.attributeSet.staminaSlot,        *at, "staminaSlot");
        ApplyHex(I.attributeSet.currentValueOffset, *at, "currentValueOffset");
    }
}

inline void ApplyBob(All& all, const std::string& body) {
    auto bobBlock = xen_api::JsonSubObject(body, "bob");
    if (!bobBlock) return;
    auto& B = all.bob;
    if (auto ps = xen_api::JsonSubObject(*bobBlock, "playerState")) {
        ApplyHex(B.playerState.steamId,       *ps, "steamId");
        ApplyHex(B.playerState.controllerCID, *ps, "controllerCID");
    }
    if (auto pn = xen_api::JsonSubObject(*bobBlock, "pawn")) {
        ApplyHex(B.pawn.userFov, *pn, "userFov");
    }
    if (auto cm = xen_api::JsonSubObject(*bobBlock, "camera")) {
        ApplyHex(B.camera.fieldOfView, *cm, "fieldOfView");
        ApplyHex(B.camera.defaultFov,  *cm, "defaultFov");
    }
    if (auto ca = xen_api::JsonSubObject(*bobBlock, "carcass")) {
        ApplyHex(B.carcass.carcassDinoType, *ca, "carcassDinoType");
        ApplyHex(B.carcass.rancidness,      *ca, "rancidness");
    }
    if (auto dn = xen_api::JsonSubObject(*bobBlock, "dinosaur")) {
        ApplyHex(B.dinosaur.speciesType,     *dn, "speciesType");
        ApplyHex(B.dinosaur.nutrientsValues, *dn, "nutrientsValues");
    }
}

inline void ApplyForGame(All& all, const std::string& gameKey, const std::string& body) {
    if (gameKey == "evrima") ApplyIsle(all, body);
    else if (gameKey == "bob") ApplyBob(all, body);
}

} // namespace runtime_offsets
