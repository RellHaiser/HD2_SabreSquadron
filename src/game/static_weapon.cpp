// Static weapons (C_StaticWeapon; original 0x00602a10..0x00606250).
#include "game/static_weapon.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage, SendActorMessage
#include "game/checkpoints.h"
#include "game/game.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"                          // g_itemsModule, SaveActorItems
#include "game/pending.h"                               // g_netReplication, g_gameRole, g_texts
#include "game/profile.h"                               // g_gameProfile
#include "game/resource_cache.h"                        // g_modelCache
#include "game/shots.h"                                 // g_hitZones
#include "game/team.h"                                  // g_teamManager
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"                              // NotifyFrameHit

namespace {

bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }
bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

// The actor messages: the kind (2 bits: log(2) / log(2) + 1 in the original), then the store or
// the state (2 bits).
constexpr int MSG_BITS = 2;
constexpr uint32_t MSG_STORE = 0;
constexpr uint32_t MSG_STATE = 1;
constexpr int STATE_BITS = 2;

// The saved game's chunks.
constexpr uint16_t CHUNK_STATE = 1000;
constexpr uint16_t CHUNK_WEAPON = 0x3E9;
constexpr uint16_t CHUNK_STORE = 0x3EA;

constexpr uint32_t WEAPON_VOLUME_TYPE = COLLISION_TYPE_PLACE;
// Network games: the ammunition refills (ms; a launcher's twice as often).
constexpr uint32_t REFILL_INTERVAL = 30000;
constexpr uint32_t REFILL_INTERVAL_LAUNCHER = 15000;
constexpr float EXIT_BEHIND = 0.5f;                     // m (FindExit)
constexpr float EXIT_FALLBACK = 1.0f;                   // m (ExitSeat without a way out)
constexpr float EXIT_MIN_SQUARE = 1e-16f;

// S_vector::Normalize as the inline copies compile here: the square the caller summed; a NaN
// component counts as not 0 and not negative.
void NormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-08f)
        return;
    if (!(square < 1e-08f)) {
        const float scale = 1.0f / std::sqrt(square);
        v = S_vector(v.x * scale, v.y * scale, v.z * scale);
    } else if (v.x != 0.0f) {
        v.x = v.x < 0.0f ? -1.0f : 1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z < 0.0f ? -1.0f : 1.0f;
    } else {
        v.y = v.y < 0.0f ? -1.0f : 1.0f;
    }
}

float SquareYZX(const S_vector &v) { return (v.y * v.y + v.z * v.z) + v.x * v.x; }

// The normalized world z axis (I3D_frame::GetWorldDir inlined).
S_vector WorldDir(const I3D_frame *frame)
{
    S_vector dir = frame->GetMatrix()(2);
    NormalizeBy(dir, SquareYZX(dir));
    return dir;
}

// (The box GetWeaponBox fills: 0x008aca00.)
I3D_bbox g_weaponBox;

// Adds `frame`'s object visual (not flagged VISFLAGS_1000), placed by `parent`, and its
// descendants' to the box. (0x00602bc0)
void AddWeaponBox(I3D_frame *frame, const S_matrix &parent)
{
    const S_matrix matrix = frame->GetLocalMatrix() * parent;
    if (frame->GetVisualType() == I3DVISUAL_OBJECT &&
        !(static_cast<I3D_visual *>(frame)->VisualFlags() & VISFLAGS_1000)) {
        const I3D_bbox &box = static_cast<I3D_visual *>(frame)->GetLocalBound().bbox;
        if (box.min.x <= box.max.x) {
            for (int corner = 0; corner < 8; ++corner) {
                const S_vector p(corner & 1 ? box.max.x : box.min.x, corner & 2 ? box.max.y : box.min.y,
                                 corner & 4 ? box.max.z : box.min.z);
                g_weaponBox.Expand(p * matrix);
            }
        }
    }
    for (I3D_frame *child = frame->GetFirstChild(); child; child = child->GetNextSibling())
        AddWeaponBox(child, matrix);
}

// The box of `frame`'s object visual and its descendants' (but those flagged VISFLAGS_1000), in
// its space. (0x00602b20, the static weapon's own copy of the hierarchy box; the original
// returns its static box.)
const I3D_bbox &GetWeaponBox(I3D_frame *frame)
{
    if (frame->GetVisualType() == I3DVISUAL_OBJECT) {
        g_weaponBox = static_cast<I3D_visual *>(frame)->GetLocalBound().bbox;
    } else {
        // (The w components are left as they were.)
        g_weaponBox.min = S_vector(1e16f, 1e16f, 1e16f);
        g_weaponBox.max = S_vector(-1e16f, -1e16f, -1e16f);
    }
    for (I3D_frame *child = frame->GetFirstChild(); child; child = child->GetNextSibling())
        AddWeaponBox(child, IdentityMatrix());
    return g_weaponBox;
}

// The centre and the bounding sphere's radius of a box (0 for an empty one).
float BoxSphere(const I3D_bbox &box, S_vector &center)
{
    if (!(box.min.x <= box.max.x))
        return 0.0f;
    const S_vector half = (box.max - box.min) * 0.5f;
    center = box.min + half;
    return std::sqrt((half.z * half.z + half.y * half.y) + half.x * half.x);
}

C_StaticWeaponCreator g_staticWeaponCreator;            // 0x008aca20

}  // namespace

C_StaticWeapon::C_StaticWeapon() : C_Actor(ACTOR_STATIC_WEAPON)   // 0x00602ec0
{
}

// --- the state ------------------------------------------------------------------------------------

void C_StaticWeapon::SetState(int state)                // 0x006059b0
{
    if (m_state == state)
        return;
    ApplyState(state);
    if (!IsLocalGame()) {
        C_BitStream *msg = CreateActorMessage(GetNetId(), false);
        msg->WriteBits(MSG_STATE, MSG_BITS);
        msg->WriteBits(static_cast<uint32_t>(m_state), STATE_BITS);
        SendActorMessage(msg);
    }
    if (m_state != STATIC_WEAPON_DESTROYED)
        return;
    for (int seat = GetSeatCount(); seat != 0;) {
        --seat;
        if (C_Actor *occupant = m_seats[seat].occupant) {
            const S_vector at(m_position.x, m_position.y, m_position.z);
            occupant->Hit(HIT_EXPLOSION, -1.0f, at, S_vector(0.0f, 1.0f, 0.0f), nullptr, 0);
        }
    }
}

void C_StaticWeapon::ApplyState(int state)              // 0x00605ae0
{
    if (m_state == state)
        return;
    m_state = state;
    switch (state) {
    case STATIC_WEAPON_HIDDEN:
        m_frame->SetOn(false);
        break;
    case STATIC_WEAPON_USABLE:
        m_frame->SetOn(true);
        if (!m_volumeAdded)
            g_collisionWorld.AddVolume(&m_volume);
        m_volumeAdded = true;
        return;
    case STATIC_WEAPON_DESTROYED:
        ShowDestroyed();
        return;
    case STATIC_WEAPON_OFF:
        ShowIntact();
        break;
    default:
        return;
    }
    if (m_volumeAdded)
        g_collisionWorld.RemoveVolume(&m_volume);
    m_volumeAdded = false;
}

uint32_t __stdcall C_StaticWeapon::CopyPart(I3D_frame *frame, void *context)   // 0x00605d80
{
    auto *model = static_cast<I3D_model *>(context);
    I3D_frame *part = model->FindChildFrame(frame->GetShortName(), FRAME_MASK_ANY);
    if (part) {
        // (Any visual type: the slot is called as the object visual's.)
        static_cast<I3D_object_visual *>(frame)->SetMesh(static_cast<I3D_visual *>(part)->GetVisualMesh(), 1);
        frame->SetRot(part->GetRot());
        frame->Update();
    }
    return 0;
}

void C_StaticWeapon::ShowDestroyed()                    // 0x00605bb0
{
    if (!m_destroyed)
        return;
    for (int seat = GetSeatCount(); seat != 0;) {
        --seat;
        if (C_Actor *occupant = m_seats[seat].occupant)
            occupant->GetFrame()->LinkTo(nullptr, I3DLINK_KEEP_WORLD);
    }
    m_frame->EnumFrames(&CopyPart, m_destroyed, FRAME_MASK_VISUAL, nullptr);
    m_frame->Update();
    for (int seat = GetSeatCount(); seat != 0;) {
        --seat;
        if (C_Actor *occupant = m_seats[seat].occupant) {
            occupant->GetFrame()->LinkTo(m_seats[seat].frame, I3DLINK_KEEP_WORLD);
            occupant->GetFrame()->Update();
        }
    }
    m_destroyedShown = true;
}

void C_StaticWeapon::ShowIntact()                       // 0x00605c80
{
    if (!m_destroyedShown)
        return;
    auto *model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    const char *fileName = static_cast<I3D_model *>(m_frame)->GetFileName();
    if (g_modelCache.OpenInto(model, fileName) != I3D_OK) {
        model->Release();
        return;
    }
    for (int seat = GetSeatCount(); seat != 0;) {
        --seat;
        if (C_Actor *occupant = m_seats[seat].occupant)
            occupant->GetFrame()->LinkTo(nullptr, I3DLINK_KEEP_WORLD);
    }
    // (The model is kept, as the original keeps it: the visuals took its meshes.)
    m_frame->EnumFrames(&CopyPart, model, FRAME_MASK_VISUAL, nullptr);
    m_frame->Update();
    for (int seat = GetSeatCount(); seat != 0;) {
        --seat;
        if (C_Actor *occupant = m_seats[seat].occupant) {
            occupant->GetFrame()->LinkTo(m_seats[seat].frame, I3DLINK_KEEP_WORLD);
            occupant->GetFrame()->Update();
        }
    }
    m_destroyedShown = false;
}

// --- the mission ----------------------------------------------------------------------------------

void C_StaticWeapon::StartMission(bool server)          // 0x006036d0
{
    Activate(server);
    if (m_started)
        return;
    m_started = true;
    m_storeChanged = IsServer();
    if (!IsLocalGame()) {
        m_netObject = g_netReplication->FindObject(GetNetId(), true);
        m_netObject->SetLive(1);
        m_netObject->SetSendMask(2);
        m_netObject->SetForwarding(false);
        m_netObject->SetMessageFactory(&MakeSeatWeaponMessage, this);
        if (IsServer()) {
            g_netReplication->SetWriteEpoch(0);
            g_netReplication->SetReadEpoch(1);
        }
    }
    for (int seat = 0;; ++seat) {
        char name[0x80];
        std::sprintf(name, "SEAT%02d", seat);
        I3D_frame *frame = m_frame->FindChildFrame(name, FRAME_MASK_DUMMY);
        if (!frame)
            break;
        S_Seat entry;
        entry.frame = frame;
        m_seats.push_back(entry);
    }
    m_weapon.Init(this, m_frame, m_frame, 0);
    if (!IsLocalGame()) {
        m_weapon.SetMessageSender(&SeatWeaponSendMessage, this);
        // Fix: a weapon without its item counts nothing (the original reads through null).
        if (C_ItemDef *def = m_weapon.GetItemDef())
            m_weapon.SetRefillBelow(GetContainer(0)->CountItems(def->GetAmmoItemId()));
    }
    m_name = g_texts.Get(GetSeatWeaponText(0));
    // The box (the weapon's own hierarchy box).
    const I3D_bbox box = GetWeaponBox(m_frame);
    // Fix: an empty box's centre is 0 (the original's is unset).
    S_vector center(0.0f, 0.0f, 0.0f);
    const float radius = BoxSphere(box, center);
    m_volume = S_CollisionVolume();
    m_volume.actor = this;
    m_volume.flags = COLLISION_SHAPE_BOX;
    m_volume.radius = radius;
    m_volume.bbox = box;
    m_volume.matrix = &m_frame->GetMatrix();
    m_invMatrix.Inverse(m_frame->GetMatrix());
    m_volume.invMatrix = &m_invMatrix;
    m_volume.frame = m_frame;
    m_volume.type = WEAPON_VOLUME_TYPE;
    const S_vector scale = m_frame->GetWorldScale();
    const float maxYZ = scale.y <= scale.z ? scale.z : scale.y;
    const float maxScale = scale.x <= maxYZ ? maxYZ : scale.x;
    m_volume.radius = maxScale * radius;
    m_volume.center = center * m_frame->GetMatrix();
    FindExit();
    // The model of the destroyed state: "C" and the model's file name.
    char name[0x80];
    std::snprintf(name, sizeof name, "C%s", static_cast<I3D_model *>(m_frame)->GetFileName());
    m_destroyed = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (g_modelCache.OpenInto(m_destroyed, name) != I3D_OK) {
        m_destroyed->Release();
        m_destroyed = nullptr;
    }
    ApplyState(STATIC_WEAPON_USABLE);
}

void C_StaticWeapon::StopMission(bool server)           // 0x00604120
{
    Deactivate(server);
    m_seats.clear();
    m_weapon.Stop();
    if (m_started) {
        m_started = false;
        ApplyState(STATIC_WEAPON_OFF);
    }
}

void C_StaticWeapon::RestartMission(bool server)        // 0x006050b0
{
    for (int seat = 0; seat < GetSeatCount(); ++seat)
        LeaveSeat(seat);
    ApplyState(STATIC_WEAPON_OFF);
    m_weapon.Restart();
    if (!IsLocalGame())
        m_storeChanged = true;
    if (server)
        ApplyState(STATIC_WEAPON_USABLE);
}

void C_StaticWeapon::FindExit()                         // 0x006055c0
{
    C_Checkpoints *walk = GetGame()->GetCheckpoints();
    const int point = walk->FindActorPoint(this);
    if (point != CHECKPOINT_NONE) {
        const S_vector &pos = walk->Get(point)->pos;
        m_exit.x = pos.x;
        m_exit.y = pos.y;
        m_exit.z = pos.z;
        return;
    }
    const S_vector dir = m_frame->GetMatrix()(2);
    const S_vector &at = m_frame->GetWorldPos();
    m_exit.x = dir.x * -EXIT_BEHIND + at.x;
    m_exit.y = dir.y * -EXIT_BEHIND + at.y;
    m_exit.z = dir.z * -EXIT_BEHIND + at.z;
}

void C_StaticWeapon::UpdateVolume()                     // 0x00604e90
{
    if (!m_volumeAdded)
        return;
    m_frame->Update();
    const I3D_bbox box = GetWeaponBox(m_frame);
    S_vector center(0.0f, 0.0f, 0.0f);                  // (unset for an empty box in the original)
    const float radius = BoxSphere(box, center);
    m_invMatrix.Inverse(m_frame->GetMatrix());
    m_volume.bbox = box;
    // Quirk kept: the radius is not scaled here (StartMission scales it).
    m_volume.radius = radius;
    m_volume.center = center * m_frame->GetMatrix();
    g_collisionWorld.MoveVolume(&m_volume);
}

void C_StaticWeapon::Update(int frameTime)              // 0x006034d0
{
    if (!IsLocalGame())
        ReadMessages();
    m_weapon.Update(frameTime);
    // Fix: a weapon without seats has no gunner (the original reads through null).
    if (!m_seats.empty() && m_seats[0].occupant) {
        m_frame->Tick(frameTime);
        UpdateVolume();
    }
}

void C_StaticWeapon::Tick(int timeDelta)                // 0x00603070
{
    if (!IsClient())
        ReadMessages();
    m_weapon.Tick(timeDelta);
    if (IsLocalGame())
        return;
    if (m_storeChanged && IsServer()) {
        C_BitStream *msg = CreateActorMessage(GetNetId(), false);
        msg->WriteBits(MSG_STORE, MSG_BITS);
        WriteStore(msg);
        SendActorMessage(msg);
        m_storeChanged = false;
    }
    // Fix: a weapon without its item refills nothing (the original reads through null).
    C_ItemDef *def = m_weapon.GetItemDef();
    if (!def)
        return;
    const uint32_t interval =
        def->GetComponentKind() == ICK_PROJECTILE_WEAPON ? REFILL_INTERVAL_LAUNCHER : REFILL_INTERVAL;
    if (!(m_weapon.GetRefillTime() + interval < GetGame()->GetMissionTime()))
        return;
    const int below = m_weapon.GetRefillBelow();
    if (GetContainer(0)->CountItems(def->GetAmmoItemId()) > below || g_gameProfile.IsCooperativeGame())
        return;
    C_Item *ammo = g_itemsModule.CreateItem(m_weapon.GetItemDef()->GetAmmoItemId(), 0, nullptr, -1);
    C_Item *replaced = nullptr;
    GetContainer(0)->AddItem(ammo, &replaced, -1);
    auto *message = new C_SeatWeaponAmmoMessage;        // _aligned_malloc(0x38, 16) in the original
    message->typeId = ammo->GetTypeId();
    message->uid = ammo->GetUid();
    message->fromClient = true;
    m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
    m_weapon.SetRefillTime(GetGame()->GetMissionTime());
}

// --- the network ----------------------------------------------------------------------------------

void C_StaticWeapon::ReadMessages()                     // 0x00603290
{
    // A client's player sitting at `seat` (its own shots and reloads come back to it).
    const auto ownSeat = [this](int seat) {
        return IsClient() &&
               static_cast<C_Actor *>(g_teamManager.GetPlayerTeam()->GetSelectedMember()) == GetSeatOccupant(seat);
    };
    const auto livingGunner = [this](int seat) {
        C_Actor *gunner = GetSeatOccupant(seat);
        return gunner && static_cast<C_Human *>(gunner)->IsAlive();
    };
    while (C_NetObjectMessage *msg = m_netObject->PopMessage(0)) {
        switch (msg->GetType()) {
        case SEAT_WEAPON_MSG_SHOT: {
            const auto *shot = static_cast<C_SeatWeaponShotMessage *>(msg);
            if (ownSeat(shot->seat) || !livingGunner(shot->seat))
                break;
            // (Whatever the seat, the one weapon.)
            if (!IsServer()) {
                m_weapon.PlayShot(shot->barrel);
            } else if (m_weapon.FireShot(&shot->pos, &shot->dir, shot->barrel, 1000)) {
                msg->fromClient = false;
                m_netObject->QueueMessage(msg, g_netReplication->GetLocalId(), true);
            }
            break;
        }
        case SEAT_WEAPON_MSG_RELOAD: {
            const auto *reload = static_cast<C_SeatWeaponReloadMessage *>(msg);
            if (ownSeat(reload->seat) || !livingGunner(reload->seat))
                break;
            if (!IsServer()) {
                m_weapon.ShowReload(GetContainer(0));
            } else {
                m_weapon.Reload(GetContainer(0));
                msg->fromClient = false;
                m_netObject->QueueMessage(msg, g_netReplication->GetLocalId(), true);
            }
            break;
        }
        case SEAT_WEAPON_MSG_ROUNDS:
            m_weapon.SetRounds(static_cast<C_SeatWeaponRoundsMessage *>(msg)->rounds);
            break;
        case SEAT_WEAPON_MSG_AMMO: {
            const auto *ammo = static_cast<C_SeatWeaponAmmoMessage *>(msg);
            C_Item *item = g_itemsModule.CreateItem(ammo->typeId, 0, nullptr, ammo->uid);
            C_Item *replaced = nullptr;
            GetContainer(0)->AddItem(item, &replaced, -1);
            break;
        }
        default:
            break;
        }
        // (Queued again it is not deleted: the queue holds it.)
        if (msg->refCount < 1)
            delete msg;
    }
}

void C_StaticWeapon::WriteStore(C_BitStream *msg)       // 0x006035b0
{
    C_ItemContainer *store = GetContainer(0);
    for (int i = 0; i < store->GetUsedSlotCount(); ++i) {
        for (const C_Item *item : store->GetSlots()[i]) {
            msg->WriteBits(static_cast<uint32_t>(item->GetUid()), 16);
            msg->WriteBits(static_cast<uint32_t>(item->GetTypeId()), 8);
        }
    }
    msg->WriteBits(0xFFFF, 16);
}

void C_StaticWeapon::ReadStore(C_BitStream *msg, bool add)   // 0x00603650
{
    for (uint32_t uid = msg->ReadBits(16); uid != 0xFFFF; uid = msg->ReadBits(16)) {
        const int typeId = static_cast<int>(msg->ReadBits(8));
        if (!add)
            continue;
        C_Item *item = g_itemsModule.CreateItem(typeId, 0, nullptr, static_cast<int>(uid));
        C_Item *replaced = nullptr;
        GetContainer(0)->AddItem(item, &replaced, -1);
    }
}

void C_StaticWeapon::OnMessage(C_BitStream *msg)        // 0x00605030
{
    const uint32_t kind = msg->ReadBits(MSG_BITS);
    if (kind == MSG_STORE)
        ReadStore(msg, !IsServer());
    else if (kind == MSG_STATE)
        ApplyState(static_cast<int>(msg->ReadBits(MSG_BITS)));
}

void C_StaticWeapon::WriteState(C_BitStream *msg)       // 0x00603520
{
    msg->WriteBits(static_cast<uint32_t>(m_state), STATE_BITS);
    WriteStore(msg);
}

void C_StaticWeapon::ReadState(C_BitStream *msg)        // 0x00603560
{
    ApplyState(static_cast<int>(msg->ReadBits(STATE_BITS)));
    ReadStore(msg, true);
}

// --- the saved game -------------------------------------------------------------------------------

bool C_StaticWeapon::SaveGame(C_chunk &chunk)           // 0x00605120
{
    chunk.Begin(CHUNK_STATE);
    chunk.Write(m_state);
    chunk.Ascend();
    chunk.Begin(CHUNK_STORE);
    SaveActorItems(this, chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_WEAPON);
    m_weapon.SaveGame(chunk);
    chunk.Ascend();
    return true;
}

bool C_StaticWeapon::LoadGame(C_chunk &chunk)           // 0x006053c0
{
    int state = STATIC_WEAPON_OFF;
    uint16_t id = 0;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == CHUNK_STATE)
            chunk.Read(state);
        else if (id == CHUNK_WEAPON)
            m_weapon.LoadGame(chunk);
        else if (id == CHUNK_STORE)
            LoadActorItems(this, chunk);
        chunk.Ascend();
    }
    ApplyState(state);
    return true;
}

// --- the seats ------------------------------------------------------------------------------------

bool C_StaticWeapon::IsSeatBlocked(int seat)            // 0x00604a90
{
    return seat >= 0 && seat < GetSeatCount() ? m_seats[seat].blocked : true;
}

bool C_StaticWeapon::SetSeatBlocked(int seat, bool blocked)   // 0x00604ad0
{
    if (seat < 0 || seat >= GetSeatCount())
        return false;
    m_seats[seat].blocked = blocked;
    return true;
}

void C_StaticWeapon::SetSeatOccupant(int seat, C_Actor *occupant)   // 0x006041a0
{
    if (!IsSeat(seat) || m_seats[seat].occupant)
        return;
    if (occupant) {
        I3D_frame *frame = occupant->GetFrame();
        frame->LinkTo(m_seats[seat].frame, 0);
        frame->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
        frame->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
        frame->Update();
        const S_matrix &at = m_seats[seat].frame->GetMatrix();
        occupant->SetPosition4({at.m[3][0], at.m[3][1], at.m[3][2], at.m[3][3]});
        const S_vector dir = WorldDir(m_seats[seat].frame);
        const S_vector4 facing = {dir.x, dir.y, dir.z, at.m[2][3]};
        occupant->SetDirection4(facing);
        auto *human = static_cast<C_Human *>(occupant);
        human->SetViewDir(facing);
        human->SetFacingDir(occupant->GetDirection4());
        m_frame->Update();
    }
    m_seats[seat].occupant = occupant;
    m_weapon.SetGunner(static_cast<C_Human *>(occupant));
    UpdateVolume();
    if (const S_FrameData *data = m_frame->GetData()) {
        if (data->script)
            data->script->OnUse(1);
    }
    if (!IsLocalGame() && IsServer()) {
        auto *message = new C_SeatWeaponRoundsMessage;  // _aligned_malloc(0x38, 16) in the original
        message->rounds = m_weapon.GetRounds();
        message->seat = 0;
        message->fromClient = true;
        m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
    }
}

C_Actor *C_StaticWeapon::GetSeatOccupant(int seat)      // 0x00604590
{
    return IsSeat(seat) ? m_seats[seat].occupant : nullptr;
}

int C_StaticWeapon::FindSeat(const C_Actor *occupant)   // 0x006045e0
{
    for (int seat = GetSeatCount(); seat-- > 0;) {
        if (m_seats[seat].occupant == occupant)
            return seat;
    }
    return -1;
}

C_Actor *C_StaticWeapon::LeaveSeat(int seat)            // 0x00604630
{
    if (!IsSeat(seat))
        return nullptr;
    C_Actor *occupant = m_seats[seat].occupant;
    if (occupant)
        occupant->GetFrame()->LinkTo(GetGame()->GetScene()->GetPrimarySector(), I3DLINK_KEEP_WORLD);
    m_seats[seat].occupant = nullptr;
    m_seats[seat].blocked = false;
    m_weapon.SetGunner(nullptr);
    UpdateVolume();
    return occupant;
}

bool C_StaticWeapon::ExitSeat(int seat)                 // 0x006046d0
{
    if (!IsSeat(seat) || !m_seats[seat].occupant)
        return false;
    S_vector exit(m_exit.x, m_exit.y, m_exit.z);
    if ((exit.x * exit.x + exit.y * exit.y) + exit.z * exit.z < EXIT_MIN_SQUARE) {
        // No way out found: a metre behind the weapon.
        const S_vector dir = m_frame->GetMatrix()(2);
        const S_vector back(dir.x * -EXIT_FALLBACK, dir.y * -EXIT_FALLBACK, dir.z * -EXIT_FALLBACK);
        const S_vector &at = m_frame->GetWorldPos();
        exit = S_vector(back.x + at.x, back.y + at.y, back.z + at.z);
    }
    const S_vector facing = WorldDir(m_frame);
    C_Actor *occupant = m_seats[seat].occupant;
    I3D_frame *frame = occupant->GetFrame();
    frame->LinkTo(GetGame()->GetScene()->GetPrimarySector(), 0);
    frame->SetPos(exit);
    frame->SetWorldDir(facing, 0.0f);
    frame->Update();
    m_seats[seat].occupant = nullptr;
    m_weapon.SetGunner(nullptr);
    UpdateVolume();
    return true;
}

uint32_t C_StaticWeapon::GetSeatFlags(int seat)         // 0x00604a10
{
    if (seat < 0 || seat >= GetSeatCount())
        return 0;
    uint32_t flags = 0;
    if (const char *found = std::strstr(m_seats[seat].frame->GetUserProps(), "SEAT")) {
        for (const char *c = found + 4; *c != '\0' && *c != ',' && *c != ' '; ++c)
            flags |= 1u << ((static_cast<unsigned char>(*c) - '0') & 31);
    }
    return flags;
}

int C_StaticWeapon::GetSeatText(int seat)               // 0x00604b10
{
    if (seat < 0 || seat >= GetSeatCount())
        return -1;
    const char *found = std::strstr(m_seats[seat].frame->GetUserProps(), "name");
    return found ? std::atoi(found + 4) : -1;
}

bool C_StaticWeapon::IsSeatFirstPersonOnly(int seat)    // 0x00604e30
{
    if (seat < 0 || seat >= GetSeatCount())
        return false;
    return std::strstr(m_seats[seat].frame->GetUserProps(), "NO3RD") != nullptr;
}

I3D_frame *C_StaticWeapon::GetSeatCamera(int seat)      // 0x00604b70
{
    if (seat < 0 || seat >= GetSeatCount())
        return nullptr;
    // (Out of the seat meanwhile, so that its own dummies are not found.)
    C_Actor *occupant = m_seats[seat].occupant;
    if (occupant)
        occupant->GetFrame()->LinkTo(nullptr, 0);
    const auto cameraName = [this, seat] {
        return GetGame()->IsFirstPersonView() || IsSeatFirstPersonOnly(seat) ? "cameraFPV*" : "camera3RD*";
    };
    const uint32_t mask = ENUMF_WILDMASK | FRAME_MASK_DUMMY;
    I3D_frame *camera = m_seats[seat].frame->FindChildFrame(cameraName(), mask);
    if (!camera) {
        const char *name = cameraName();
        // Fix: a seat dummy without a parent has no camera there (the original calls through null).
        if (I3D_frame *parent = m_seats[seat].frame->GetParent())
            camera = parent->FindChildFrame(name, mask);
    }
    if (occupant) {
        occupant->GetFrame()->LinkTo(m_seats[seat].frame, 0);
        occupant->GetFrame()->Update();
    }
    return camera;
}

void C_StaticWeapon::ApplySeatControls(int seat, float *axes, float *buttons)   // 0x00604cf0
{
    // Fix: a seat out of range has no occupant (the original reads past the seats).
    if (IsSeat(seat) && m_seats[seat].occupant)
        m_weapon.ApplyControls(axes, buttons);
}

void C_StaticWeapon::Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
                         int)                           // 0x006057b0
{
    auto *gunner = static_cast<C_Human *>(GetSeatOccupant(0));
    if (!gunner)
        return;
    int zone = 0;
    if (type == HIT_BULLET || type == HIT_MELEE) {
        zone = g_hitZones.GetHitZone(gunner, pos, dir);
        if (zone == 0xFF) {
            // Missed him: alarmed (alive, by an enemy) as if shot at along the weapon's direction.
            if (!gunner->IsAlive() || !gunner->IsHostileTo(attacker))
                return;
            gunner->NotifyShotAt(attacker, GetDirection());
            return;
        }
    }
    S_vector d = dir;
    NormalizeBy(d, SquareYZX(d));
    gunner->Hit(type, amount, pos, d, attacker, zone);
    NotifyFrameHit(gunner->GetFrame());
}

void C_StaticWeapon::GetBoxCorners(S_vector4 corners[8])   // 0x00605e50
{
    m_frame->Update();
    const I3D_bbox &box = GetWeaponBox(m_frame);
    int n = 0;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            const float x = i == 0 ? box.max.x : box.min.x;
            const float y = j == 0 ? box.max.y : box.min.y;
            corners[n].x = x;
            corners[n].y = y;
            corners[n++].z = box.min.z;
            corners[n].x = x;
            corners[n].y = y;
            corners[n++].z = box.max.z;
        }
    }
    const S_matrix world = m_frame->GetMatrix();
    for (int k = 0; k < 8; ++k) {
        const S_vector v = S_vector(corners[k].x, corners[k].y, corners[k].z) * world;
        corners[k].x = v.x;
        corners[k].y = v.y;
        corners[k].z = v.z;
    }
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_StaticWeaponCreator::Create()                // 0x00602a70
{
    return new C_StaticWeapon();                        // _aligned_malloc(0x300, 16) in the original
}

void C_StaticWeaponCreator::Destroy(C_Actor *actor)     // 0x00602ae0
{
    delete actor;
}

// Registers the creator (static initialiser 0x00602a50, thunk 0x00602a40).
static const bool g_staticWeaponCreatorRegistered = [] {
    g_actorCreators[ACTOR_STATIC_WEAPON] = &g_staticWeaponCreator;
    return true;
}();
