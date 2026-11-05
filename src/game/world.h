// Game subsystems driven by the mission scripts (teams, actors, effects, sound,
// cameras, checkpoints, interface). None of these is hand-ported yet: the
// declarations name the original functions (addresses in the comments; their
// mechanical translation is in src/tier1) so that ported code can call them.
// Members that the original accesses inline are declared at their offsets.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/air_attack.h"
#include "game/animation_actor.h"
#include "game/barrel.h"
#include "game/bike.h"
#include "game/camera_module.h"
#include "game/car.h"
#include "game/checkpoints.h"
#include "game/clock.h"
#include "game/debris.h"
#include "game/document_place.h"
#include "game/door.h"
#include "game/explosive_place.h"
#include "game/fish.h"
#include "game/fowl_generator.h"
#include "game/item_actor.h"
#include "game/junkers88.h"
#include "game/ladder.h"
#include "game/melee_tracker.h"
#include "game/melee_tracker_ai.h"
#include "game/mine.h"
#include "game/mp_zone.h"
#include "game/null_actor.h"
#include "game/photo_place.h"
#include "game/pike.h"
#include "game/radio.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/resource_cache.h"
#include "game/rigid_body.h"
#include "game/sound_module.h"
#include "game/spawn_point.h"
#include "game/static_weapon.h"
#include "game/supply_box.h"
#include "game/submarine.h"
#include "game/train.h"
#include "game/switch_actor.h"
#include "game/team.h"
#include "game/ts_explosion.h"
#include "game/vehicle.h"
#include "game/wire_cut.h"
#include "ls3d/i3d.h"

class C_BitStream;
class C_Item;

// ---------------------------------------------------------------------------
// Actor classes (members used by scripts)

// Humans: see game/human.h (controllers: game/human_controller.h, body: game/human_body.h).

// C_Bike ("Bike", vtable 0x00812440, 0x1d0 bytes) is defined in game/bike.h and C_Fish ("Fish", vtable
// 0x00812538, 0xe0 bytes) in game/fish.h, included above.

// C_MeleeTrackerAI (C_MeleeTracker +0x534, the meleeTracker's AI controller) is in game/melee_tracker_ai.h.

// C_SpawnPoint ("SpawnPoint", vtable 0x008138e0, 0xb0 bytes) is defined in game/spawn_point.h, included above.

// C_Car (game/car.h) with its vehicle C_Vehicle (C_Car +0x90, game/vehicle.h).

// C_Door ("Door", vtable 0x00812ff8, 0x270 bytes) is defined in game/door.h, included above.

// Actors with a script-visible state (SetActorState / _ACTOR_GetState).
// C_Radio ("Radio", vtable 0x00813310, 0x150 bytes) is defined in game/radio.h, included above.
// C_SupplyBox ("SupplyBox", vtable 0x00812d10, 0x260 bytes) is defined in game/supply_box.h, included above.
// C_Switch ("Switch", vtable 0x008139d0, 0x150 bytes) is defined in game/switch_actor.h and C_PhotoPlace
// ("Photo place", vtable 0x00813400, 0x140 bytes) in game/photo_place.h, included above.
// C_StaticWeapon ("StaticWeapon", vtable 0x00813130, 0x300 bytes) is defined in game/static_weapon.h, included
// above.
// C_ExplosivePlace ("Explosive place", vtable 0x008134f0, 0x180 bytes) is defined in game/explosive_place.h,
// included above.
// C_DocumentPlace ("Document place", vtable 0x008135e0, 0x150 bytes) is defined in game/document_place.h, included
// above.
// C_Submarine ("Submarine", vtable 0x00812650, 0x120 bytes) and GetSubmarines() are in game/submarine.h, included
// above.
// C_Pike ("Pike", vtable 0x00811c90, 0x210 bytes) is defined in game/pike.h, included above.
// C_Junkers88 ("Junkers88", vtable 0x00812c10, 0x330 bytes) is defined in game/junkers88.h, included above.
// C_TSExplosion ("Explosion [TS]", vtable 0x008136d0, 0xd0 bytes) is defined in game/ts_explosion.h, included above.

// C_Train ("Train", vtable 0x00811e78, 0x8250 bytes) is defined in game/train.h, included above.

// C_FowlGenerator ("FowlGenerator", vtable 0x00811d80, 0xd0 bytes) is defined in game/fowl_generator.h,
// included above.

// C_AnimationActor ("Animation", vtable 0x008119b8, 0xf0 bytes) is defined in game/animation_actor.h, included above.

// C_WireCut ("WireCut", vtable 0x00812750, 0xb0 bytes) is defined in game/wire_cut.h, included above.

// C_AirAttack ("AirAttack", vtable 0x00812930, 0x1f0 bytes) is defined in game/air_attack.h, included above.

// C_ItemActor ("Item", vtable 0x00811aa8, 0x6c0 bytes) is defined in game/item_actor.h, included above.

// C_MPZone ("MPZone", vtable 0x00812b20, 0x120 bytes) is defined in game/mp_zone.h, included above.
// C_Mine ("Mine", vtable 0x00812f08, 0x120 bytes) is defined in game/mine.h, included above.

// C_MeleeTracker ("MeleeTracker", vtable 0x008120e0, 0x5b0 bytes) is defined in game/melee_tracker.h, included above.

// The rigid body (C_RigidBody) and its parameters: see game/rigid_body.h, included above.

// C_Debris ("Debris", vtable 0x00812e18, 0x6a0 bytes) is defined in game/debris.h, included above.

// ---------------------------------------------------------------------------
// Modules


// Teams, formations, the team manager: see game/team.h.

// Particles module: see game/particles.h.

// Sound module: see game/sound_module.h.

enum E_FadeDirection {
    FADE_IN = 0,
    FADE_OUT = 1,
};

// (the module object itself: game/geffects.h)
class C_GEffectsModule;

// The camera module (C_CameraModule, C_CameraHumanEffects, C_CameraFrameKeeper):
// game/camera_module.h.

// The texture shifting module: see game/tshift.h; the lightmap groups: game/lightmap_groups.h.

// The decorators (C_Decorators, g_decorators): game/decorators.h.

// The Junkers 88 setup (SetupJunkers88) is the Junkers itself: see game/junkers88.h.

// Checkpoints (AI navigation graph): see game/checkpoints.h.

// ---------------------------------------------------------------------------
// Interface, profile, mission state

// The game interface (the HUD): see game/interface.h.
class C_Interface;

// The player's profile, diary and debriefing: see game/profile.h.

void SetObjectiveStatus(int objective, int status);    // 0x006b6830
// Enables a multiplayer spawn zone for a side (0 axis, 1 allied, 2 neutral).
bool EnableSpawnZone(const char *zoneName, int side);   // 0x0069f320

// Values kept across missions (SaveGameValue / _LoadGameValue), stored in the
// script module (C_ScriptModule::GetSaveGameValues(), address 0x00868b3c).
constexpr int SAVE_GAME_VALUES = 256;

// An explosion with area damage (MakeExplosion).
struct S_Explosion {                                    // 0x20 bytes
    S_vector pos = {0, 0, 0};
    float posW = 0.0f;
    float range = 10.0f;                                // +0x10 m
    float damage = 1.0f;                                // +0x14
    bool unknown18 = false;
    C_Actor *source = nullptr;                          // +0x1C who caused it

    void Execute();                                     // 0x006df540
};

// Engine-side hooks on the "a" child of animated models.
void AttachModelUpdateHook(I3D_frame *model);           // 0x007764d0
void DetachModelUpdateHook(I3D_frame *model);           // 0x00776520
// The speech slots (0x008cb8e0, 16 x 0x44 bytes): each plays one dubbing sample at a speaker
// and moves the speaker's mouth from the dubbing's lip-sync data (tables\dabing\<n>.dat, one
// frame per 20 ms) on the channels of the model's 'SMMR' morph visual (the "a" child, as in
// C_HumanMorph). States: 0 free, 1 attached, 2 playing, 3 closing the mouth (200 ms), 4 a delay
// before free, 5 plays like 2 but its sound is never released here. Preceded by a list of morph
// fades (0x008cb8d4, std::vector of 16-byte entries) that the update also runs.
// Neutral mouths, every slot freed and emptied (cutscene start / end).
void ResetSpeechSlots();                                // 0x0071e680
// The mission's start / end with the table (client side): emptied without freeing, and freed.
void ClearSpeechSlots();                                // 0x0071e1e0
void FreeSpeechSlots();                                 // 0x0071e230
// Frame updates of the slots (the game's Update): playback, lip sync, mouth closing, the fades.
void UpdateSpeechSlots(int timeDelta);                  // 0x0071f1d0
// Updates the two extra frames of each slot (the C_Human +0x2B8 / +0x2BC frames; never set here).
void RefreshSpeechSlotFrames();                         // 0x0071f100
// Pausing stops the playing samples and keeps their positions; resuming restarts slots in state 2.
void PauseSpeechSlots(bool paused);                     // 0x0071fa10
int FindFreeSpeechSlot();                               // 0x0071e2c0 (15 when all are in use)
// Ends the speech playing at `owner` (state 3: the mouth closes).
void StopFrameSpeech(I3D_frame *owner);                 // 0x0071e830
// Binds slot `slot` to a speaker (its frame and morph visual; the mouth neutral).
void AttachSpeechSlot(int slot, C_Human *owner);        // 0x0071e300
// A slot plays speech (state 2) at the human's frame.
bool IsHumanSpeaking(const C_Human *human);             // 0x0071f090
// Neutral mouth, frees and empties the slots of the human's frame (before a uniform change
// replaces the model and its morph visual).
void ReleaseHumanSpeech(const C_Human *human);          // 0x0071ea20
void AttachSpeechSlot(int slot, I3D_frame *frame);      // 0x0071e860
// Plays dubbing number `dubbing` in slot `slot`: loads tables\dabing\<dubbing>.dat (the lip-sync
// data) into the slot and the <dubbing>.wav sample (0x0071ebc0) into its sound frame (ranges
// nearRange..farRange; I3DSOUND_AMBIENT for the active player, else I3DSOUND_POINT);
// `showSubtitle` also passes the dubbing to the quick messages (0x0048f180, with the sample's length).
void PlaySpeechSlot(int slot, int dubbing, float nearRange, float farRange, bool showSubtitle,
                    bool isActivePlayer);               // 0x0071ec30 __fastcall

// Model / animation / sound caches: see game/resource_cache.h.
