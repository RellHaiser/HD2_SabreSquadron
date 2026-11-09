// A supply box (C_SupplyBox, actor type 9, vtable 0x00812d10, 0x260 bytes; original
// 0x005f9700..0x005fd9f0): a crate that hands out items (its lists for either side, else ammunition
// for the weapon in the taker's hands) a limited number of times, its "Ammo00".."Ammo13" parts
// showing how full it is. Empty, it closes, hides and (with a respawn time) comes back by
// parachute: a plane is heard flying over, the crate falls in the model "cratedummy" (its frame
// "letibedna" swinging on the animation "cratefly") under the model "parachute" until it lands
// (crushing what it lands on), the parachute fades and the crate opens. The model's own
// animations: "crateopen", "crateclosed", "cratedrop". Its box (COLLISION_TYPE_PIKE, a separate
// allocation) is in the world while the crate is. Network games: the server sends the whole
// state.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "ls3d/math.h"

class C_BitStream;
class C_chunk;
class I3D_animation_set;
class I3D_frame;
class I3D_model;
struct S_CollisionVolume;
struct S_VertexAnimPlayer;

class alignas(16) C_SupplyBox : public C_Actor {        // "SupplyBox", vtable 0x00812d10
public:
    C_SupplyBox();                                      // 0x005f97d0
    ~C_SupplyBox() override = default;                  // 0x005f9880 (scalar deleting 0x005f9860)

    // Off: hidden at once (and not updated). On: hidden, back by parachute in 3 s; the server of a
    // network game sends the state (message kind 0).
    void SetEnabled(bool on);                           // 0x005fa290
    // Items left to take.
    int GetCharges() const { return m_charges; }        // +0x130
    // Arriving, empty or going (only ready boxes hand out items; +0x250 != 0).
    bool IsBusy() const { return m_state != SUPPLY_READY; }
    // In the world (flag 1 of +0x124).
    bool IsPlaced() const { return (m_flags & SUPPLY_SHOWN) != 0; }
    // The box hands out the items of its lists; without lists, ammunition for the weapon in the
    // taker's hands.
    bool HasItemLists() const { return m_hasItemLists != 0; }   // +0x11C
    // Item types for the axis / allied side (vectors at +0xFC / +0x10C).
    const int *GetItemList(bool allies) const { return (allies ? m_alliedItems : m_axisItems).data(); }
    int GetItemCount(bool allies) const { return static_cast<int>((allies ? m_alliedItems : m_axisItems).size()); }
    // (+0x128: the signals offer an item to a member who can take no more of it only while he
    // carries fewer than this many.)
    int GetMaxCarried() const { return m_maxCarried; }
    // One item was taken from a ready, placed box: one charge less (the last one closes it) and the
    // parts show it (inline in the original's takers: 0x005fcd80, 0x005fd760).
    void UseCharge();

    // The drop's timers, the plane, the fall (not while disabled).
    void Update(int frameTime) override;                // 0x14 0x005fb380
    // Update, but on a pure client.
    void Tick(int timeDelta) override;                  // 0x18 0x005fbd20
    // Kind 1: destroyed (as damage of -1); kind 0: the whole state.
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x005fa3f0
    void Unknown20(C_BitStream *) override {}           // 0x20 0x005f9840
    // The drop point and rotation (the frame's world), the lists emptied, the defaults; the parts
    // found, the feet (up to four vertices of the walls "Steny02" below half the box's height,
    // ordered round the box). False without the walls.
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005fa6a0
    // A version byte (1), the fields raw, ten items of each list (-1 past the end), the text.
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x005fc050
    // The lists are added to (SetFrame empties them).
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005fc200
    // The lists, the item flag and the counts (not the shown flags or the text).
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x005fbec0
    // Once until StopMission: the models ("cratedummy", "parachute"), the plane's dummy, the
    // animations, the box (InitBox), the name. Quirk kept: the text id is set to 0x1BFA first (the
    // property's is never used).
    void StartMission(bool server) override;            // 0x3C 0x005fac80
    void StopMission(bool server) override;             // 0x40 0x005fb220
    void RestartMission(bool server) override;          // 0x44 0x005fc480
    // The base's chunk, then 0x202 the state (0x60 bytes).
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005fc530
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005fc640
    // A joining client's: the state (without the kind bit).
    void WriteState(C_BitStream *msg) override;         // 0x50 0x005fd560
    void ReadState(C_BitStream *msg) override;          // 0x54 0x005fd640
    // (Hits do nothing.)
    void Hit(E_HitType, float, const S_vector &, const S_vector &, C_Actor *, int) override {}   // 0xD4 0x005fbd80
    int GetMaterialId(I3D_frame *) override { return 25; }   // 0xD8 0x005f9850

    // The drop's states (+0x250).
    enum : int {
        SUPPLY_READY = 0,
        SUPPLY_CLOSING = 1,                             // emptied
        SUPPLY_HIDDEN = 2,                              // waiting for the drop (respawn time)
        SUPPLY_FALLING = 3,
        SUPPLY_LANDED = 4,
        SUPPLY_PARACHUTE_FADING = 5,
        SUPPLY_OPENING = 6,
        SUPPLY_DESTROYED = 7,
    };
    // Flags (+0x124, SetVisibility's low three bits).
    enum : uint32_t {
        SUPPLY_SHOWN = 0x1,
        SUPPLY_DISABLED = 0x2,                          // not updated
        SUPPLY_FLAG_4 = 0x4,                            // (set while hidden for the drop)
        SUPPLY_MOVE_TO_DROP = 0x8,                      // SetVisibility: back to the drop point
        SUPPLY_AT_ONCE = 0x10,                          // SetVisibility: no fade
    };

private:
    // The state for saved games and the network (0x60 bytes in the original).
    struct S_State {
        uint32_t flags = 0;                             // +0x00 (+0x124 with its padding)
        int charges = 0;                                // +0x04
        int health = 0;                                 // +0x08
        bool planeFlying = false;                       // +0x0C
        int blendTime = 0;                              // +0x10
        int animStage = 0;                              // +0x14
        int anim = 0;                                   // +0x18
        int state = 0;                                  // +0x1C
        int stateTime = 0;                              // +0x20
        uint32_t unknown24[3] = {};                     // (stack leftovers in the original: 0)
        S_vector4 pos = {};                             // +0x30 falling: the cratedummy's, else the frame's
        S_vector4 crate = {};                           // +0x40 falling: the frame's, else 0
        int animTime = 0;                               // +0x50 the model's time on its stage
        uint32_t unknown54[3] = {};
    };
    static_assert(sizeof(S_State) == 0x60, "S_State layout");

    // The defaults the drop restarts from: the health, a full box, the walls and ammunition parts,
    // ready, the text 0x1BFA.
    void Reset();                                       // 0x005f98e0
    // The parachute's morph animated ("ammobox"), the box volume made, the parts, shown as the
    // property says (the volume added), opening if shown.
    void InitBox();                                     // 0x005f9a60 (DS 0x0061d8e0)
    // The crate 85 m over the drop point (in network games under the scene's top), the parachute
    // and the cratedummy (3 m higher) there, the crate and parachute under "letibedna", shown.
    void StartDrop();                                   // 0x005f9f00
    // The box volume follows the frame (its matrices without scale).
    void UpdateBoxVolume();                             // 0x005fa530
    // Damage (-1: all): destroyed at no health.
    void Damage(int amount);                            // 0x005fbd40
    // Shown (the volume added when it was hidden for the drop or disabled; faded in unless
    // SUPPLY_AT_ONCE; at the drop point with SUPPLY_MOVE_TO_DROP) or hidden (the parachute too; the
    // volume out when it was shown); the low three bits kept.
    void SetVisibility(uint32_t flags);                 // 0x005fbd90
    // The model's animation `anim` (1 open, 2 closed, 3 drop, 4 none; blended over 350 ms from the
    // other stage when one plays); 5: the pose reset.
    void SetAnimation(int anim);                        // 0x005fc860
    // The blend (whole steps of 350 ms), the model ticked.
    void TickAnimation(int time);                       // 0x005fc9a0
    // The state's timer (on to the next state); the plane starts 5 s before the drop.
    void TickState(int time);                           // 0x005fca60
    // To state `state` (its animation, sound, timer).
    void SetState(int state);                           // 0x005fcd80
    void GetState(S_State &state);                      // 0x005fd180
    void ApplyState(const S_State &state);              // 0x005fd2a0
    void WriteStateBits(C_BitStream *msg);              // (inline in SetEnabled / WriteState)
    void ReadStateBits(C_BitStream *msg, S_State &state);
    // The ammunition parts on as far as the box is full.
    void UpdateAmmoParts();                             // 0x005fd760
    // Plays the box's sound of group `group` (chapter 1) at the frame.
    void PlaySound(int group);
    // The cratedummy and parachute put away, the crate frame unlinked.
    void PutAwayDrop();

    bool m_started = false;                             // +0x90
    S_vector4 m_dropPos = {};                           // +0xA0 the frame's world position at SetFrame
    S_quat m_dropRot = {};                              // +0xB0
    I3D_frame *m_walls = nullptr;                       // +0xC0 "Steny02" (an object visual)
    I3D_frame *m_ammo[14] = {};                         // +0xC4
    std::vector<int> m_axisItems;                       // +0xFC
    std::vector<int> m_alliedItems;                     // +0x10C
    int m_hasItemLists = 0;                             // +0x11C
    uint32_t m_shownFlags = 0;                          // +0x120 property: SetVisibility's at the start
    uint32_t m_flags = 0;                               // +0x124
    int m_maxCarried = 5;                               // +0x128
    int m_capacity = 20;                                // +0x12C
    int m_charges = 0;                                  // +0x130
    int m_respawnTime = -1;                             // +0x134 s (none: not back)
    int m_health = 6000;                                // +0x138
    S_matrix m_boxMatrix = {};                          // +0x140 the frame's world without its scale
    S_matrix m_boxInvMatrix = {};                       // +0x180
    S_CollisionVolume *m_volume = nullptr;              // +0x1C0
    S_vector4 m_feet[4] = {};                           // +0x1D0 in the frame's space
    int m_textId = 0x1BFA;                              // +0x210
    S_VertexAnimPlayer *m_morphPlayer = nullptr;        // +0x214
    I3D_model *m_crateDummy = nullptr;                  // +0x218 "cratedummy"
    I3D_frame *m_flyingCrate = nullptr;                 // +0x21C its "letibedna"
    I3D_frame *m_plane = nullptr;                       // +0x220 the plane's sound (a dummy)
    bool m_planeFlying = false;                         // +0x224
    I3D_model *m_parachute = nullptr;                   // +0x228
    I3D_animation_set *m_anims[6] = {};                 // +0x22C cratefly, crateopen, crateclosed, cratedrop
    int m_blendTime = 0;                                // +0x244 ms
    int m_animStage = -1;                               // +0x248 (0 / 1)
    int m_anim = 5;                                     // +0x24C
    int m_state = SUPPLY_READY;                         // +0x250
    int m_stateTime = 0;                                // +0x254 ms (-1: none)
};

// The supply boxes' creator (g_actorCreators[ACTOR_SUPPLY_BOX], vtable 0x00812d00).
class C_SupplyBoxCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005f9730
    const char *GetName() override { return "SupplyBox"; }   // 0x005f97a0
    void Destroy(C_Actor *actor) override;              // 0x005f97b0
};
