# Beasts of Bermuda — RPC catalog

Source: `BeastsOfBermuda_classes.hpp` (Dumper-7 SDK, BoB 1.0)

Totals: **614 RPCs** across **57 classes** — 347 SERVER_, 182 CLIENT_, 85 MULTICAST_.

Protection breakdown (heuristic): 142 PROTECTED, 10 LIKELY-PROTECTED, 462 UNPROTECTED.

## RPC kinds (UE convention)

- **SERVER_** — `Server` reliable/unreliable; client→server. The function body runs on the *server*. If a SERVER_ RPC has no permission gate, **any client can call it** by invoking ProcessEvent on a replicated actor it owns.
- **CLIENT_** — `Client` RPC; server→owning-client. Cannot be invoked by another client.
- **MULTICAST_** — `NetMulticast`; server→all relevant clients. Only the server is authoritative; clients calling locally do nothing on the wire.

## Protection legend (best-effort)

- **PROTECTED** — name pattern strongly implies a server-side permission check (admin / moderator / cheat / save-file / ban-kick).
- **LIKELY-PROTECTED** — touches stats or stateful overrides; should be gated, but historically BoB has had unchecked SetX/Force* paths. Treat as worth probing.
- **UNPROTECTED** — gameplay-tier RPC (food/water/damage/grab/etc). Server may still validate distance/cooldown, but does not check role.

> The classification is by name only — the only authoritative source is the server's UFunction body. Confirm by reading the disassembly before relying on it.

---

## `ABaseCharacter` *(: ACharAndAI)* — 78 RPCs

### SERVER_

- `SERVER_Comfort_Bias_Decrease(float Value, int32 ReasonIndex)`  
  *Comfort Bias Decrease.* — **UNPROTECTED**
- `SERVER_Comfort_Bias_Increase(float Value, uint8 bAddToTrials)`  
  *Comfort Bias Increase.* — **UNPROTECTED**
- `SERVER_DamageEgg(int32 EggIndex)`  
  *Damage Egg.* — **UNPROTECTED**
- `SERVER_DestroyEggs()`  
  *Destroy Eggs.* — **UNPROTECTED**
- `SERVER_DoAggressiveAction(int32 MontageIndex)`  
  *Do Aggressive Action.* — **UNPROTECTED**
- `SERVER_DoAttack1Action(uint8 MontageIndex)`  
  *Do Attack1Action.* — **UNPROTECTED**
- `SERVER_DoAttack2Action(uint8 MontageIndex)`  
  *Do Attack2Action.* — **UNPROTECTED**
- `SERVER_DoCallAction(int32 MontageIndex)`  
  *Do Call Action.* — **UNPROTECTED**
- `SERVER_DoDistressAction(int32 MontageIndex)`  
  *Do Distress Action.* — **UNPROTECTED**
- `SERVER_DoEmote(class FName EmoteName)`  
  *Do Emote.* — **UNPROTECTED**
- `SERVER_DoRoarAction(int32 MontageIndex)`  
  *Do Roar Action.* — **UNPROTECTED**
- `SERVER_DoSubmissiveAction(int32 MontageIndex)`  
  *Do Submissive Action.* — **UNPROTECTED**
- `SERVER_Experience_Gained(int64 Value)`  
  *Experience Gained.* — **UNPROTECTED**
- `SERVER_Experience_Lost(int64 Value)`  
  *Experience Lost.* — **UNPROTECTED**
- `SERVER_Food_Consumed(float Value, bool bConsumeSatiationBeforeFood)`  
  *Food Consumed.* — **UNPROTECTED**
- `SERVER_Food_Regenerated(float Value, float SatiationMod, class AActor* FromActor, class AActor* ToActor, uint8 bAddToTrials, EFoodType FoodType)`  
  *Award food (+ optional satiation, diet, trial credit).* — **UNPROTECTED**
- `SERVER_FreezeCharacterUntilTilesLoaded()`  
  *Freeze Character Until Tiles Loaded.* — **UNPROTECTED**
- `SERVER_GrowInternalEgg(float Amount)`  
  *Grow Internal Egg.* — **UNPROTECTED**
- `SERVER_LayEggsInNest(class ANestBase* Nest, int32 NumEggsToLay)`  
  *Lay Eggs In Nest.* — **UNPROTECTED**
- `SERVER_OnTeleported()`  
  *On Teleported.* — **PROTECTED**
- `SERVER_ProcessJump()`  
  *Process Jump.* — **UNPROTECTED**
- `SERVER_RemoveTalentLevels__Checked(ETalentEnum Talent, int32 Num)`  
  *Remove Talent Levels Checked.* — **UNPROTECTED**
- `SERVER_RepairNest(class ANestBase* Nest, float Amount)`  
  *Repair Nest.* — **UNPROTECTED**
- `SERVER_SetCanGrow(bool bCanGrowIn)`  
  *Set Can Grow.* — **UNPROTECTED**
- `SERVER_SetHasBeenSummoned(bool bHasBeenSummoned)`  
  *Set Has Been Summoned.* — **UNPROTECTED**
- `SERVER_SetHeldObject(const struct FHeldObject& ObjectData)`  
  *Set Held Object.* — **UNPROTECTED**
- `SERVER_SetInEventArea(bool bValue)`  
  *Set In Event Area.* — **UNPROTECTED**
- `SERVER_SetLookUpState(bool SetLookingUp)`  
  *Set Look Up State.* — **UNPROTECTED**
- `SERVER_SetMovementState(EMovementMode Mode, uint8 reason)`  
  *Set Movement State.* — **UNPROTECTED**
- `SERVER_SetStats(float ComfortIn, float BreathFillIn, float StaminaIn, float AbilityIn, float BiasIn, float VenomResourceIn, int32 NumEggsIn)`  
  *Set Stats.* — **LIKELY-PROTECTED**
- `SERVER_SetTalentLevel(const struct FTalentLevel& Talent, bool bForce, bool bBypassInheritCheck)`  
  *Set Talent Level.* — **PROTECTED**
- `SERVER_SetTalentLevel__Checked(const struct FTalentLevel& Talent)`  
  *Set Talent Level Checked.* — **PROTECTED**
- `SERVER_StopEmote()`  
  *Stop Emote.* — **UNPROTECTED**
- `SERVER_TilesLoadedUnfreezeCharacter()`  
  *Tiles Loaded Unfreeze Character.* — **UNPROTECTED**
- `SERVER_UpdateLastReceivedCombatAction()`  
  *Update Last Received Combat Action.* — **UNPROTECTED**
- `SERVER_Water_Consumed(float Value, bool bConsumeSatiationBeforeWater)`  
  *Water Consumed.* — **UNPROTECTED**
- `SERVER_Water_Regenerated(float Value, float SatiationMod, float WaterDirtiness, class AActor* DrinkingFromWaterBody, float VarietyValue, uint8 bAddToTrials)`  
  *Award water (+ satiation/dirtiness/variety, trial credit).* — **UNPROTECTED**
- `SERVER_Water_Satiation_Regenerated(float Value)`  
  *Award only water satiation.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_Damage_Received(float Value, class AActor* FromActor, class AActor* ToActor, bool bLegHit, bool bTailHit, TSubclassOf<class UCombatDamageTypes> DamageType, const struct FVector& Direction, ECombatLogVerbosity Verbosity, int32 ReasonIndex, EHitboxType HitBox)`  
  *Damage Received.* — **UNPROTECTED**
- `CLIENT_DamageEgg()`  
  *Damage Egg.* — **UNPROTECTED**
- `CLIENT_DoAggressiveAction(int32 MontageIndex)`  
  *Do Aggressive Action.* — **UNPROTECTED**
- `CLIENT_DoAttack1Action(uint8 MontageIndex)`  
  *Do Attack1Action.* — **UNPROTECTED**
- `CLIENT_DoAttack2Action(uint8 MontageIndex)`  
  *Do Attack2Action.* — **UNPROTECTED**
- `CLIENT_DoCallAction(int32 MontageIndex)`  
  *Do Call Action.* — **UNPROTECTED**
- `CLIENT_DoDistressAction(int32 MontageIndex)`  
  *Do Distress Action.* — **UNPROTECTED**
- `CLIENT_DoEmote(class FName EmoteName)`  
  *Do Emote.* — **UNPROTECTED**
- `CLIENT_DoRoarAction(int32 MontageIndex)`  
  *Do Roar Action.* — **UNPROTECTED**
- `CLIENT_DoSubmissiveAction(int32 MontageIndex)`  
  *Do Submissive Action.* — **UNPROTECTED**
- `CLIENT_EggReadyNotification()`  
  *Egg Ready Notification.* — **UNPROTECTED**
- `CLIENT_Experience_Gained(int64 Value, ECombatLogVerbosity Verbosity, int32 ReasonIndex)`  
  *Experience Gained.* — **UNPROTECTED**
- `CLIENT_Experience_Lost(int64 Value, ECombatLogVerbosity Verbosity, int32 ReasonIndex)`  
  *Experience Lost.* — **UNPROTECTED**
- `CLIENT_Food_Consumed(float Value, bool bConsumeSatiationBeforeFood, class AActor* FromActor, class AActor* ToActor, ECombatLogVerbosity Verbosity, int32 ReasonIndex)`  
  *Food Consumed.* — **UNPROTECTED**
- `CLIENT_Food_Regenerated(float Value, float SatiationMod, class AActor* FromActor, class AActor* ToActor, ECombatLogVerbosity Verbosity, int32 ReasonIndex, EFoodType FoodType)`  
  *Award food (+ optional satiation, diet, trial credit).* — **UNPROTECTED**
- `CLIENT_FreezeCharacterUntilTilesLoaded()`  
  *Freeze Character Until Tiles Loaded.* — **UNPROTECTED**
- `CLIENT_Growth_Tick(float GrowthValue, float BaseValue, ECombatLogVerbosity Verbosity)`  
  *Growth Tick.* — **UNPROTECTED**
- `CLIENT_InformPlayerTheyCaughtSickness(EStatusCondition SicknessType, EGotSickReason reason)`  
  *Inform Player They Caught Sickness.* — **UNPROTECTED**
- `CLIENT_NewWaterSourceVisited(class AActor* WaterSource, int32 NumVisited)`  
  *New Water Source Visited.* — **UNPROTECTED**
- `CLIENT_OnAcceptedMatingRequest(const struct FUniquePawnID& MateID, int32 NumEggs)`  
  *On Accepted Mating Request.* — **UNPROTECTED**
- `CLIENT_SetLookUpState(bool SetLookingUp)`  
  *Set Look Up State.* — **UNPROTECTED**
- `CLIENT_Water_Consumed(float Value, bool bConsumeSatiationBeforeWater, bool bIsAir, class AActor* FromActor, class AActor* ToActor, ECombatLogVerbosity Verbosity, int32 ReasonIndex)`  
  *Water Consumed.* — **UNPROTECTED**
- `CLIENT_Water_Regenerated(float Value, float SatiationMod, bool bIsAir, class AActor* FromActor, class AActor* ToActor, ECombatLogVerbosity Verbosity, float VarietyValue, int32 ReasonIndex)`  
  *Award water (+ satiation/dirtiness/variety, trial credit).* — **UNPROTECTED**
- `CLIENT_Water_Satiation_Regenerated(float Value, class AActor* FromActor, class AActor* ToActor, ECombatLogVerbosity Verbosity, int32 ReasonIndex)`  
  *Award only water satiation.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_DoAggressiveAction(int32 MontageIndex)`  
  *Do Aggressive Action.* — **UNPROTECTED**
- `MULTICAST_DoAttack1Action(uint8 MontageIndex)`  
  *Do Attack1Action.* — **UNPROTECTED**
- `MULTICAST_DoAttack2Action(uint8 MontageIndex)`  
  *Do Attack2Action.* — **UNPROTECTED**
- `MULTICAST_DoCallAction(int32 MontageIndex)`  
  *Do Call Action.* — **UNPROTECTED**
- `MULTICAST_DoDistressAction(int32 MontageIndex)`  
  *Do Distress Action.* — **UNPROTECTED**
- `MULTICAST_DoEmote(class FName EmoteName)`  
  *Do Emote.* — **UNPROTECTED**
- `MULTICAST_DoRoarAction(int32 MontageIndex)`  
  *Do Roar Action.* — **UNPROTECTED**
- `MULTICAST_DoSubmissiveAction(int32 MontageIndex)`  
  *Do Submissive Action.* — **UNPROTECTED**
- `MULTICAST_OnPossessed(bool bIsPlayerController)`  
  *On Possessed.* — **UNPROTECTED**
- `MULTICAST_OnTeleported()`  
  *On Teleported.* — **PROTECTED**
- `MULTICAST_PlayBleed()`  
  *Play Bleed.* — **UNPROTECTED**
- `MULTICAST_PlayInjury()`  
  *Play Injury.* — **UNPROTECTED**
- `MULTICAST_ProcessJump()`  
  *Process Jump.* — **UNPROTECTED**
- `MULTICAST_SetModelAngles(float ModelTiltPitch, float ModelTiltRoll, float ModelJawAngle, float ModelZOffset, float EyePitch, float EyeYaw, float EyeClosedness)`  
  *Set Model Angles.* — **UNPROTECTED**
- `MULTICAST_SetMovementState(EMovementMode Mode, uint8 reason)`  
  *Set Movement State.* — **UNPROTECTED**
- `MULTICAST_StopEmote()`  
  *Stop Emote.* — **UNPROTECTED**

## `ABBGameStateBase` *(: AGameStateBase)* — 6 RPCs

### SERVER_

- `SERVER_AddCreatureToOthersGroup(int64 InviterCreatureUniquePawnID, int64 InviteeCreatureUniquePawnID)`  
  *Add Creature To Others Group.* — **UNPROTECTED**
- `SERVER_AddCreatureToSpecifiedGroup(int64 GroupID, int64 CreatureUniquePawnID, bool bForceJoin)`  
  *Add Creature To Specified Group.* — **UNPROTECTED**
- `SERVER_CreateSoloGroup(int64 CreatureUniquePawnID)`  
  *Create Solo Group.* — **UNPROTECTED**
- `SERVER_PostGroupToFinder(int64 GroupID, const class FString& GroupTitle)`  
  *Post Group To Finder.* — **UNPROTECTED**
- `SERVER_RemoveCreatureFromGroup(int64 CreatureUniquePawnID, ELeftGroupReason LeftGroupReason)`  
  *Remove Creature From Group.* — **UNPROTECTED**
- `SERVER_RemoveGroupFromFinder(int64 GroupID)`  
  *Remove Group From Finder.* — **UNPROTECTED**

## `ABBPlayerState` *(: AProfiledPlayer)* — 51 RPCs

### SERVER_

- `SERVER_AddDeathForPlayer()`  
  *Add Death For Player.* — **UNPROTECTED**
- `SERVER_AddKillForPlayer()`  
  *Add Kill For Player.* — **UNPROTECTED**
- `SERVER_AttemptReincarnateEntity(uint8 ReincarnateIndex, uint8 Flags_0, class FName SpawnRegion)`  
  *Attempt Reincarnate Entity.* — **UNPROTECTED**
- `SERVER_AttemptResurrectEntity(const TArray<uint8>& SacrificedIndices, uint8 ResurrectIndex, bool bForceResurrectAnyways, class FName SpawnRegion)`  
  *Attempt Resurrect Entity.* — **LIKELY-PROTECTED**
- `SERVER_BroadcastDinoVoice(const struct FDinoVoiceBroadcast& Broadcast)`  
  *Broadcast Dino Voice.* — **UNPROTECTED**
- `SERVER_ClearChatFromPlayer(int64 Target)`  
  *Clear Chat From Player.* — **UNPROTECTED**
- `SERVER_ClientRecordLeaderboardAnonymous(bool bNewValue)`  
  *Client Record Leaderboard Anonymous.* — **UNPROTECTED**
- `SERVER_ClientRequestsAdminCamData()`  
  *Client Requests Admin Cam Data.* — **PROTECTED**
- `SERVER_ClientRequestsPlayerListData()`  
  *Client Requests Player List Data.* — **UNPROTECTED**
- `SERVER_ClientSendMessage(const struct FPlayerMessage& InMessage)`  
  *Client Send Message.* — **UNPROTECTED**
- `SERVER_ClientSendQuickMessage(const struct FQuickMessage& InMessage)`  
  *Client Send Quick Message.* — **UNPROTECTED**
- `SERVER_InvitePlayerToGroup(int64 InPlayerId64)`  
  *Invite Player To Group.* — **UNPROTECTED**
- `SERVER_QueryLeaderboard(EGauntlets Query, const struct FLeaderboardQueryParams& QueryParams)`  
  *Query Leaderboard.* — **UNPROTECTED**
- `SERVER_RemoveCharacterFromSave(int64 InUID)`  
  *Remove Character From Save.* — **UNPROTECTED**
- `SERVER_RemovePlayerFromGroup(int64 InPlayerId64)`  
  *Remove Player From Group.* — **UNPROTECTED**
- `SERVER_RemovePlayerFromSave(int64 InID64)`  
  *Remove Player From Save.* — **UNPROTECTED**
- `SERVER_RequestEntityDataFor(int64 UID)`  
  *Request Entity Data For.* — **UNPROTECTED**
- `SERVER_RequestEntityTrackingDataFor(const int64 UID)`  
  *Request Entity Tracking Data For.* — **UNPROTECTED**
- `SERVER_RequestEntityTrackingDatas()`  
  *Request Entity Tracking Datas.* — **UNPROTECTED**
- `SERVER_RequestPlayerSkin(class AActor* Target)`  
  *Request Player Skin.* — **UNPROTECTED**
- `SERVER_RequestTalentsFor(int64 UID)`  
  *Request Talents For.* — **UNPROTECTED**
- `SERVER_ResetEntityGrowth(uint8 EntityIndex)`  
  *Reset Entity Growth.* — **UNPROTECTED**
- `SERVER_SetPlayerMuted(bool bValue)`  
  *Set Player Muted.* — **PROTECTED**
- `SERVER_SetPlayerToRandomName()`  
  *Set Player To Random Name.* — **UNPROTECTED**
- `SERVER_ToggleIncognitoMode(const class FString& NewName)`  
  *Toggle Incognito Mode.* — **UNPROTECTED**
- `SERVER_TryPurchaseInheritIn(ETalentEnum Talent, int64 UID)`  
  *Try Purchase Inherit In.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_ClearChatFromPlayer(int64 Target)`  
  *Clear Chat From Player.* — **UNPROTECTED**
- `CLIENT_FailSendMessage(uint8 reason)`  
  *Fail Send Message.* — **UNPROTECTED**
- `CLIENT_GetWarnedAboutCheatsOnThisServer()`  
  *Get Warned About Cheats On This Server.* — **PROTECTED**
- `CLIENT_GotEntityTrackingDataFor(const struct FEntityTrackingData& EntityData)`  
  *Got Entity Tracking Data For.* — **UNPROTECTED**
- `CLIENT_GotEntityTrackingDatas(const TArray<struct FEntityTrackingData>& EntityDatas)`  
  *Got Entity Tracking Datas.* — **UNPROTECTED**
- `CLIENT_OnAchievementProgress(const struct FGameAchievement& A)`  
  *On Achievement Progress.* — **UNPROTECTED**
- `CLIENT_OnAchievementUnlocked(int32 AchievementId)`  
  *On Achievement Unlocked.* — **UNPROTECTED**
- `CLIENT_OnIncognitoModeToggled(bool bIncognitoIn, const class FString& NewName)`  
  *On Incognito Mode Toggled.* — **UNPROTECTED**
- `CLIENT_OnReincarnatedEntity(EReincarnateSuccessCode SuccessCode)`  
  *On Reincarnated Entity.* — **UNPROTECTED**
- `CLIENT_ReceivedAdminCamData(const TArray<struct FAdminCamInfo>& Data)`  
  *Received Admin Cam Data.* — **PROTECTED**
- `CLIENT_ReceiveDinoVoiceBroadcast(const struct FDinoVoiceBroadcast& Broadcast)`  
  *Receive Dino Voice Broadcast.* — **UNPROTECTED**
- `CLIENT_ReceivedPLData(const struct FPlayerListPacket& Data)`  
  *Received PLData.* — **UNPROTECTED**
- `CLIENT_ReceiveLeaderboard(const struct FLeaderboardQueryResults& result)`  
  *Receive Leaderboard.* — **UNPROTECTED**
- `CLIENT_ReceiveMessage(const struct FDispatchedMessage& InMessage)`  
  *Receive Message.* — **UNPROTECTED**
- `CLIENT_ReceiveMsgOfTheDay(const class FString& TheMessage)`  
  *Receive Msg Of The Day.* — **UNPROTECTED**
- `CLIENT_ReceiveQuickMessage(const struct FQuickMessage& InMessage)`  
  *Receive Quick Message.* — **UNPROTECTED**
- `CLIENT_ReceiveSkinToCache(class AActor* Target, const struct FNetGeneratedSkin& NewSkin)`  
  *Receive Skin To Cache.* — **UNPROTECTED**
- `CLIENT_RequestedEntityDataReceived(const struct FEntityTrackingData& DataReceived, EDinoType Species, float GrowthLevel)`  
  *Requested Entity Data Received.* — **UNPROTECTED**
- `CLIENT_RequestedTalentDataReceived(const TArray<struct FTalentLevel>& DataReceived, uint8 TalentTreeIndex)`  
  *Requested Talent Data Received.* — **UNPROTECTED**
- `CLIENT_SetAdminLevel(int32 AdminLevelIn)`  
  *Set Admin Level.* — **PROTECTED**
- `CLIENT_SetAdminRules(const struct FAdminCommandRules& AdminRulesIn)`  
  *Set Admin Rules.* — **PROTECTED**
- `CLIENT_SetIsAdmin(bool bIsAdminIn)`  
  *Set Is Admin.* — **PROTECTED**
- `CLIENT_SetPlayerMuted(bool bValue)`  
  *Set Player Muted.* — **PROTECTED**
- `CLIENT_TryPurchaseInheritIn(ETalentEnum Talent, bool bSuccess)`  
  *Try Purchase Inherit In.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_SetIncognito(bool bValue)`  
  *Set Incognito.* — **UNPROTECTED**

## `ACharAndAI` *(: ACharacter)* — 4 RPCs

### SERVER_

- `SERVER_Damage_Dealt(float Value, class AActor* FromActor, class AActor* ToActor, bool bLegHit, bool bTailHit, TSubclassOf<class UCombatDamageTypes> DamageType, const struct FVector& Direction, ECombatLogVerbosity Verbosity, float ClientServerTime, bool bIgnoreLagCheck, int32 ReasonIndex, EHitboxType HitBox, int32 DamageIndex)`  
  *Apply combat damage from FromActor to ToActor.* — **UNPROTECTED**
- `SERVER_Damage_Dealt_Internal(float Value, class AActor* FromActor, class AActor* ToActor, bool bLegHit, bool bTailHit, TSubclassOf<class UCombatDamageTypes> DamageType, const struct FVector& Direction, ECombatLogVerbosity Verbosity, float ClientServerTime, bool bIgnoreLagCheck, int32 ReasonIndex, EHitboxType HitBox)`  
  *Apply combat damage from FromActor to ToActor.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_Damage_Dealt(float Value, class AActor* FromActor, class AActor* ToActor, bool bLegHit, bool bTailHit, TSubclassOf<class UCombatDamageTypes> DamageType, const struct FVector& Direction, ECombatLogVerbosity Verbosity, float ClientServerTime, bool bIgnoreLagCheck, int32 ReasonIndex, EHitboxType HitBox)`  
  *Apply combat damage from FromActor to ToActor.* — **UNPROTECTED**
- `CLIENT_Damage_Dealt_Internal(float Value, class AActor* FromActor, class AActor* ToActor, bool bLegHit, bool bTailHit, TSubclassOf<class UCombatDamageTypes> DamageType, const struct FVector& Direction, ECombatLogVerbosity Verbosity, float ClientServerTime, bool bIgnoreLagCheck, int32 ReasonIndex, EHitboxType HitBox)`  
  *Apply combat damage from FromActor to ToActor.* — **UNPROTECTED**

## `ADeityShrineBase` *(: ABBActorBase)* — 2 RPCs

### SERVER_

- `SERVER_BlessPlayer(class ACharacter* CharRef, float TimeSeconds)`  
  *Bless Player.* — **UNPROTECTED**
- `SERVER_SacrificePlayer(class ACharacter* CharRef)`  
  *Sacrifice Player.* — **UNPROTECTED**

## `AFishControllerBase` *(: AActor)* — 1 RPC

### MULTICAST_

- `MULTICAST_SetNewFishDestinations(const struct FRandomStream& NumStream)`  
  *Set New Fish Destinations.* — **UNPROTECTED**

## `AFoliageControllerBase` *(: AActor)* — 22 RPCs

### SERVER_

- `SERVER_AddDynamicActorByName(class FName Name_0, const struct FTransform& WorldTransform, uint8 bSave)`  
  *Add Dynamic Actor By Name.* — **UNPROTECTED**
- `SERVER_AddDynamicFoliageInstance(uint16 DataTableIndex, const struct FTransform& WorldTransform, uint8 bDestroyOnEaten, uint8 bSave, uint8 bInedible, float DespawnsIn)`  
  *Add Dynamic Foliage Instance.* — **UNPROTECTED**
- `SERVER_AddDynamicFoliageInstanceByName(class FName Name_0, const struct FTransform& WorldTransform, uint8 bDestroyOnEaten, uint8 bSave, uint8 bInedible, float DespawnsIn)`  
  *Add Dynamic Foliage Instance By Name.* — **UNPROTECTED**
- `SERVER_AddEatenFoliage(const struct FFoliageNetPacketData& Data)`  
  *Add Eaten Foliage.* — **UNPROTECTED**
- `SERVER_DeleteDynamicActorsNearLocation(const struct FVector& Location, float Radius, class FName Name_0)`  
  *Delete Dynamic Actors Near Location.* — **UNPROTECTED**
- `SERVER_RemoveAllDynamicFoliage()`  
  *Remove All Dynamic Foliage.* — **UNPROTECTED**
- `SERVER_RemoveAllDynamicFoliageByName(class FName Name_0)`  
  *Remove All Dynamic Foliage By Name.* — **UNPROTECTED**
- `SERVER_RemoveAllDynamicFoliageOfType(uint16 DataTableIndex)`  
  *Remove All Dynamic Foliage Of Type.* — **UNPROTECTED**
- `SERVER_RemoveDynamicFoliageInstance(uint16 DataTableIndex, int32 index)`  
  *Remove Dynamic Foliage Instance.* — **UNPROTECTED**
- `SERVER_RemoveDynamicFoliageInstanceByName(class FName Name_0, int32 index)`  
  *Remove Dynamic Foliage Instance By Name.* — **UNPROTECTED**
- `SERVER_RemoveStump(int32 index, bool bDestroy, bool bKnockDown, uint8 LevelIndex, uint16 DataTableIndex)`  
  *Remove Stump.* — **UNPROTECTED**
- `SERVER_RespawnFoliageTick()`  
  *Respawn Foliage Tick.* — **LIKELY-PROTECTED**
### MULTICAST_

- `MULTICAST_AddDynamicActorByName(class FName Name_0, const struct FTransform& WorldTransform)`  
  *Add Dynamic Actor By Name.* — **UNPROTECTED**
- `MULTICAST_AddDynamicFoliageInstance(uint16 DataTableIndex, const struct FTransform& WorldTransform, uint32 ActionIndex, int32 index, uint8 bDestroyOnEaten, uint8 bSave, uint8 bInedible, float DespawnsIn)`  
  *Add Dynamic Foliage Instance.* — **UNPROTECTED**
- `MULTICAST_AddEatenFoliage(const struct FFoliageNetPacketData& Data)`  
  *Add Eaten Foliage.* — **UNPROTECTED**
- `MULTICAST_AddMultipleEatenFoliages(const TArray<struct FFoliageNetPacketData>& DataIn)`  
  *Add Multiple Eaten Foliages.* — **UNPROTECTED**
- `MULTICAST_DeleteDynamicActorsNearLocation(const struct FVector& Location, float Radius, class FName Name_0)`  
  *Delete Dynamic Actors Near Location.* — **UNPROTECTED**
- `MULTICAST_RemoveAllDynamicFoliage()`  
  *Remove All Dynamic Foliage.* — **UNPROTECTED**
- `MULTICAST_RemoveAllDynamicFoliageOfType(uint16 DataTableIndex)`  
  *Remove All Dynamic Foliage Of Type.* — **UNPROTECTED**
- `MULTICAST_RemoveDynamicFoliageInstance(uint16 DataTableIndex, uint32 ActionIndex, int32 index)`  
  *Remove Dynamic Foliage Instance.* — **UNPROTECTED**
- `MULTICAST_RemoveStump(int32 index, bool bDestroy, bool bKnockDown, uint8 LevelIndex, uint16 DataTableIndex)`  
  *Remove Stump.* — **UNPROTECTED**
- `MULTICAST_RespawnFoliageTick()`  
  *Respawn Foliage Tick.* — **LIKELY-PROTECTED**

## `AForestFire` *(: ABBActorBase)* — 1 RPC

### MULTICAST_

- `MULTICAST_BurnFoliages(const struct FBurnedFoliageDataToClient& Data)`  
  *Burn Foliages.* — **UNPROTECTED**

## `AForestFireCell` *(: AActor)* — 1 RPC

### MULTICAST_

- `MULTICAST_SpawnFireAt(const struct FFireReplicatedLocations& NewFires)`  
  *Spawn Fire At.* — **UNPROTECTED**

## `ANestBase` *(: AActor)* — 2 RPCs

### SERVER_

- `SERVER_RepairNest(float Amount)`  
  *Repair Nest.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_NestReceivedDamage()`  
  *Nest Received Damage.* — **UNPROTECTED**

## `APlayerCamCPP` *(: AEditorCam)* — 6 RPCs

### SERVER_

- `SERVER_MoveToSpawn(ESpawnPointType Desired)`  
  *Move To Spawn.* — **UNPROTECTED**
- `SERVER_RerollSpawns(bool bForce)`  
  *Reroll Spawns.* — **UNPROTECTED**
- `SERVER_ResetAction()`  
  *Reset Action.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_EnsureNextActionDelayed(EPlayerCamAction InNextAction)`  
  *Ensure Next Action Delayed.* — **UNPROTECTED**
- `CLIENT_InitCam()`  
  *Init Cam.* — **UNPROTECTED**
- `CLIENT_OnRerolledSpawns(class AMapSpawnPoint* Selected, const struct FVector& NewLoc, const struct FRotator& NewRot)`  
  *On Rerolled Spawns.* — **UNPROTECTED**

## `APossessShrineBase` *(: ABBActorBase)* — 1 RPC

### SERVER_

- `SERVER_PlayerWantsToPossess(class ACharacter* CharRef)`  
  *Player Wants To Possess.* — **UNPROTECTED**

## `APPBasePlayerController` *(: APlayerController)* — 140 RPCs

### SERVER_

- `SERVER_AddInheritToTalent(const class FString& PlayerNameOrID, const class FString& Talent)`  
  *Add inherit points to a talent (admin).* — **PROTECTED**
- `SERVER_AddScentToArea(float Amount, float X, float Y, float Z, EDinoType ScentType)`  
  *Spawn scent at world location.* — **PROTECTED**
- `SERVER_AddToPlayerGrowth(const float GrowthToAdd, const class FString& PlayerNameOrID)`  
  *Increase player growth (admin).* — **PROTECTED**
- `SERVER_BackupSaveFiles()`  
  *Trigger server save backup (admin).* — **PROTECTED**
- `SERVER_BanPlayer(const class FString& NameOrCIDOrSteamID, int32 TimeMinutes, const class FString& reason, bool bBanMainAccount, bool bForceWebhookPrint)`  
  *Ban a player (admin).* — **PROTECTED**
- `SERVER_CheckAllDeadEntities()`  
  *Server-side admin diagnostic.* — **PROTECTED**
- `SERVER_CheckPlayerCanDo(EAdminCommands Cmd)`  
  *Permission check round-trip.* — **PROTECTED**
- `SERVER_CheckRESTQuery()`  
  *Server-side admin diagnostic.* — **PROTECTED**
- `SERVER_CheckSaveIntegrity()`  
  *Server-side admin diagnostic.* — **PROTECTED**
- `SERVER_Cleanse()`  
  *Cleanse sickness/state (admin).* — **PROTECTED**
- `SERVER_ClearAchievements(const class FString& pnid)`  
  *Wipe player achievements (admin).* — **PROTECTED**
- `SERVER_ClearAllBones()`  
  *Remove world bone props (admin).* — **PROTECTED**
- `SERVER_ClearDynamicFoliageOfType(const class FString& Name_0)`  
  *Clear specific dynamic foliage (admin).* — **PROTECTED**
- `SERVER_ClientGoesToBiome(const class FString& BiomeName)`  
  *Client Goes To Biome.* — **PROTECTED**
- `SERVER_Console_AddToTrial_FString(int32 Trial, const class FString& Value)`  
  *Console Add To Trial FString.* — **PROTECTED**
- `SERVER_Console_AddToTrial_Int64(int32 Trial, int64 Value)`  
  *Console Add To Trial Int64.* — **PROTECTED**
- `SERVER_Console_PledgePlayer(const class FString& InPlayer, int32 PledgeType)`  
  *Console Pledge Player.* — **PROTECTED**
- `SERVER_Console_SacrificePlayer(const class FString& InPlayer, int32 SacrificeType)`  
  *Console Sacrifice Player.* — **PROTECTED**
- `SERVER_CreateGenerations(int32 Count, const class FString& F, const class FString& M)`  
  *Create Generations.* — **PROTECTED**
- `SERVER_CreateTunnelVoid(float Xcoord, float Ycoord, float Zcoord, int32 Xlength, int32 Ylength, int32 Zlength)`  
  *Create Tunnel Void.* — **PROTECTED**
- `SERVER_DeleteTunnelNetwork()`  
  *Delete Tunnel Network.* — **PROTECTED**
- `SERVER_DestroyAllAI()`  
  *Destroy All AI.* — **PROTECTED**
- `SERVER_DestroyAllSpawnedFoliage()`  
  *Destroy All Spawned Foliage.* — **PROTECTED**
- `SERVER_DestroyAllWorldEvents()`  
  *Destroy All World Events.* — **PROTECTED**
- `SERVER_DisbandAllGroups()`  
  *Disband All Groups.* — **PROTECTED**
- `SERVER_DispatchAnnouncement(const class FString& FmtMessage)`  
  *Dispatch Announcement.* — **PROTECTED**
- `SERVER_DisplayCharactersByID()`  
  *Print player's characters list.* — **PROTECTED**
- `SERVER_DisplayCharactersByID64()`  
  *Print player's characters list.* — **PROTECTED**
- `SERVER_EatAllFoliageAt(float X, float Y, float Z, float Radius, bool bKnockDown, bool bWasEaten)`  
  *Eat foliage in radius (multicast for replication).* — **PROTECTED**
- `SERVER_EditorCreateDummyLeaderboard()`  
  *Editor Create Dummy Leaderboard.* — **PROTECTED**
- `SERVER_EnsureCharIsValid()`  
  *Client validity probe.* — **PROTECTED**
- `SERVER_EnterSpectate()`  
  *Enter Spectate.* — **PROTECTED**
- `SERVER_ExportCustomMap(const class FString& CustomName, const class FString& Desc)`  
  *Export Custom Map.* — **PROTECTED**
- `SERVER_FillAllFreshwater()`  
  *Fill All Freshwater.* — **PROTECTED**
- `SERVER_ForceServerSave()`  
  *Force Server Save.* — **PROTECTED**
- `SERVER_ForceSicknessState(bool bSickness, bool bRecovering, bool bClearAll, float SickTime)`  
  *Force Sickness State.* — **PROTECTED**
- `SERVER_GetBroodMatching(const class FString& IdList)`  
  *Get Brood Matching.* — **PROTECTED**
- `SERVER_GetOnlinePlayersString()`  
  *Get Online Players String.* — **PROTECTED**
- `SERVER_GetPunishedPlayers()`  
  *Get Punished Players.* — **PROTECTED**
- `SERVER_GiveEgg(int32 MotherID, int32 FatherID, float Completion)`  
  *Give Egg.* — **PROTECTED**
- `SERVER_GiveFavor(int32 Amount)`  
  *Give Favor.* — **PROTECTED**
- `SERVER_GiveInvItem(int32 ID)`  
  *Give Inv Item.* — **PROTECTED**
- `SERVER_KickPlayer(const class FString& NameOrId, const class FString& reason, bool bDetachPawnBeforeKick, bool bForceWebhookPrint)`  
  *Kick Player.* — **PROTECTED**
- `SERVER_KillForestFire()`  
  *Kill Forest Fire.* — **PROTECTED**
- `SERVER_LeaveSpectate()`  
  *Leave Spectate.* — **PROTECTED**
- `SERVER_ListTalentPointsFor(const class FString& NameOrId)`  
  *Send talent point list.* — **PROTECTED**
- `SERVER_LoadCustomMap(const class FString& MapName)`  
  *Load Custom Map.* — **PROTECTED**
- `SERVER_MakeBrood(int32 BabyCount, const class FString& M, const class FString& F, bool bSpawnDebugActors, float SpawnGrowth)`  
  *Make Brood.* — **PROTECTED**
- `SERVER_MakebroodDebug(int32 BabyCount, const class FString& M, const class FString& F)`  
  *Makebrood Debug.* — **PROTECTED**
- `SERVER_MatePlayers(int32 MaleID, int32 FemaleID)`  
  *Mate Players.* — **PROTECTED**
- `SERVER_ModeratorLogin(const class FString& Password)`  
  *Moderator Login.* — **PROTECTED**
- `SERVER_MoveMeToRandomSpawnPoint()`  
  *Move Me To Random Spawn Point.* — **PROTECTED**
- `SERVER_MoveToRandomSpawnPoint(class APlayerState* PSRef)`  
  *Move To Random Spawn Point.* — **PROTECTED**
- `SERVER_MutePlayer(const class FString& NameOrCIDOrSteamID, int32 TimeMinutes, const class FString& reason, bool bForceWebhookPrint)`  
  *Mute Player.* — **PROTECTED**
- `SERVER_PlayerWantsToUsePossessShrine(class AActor* PossessShrine)`  
  *Player Wants To Use Possess Shrine.* — **PROTECTED**
- `SERVER_PrintAllEatenFoliageInstancesMap()`  
  *Print All Eaten Foliage Instances Map.* — **PROTECTED**
- `SERVER_PrintDebugEntityData(int64 UID)`  
  *Print Debug Entity Data.* — **PROTECTED**
- `SERVER_PrintDebugSaveData()`  
  *Print Debug Save Data.* — **PROTECTED**
- `SERVER_PrintFoliageRAM()`  
  *Admin diagnostic print.* — **PROTECTED**
- `SERVER_PrintSicknessState()`  
  *Admin diagnostic print.* — **PROTECTED**
- `SERVER_PurchaseStoreItem(class FName ItemRowName, int64 CharacterUID)`  
  *Cosmetic store purchase.* — **PROTECTED**
- `SERVER_PurgeOrphanData()`  
  *Purge Orphan Data.* — **PROTECTED**
- `SERVER_RemoveAllScentNodeData()`  
  *Remove All Scent Node Data.* — **PROTECTED**
- `SERVER_RemovePlayerFromLeaderboard(int64 SteamIdIn)`  
  *Remove Player From Leaderboard.* — **PROTECTED**
- `SERVER_RemoveTunnelNetworkDirtPaddies()`  
  *Remove Tunnel Network Dirt Paddies.* — **PROTECTED**
- `SERVER_ResetFriendships()`  
  *Reset Friendships.* — **PROTECTED**
- `SERVER_ResetMaxGrowthTo(float V)`  
  *Reset Max Growth To.* — **PROTECTED**
- `SERVER_ResetServerFiles()`  
  *Reset Server Files.* — **PROTECTED**
- `SERVER_ResetSpecialization(const class FString& NameOrId)`  
  *Reset Specialization.* — **PROTECTED**
- `SERVER_ResetTalents(const class FString& NameOrId)`  
  *Reset Talents.* — **PROTECTED**
- `SERVER_Reskin(int32 TargetPlayerId)`  
  *Reskin.* — **PROTECTED**
- `SERVER_RestoreDeadEntity(int64 EntityUID)`  
  *Restore Dead Entity.* — **PROTECTED**
- `SERVER_RunGC()`  
  *Run GC.* — **PROTECTED**
- `SERVER_SecurityCheckConsoleCommandUsage(const class FString& Command)`  
  *Security Check Console Command Usage.* — **PROTECTED**
- `SERVER_ServerGlobalMute(float DurationSeconds, uint8 Flags_0)`  
  *Server Global Mute.* — **PROTECTED**
- `SERVER_SetAllTalentInheritsTo(const int32 Inherited, const int32 Purchased)`  
  *Set All Talent Inherits To.* — **PROTECTED**
- `SERVER_SetDiet(float plants, float Meat, float Fish, float Egg)`  
  *Set Diet.* — **PROTECTED**
- `SERVER_SetDisableDeath(bool bNewValue)`  
  *Set Disable Death.* — **PROTECTED**
- `SERVER_SetFriendship(int32 Player1ID, int32 Player2ID, float Points)`  
  *Set Friendship.* — **PROTECTED**
- `SERVER_SetGender(const class FString& GenderString, const class FString& NameOrId)`  
  *Set Gender.* — **PROTECTED**
- `SERVER_SetGrowthForPlayer(float Value)`  
  *Set Growth For Player.* — **PROTECTED**
- `SERVER_SetInBurrow(bool bInBurrow)`  
  *Set In Burrow.* — **PROTECTED**
- `SERVER_SetIncestMorph(float Value)`  
  *Set Incest Morph.* — **PROTECTED**
- `SERVER_SetModelAngles(float ModelTiltPitch, float ModelTiltRoll, float ModelJawAngle, float ModelZOffset, float EyePitch, float EyeYaw, float EyeClosedness)`  
  *Set Model Angles.* — **PROTECTED**
- `SERVER_SetMoonPhase(const float Phase)`  
  *Set Moon Phase.* — **PROTECTED**
- `SERVER_SetMutationOverrides(const class FString& EnabledMutations, const class FString& DisabledMutations)`  
  *Set Mutation Overrides.* — **PROTECTED**
- `SERVER_SetOnLockdown(bool bValue)`  
  *Set On Lockdown.* — **PROTECTED**
- `SERVER_SetPlayerGrowthLevel(const float GrowthLevel, const class FString& PlayerNameOrID)`  
  *Set Player Growth Level.* — **PROTECTED**
- `SERVER_SetPlayerTalent(const class FString& PlayerNameOrID, const class FString& Talent, int32 NumTalents, int32 NumInherits)`  
  *Set Player Talent.* — **PROTECTED**
- `SERVER_SetShelterState(float ShelterStateUpdate, float ShelterStateWithoutSheltererUpdate)`  
  *Set Shelter State.* — **PROTECTED**
- `SERVER_SetTalentDevOnly(const class FString& TalentName, const int32 Talented, const int32 Inherited, const int32 Purchased)`  
  *Set Talent Dev Only.* — **PROTECTED**
- `SERVER_SetTryingToLogOut(bool bValue)`  
  *Set Trying To Log Out.* — **PROTECTED**
- `SERVER_SimulateHeavySave(int32 Count, int32 Mode, bool bForceTyped)`  
  *Simulate Heavy Save.* — **PROTECTED**
- `SERVER_SimulateLogin(bool bSimulateAbandonedPawn, bool bDoSave)`  
  *Simulate Login.* — **PROTECTED**
- `SERVER_SlowMode(float DelaySeconds, float DurationSeconds, uint8 Flags_0)`  
  *Slow Mode.* — **PROTECTED**
- `SERVER_SpawnAI(class FName AIName, const struct FVector& Location)`  
  *Spawn AI.* — **PROTECTED**
- `SERVER_SpawnArganodus()`  
  *Spawn Arganodus.* — **PROTECTED**
- `SERVER_SpawnClone(const class FString& PlayerNameOrID)`  
  *Spawn Clone.* — **PROTECTED**
- `SERVER_SpawnEntity(const class FName& EntityName, float Size, const struct FVector& Location)`  
  *Spawn Entity.* — **PROTECTED**
- `SERVER_SpawnFire()`  
  *Spawn Fire.* — **PROTECTED**
- `SERVER_SpawnFoliage(class FName FoliageName, bool bDestroyOnEaten, float Scale, float DespawnIn)`  
  *Spawn Foliage.* — **PROTECTED**
- `SERVER_SpawnSavedPawnFromUid(int64 UID)`  
  *Spawn Saved Pawn From Uid.* — **PROTECTED**
- `SERVER_SpawnTrinket(int32 TrialCategory, EDinoType DinoType)`  
  *Spawn Trinket.* — **PROTECTED**
- `SERVER_SpawnWorldEventAt(float X, float Y, float Z, float StartDelayMinutes, float DurationMinutes, float Radius, uint8 EventCode1, uint8 EventCode2, const class FString& EventName)`  
  *Spawn World Event At.* — **PROTECTED**
- `SERVER_TeleportPToP(const class FString& PlayerNameRecipient, const class FString& PlayerNameDestination)`  
  *Teleport PTo P.* — **PROTECTED**
- `SERVER_TogglePlayerNameTags(bool bShowTags)`  
  *Toggle Player Name Tags.* — **PROTECTED**
- `SERVER_UnBanPlayer(const class FString& SteamID, bool bForceWebhookPrint)`  
  *Ban a player (admin).* — **PROTECTED**
- `SERVER_UndirtyPlayer(const class FString& PlayerNameOrID)`  
  *Undirty Player.* — **PROTECTED**
- `SERVER_UnlockAchievement(int32 ID)`  
  *Unlock Achievement.* — **PROTECTED**
- `SERVER_UnMutePlayer(const class FString& NameOrCIDOrSteamID, bool bForceWebhookPrint)`  
  *Un Mute Player.* — **PROTECTED**
- `SERVER_VerifyClass(EDinoType InType)`  
  *Client validity probe.* — **PROTECTED**
### CLIENT_

- `CLIENT_BoughtOryBurrowItem(int32 ItemId)`  
  *Confirm Ory burrow item purchase.* — **UNPROTECTED**
- `CLIENT_CharIsValid()`  
  *Client validity probe.* — **UNPROTECTED**
- `CLIENT_DisplayCharactersByID(const class FString& ToPrint)`  
  *Print player's characters list.* — **UNPROTECTED**
- `CLIENT_DisplayCharactersByID64(const class FString& ToPrint)`  
  *Print player's characters list.* — **UNPROTECTED**
- `CLIENT_GetRetCodeForEntityRestore(uint8 RetCode)`  
  *Return entity-restore status.* — **UNPROTECTED**
- `CLIENT_ListTalentPointsFor(const TArray<struct FTalentLevel>& Talents)`  
  *Send talent point list.* — **UNPROTECTED**
- `CLIENT_OnBackupFinished()`  
  *Admin response callback.* — **PROTECTED**
- `CLIENT_OnPlayerGrowthLevelAddedTo(bool bSuccess, const class FString& DisplayMessage)`  
  *Admin response callback.* — **UNPROTECTED**
- `CLIENT_OnPlayerGrowthLevelSet(bool bSuccess, const class FString& DisplayMessage)`  
  *Admin response callback.* — **UNPROTECTED**
- `CLIENT_OnPlayerInheritAdded(bool bSuccess, const class FString& DisplayMessage)`  
  *Admin response callback.* — **PROTECTED**
- `CLIENT_OnPlayerPunished(const class FString& PName, EPunishmentType Punishment, bool bSuccess)`  
  *Admin response callback.* — **PROTECTED**
- `CLIENT_OnPlayerTalentSet(bool bSuccess, const class FString& DisplayMessage)`  
  *Admin response callback.* — **UNPROTECTED**
- `CLIENT_OnResetGrowth(int32 rc)`  
  *Admin response callback.* — **PROTECTED**
- `CLIENT_OnUndirtyPlayer(bool bSuccess, const class FString& DisplayMessage)`  
  *Admin response callback.* — **PROTECTED**
- `CLIENT_PlayerCanDo()`  
  *Permission check round-trip.* — **UNPROTECTED**
- `CLIENT_PlayerPunishmentExpired(EPunishmentType Punishment)`  
  *Notify client of punishment state.* — **PROTECTED**
- `CLIENT_PlayerReceivedPunishment(EPunishmentType Punishment)`  
  *Notify client of punishment state.* — **PROTECTED**
- `CLIENT_PlayerUnbanned(bool bSuccess)`  
  *Notify client of punishment state.* — **PROTECTED**
- `CLIENT_PlayerUnMuted(bool bSuccess)`  
  *Notify client of punishment state.* — **PROTECTED**
- `CLIENT_PrintFoliageRAM(const class FString& ToPrint)`  
  *Admin diagnostic print.* — **UNPROTECTED**
- `CLIENT_PrintOnlinePlayersMap(const class FString& StringToPrint)`  
  *Admin diagnostic print.* — **UNPROTECTED**
- `CLIENT_PrintSicknessState(const class FString& ServerPrint)`  
  *Admin diagnostic print.* — **UNPROTECTED**
- `CLIENT_PurchaseStoreItemComplete(class FName ItemRowName, bool bSuccess, uint8 MessageIndex)`  
  *Cosmetic store purchase.* — **UNPROTECTED**
- `CLIENT_ReceivedPunishedPlayers(const TArray<struct FPunishedAccount>& result, bool bIsLastPacket)`  
  *Send list of punished accounts to admin.* — **PROTECTED**
- `CLIENT_SequencerExecuteThis(const TArray<uint8>& Data)`  
  *Run cinematic sequencer payload.* — **PROTECTED**
- `CLIENT_SetModeratorLoggedIn(bool bInLoggedIn)`  
  *Toggle moderator-online state.* — **PROTECTED**
- `CLIENT_TellPlayerCreatureCannotSave(class FName CreatureName)`  
  *Notify client a creature cannot save.* — **UNPROTECTED**
- `CLIENT_VerifyClass()`  
  *Client validity probe.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_EatAllFoliageAt(float X, float Y, float Z, float Radius, bool bKnockDown, bool bWasEaten)`  
  *Eat foliage in radius (multicast for replication).* — **UNPROTECTED**

## `AProfiledPlayer` *(: APlayerState)* — 1 RPC

### CLIENT_

- `CLIENT_UpdateUserProfile(const struct FPlayerProfile& ProfileIn)`  
  *Update User Profile.* — **UNPROTECTED**

## `AScentControllerBase` *(: AActor)* — 2 RPCs

### SERVER_

- `SERVER_GetScentData()`  
  *Get Scent Data.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_GotScentData(const TArray<struct FScentDataForCreature>& ScentData)`  
  *Got Scent Data.* — **UNPROTECTED**

## `ATunnelNetwork` *(: AActor)* — 16 RPCs

### SERVER_

- `SERVER_AddDirtClumpsAt(const struct FVector& Location, int32 NumClumpsToAdd, bool bSendMulticastAndEmptyArray)`  
  *Add Dirt Clumps At.* — **UNPROTECTED**
- `SERVER_CreateBigVoidAt(const struct FVector& Location, int32 SideLengthX, int32 SideLengthY, int32 SideLengthZ)`  
  *Create Big Void At.* — **UNPROTECTED**
- `SERVER_CreateNodeMesh(const struct FVector& Location, int16 DecorativeMeshIndex, bool bSendMulticastAndEmptyArray, bool bUpdateAdjacentNodes)`  
  *Create Node Mesh.* — **UNPROTECTED**
- `SERVER_HandleUseInput(class ABaseCharacter* CharacterUsing, int32 MeshHitID, bool bHitExitMesh)`  
  *Handle Use Input.* — **UNPROTECTED**
- `SERVER_RemoveAllDirtPiles()`  
  *Remove All Dirt Piles.* — **UNPROTECTED**
- `SERVER_RemoveDecorativeNodeMesh(const struct FVector& Location)`  
  *Remove Decorative Node Mesh.* — **UNPROTECTED**
- `SERVER_RemoveNodeMesh(const struct FVector& Location, bool bSendMulticastAndEmptyArray, bool bUpdateAdjacentNodes)`  
  *Remove Node Mesh.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_BeginPlay_AskForTunnelNodeData()`  
  *Begin Play Ask For Tunnel Node Data.* — **UNPROTECTED**
- `CLIENT_BeginPlay_GotTunnelNodeDataFromServer(const TArray<struct FTunnelNode>& TunnelNodes, const TArray<struct FDirtClumpPile>& DirtClumps, const TArray<struct FEntrance>& TunnelEntrances, int32 NumExpectedPackets)`  
  *Begin Play Got Tunnel Node Data From Server.* — **UNPROTECTED**
- `CLIENT_BeginPlay_WaitForLoginProcess()`  
  *Begin Play Wait For Login Process.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_AddDirtClumpsAt(const TArray<struct FDirtClumpPile>& Piles)`  
  *Add Dirt Clumps At.* — **UNPROTECTED**
- `MULTICAST_BurrowUpdate(const struct FEntranceChangesToClient& EntranceUpdate)`  
  *Burrow Update.* — **UNPROTECTED**
- `MULTICAST_CreateNodeMeshes(const TArray<struct FTunnelNode>& Locations, bool bUpdateAdjacentNodes)`  
  *Create Node Meshes.* — **UNPROTECTED**
- `MULTICAST_HandleUseInput(int32 MeshHitID, bool bHitExitMesh, int64 ID)`  
  *Handle Use Input.* — **UNPROTECTED**
- `MULTICAST_RemoveAllDirtPiles()`  
  *Remove All Dirt Piles.* — **UNPROTECTED**
- `MULTICAST_RemoveNodeMeshes(const TArray<struct FTunnelNode>& Locations, bool bUpdateAdjacentNodes)`  
  *Remove Node Meshes.* — **UNPROTECTED**

## `AWorldEvent` *(: AActor)* — 1 RPC

### MULTICAST_

- `MULTICAST_PrintEventChatMessage(uint16 Minutes, uint8 bIsActive, uint8 bJustBegan, uint8 bJustEnded, EWorldEvent Event1, EWorldEvent Event2)`  
  *Print Event Chat Message.* — **UNPROTECTED**

## `SDK_ALIGN` — 1 RPC

### MULTICAST_

- `MULTICAST_PerformAction(uint8 ActionIndex)`  
  *Trigger actor action by index.* — **UNPROTECTED**

## `UBBPlayerManager` *(: UActorComponent)* — 53 RPCs

### SERVER_

- `SERVER_ClientGotPawn(uint32 UID)`  
  *Client Got Pawn.* — **UNPROTECTED**
- `SERVER_ClientHasTheirPawn()`  
  *Client Has Their Pawn.* — **UNPROTECTED**
- `SERVER_ClientRequestsEggData(EDinoType EggType)`  
  *Client Requests Egg Data.* — **UNPROTECTED**
- `SERVER_ClientRequestsGroupFinderData()`  
  *Client Requests Group Finder Data.* — **UNPROTECTED**
- `SERVER_ClientRequestsPlayerRegionData()`  
  *Client Requests Player Region Data.* — **UNPROTECTED**
- `SERVER_ClientRequestsPostGroupToFinder(const class FString& GroupTitle, bool bNotifyGroupMembersOfChange)`  
  *Client Requests Post Group To Finder.* — **UNPROTECTED**
- `SERVER_ClientRequestsQuickEggData()`  
  *Client Requests Quick Egg Data.* — **UNPROTECTED**
- `SERVER_ClientRequestsRemoveGroupFromFinder(int64 GroupID, bool bNotifyGroupMembersOfChange)`  
  *Client Requests Remove Group From Finder.* — **UNPROTECTED**
- `SERVER_ClientRequestsUsersData(bool bGetMinimalInfo, bool bOnlyGroupMembers, bool bOnlyForPlayer)`  
  *Client Requests Users Data.* — **UNPROTECTED**
- `SERVER_Cookie(const TArray<uint8>& Data)`  
  *Cookie.* — **UNPROTECTED**
- `SERVER_CreateTimer(float TimerDuration)`  
  *Create Timer.* — **UNPROTECTED**
- `SERVER_DeleteSavedEntityAtIndex(uint8 ID)`  
  *Delete Saved Entity At Index.* — **UNPROTECTED**
- `SERVER_GetDeadEntitiesFor(int64 PlayerId)`  
  *Get Dead Entities For.* — **UNPROTECTED**
- `SERVER_GetValidatedAdminStatus()`  
  *Get Validated Admin Status.* — **PROTECTED**
- `SERVER_HandleCommandOnEntity(int32 TargetEntity, const TArray<uint8>& CmdBuffer)`  
  *Handle Command On Entity.* — **UNPROTECTED**
- `SERVER_InitialPacketReceived()`  
  *Initial Packet Received.* — **UNPROTECTED**
- `SERVER_LookupSkin(EEntityType st, const class FName& SkinName)`  
  *Lookup Skin.* — **UNPROTECTED**
- `SERVER_OnPrepared()`  
  *On Prepared.* — **UNPROTECTED**
- `SERVER_PossessPlayablePawn(uint32 NetGUID, bool bOverrideLocation, const struct FVector& NewLocation, uint8 PossessFlags)`  
  *Possess Playable Pawn.* — **UNPROTECTED**
- `SERVER_PossessPlayablePawnByRef(class APawn* Entity, bool bOverrideLocation, const struct FVector& NewLocation, uint8 PossessFlags)`  
  *Possess Playable Pawn By Ref.* — **UNPROTECTED**
- `SERVER_ReceivedStreamedData(uint8 PacketId)`  
  *Received Streamed Data.* — **UNPROTECTED**
- `SERVER_ReceiveSteamAppInfo(int32 Cookie, const TArray<uint8>& Blob)`  
  *Receive Steam App Info.* — **UNPROTECTED**
- `SERVER_RemoveDeadEntity(int64 UID)`  
  *Remove Dead Entity.* — **UNPROTECTED**
- `SERVER_RenameSlot(uint8 ID, const class FName& NewName)`  
  *Rename Slot.* — **UNPROTECTED**
- `SERVER_RestartPlayer(EDestroyReason reason, bool bSaveEntity)`  
  *Restart Player.* — **UNPROTECTED**
- `SERVER_RestoreDeadEntity(int64 UID, int64 SpecificOwner)`  
  *Restore Dead Entity.* — **UNPROTECTED**
- `SERVER_SaveMe(uint8 Behaviour)`  
  *Save Me.* — **UNPROTECTED**
- `SERVER_SpawnPlayable(TSubclassOf<class APawn> Class_0, const struct FTransform& InSpawnTransform, uint8 SpawnBehaviour, ESpawnActorCollisionHandlingMethod SpawnHandling, const TArray<uint8>& StatsBytes, bool bPossessImmediately, uint8 SpawnModeFlags, uint8 Specialization)`  
  *Spawn Playable.* — **UNPROTECTED**
- `SERVER_SpawnSavedEntityFromIndex(uint8 ID, bool bPossess)`  
  *Spawn Saved Entity From Index.* — **UNPROTECTED**
- `SERVER_WipePlayer(int64 Target)`  
  *Wipe Player.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_CharacterDied(int64 PawnUID, uint8 reason)`  
  *Character Died.* — **UNPROTECTED**
- `CLIENT_Cookie(const TArray<uint8>& Data)`  
  *Cookie.* — **UNPROTECTED**
- `CLIENT_FinishedLoading()`  
  *Finished Loading.* — **UNPROTECTED**
- `CLIENT_FinishedPreLoadingTiles()`  
  *Finished Pre Loading Tiles.* — **UNPROTECTED**
- `CLIENT_GetInitialPacket(const struct FGenericPacket& Packet)`  
  *Get Initial Packet.* — **UNPROTECTED**
- `CLIENT_GetPrepared()`  
  *Get Prepared.* — **UNPROTECTED**
- `CLIENT_GetRemoteCmdResult(uint8 result)`  
  *Get Remote Cmd Result.* — **UNPROTECTED**
- `CLIENT_GotGroupFinderData(const struct FGroupData_ForGroupFinder& GroupFinderData)`  
  *Got Group Finder Data.* — **UNPROTECTED**
- `CLIENT_GoToMenu(uint8 MenuId)`  
  *Go To Menu.* — **UNPROTECTED**
- `CLIENT_GroupPostedToFinder(bool bSuccess)`  
  *Group Posted To Finder.* — **UNPROTECTED**
- `CLIENT_GroupRemovedFromFinder(bool bSuccess)`  
  *Group Removed From Finder.* — **UNPROTECTED**
- `CLIENT_HandleCommandOnEntity(int32 TargetEntity, const TArray<uint8>& CmdBuffer)`  
  *Handle Command On Entity.* — **UNPROTECTED**
- `CLIENT_OnSaved()`  
  *On Saved.* — **UNPROTECTED**
- `CLIENT_PlayableSpawned(uint32 UID)`  
  *Playable Spawned.* — **UNPROTECTED**
- `CLIENT_ReceiveDeadEntitiesFor(const struct FGenericPacket& Data)`  
  *Receive Dead Entities For.* — **UNPROTECTED**
- `CLIENT_ReceivedEggData(const TArray<struct FEggDataForUIToClient>& EggData)`  
  *Received Egg Data.* — **UNPROTECTED**
- `CLIENT_ReceivedPlayerRegionData(const TArray<class FName>& RegionNames, const TArray<int32>& PlayerCounts)`  
  *Received Player Region Data.* — **UNPROTECTED**
- `CLIENT_ReceivedQuickEggData(const TArray<EDinoType>& EggData)`  
  *Received Quick Egg Data.* — **UNPROTECTED**
- `CLIENT_ReceivedUsersData(const TArray<struct FPlayerProfile_ForAdminDisplay>& Data, int32 NumProfilesTotal)`  
  *Received Users Data.* — **UNPROTECTED**
- `CLIENT_ReceiveStreamedData(const struct FGenericPacket& Data)`  
  *Receive Streamed Data.* — **UNPROTECTED**
- `CLIENT_ReceiveValidatedAdminStatus(int32 IsAdmin)`  
  *Receive Validated Admin Status.* — **PROTECTED**
- `CLIENT_RequestSteamAppInfo(int32 Cookie)`  
  *Request Steam App Info.* — **UNPROTECTED**
- `CLIENT_TimerExpired()`  
  *Timer Expired.* — **UNPROTECTED**

## `UComp_AirJump` *(: UBBComponentBase)* — 2 RPCs

### SERVER_

- `SERVER_DoAirJump(const struct FVector& Direction)`  
  *Do Air Jump.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_DoAirJump(const struct FVector& Direction)`  
  *Do Air Jump.* — **UNPROTECTED**

## `UComp_AutoJump` *(: UBBComponentBase)* — 1 RPC

### SERVER_

- `SERVER_AutoJumpToPoints(const TArray<struct FAutoJumpWaypoint>& Points)`  
  *Auto Jump To Points.* — **UNPROTECTED**

## `UComp_BackKick` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_DoBackKick()`  
  *Trigger back-kick attack animation/logic.* — **PROTECTED**
### CLIENT_

- `CLIENT_DoBackKick()`  
  *Trigger back-kick attack animation/logic.* — **PROTECTED**
### MULTICAST_

- `MULTICAST_DoBackKick()`  
  *Trigger back-kick attack animation/logic.* — **PROTECTED**

## `UComp_Bite` *(: UBBComponentBase)* — 10 RPCs

### SERVER_

- `SERVER_ApplyTendonTearTo(class ACharacter* Char, float Time)`  
  *Apply Tendon Tear To.* — **UNPROTECTED**
- `SERVER_CancelBite()`  
  *Cancel Bite.* — **UNPROTECTED**
- `SERVER_DoBiteAttack()`  
  *Do Bite Attack.* — **UNPROTECTED**
- `SERVER_StartBite()`  
  *Start Bite.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_CancelBite()`  
  *Cancel Bite.* — **UNPROTECTED**
- `CLIENT_DoBiteAttack()`  
  *Do Bite Attack.* — **UNPROTECTED**
- `CLIENT_StartBite()`  
  *Start Bite.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_CancelBite()`  
  *Cancel Bite.* — **UNPROTECTED**
- `MULTICAST_DoBiteAttack()`  
  *Do Bite Attack.* — **UNPROTECTED**
- `MULTICAST_StartBite()`  
  *Start Bite.* — **UNPROTECTED**

## `UComp_BiteHeal` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_ActivateVoraciousBite()`  
  *Activate Voracious Bite.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_ActivateVoraciousBite()`  
  *Activate Voracious Bite.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_ActivateVoraciousBite(const TArray<class ACharacter*>& ActivatedCharacters)`  
  *Activate Voracious Bite.* — **UNPROTECTED**

## `UComp_Burrowing` *(: UBBComponentBase)* — 31 RPCs

### SERVER_

- `SERVER_BeginBurrowDig(const struct FVector& BurrowLocation, const struct FRotator& BurrowRotation, float BurrowSize)`  
  *Begin Burrow Dig.* — **UNPROTECTED**
- `SERVER_BeginEnterBurrow(const struct FVector& EntryLocation, const struct FVector& PortLocation, bool bEntering)`  
  *Begin Enter Burrow.* — **UNPROTECTED**
- `SERVER_BeginTunnelDig(const struct FVector& NextNodeLocation, bool bDiggingNode, const struct FVector& CharacterPlaceLocation, bool bSnapToPlaceLocation)`  
  *Begin Tunnel Dig.* — **UNPROTECTED**
- `SERVER_EndTunnelDig()`  
  *End Tunnel Dig.* — **UNPROTECTED**
- `SERVER_ExcavateObjectAtNode(const struct FVector& Location)`  
  *Excavate Object At Node.* — **UNPROTECTED**
- `SERVER_FailedToEnterBurrow()`  
  *Failed To Enter Burrow.* — **UNPROTECTED**
- `SERVER_ForceExitBurrow()`  
  *Force Exit Burrow.* — **LIKELY-PROTECTED**
- `SERVER_InterruptBurrowDig()`  
  *Interrupt Burrow Dig.* — **UNPROTECTED**
- `SERVER_InterruptTunnelDig()`  
  *Interrupt Tunnel Dig.* — **UNPROTECTED**
- `SERVER_PlaceExcavatedObjectAtNode(const struct FVector& Location)`  
  *Place Excavated Object At Node.* — **UNPROTECTED**
- `SERVER_RefreshNearbyTunnelDespawnTimers()`  
  *Refresh Nearby Tunnel Despawn Timers.* — **UNPROTECTED**
- `SERVER_SetDirt(int32 Value)`  
  *Set Dirt.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_BeginBurrowDig(const struct FVector& BurrowLocation, const struct FRotator& BurrowRotation, float BurrowSize)`  
  *Begin Burrow Dig.* — **UNPROTECTED**
- `CLIENT_BeginTunnelDig(bool bDiggingNode, const struct FVector& CharacterPlaceLocation, bool bSnapToPlaceLocation)`  
  *Begin Tunnel Dig.* — **UNPROTECTED**
- `CLIENT_EndTunnelDig()`  
  *End Tunnel Dig.* — **UNPROTECTED**
- `CLIENT_FailedToEnterBurrow()`  
  *Failed To Enter Burrow.* — **UNPROTECTED**
- `CLIENT_ForceExitBurrow()`  
  *Force Exit Burrow.* — **LIKELY-PROTECTED**
- `CLIENT_InterruptBurrowDig()`  
  *Interrupt Burrow Dig.* — **UNPROTECTED**
- `CLIENT_InterruptTunnelDig()`  
  *Interrupt Tunnel Dig.* — **UNPROTECTED**
- `CLIENT_OnExcavateObjectAtNode(int32 ObjectID, uint8 SuccessCode)`  
  *On Excavate Object At Node.* — **UNPROTECTED**
- `CLIENT_OnPlacedExcavatedObjectAtNode(int32 ObjectID, uint8 SuccessCode)`  
  *On Placed Excavated Object At Node.* — **UNPROTECTED**
- `CLIENT_ServerSuccessfullyEnteredBurrow(bool bEntered)`  
  *Server Successfully Entered Burrow.* — **UNPROTECTED**
- `CLIENT_UpdateHUD(float Fill)`  
  *Update HUD.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_BeginBurrowDig(const struct FVector& BurrowLocation, const struct FRotator& BurrowRotation, float BurrowSize)`  
  *Begin Burrow Dig.* — **UNPROTECTED**
- `MULTICAST_BeginEnterBurrow(const struct FVector& EntryLocation, const struct FVector& PortLocation, bool bEntering)`  
  *Begin Enter Burrow.* — **UNPROTECTED**
- `MULTICAST_BeginTunnelDig(bool bDiggingNode, const struct FVector& CharacterPlaceLocation, bool bSnapToPlaceLocation)`  
  *Begin Tunnel Dig.* — **UNPROTECTED**
- `MULTICAST_EndTunnelDig(const struct FVector& CharacterPlacementLocation, bool bPlaceCharacter)`  
  *End Tunnel Dig.* — **UNPROTECTED**
- `MULTICAST_InterruptBurrowDig()`  
  *Interrupt Burrow Dig.* — **UNPROTECTED**
- `MULTICAST_InterruptTunnelDig()`  
  *Interrupt Tunnel Dig.* — **UNPROTECTED**
- `MULTICAST_PlayerExcavatedObject()`  
  *Player Excavated Object.* — **UNPROTECTED**
- `MULTICAST_PlayerPlacedExcavatedObject()`  
  *Player Placed Excavated Object.* — **UNPROTECTED**

## `UComp_Charge` *(: UBBComponentBase)* — 8 RPCs

### SERVER_

- `SERVER_BeginCharge()`  
  *Begin Charge.* — **UNPROTECTED**
- `SERVER_ChargeHitSomething()`  
  *Charge Hit Something.* — **UNPROTECTED**
- `SERVER_ReleaseCharge()`  
  *Release Charge.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_BeginCharge()`  
  *Begin Charge.* — **UNPROTECTED**
- `CLIENT_ReleaseCharge()`  
  *Release Charge.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_BeginCharge()`  
  *Begin Charge.* — **UNPROTECTED**
- `MULTICAST_ChargeHitSomething()`  
  *Charge Hit Something.* — **UNPROTECTED**
- `MULTICAST_ReleaseCharge()`  
  *Release Charge.* — **UNPROTECTED**

## `UComp_Coil` *(: UBBComponentBase)* — 2 RPCs

### SERVER_

- `SERVER_SetCoiled(bool bCoiledIn)`  
  *Set Coiled.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_SetCoiled(bool bCoiledIn)`  
  *Set Coiled.* — **UNPROTECTED**

## `UComp_Counter` *(: UBBComponentBase)* — 4 RPCs

### SERVER_

- `SERVER_ActivateCounter()`  
  *Activate Counter.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_ActivateCounter()`  
  *Activate Counter.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_ActivateCounter()`  
  *Activate Counter.* — **UNPROTECTED**
- `MULTICAST_OnDamageReceived(class AActor* FromActor)`  
  *On Damage Received.* — **UNPROTECTED**

## `UComp_DamageAmbush` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_ActivateDamageAmbush(int32 Key)`  
  *Activate ambush damage state.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_ActivateDamageAmbush()`  
  *Activate ambush damage state.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_ActivateDamageAmbush()`  
  *Activate ambush damage state.* — **UNPROTECTED**

## `UComp_Dart` *(: UBBComponentBase)* — 8 RPCs

### SERVER_

- `SERVER_BeginDart()`  
  *Begin Dart.* — **UNPROTECTED**
- `SERVER_DartDone()`  
  *Dart Done.* — **UNPROTECTED**
- `SERVER_Struggle()`  
  *Struggle.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_BeginDart()`  
  *Begin Dart.* — **UNPROTECTED**
- `CLIENT_DartDone()`  
  *Dart Done.* — **UNPROTECTED**
- `CLIENT_Struggle()`  
  *Struggle.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_BeginDart()`  
  *Begin Dart.* — **UNPROTECTED**
- `MULTICAST_DartDone()`  
  *Dart Done.* — **UNPROTECTED**

## `UComp_DataTransfer` *(: UBBComponentBase)* — 4 RPCs

### SERVER_

- `SERVER_RequestDataFromTunnelNetwork(int32 UniqueActorID)`  
  *Burrow/tunnel network data sync.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_ReceiveDataFromTunnelNetwork(int32 UniqueActorID, const TArray<struct FTunnelNode>& TunnelNodes, const TArray<struct FDirtClumpPile>& DirtClumps, const TArray<struct FEntrance>& EntrancesToClient, int32 NumExpectedPackets)`  
  *Burrow/tunnel network data sync.* — **UNPROTECTED**
- `CLIENT_ReceiveNotificationNetworkIsEmpty(int32 UniqueActorID)`  
  *Burrow/tunnel network data sync.* — **UNPROTECTED**
- `CLIENT_RequestDataFromTunnelNetwork(int32 UniqueActorID)`  
  *Burrow/tunnel network data sync.* — **UNPROTECTED**

## `UComp_DevastatingBite` *(: UBBComponentBase)* — 9 RPCs

### SERVER_

- `SERVER_ActivateDevastatingBite()`  
  *Activate Devastating Bite.* — **UNPROTECTED**
- `SERVER_BiteHitFailure()`  
  *Bite Hit Failure.* — **UNPROTECTED**
- `SERVER_BiteHitSuccess()`  
  *Bite Hit Success.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_ActivateDevastatingBite()`  
  *Activate Devastating Bite.* — **UNPROTECTED**
- `CLIENT_BiteHitFailure()`  
  *Bite Hit Failure.* — **UNPROTECTED**
- `CLIENT_BiteHitSuccess()`  
  *Bite Hit Success.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_ActivateDevastatingBite()`  
  *Activate Devastating Bite.* — **UNPROTECTED**
- `MULTICAST_BiteHitFailure()`  
  *Bite Hit Failure.* — **UNPROTECTED**
- `MULTICAST_BiteHitSuccess()`  
  *Bite Hit Success.* — **UNPROTECTED**

## `UComp_Grab` *(: UBBComponentBase)* — 4 RPCs

### SERVER_

- `SERVER_CheckAndStartGrabbing(int32 TargetPlayer, const struct FVector& MouthRelativeLocation)`  
  *Mouth-grab another character (prey/player).* — **UNPROTECTED**
- `SERVER_ForceReleaseMe()`  
  *Mouth-grab another character (prey/player).* — **LIKELY-PROTECTED**
- `SERVER_SetGrabData(const struct FGrabData& Data)`  
  *Mouth-grab another character (prey/player).* — **UNPROTECTED**
### CLIENT_

- `CLIENT_SetGrabData(const struct FGrabData& Data)`  
  *Mouth-grab another character (prey/player).* — **UNPROTECTED**

## `UComp_Group` *(: UBBComponentBase)* — 11 RPCs

### SERVER_

- `SERVER_DebugPrintGroupState(float Duration)`  
  *Debug Print Group State.* — **UNPROTECTED**
- `SERVER_SetGroupID(int64 NewGroupID)`  
  *Set Group ID.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_DebugPrintGroupState(const class FString& DebugPrint, float Duration)`  
  *Debug Print Group State.* — **UNPROTECTED**
- `CLIENT_ForceToggleGroupTagsToShow(bool bShow)`  
  *Force Toggle Group Tags To Show.* — **LIKELY-PROTECTED**
- `CLIENT_InformPlayerGroupKick(ELeftGroupReason reason)`  
  *Inform Player Group Kick.* — **PROTECTED**
- `CLIENT_PlayerInGroupChanged(int64 GroupMemberID, EDinoType NewDinoType, int64 NewSteamID, const class FString& NewName)`  
  *Player In Group Changed.* — **UNPROTECTED**
- `CLIENT_PlayerJoinedMyGroup(int64 GroupMemberID, EDinoType GroupMemberDinoType, int64 SteamID, const class FString& Name_0)`  
  *Player Joined My Group.* — **UNPROTECTED**
- `CLIENT_PlayerLeftMyGroup(int64 GroupMemberID, EDinoType GroupMemberDinoType, int64 SteamID, const class FString& Name_0, ELeftGroupReason LeftGroupReason)`  
  *Player Left My Group.* — **UNPROTECTED**
- `CLIENT_SetGroupID(int64 NewGroupID)`  
  *Set Group ID.* — **UNPROTECTED**
- `CLIENT_SetGroupMembers(const TArray<int64>& NewGroupMemberIDs, const TArray<EDinoType>& NewGroupMemberDinoTypes, const TArray<int64>& SteamIDs, const TArray<class FString>& Names)`  
  *Set Group Members.* — **UNPROTECTED**
- `CLIENT_SetSoftComfortDrop(float Value)`  
  *Set Soft Comfort Drop.* — **UNPROTECTED**

## `UComp_JumpBack` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_DoJumpBack(const struct FVector& Direction)`  
  *Do Jump Back.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_DoJumpBack()`  
  *Do Jump Back.* — **UNPROTECTED**
- `MULTICAST_OnDamageReceived()`  
  *On Damage Received.* — **UNPROTECTED**

## `UComp_MapEdit` *(: UBBComponentBase)* — 7 RPCs

### SERVER_

- `SERVER_DeleteAllDynamicFoliage()`  
  *Delete All Dynamic Foliage.* — **UNPROTECTED**
- `SERVER_DeleteDynamicActorsNearLocation(const struct FVector& Location, float Radius, class FName Name_0)`  
  *Delete Dynamic Actors Near Location.* — **UNPROTECTED**
- `SERVER_EraseAt(const struct FVector& Location, float Radius, class FName Name_0)`  
  *Erase At.* — **UNPROTECTED**
- `SERVER_RemoveDynamicFoliageInstance(class FName Name_0, int32 index)`  
  *Remove Dynamic Foliage Instance.* — **UNPROTECTED**
- `SERVER_SpawnActorOfTypeAt(const struct FVector& Location, const struct FRotator& Rotation, const struct FVector& Scale, class FName ActorName, uint8 bSave)`  
  *Spawn Actor Of Type At.* — **UNPROTECTED**
- `SERVER_SpawnFoliageOfTypeAt(const struct FVector& Location, const struct FRotator& Rotation, float Scale, class FName FoliageType, uint8 bDestroyWhenEaten, uint8 bSave, uint8 bInedible)`  
  *Spawn Foliage Of Type At.* — **UNPROTECTED**
- `SERVER_SpawnMultipleFoliagesOfTypeAt(const TArray<struct FVector>& Locations, const TArray<struct FRotator>& Rotations, const TArray<float>& Scales, class FName FoliageType, uint8 bDestroyWhenEaten, uint8 bSave, uint8 bInedible)`  
  *Spawn Multiple Foliages Of Type At.* — **UNPROTECTED**

## `UComp_MultiTalentTree` *(: UBBComponentBase)* — 2 RPCs

### SERVER_

- `SERVER_PlayerRequestsChangeSpecialization(uint8 NewValue)`  
  *Player Requests Change Specialization.* — **UNPROTECTED**
- `SERVER_SetTreeIndex(uint8 TreeIndexIn)`  
  *Set Tree Index.* — **UNPROTECTED**

## `UComp_Pounce` *(: UBBComponentBase)* — 9 RPCs

### SERVER_

- `SERVER_CancelPounce(int32 Key)`  
  *Cancel Pounce.* — **UNPROTECTED**
- `SERVER_DoPounceLeap(int32 Key, const struct FVector& Direction, float DamageBoostMultiplierIn, float CoilAmountIn)`  
  *Do Pounce Leap.* — **UNPROTECTED**
- `SERVER_StartPounceCoil(int32 Key)`  
  *Start Pounce Coil.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_CancelPounce()`  
  *Cancel Pounce.* — **UNPROTECTED**
- `CLIENT_DoPounceLeap()`  
  *Do Pounce Leap.* — **UNPROTECTED**
- `CLIENT_StartPounceCoil()`  
  *Start Pounce Coil.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_CancelPounce()`  
  *Cancel Pounce.* — **UNPROTECTED**
- `MULTICAST_DoPounceLeap(const struct FVector& Direction, float CoilAmountIn)`  
  *Do Pounce Leap.* — **UNPROTECTED**
- `MULTICAST_StartPounceCoil()`  
  *Start Pounce Coil.* — **UNPROTECTED**

## `UComp_Regurgitate` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_StartRegurgitating()`  
  *Start Regurgitating.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_StartRegurgitating()`  
  *Start Regurgitating.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_StartRegurgitating()`  
  *Start Regurgitating.* — **UNPROTECTED**

## `UComp_Scent` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_GetScentData(float LifeTime)`  
  *Get Scent Data.* — **UNPROTECTED**
- `SERVER_GotCompassScentData(const TArray<struct FScentCompassNode>& CompassNodes)`  
  *Got Compass Scent Data.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_GotScentData(const TArray<struct FScentDataForCreature>& ScentData, const TArray<struct FScentDataForCarcass>& CarcassData, const TArray<struct FScentDataForBurrow>& BurrowData, const TArray<struct FScentCompassNode>& ScentNodeDataIn)`  
  *Got Scent Data.* — **UNPROTECTED**

## `UComp_ShedSkin` *(: UBBComponentBase)* — 9 RPCs

### SERVER_

- `SERVER_ShedSkinSuccess()`  
  *Shed Skin Success.* — **UNPROTECTED**
- `SERVER_StartSheddingSkin()`  
  *Start Shedding Skin.* — **UNPROTECTED**
- `SERVER_StopSheddingSkin()`  
  *Stop Shedding Skin.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_ShedSkinSuccess()`  
  *Shed Skin Success.* — **UNPROTECTED**
- `CLIENT_StartSheddingSkin()`  
  *Start Shedding Skin.* — **UNPROTECTED**
- `CLIENT_StopSheddingSkin()`  
  *Stop Shedding Skin.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_ShedSkinSuccess()`  
  *Shed Skin Success.* — **UNPROTECTED**
- `MULTICAST_StartSheddingSkin()`  
  *Start Shedding Skin.* — **UNPROTECTED**
- `MULTICAST_StopSheddingSkin()`  
  *Stop Shedding Skin.* — **UNPROTECTED**

## `UComp_SlashAttack` *(: UBBComponentBase)* — 2 RPCs

### SERVER_

- `SERVER_SetIsDoingSlashAttack(bool bValue)`  
  *Set Is Doing Slash Attack.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_SetIsDoingSlashAttack(bool bValue)`  
  *Set Is Doing Slash Attack.* — **UNPROTECTED**

## `UComp_Slither` *(: UBBComponentBase)* — 6 RPCs

### SERVER_

- `SERVER_ForcePlayerLocation(const struct FVector& Location)`  
  *Force Player Location.* — **LIKELY-PROTECTED**
- `SERVER_OnPlayerJumped()`  
  *On Player Jumped.* — **UNPROTECTED**
- `SERVER_UpdateSnakeSimTransform(const TArray<struct FVector>& BoneLocs)`  
  *Update Snake Sim Transform.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_OnPlayerReceivedKnockback()`  
  *Notify client of punishment state.* — **UNPROTECTED**
- `CLIENT_SetLocationFromServer(const struct FVector& Location)`  
  *Set Location From Server.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_OnPlayerJumped()`  
  *On Player Jumped.* — **UNPROTECTED**

## `UComp_Stats` *(: UBBComponentBase)* — 15 RPCs

### SERVER_

- `SERVER_BreathSound()`  
  *Breath Sound.* — **UNPROTECTED**
- `SERVER_CaughtCheating()`  
  *Caught Cheating.* — **PROTECTED**
- `SERVER_DrownSound()`  
  *Drown Sound.* — **UNPROTECTED**
- `SERVER_EndDuel(EDuelEndReason reason)`  
  *End Duel.* — **UNPROTECTED**
- `SERVER_SetCharacterBooleanStates(uint32 Value, uint32 ValuesToSet)`  
  *Set Character Boolean States.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_DuelOver(EDuelEndReason reason)`  
  *Duel Over.* — **UNPROTECTED**
- `CLIENT_SetAbility(float Value)`  
  *Set Ability.* — **UNPROTECTED**
- `CLIENT_SetComfort(float Value)`  
  *Set Comfort.* — **UNPROTECTED**
- `CLIENT_SetComfortBias(float Value)`  
  *Set Comfort Bias.* — **UNPROTECTED**
- `CLIENT_SetStamina(float Value)`  
  *Set Stamina.* — **UNPROTECTED**
- `CLIENT_SetTimeSinceLastBreath(float Value)`  
  *Set Time Since Last Breath.* — **UNPROTECTED**
- `CLIENT_SetVenomResource(float Value)`  
  *Set Venom Resource.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_AnnounceDuelResults(const class FString& VictorName, const class FString& DefeatName, EDuelEndReason reason, float VictorHealth)`  
  *Announce Duel Results.* — **UNPROTECTED**
- `MULTICAST_BreathSound()`  
  *Breath Sound.* — **UNPROTECTED**
- `MULTICAST_DrownSound()`  
  *Drown Sound.* — **UNPROTECTED**

## `UComp_StatShow` *(: UBBComponentBase)* — 5 RPCs

### SERVER_

- `SERVER_GetPlayerData(class ABaseCharacter* CharRef)`  
  *Get Player Data.* — **UNPROTECTED**
- `SERVER_PlayIdentificationAnimation()`  
  *Play Identification Animation.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_GotPlayerData(const struct FStatDataToClient& CharData)`  
  *Got Player Data.* — **UNPROTECTED**
- `CLIENT_PlayIdentificationAnimation()`  
  *Play Identification Animation.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_PlayIdentificationAnimation()`  
  *Play Identification Animation.* — **UNPROTECTED**

## `UComp_Stomp` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_BeginStomp()`  
  *Begin Stomp.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_BeginStomp()`  
  *Begin Stomp.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_BeginStomp()`  
  *Begin Stomp.* — **UNPROTECTED**

## `UComp_SurfaceClimbing` *(: UBBComponentBase)* — 15 RPCs

### SERVER_

- `SERVER_AddForce(const struct FVector& InForce)`  
  *Add Force.* — **LIKELY-PROTECTED**
- `SERVER_AdjustPlaneNormal(const struct FVector& NewNormal)`  
  *Adjust Plane Normal.* — **UNPROTECTED**
- `SERVER_BeginGrab(const struct FVector& Direction, float Distance)`  
  *Mouth-grab another character (prey/player).* — **UNPROTECTED**
- `SERVER_EndSurfaceClimbing(bool bFromUser, float Charge)`  
  *End Surface Climbing.* — **UNPROTECTED**
- `SERVER_GrabCompleted()`  
  *Mouth-grab another character (prey/player).* — **UNPROTECTED**
- `SERVER_SetClimbingActor(class AActor* ActorRef, class USceneComponent* ComponentRef, int32 Instance)`  
  *Set Climbing Actor.* — **UNPROTECTED**
- `SERVER_SetCollision(bool bEnabled)`  
  *Set Collision.* — **UNPROTECTED**
- `SERVER_SetHoldingJumpCharge(bool bValue)`  
  *Set Holding Jump Charge.* — **UNPROTECTED**
- `SERVER_SetLocation(const struct FVector& NewLocation)`  
  *Set Location.* — **UNPROTECTED**
- `SERVER_StopMovementNow()`  
  *Stop Movement Now.* — **UNPROTECTED**
- `SERVER_UpdateMovement(const struct FNetV2D& MovementDirections)`  
  *Update Movement.* — **UNPROTECTED**
- `SERVER_UpdateRotation(const struct FQuat& NewOrient)`  
  *Update Rotation.* — **UNPROTECTED**
- `SERVER_UpdateSpeed(float M_Speed)`  
  *Update Speed.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_EndSurfaceClimbing(bool bFromUser, float Charge)`  
  *End Surface Climbing.* — **UNPROTECTED**
- `CLIENT_GrabCompleted()`  
  *Mouth-grab another character (prey/player).* — **UNPROTECTED**

## `UComp_Talents` *(: UBBComponentBase)* — 1 RPC

### SERVER_

- `SERVER_BuildClientTalentData()`  
  *Build Client Talent Data.* — **UNPROTECTED**

## `UComp_VenomSpit` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_SpitVenom(class ACharacter* Target, class FName TargetEyeSocket)`  
  *Spit Venom.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_SpitVenom()`  
  *Spit Venom.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_SpitVenom(class ACharacter* Target, class FName TargetEyeSocket, float TalentMod)`  
  *Spit Venom.* — **UNPROTECTED**

## `UComp_WallJump` *(: UBBComponentBase)* — 1 RPC

### SERVER_

- `SERVER_HandleImpulse(const struct FVector& Vector)`  
  *Handle Impulse.* — **UNPROTECTED**

## `UComp_WaterLunge` *(: UBBComponentBase)* — 7 RPCs

### SERVER_

- `SERVER_BeginAimingLunge()`  
  *Begin Aiming Lunge.* — **UNPROTECTED**
- `SERVER_ImpactSound()`  
  *Impact Sound.* — **UNPROTECTED**
- `SERVER_LungeCompleted(const struct FVector& ActorLocation, const struct FRotator& ActorRotation)`  
  *Lunge Completed.* — **UNPROTECTED**
- `SERVER_ReleaseAimingLunge(const struct FVector& DirectionVect, float PowerVal, const struct FVector& ActorLocation, const struct FRotator& ActorRotation)`  
  *Release Aiming Lunge.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_BeginAimingLunge()`  
  *Begin Aiming Lunge.* — **UNPROTECTED**
- `CLIENT_LungeCompleted()`  
  *Lunge Completed.* — **UNPROTECTED**
- `CLIENT_ReleaseAimingLunge()`  
  *Release Aiming Lunge.* — **UNPROTECTED**

## `UComp_WaterSpit` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_SpitWater(const TArray<class ACharacter*>& Targets)`  
  *Spit Water.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_SpitWater()`  
  *Spit Water.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_SpitWater(const TArray<class ACharacter*>& Targets, float TalentMod)`  
  *Spit Water.* — **UNPROTECTED**

## `UComp_WingBeat` *(: UBBComponentBase)* — 5 RPCs

### SERVER_

- `SERVER_ApplyWingBeatDR(class ABaseCharacter* HitChar)`  
  *Apply Wing Beat DR.* — **UNPROTECTED**
- `SERVER_DoWingBeat(uint8 index)`  
  *Do Wing Beat.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_TryDoWingBeat(uint8 index)`  
  *Try Do Wing Beat.* — **UNPROTECTED**
### MULTICAST_

- `MULTICAST_ApplyWingBeatDR(class ABaseCharacter* HitChar)`  
  *Apply Wing Beat DR.* — **UNPROTECTED**
- `MULTICAST_DoWingBeat(uint8 index)`  
  *Do Wing Beat.* — **UNPROTECTED**

## `UDinoSkinManager` *(: UBBComponentBase)* — 5 RPCs

### SERVER_

- `SERVER_AddWearable(int32 WearableId)`  
  *Add Wearable.* — **UNPROTECTED**
- `SERVER_NotifySmeltNewSkin(class ACharacter* C)`  
  *Notify Smelt New Skin.* — **UNPROTECTED**
- `SERVER_RemoveWearable(int32 WearableId)`  
  *Remove Wearable.* — **UNPROTECTED**
- `SERVER_SetPlayerSkin(const struct FNetGeneratedSkin& NewSkin)`  
  *Set Player Skin.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_SpawnDisplayStuff(const TArray<int32>& GeneratorFrameIds, const TArray<struct FVector>& Orig)`  
  *Spawn Display Stuff.* — **UNPROTECTED**

## `UGameReporter` *(: UActorComponent)* — 2 RPCs

### SERVER_

- `SERVER_ReceivePlayerReport(const struct FPlayerReport& Report)`  
  *Receive Player Report.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_AddRecentKillerLocally(const struct FKillerDetails& NewKiller)`  
  *Add Recent Killer Locally.* — **UNPROTECTED**

## `UPlayerRequestHandler` *(: UActorComponent)* — 10 RPCs

### SERVER_

- `SERVER_AskAcceptsPlayer(const struct FRequestPayload& Request)`  
  *Ask Accepts Player.* — **UNPROTECTED**
- `SERVER_CancelAllRequests()`  
  *Cancel All Requests.* — **UNPROTECTED**
- `SERVER_CancelRequest(const struct FRequestID& Request)`  
  *Cancel Request.* — **UNPROTECTED**
- `SERVER_ClientAcceptsPlayer(const struct FRequestPayload& Request, int32 rc)`  
  *Client Accepts Player.* — **UNPROTECTED**
- `SERVER_Notify_ClientProcessingRequest(const struct FRequestID& RequestID)`  
  *Notify Client Processing Request.* — **UNPROTECTED**
- `SERVER_ReceiveClientConfirm(const struct FRequestID& RequestID, int32 ResponseValue)`  
  *Receive Client Confirm.* — **UNPROTECTED**
- `SERVER_RequestTo(const int64 PlayerToAsk, const struct FRequestPayload& Request)`  
  *Request To.* — **UNPROTECTED**
### CLIENT_

- `CLIENT_AcceptsPlayer(const struct FRequestPayload& Request)`  
  *Accepts Player.* — **UNPROTECTED**
- `CLIENT_ReceiveServerSecondCheck(int32 Code)`  
  *Receive Server Second Check.* — **UNPROTECTED**
- `CLIENT_ReceiveTargetResponse(const struct FRequestPayload& Request, int32 rc)`  
  *Receive Target Response.* — **UNPROTECTED**

## `UTrackingComponent` *(: UBBComponentBase)* — 3 RPCs

### SERVER_

- `SERVER_CheckClient()`  
  *Check Client.* — **UNPROTECTED**
- `SERVER_PlacePlayerElsewhere(const struct FVector& NewLocation)`  
  *Place Player Elsewhere.* — **UNPROTECTED**
- `SERVER_ReceiveClientDelta()`  
  *Receive Client Delta.* — **UNPROTECTED**

