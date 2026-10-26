// C_SeatWeapon (original 0x004c19d0 constructor, 0x004c1aa0 destructor, 0x004c1ad0 Init, 0x004c2380
// Stop, 0x004c6620 Restart, 0x004c6dd0 SetGunner, 0x004c6d90 ShowView, 0x004c6570 GetFrameInt,
// 0x004c6e50 LoadSounds, 0x004c7630 ReleaseSounds, 0x004c1990 OnBarrelNote, 0x004c5080 EjectShell, the
// accessors 0x004c57b0..0x004c5870 / 0x004c6550 / 0x004c6560 / 0x004c7670 / 0x004c3100, 0x004c6660
// SaveGame, 0x004c68b0 LoadGame, 0x004c6cb0 QuatMultiply; the messages 0x004c1230
// SeatWeaponSendMessage, 0x004c14b0 MakeSeatWeaponMessage, 0x004c16d0..0x004c1970 their Write / Read;
// part 3b: 0x004c56a0 Update, 0x004c55e0 Tick, 0x004c3120 TurnByControls, 0x004c2580 TurnToView,
// 0x004c3b60 ApplyControls, 0x004c4780 Fire, 0x004c4440 SpreadDirection, 0x004c3dc0 FireShot,
// 0x004c4010 PlayShot, 0x004c6400 StartReload, 0x004c6270 Reload, 0x004c62e0 ShowReload, 0x004c5a30
// CanTurnTo, 0x004c5f60 CanTiltTo, 0x004c6230 CanAim).
#include "game/seat_weapon.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/car.h"
#include "game/collision.h"                             // g_collisionWorld
#include "game/game.h"                                  // GetGame
#include "game/human.h"
#include "game/human_controller.h"                      // AXIS_FIRE, AXIS_RELOAD, BUTTON_FIRE
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"                          // g_itemsModule
#include "game/pending.h"                               // g_netReplication, g_gameRole, SetFrameData
#include "game/physics.h"
#include "game/particles.h"                             // g_particlesModule
#include "game/resource_cache.h"                        // g_modelCache, g_animationCache, g_soundCache
#include "game/shots.h"                                 // g_shotManager
#include "game/sound_module.h"
#include "game/sound_table.h"                           // SOUND_CHAPTER_WEAPON_MANIPULATION
#include "game/team.h"                                  // g_teamManager
#include "game/vehicle.h"
#include "game/world.h"                                 // C_CameraModule
#include "ls3d/i3d.h"
#include "net/bitstream.h"

// Two directions across `dir` (game/player_weapons.cpp; the player's spread).
void BuildBasis(const S_vector &dir, S_vector &side, S_vector &up);   // 0x00544d20 (__fastcall)

namespace {

// The flags' bits (m_flags).
constexpr uint32_t SEAT_WEAPON_TRAVERSE_PLUS = 0x01;    // TurnToView's last step turned the traverse angle up
constexpr uint32_t SEAT_WEAPON_TRAVERSE_MINUS = 0x02;   // ... down
constexpr uint32_t SEAT_WEAPON_ELEVATION_UP = 0x04;
constexpr uint32_t SEAT_WEAPON_ELEVATION_DOWN = 0x08;
constexpr uint32_t SEAT_WEAPON_TURNING = 0x0F;
constexpr uint32_t SEAT_WEAPON_RELOADING = 0x20;        // until the delay runs out
constexpr uint32_t SEAT_WEAPON_MOTOR_SOUND = 0x400;     // the turning sound plays

// The controller's axes of the turret (C_HumanController's; the traverse's and elevation's speed) and
// what one unit of them makes of a non-launcher's speed.
constexpr int AXIS_SEAT_TRAVERSE = 0x34;
constexpr int AXIS_SEAT_ELEVATION = 0x35;
constexpr float CONTROL_SPEED = 15.0f;
// The controls' speed slows once released: the traverse by 5 x 0.001 a ms, the elevation by
// 0.004 x its rate a ms.
constexpr int TRAVERSE_SLOWING = 5;
constexpr float ELEVATION_SLOWING = 0.004f;
// The elevation's bounds besides the limits (rad, just short of pi / 2).
constexpr float MAX_ELEVATION = 1.5676548f;
constexpr float PI = 3.1415927f;
constexpr float TWO_PI = 6.2831855f;
// TurnToView's smallest step (rad; a double in the original).
constexpr double MIN_TURN = 0.001;
// The turning sound stops after more ms than that without turning.
constexpr int MOTOR_STOP_IDLE = 300;
// A local player's aim in third person: a ray from 5 m in front of the camera, 500 m along it (the
// collision world's RayTest exclusions); a point within 3 m of the muzzle is passed over.
constexpr float AIM_RAY_START = 5.0f;
constexpr float AIM_RAY_LENGTH = 500.0f;
constexpr uint32_t AIM_RAY_EXCLUDE_TYPES = 0x208001;
constexpr uint32_t AIM_RAY_EXCLUDE_FLAGS = 0xB00000;
constexpr float AIM_MIN_SQUARE = 9.0f;
// A launcher's recoil on a car, x the shot's momentum: Fire's along the shot, FireShot's (the server
// firing a client's shot) back, PlayShot's (a client showing another's) along it again.
constexpr float FIRE_RECOIL = 1.0f;
constexpr float FIRE_SHOT_RECOIL = -0.8f;
constexpr float PLAY_SHOT_RECOIL = 0.4f;
// Single player: firing costs the player's team's gunner this disguise.
constexpr float FIRE_DISGUISE = -10.0f;

constexpr float DEG_TO_RAD = 0.017453292f;
constexpr float HALF_PI = 1.5707964f;
// The turning rates without "hs" / "vs" (rad/s; pi/4) and a missing limit.
constexpr float DEFAULT_TURN_RATE = 0.7853982f;
constexpr float NO_LIMIT = 1e6f;
// The HUD's text of a reload (UnknownC8).
constexpr int RELOAD_TEXT = 0x1B9F;
// The ejected shells' physics material.
constexpr int SHELL_MATERIAL = 0xC;
// The push given to a shell (the physics module's ApplyImpulse factor).
constexpr float SHELL_IMPULSE_FACTOR = 0.05f;

// Saved-game chunks of a seat weapon (inside the car's CHUNK_CAR_SEAT_WEAPON).
enum : uint16_t {
    CHUNK_SEAT_WEAPON_STATE = 2000,     // tilt, turn, flags, barrel, delay, reload left, reload time
    CHUNK_SEAT_WEAPON_ROUNDS = 0x7D1,   // the item's amount (a float)
};

// The bits of the messages' type: ln 4 / ln 2 + 1 (the original works it out in doubles each time).
constexpr int MSG_TYPE_BITS = 3;
// The number of message types the factory writes.
constexpr uint32_t MSG_TYPE_COUNT = 4;

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

// Squares in the orders the original sums them.
float SquareYZX(const S_vector &v) { return (v.y * v.y + v.z * v.z) + v.x * v.x; }
float SquareZYX(const S_vector &v) { return (v.z * v.z + v.y * v.y) + v.x * v.x; }
float SquareXZY(const S_vector &v) { return (v.x * v.x + v.z * v.z) + v.y * v.y; }

// S_vector::Normalize from a square summed in another order.
void NormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-8f)
        return;
    if (square >= 1e-8f)
        v *= 1.0f / std::sqrt(square);
    else if (v.x != 0.0f)
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    else if (v.z != 0.0f)
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    else
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
}

// `a` x `b` (0x004c6cb0, a thiscall on `a`; the sums grouped as the original's).
S_quat QuatMultiply(const S_quat &a, const S_quat &b)
{
    const float zx = a.v.z * b.v.x - a.v.x * b.v.z;
    const float xy = b.v.y * a.v.x - a.v.y * b.v.x;
    const float sx = b.v.x * a.s + b.s * a.v.x;
    const float sy = b.v.y * a.s + a.v.y * b.s;
    const float sz = a.s * b.v.z + a.v.z * b.s;
    S_quat out;
    out.s = a.s * b.s - ((a.v.z * b.v.z + b.v.x * a.v.x) + b.v.y * a.v.y);
    out.v.x = (a.v.y * b.v.z - b.v.y * a.v.z) + sx;
    out.v.y = sy + zx;
    out.v.z = sz + xy;
    return out;
}

// The frame's local z axis as I3D_frame::GetLocalDir finds it, normalised from a y z x square
// (TurnByControls).
S_vector LocalDirYZX(I3D_frame *frame)
{
    bool normalize;
    S_vector dir = frame->GetLocalDirUnnormalized(&normalize);
    if (normalize)
        NormalizeBy(dir, SquareYZX(dir));
    return dir;
}

// An angle in [-2 pi, 4 pi] brought into [0, 2 pi] (TurnToView's headings).
float WrapAngle(float angle)
{
    if (angle < 0.0f)
        return angle + TWO_PI;
    if (angle > TWO_PI)
        return angle - TWO_PI;
    return angle;
}

// `dir` turned off by up to spread x 0.01 rad (a random direction across it, a random share).
void SpreadDirection(S_vector &dir, float spread)       // 0x004c4440 (a thiscall on the direction)
{
    if (spread == 0.0f)
        return;
    S_vector side, up;
    BuildBasis(dir, side, up);
    const float a = RandFloat() - 0.5f;
    up *= a + a;
    const float b = RandFloat() - 0.5f;
    side *= b + b;
    S_vector across(side.x + up.x, up.y + side.y, up.z + side.z);
    NormalizeBy(across, SquareZYX(across));
    const float scale = spread * 0.01f * RandFloat();
    dir.x += across.x * scale;
    dir.y += across.y * scale;
    dir.z += across.z * scale;
    NormalizeBy(dir, SquareZYX(dir));
}

// A sound at HORT from the group's first sample, off (null: it could not be opened).
I3D_sound *OpenHortSound(const S_SoundSample &sample, I3D_frame *hort, bool loop)
{
    auto *sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    if (g_soundCache.OpenInto(sound, sample.fileName) != I3D_OK) {
        sound->Release();
        return nullptr;
    }
    sound->SetSoundType(I3DSOUND_POINT);
    sound->SetCone(HALF_PI, HALF_PI);
    if (loop)
        sound->SetLoop(true);
    sound->SetVolume(sample.volume);
    sound->SetRange(sample.nearRange, sample.farRange, 0.5f, 0.5f);
    sound->LinkTo(hort, 0);
    sound->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    sound->SetOn(false);
    return sound;
}

uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

float BitsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

}  // namespace

// --- the lifetime -----------------------------------------------------------------------------------

C_SeatWeapon::C_SeatWeapon() = default;                 // 0x004c19d0 (the members' initialisers)

C_SeatWeapon::~C_SeatWeapon()                           // 0x004c1aa0
{
    // Fix: what a weapon whose Init failed holds is released (the original only frees the barrels'
    // storage; the car deletes such a weapon without Stop, which leaked its item, the barrels'
    // animations and the shells model). After Stop there is nothing left.
    if (m_item)
        g_itemsModule.DestroyItem(m_item);
    for (S_SeatWeaponBarrel &barrel : m_barrels) {
        if (barrel.animation)
            barrel.animation->Release();
    }
    if (m_shells)
        m_shells->Release();
}

bool C_SeatWeapon::Init(C_Actor *owner, I3D_frame *frame, I3D_frame *seatFrame, int seat)   // 0x004c1ad0
{
    if (m_started || !seatFrame || !frame)
        return false;
    m_unknownE8 = 0;
    m_seat = seat;
    m_reloadTime = 0;
    m_reloadLeft = 0;
    // The seat's dummy, or its parent when that is the seat's HORT (the first 6 characters).
    char name[64];
    std::sprintf(name, "HORT%02d*", seat);
    I3D_frame *parent = seatFrame->GetParent();
    m_seatFrame = parent && std::strncmp(parent->GetShortName(), name, 6) == 0 ? parent : seatFrame;
    m_owner = owner;
    m_model = frame;
    m_gunner = nullptr;
    m_started = true;
    m_failed = false;
    m_delay = 0;
    m_barrel = 0;
    m_flags = 0;
    m_refillTime = GetGame()->GetMissionTime();
    m_refillBelow = 0;
    std::sprintf(name, "VIEW%02d", m_seat);
    m_view = m_model->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_VISUAL);
    ShowView(false);

    // The item: the first barrel's "itemid".
    std::sprintf(name, "BARREL%02d_%02d*", m_seat, 0);
    const int itemId = GetFrameInt(m_model->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_VISUAL), "itemid");
    m_item = itemId < 0 ? nullptr : static_cast<C_WeaponItem *>(g_itemsModule.CreateFreeItem(itemId));
    if (!m_item) {
        m_failed = true;
        return false;
    }
    // Fix: an item without a component has no flash (the original reads through null).
    C_ItemComponent *component = m_item->GetDef()->GetComponent();
    m_flashEffect = component ? static_cast<C_WeaponComp *>(component)->GetEffect() : 0;

    // The barrels' shot animations: the model file's name (no directory, no extension) +
    // "_f<seat>_<n>". (Fix: no 64-byte buffer for it, which a longer name overran.)
    const char *file = static_cast<I3D_model *>(frame)->GetFileName();
    const char *slash = std::strrchr(file, '\\');
    std::string base = slash ? slash + 1 : file;
    const std::string::size_type dot = base.rfind('.');
    if (dot != std::string::npos)
        base.resize(dot);
    for (int n = 0;; ++n) {
        std::sprintf(name, "BARREL%02d_%02d*", m_seat, n);
        I3D_frame *barrelFrame = m_model->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_VISUAL);
        if (!barrelFrame)
            break;
        if (n == 0) {
            // The turning rates (degrees a second) and limits (degrees; down and left negated).
            m_tiltRate = static_cast<float>(GetFrameInt(barrelFrame, "vs"));
            m_turnRate = static_cast<float>(GetFrameInt(barrelFrame, "hs"));
            m_upLimit = static_cast<float>(GetFrameInt(barrelFrame, "up"));
            m_downLimit = static_cast<float>(GetFrameInt(barrelFrame, "down"));
            m_leftLimit = static_cast<float>(GetFrameInt(barrelFrame, "left"));
            m_rightLimit = static_cast<float>(GetFrameInt(barrelFrame, "right"));
            m_tiltRate = m_tiltRate == -1.0f ? DEFAULT_TURN_RATE : m_tiltRate * DEG_TO_RAD;
            m_turnRate = m_turnRate == -1.0f ? DEFAULT_TURN_RATE : m_turnRate * DEG_TO_RAD;
            m_upLimit = m_upLimit == -1.0f ? NO_LIMIT : m_upLimit * DEG_TO_RAD;
            m_downLimit = m_downLimit == -1.0f ? NO_LIMIT : m_downLimit * -DEG_TO_RAD;
            m_leftLimit = m_leftLimit == -1.0f ? NO_LIMIT : m_leftLimit * -DEG_TO_RAD;
            m_rightLimit = m_rightLimit == -1.0f ? NO_LIMIT : m_rightLimit * DEG_TO_RAD;
        }
        S_SeatWeaponBarrel barrel;
        barrel.frame = barrelFrame;
        barrel.muzzle = barrelFrame->FindChildFrame("SHDUM*", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
        barrel.ejector = barrelFrame->FindChildFrame("CARDUM*", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
        char suffix[32];
        std::sprintf(suffix, "_f%02d_%02d", seat, n);
        g_animationCache.Get((base + suffix).c_str(), &barrel.animation);
        if (!barrel.muzzle) {
            // A barrel without a muzzle is left out. (Fix: its animation's reference is dropped;
            // the original keeps it.)
            if (barrel.animation)
                barrel.animation->Release();
            continue;
        }
        // The barrel's animation notes eject the shells (its frame data points to the weapon).
        barrelFrame->SetCallback(&OnBarrelNote, I3DCALLBACK_ANIM_NOTE);
        const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
        if (S_FrameData *data = barrelFrame->GetData()) {
            data->unknown1C = self;
        } else {
            S_FrameData newData = {};
            newData.physicsObject = g_noPhysicsObject;
            newData.unknown1C = self;
            SetFrameData(barrelFrame, newData);
        }
        m_barrels.push_back(barrel);
    }
    m_launcher = m_item->GetDef()->GetComponentKind() == ICK_PROJECTILE_WEAPON;

    std::sprintf(name, "HORT%02d*", m_seat);
    m_hort = m_model->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    std::sprintf(name, "VERT%02d*", m_seat);
    m_vert = m_model->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    std::sprintf(name, "AIM%02d*", m_seat);
    m_aim = m_model->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    g_modelCache.Get("GunShells", &m_shells);
    if (m_shells)
        m_shells->SetOn(false);
    if (!m_hort || !m_vert || m_barrels.empty()) {
        m_failed = true;
        return false;
    }
    // The turret's rest (Stop turns it back).
    m_vert->GetRot().Inverse(reinterpret_cast<S_vector &>(m_vertRestAxis), m_vertRestAngle);
    m_vert->Update();
    m_hort->Update();
    m_hort->GetRot().Inverse(reinterpret_cast<S_vector &>(m_hortRestAxis), m_hortRestAngle);
    m_tilt = 0.0f;
    m_turn = 0.0f;
    LoadSounds();
    m_projectileFrame = m_model->FindChildFrame("projectile", FRAME_MASK_VISUAL);
    return true;
}

void C_SeatWeapon::Stop()                               // 0x004c2380
{
    if (!m_started)
        return;
    m_started = false;
    if (m_hort && !m_failed) {
        S_quat rest;
        rest.Make(XYZ(m_hortRestAxis), m_hortRestAngle);
        m_hort->SetRot(rest);
        m_hort->Update();
    }
    if (m_vert && !m_failed) {
        S_quat rest;
        rest.Make(XYZ(m_vertRestAxis), m_vertRestAngle);
        m_vert->SetRot(rest);
        m_vert->Update();
    }
    m_model->Update();
    if (m_item) {
        g_itemsModule.DestroyItem(m_item);
        m_item = nullptr;
    }
    for (S_SeatWeaponBarrel &barrel : m_barrels) {
        if (barrel.animation)
            barrel.animation->Release();
        barrel.animation = nullptr;
    }
    m_barrels.clear();
    if (m_shells)
        m_shells->Release();
    m_shells = nullptr;
    m_hort = nullptr;
    m_vert = nullptr;
    m_aim = nullptr;
    ReleaseSounds();
}

void C_SeatWeapon::Restart()                            // 0x004c6620
{
    C_Human *gunner = m_gunner;
    Stop();
    Init(m_owner, m_model, m_seatFrame, m_seat);
    SetGunner(gunner);
}

void C_SeatWeapon::SetGunner(C_Human *gunner)           // 0x004c6dd0
{
    m_gunner = gunner;
    ShowView(gunner && gunner->IsLocalPlayer() && GetGame()->IsFirstPersonView());
    if (m_flags & SEAT_WEAPON_MOTOR_SOUND)
        StopMotorSound();
}

void C_SeatWeapon::StartMotorSound()
{
    if (m_stopSound)
        m_stopSound->SetOn(false);
    if (m_motorSound)
        m_motorSound->SetOn(true);
    m_flags |= SEAT_WEAPON_MOTOR_SOUND;
}

void C_SeatWeapon::StopMotorSound()
{
    if (m_motorSound)
        m_motorSound->SetOn(false);
    if (m_stopSound)
        m_stopSound->SetOn(true);
    m_flags &= ~SEAT_WEAPON_MOTOR_SOUND;
}

void C_SeatWeapon::ShowView(bool on)                    // 0x004c6d90
{
    if (!m_started || !m_view)
        return;
    m_view->SetOn(on);
    m_view->Update();
}

// --- the setup's helpers ----------------------------------------------------------------------------

int C_SeatWeapon::GetFrameInt(I3D_frame *frame, const char *key)   // 0x004c6570
{
    if (!frame)
        return -1;
    // Both lower-cased. (Fix: the whole properties are searched; the original copied them into
    // 256 bytes on the stack.)
    std::string props = frame->GetUserProps();
    std::string lowerKey = key;
    for (char &c : props)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char &c : lowerKey)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string::size_type at = props.find(lowerKey);
    return at == std::string::npos ? -1 : std::atoi(props.c_str() + at + lowerKey.size());
}

void C_SeatWeapon::LoadSounds()                         // 0x004c6e50 (__fastcall)
{
    m_motorSound = nullptr;
    m_stopSound = nullptr;
    int id = GetFrameInt(m_hort, "soundm");
    if (id >= 0) {
        const S_SoundGroup *group = GetSoundModule()->GetGroup(SOUND_CHAPTER_WEAPON_MANIPULATION, id);
        if (!group)
            return;                                     // (Quirk kept: no stopping sound then either)
        if (!group->samples.empty())
            m_motorSound = OpenHortSound(*group->samples[0], m_hort, true);
    }
    id = GetFrameInt(m_hort, "sounds");
    if (id < 0)
        return;
    const S_SoundGroup *group = GetSoundModule()->GetGroup(SOUND_CHAPTER_WEAPON_MANIPULATION, id);
    if (group && !group->samples.empty())
        m_stopSound = OpenHortSound(*group->samples[0], m_hort, false);
}

void C_SeatWeapon::ReleaseSounds()                      // 0x004c7630 (__fastcall)
{
    if (m_motorSound)
        m_motorSound->Release();
    m_motorSound = nullptr;
    if (m_stopSound)
        m_stopSound->Release();
    m_stopSound = nullptr;
}

int __stdcall C_SeatWeapon::OnBarrelNote(I3D_frame *frame, uint32_t message, uintptr_t note,
                                         uintptr_t time)   // 0x004c1990
{
    (void)message;
    (void)time;
    const S_FrameData *data = frame ? frame->GetData() : nullptr;
    auto *weapon = data ? reinterpret_cast<C_SeatWeapon *>(static_cast<uintptr_t>(data->unknown1C)) : nullptr;
    const char *text = reinterpret_cast<const char *>(note);
    if (text && weapon && *text == 'S')
        weapon->EjectShell(frame);
    return 0;
}

void C_SeatWeapon::EjectShell(I3D_frame *barrel)        // 0x004c5080
{
    if (!barrel)
        return;
    const S_matrix &matrix = barrel->GetMatrix();
    const S_vector pos = matrix(3);
    S_vector dir = matrix(2);
    NormalizeBy(dir, SquareYZX(dir));
    // Thrown up the barrel's y, scattered (x and z by up to half, y by up to double), at 3.
    S_vector up = matrix(1);
    up.x *= RandFloat() * 0.5f + 1.0f;
    up.y *= RandFloat() + 1.0f;
    up.z *= RandFloat() * 0.5f + 1.0f;
    NormalizeBy(up, SquareZYX(up));
    up *= 3.0f;
    // Fix: no shell without the shells model or its "SHELL" (the original calls through null).
    I3D_frame *source = m_shells ? m_shells->FindChildFrame("SHELL", FRAME_MASK_VISUAL) : nullptr;
    if (!source)
        return;
    I3D_frame *shell = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
    shell->Duplicate(source);
    I3D_scene *scene = GetGame()->GetScene();
    shell->LinkTo(scene->GetPrimarySector(), 0);
    scene->UpdateFrameSector(shell, shell->GetWorldPos());
    // Somewhere along the throw from the barrel.
    const float along = RandFloat();
    shell->SetPosInline(S_vector(pos.x + up.x * along, pos.y + up.y * along, pos.z + up.z * along));
    shell->SetWorldDir(dir, 0.0f);
    shell->SetOn(true);
    shell->Update();
    S_PhysicsObjectDesc desc;
    desc.frame = shell;
    desc.type = PHYS_SIMPLE;
    desc.callbackContext = this;
    desc.materialId = SHELL_MATERIAL;
    desc.startActive = true;
    desc.shootThrough = true;
    desc.radius = -1.0f;
    const T_PhysicsObject object = GetPhysicsModule()->CreateObject(desc);
    GetPhysicsModule()->ApplyImpulse(object, up, S_vector(0.0f, 0.0f, 0.0f), SHELL_IMPULSE_FACTOR);
    shell->Release();
}

// --- the turret (part 3b) ---------------------------------------------------------------------------

void C_SeatWeapon::Update(int time)                     // 0x004c56a0
{
    if (m_reloadLeft != 0) {
        m_reloadLeft -= time;
        if (m_reloadLeft < 1)
            m_reloadLeft = 0;
    }
    if (m_failed || !m_gunner)
        return;
    if (m_gunner->IsDead()) {
        if (m_flags & SEAT_WEAPON_MOTOR_SOUND)
            StopMotorSound();
        return;
    }
    if (m_gunner->IsLocalPlayer())
        TurnByControls(time);
    else
        TurnToView(time);
    if (m_flags & SEAT_WEAPON_TURNING)
        m_model->Update();
    if (!(g_gameRole & GAME_ROLE_SERVER))
        Tick(time);
    if (m_turnReleased && m_tiltReleased)
        m_idleTime += time;
    else
        m_idleTime = 0;
}

void C_SeatWeapon::Tick(int time)                       // 0x004c55e0
{
    if (m_failed || !m_gunner)
        return;
    if (m_gunner->IsDead()) {
        if (m_flags & SEAT_WEAPON_MOTOR_SOUND)
            StopMotorSound();
        return;
    }
    if (!(g_gameRole & GAME_ROLE_CLIENT))
        Update(time);
    if (m_delay != 0) {
        m_delay -= time;
        if (m_delay < 1) {
            m_flags &= ~SEAT_WEAPON_RELOADING;
            m_delay = 0;
        }
    }
}

void C_SeatWeapon::TurnByControls(int time)             // 0x004c3120
{
    // (The angles in doubles where the original keeps them on the x87 stack: atan2, sin, cos.)
    bool turned = false;
    if (m_turnSpeed != 0.0f) {
        // The traverse: HORT's heading moved by the speed, within the side limits (1e6: none).
        m_hort->Update();
        S_vector dir = LocalDirYZX(m_hort);
        const float flat = std::sqrt(dir.z * dir.z + dir.x * dir.x);
        const double heading = std::atan2(-static_cast<double>(dir.x), static_cast<double>(dir.z));
        const float step = static_cast<float>(time) * m_turnSpeed * 0.001f;
        float next = static_cast<float>(heading - static_cast<double>(step));
        if (m_rightLimit != NO_LIMIT && next > m_rightLimit)
            next = m_rightLimit;
        else if (m_leftLimit != NO_LIMIT && next < m_leftLimit)
            next = m_leftLimit;
        if (static_cast<double>(next) != heading) {
            dir.x = -static_cast<float>(static_cast<double>(flat) * std::sin(static_cast<double>(next)));
            dir.z = static_cast<float>(std::cos(static_cast<double>(next)) * static_cast<double>(flat));
            NormalizeBy(dir, SquareYZX(dir));
            m_hort->SetWorldDir(dir, 0.0f);
            m_hort->Update();
            turned = true;
            m_turn = next;
        }
        if (m_turnReleased && m_turnSpeed != 0.0f) {
            const float slowing = static_cast<float>(time * TRAVERSE_SLOWING) * 0.001f;
            if (m_turnSpeed > 0.0f) {
                m_turnSpeed -= slowing;
                if (m_turnSpeed < 0.0f)
                    m_turnSpeed = 0.0f;
            } else if (m_turnSpeed < 0.0f) {
                m_turnSpeed = slowing + m_turnSpeed;
                if (m_turnSpeed > 0.0f)
                    m_turnSpeed = 0.0f;
            }
        }
    }
    if (m_tiltSpeed != 0.0f) {
        // The elevation: VERT's moved by the speed, within the limits and +-MAX_ELEVATION, along its
        // own heading (VERT not updated after). (Quirk kept: unlike the traverse's, the limits are
        // not tested for NO_LIMIT: a barrel without "down" would hold the elevation at its top; the
        // shipped barrels have one.)
        m_vert->Update();
        const S_vector dir = LocalDirYZX(m_vert);
        const double heading = std::atan2(-static_cast<double>(dir.x), static_cast<double>(dir.z));
        const float flat = std::sqrt(dir.z * dir.z + dir.x * dir.x);
        const double elevation = std::atan2(static_cast<double>(dir.y), static_cast<double>(flat));
        const float seconds = static_cast<float>(time);
        float next = static_cast<float>(elevation - static_cast<double>(seconds * m_tiltSpeed * 0.001f));
        if (next > m_upLimit)
            next = m_upLimit;
        else if (next < m_downLimit)
            next = m_downLimit;
        if (static_cast<double>(next) != elevation) {
            turned = true;
            next = std::max(std::min(next, MAX_ELEVATION), -MAX_ELEVATION);
            const double across = std::cos(static_cast<double>(next));
            S_vector up;
            up.x = -static_cast<float>(std::sin(heading) * across);
            up.y = static_cast<float>(std::sin(static_cast<double>(next)));
            up.z = static_cast<float>(std::cos(heading) * across);
            m_vert->SetWorldDir(up, 0.0f);
            m_tilt = next;
        }
        if (m_tiltReleased && m_tiltSpeed != 0.0f) {
            const float slowing = seconds * m_tiltRate * ELEVATION_SLOWING;
            if (m_tiltSpeed > 0.0f) {
                m_tiltSpeed -= slowing;
                if (m_tiltSpeed < 0.0f)
                    m_tiltSpeed = 0.0f;
            } else if (m_tiltSpeed < 0.0f) {
                m_tiltSpeed = slowing + m_tiltSpeed;
                if (m_tiltSpeed > 0.0f)
                    m_tiltSpeed = 0.0f;
            }
        }
    }
    if (turned) {
        if (!(m_flags & SEAT_WEAPON_MOTOR_SOUND))
            StartMotorSound();
    } else if ((m_flags & SEAT_WEAPON_MOTOR_SOUND) && m_idleTime > MOTOR_STOP_IDLE) {
        StopMotorSound();
    }
    // The gunner looks along the barrel.
    m_model->Update();
    S_vector aim = m_vert->GetMatrix()(2);
    const float aimW = m_vert->GetMatrix().m[2][3];
    NormalizeBy(aim, SquareYZX(aim));
    m_gunner->SetViewDir(S_vector4{aim.x, aim.y, aim.z, aimW});
}

void C_SeatWeapon::TurnToView(int time)                 // 0x004c2580
{
    // (The angles in doubles where the original keeps them on the x87 stack.)
    bool turned = false;
    const S_vector view = m_gunner->GetViewDir();
    const S_vector aim = m_vert->GetWorldDir();
    m_flags &= ~SEAT_WEAPON_TURNING;
    float viewHeading = static_cast<float>(std::atan2(-static_cast<double>(view.x), static_cast<double>(view.z)));
    const float viewElevation = static_cast<float>(std::atan2(
        static_cast<double>(view.y), static_cast<double>(std::sqrt(view.z * view.z + view.x * view.x))));
    float aimHeading = static_cast<float>(std::atan2(-static_cast<double>(aim.x), static_cast<double>(aim.z)));
    const float aimElevation = static_cast<float>(std::atan2(
        static_cast<double>(aim.y), static_cast<double>(std::sqrt(aim.x * aim.x + aim.z * aim.z))));
    aimHeading = WrapAngle(aimHeading);
    viewHeading = WrapAngle(viewHeading);
    // The traverse: towards the view's heading the shorter way, at most the rate's step. (Quirk kept:
    // across heading 0 the gap is measured the long way round, so a step longer than that, a frame of
    // over 2 pi / rate seconds, overshoots.)
    const float headingGap = std::fabs(aimHeading - viewHeading);
    if (static_cast<double>(headingGap) > MIN_TURN) {
        m_hort->Update();
        S_vector dir = m_hort->GetLocalDir();
        float step = static_cast<float>(time) * m_turnRate * 0.001f;
        if (!(step <= headingGap))
            step = headingGap;
        if (WrapAngle(viewHeading - aimHeading) < PI)
            step *= -1.0f;
        const float flat = std::sqrt(dir.z * dir.z + dir.x * dir.x);
        const double heading = std::atan2(-static_cast<double>(dir.x), static_cast<double>(dir.z));
        const float headingNow = static_cast<float>(heading);
        float next = static_cast<float>(heading - static_cast<double>(step));
        if (m_rightLimit != NO_LIMIT && next > m_rightLimit)
            next = m_rightLimit;
        else if (m_leftLimit != NO_LIMIT && next < m_leftLimit)
            next = m_leftLimit;
        const float change = next - headingNow;
        if (static_cast<double>(std::fabs(change)) > MIN_TURN) {
            m_flags |= change > 0.0f ? SEAT_WEAPON_TRAVERSE_PLUS : SEAT_WEAPON_TRAVERSE_MINUS;
            dir.x = -static_cast<float>(std::sin(static_cast<double>(next)) * static_cast<double>(flat));
            dir.z = static_cast<float>(std::cos(static_cast<double>(next)) * static_cast<double>(flat));
            NormalizeBy(dir, SquareYZX(dir));
            m_hort->SetWorldDir(dir, 0.0f);
            m_hort->Update();
            turned = true;
            m_turn = next;
        }
    }
    // The elevation: towards the view's, at most the rate's step, VERT straight up its local z.
    const float elevationGap = std::fabs(aimElevation - viewElevation);
    if (static_cast<double>(elevationGap) > MIN_TURN) {
        m_vert->Update();
        const S_vector dir = m_vert->GetLocalDir();
        float step = static_cast<float>(time) * m_tiltRate * 0.001f;
        if (!(step <= elevationGap))
            step = elevationGap;
        if (aimElevation < viewElevation)
            step *= -1.0f;
        const double elevation = std::atan2(static_cast<double>(dir.y),
                                            static_cast<double>(std::sqrt(dir.z * dir.z + dir.x * dir.x)));
        const float elevationNow = static_cast<float>(elevation);
        float next = static_cast<float>(elevation - static_cast<double>(step));
        if (next > m_upLimit)
            next = m_upLimit;
        else if (next < m_downLimit)
            next = m_downLimit;
        const float change = next - elevationNow;
        if (static_cast<double>(std::fabs(change)) > MIN_TURN) {
            m_flags |= change > 0.0f ? SEAT_WEAPON_ELEVATION_UP : SEAT_WEAPON_ELEVATION_DOWN;
            turned = true;
            next = std::max(std::min(next, MAX_ELEVATION), -MAX_ELEVATION);
            S_vector up(0.0f, static_cast<float>(std::sin(static_cast<double>(next))),
                        static_cast<float>(std::cos(static_cast<double>(next))));
            NormalizeBy(up, up.y * up.y + up.z * up.z);
            m_vert->SetWorldDir(up, 0.0f);
            m_vert->Update();
            m_tilt = next;
        }
    }
    if (m_flags & SEAT_WEAPON_TURNING)
        m_idleTime = 0;
    else
        m_idleTime += time;
    if (turned) {
        if (!(m_flags & SEAT_WEAPON_MOTOR_SOUND))
            StartMotorSound();
    } else if ((m_flags & SEAT_WEAPON_MOTOR_SOUND) && m_idleTime > MOTOR_STOP_IDLE) {
        StopMotorSound();
    }
    m_model->Update();
}

void C_SeatWeapon::ApplyControls(float *axes, float *buttons)   // 0x004c3b60
{
    if (!m_gunner)
        return;
    // The speeds: a launcher's controls add to them while held, the others set them; within the
    // rates.
    const float traverse = axes[AXIS_SEAT_TRAVERSE];
    const float elevation = axes[AXIS_SEAT_ELEVATION];
    m_turnReleased = traverse == 0.0f;
    if (!m_launcher)
        m_turnSpeed = traverse * CONTROL_SPEED;
    else if (traverse != 0.0f)
        m_turnSpeed = traverse + m_turnSpeed;
    if (m_turnRate < std::fabs(m_turnSpeed))
        m_turnSpeed = m_turnSpeed <= 0.0f ? -m_turnRate : m_turnRate;
    m_tiltReleased = elevation == 0.0f;
    if (!m_launcher)
        m_tiltSpeed = CONTROL_SPEED * elevation;
    else if (elevation != 0.0f)
        m_tiltSpeed = elevation + m_tiltSpeed;
    if (m_tiltRate < std::fabs(m_tiltSpeed))
        m_tiltSpeed = m_tiltSpeed <= 0.0f ? -m_tiltRate : m_tiltRate;
    // Firing; the trigger of an empty weapon reloads it, and one just emptied is reloaded when every
    // seat of the owner is taken (or it has none).
    if (IsEmpty()) {
        if (buttons[BUTTON_FIRE] != 0.0f)
            StartReload(m_owner->GetContainer(0));
    } else if (buttons[BUTTON_FIRE] != 0.0f || axes[AXIS_FIRE] != 0.0f) {
        Fire();
        if (IsEmpty()) {
            bool crewed = true;
            for (int seat = m_owner->GetSeatCount(); seat-- > 0;) {
                if (!m_owner->GetSeatOccupant(seat))
                    crewed = false;
            }
            if (crewed)
                StartReload(m_owner->GetContainer(0));
        }
    }
    if (axes[AXIS_RELOAD] != 0.0f)
        StartReload(m_owner->GetContainer(0));
}

// --- firing (part 3b) -------------------------------------------------------------------------------

void C_SeatWeapon::Fire()                               // 0x004c4780 (__fastcall)
{
    if (m_delay > 0 || m_item->GetAmount() == 0.0f || !m_gunner)
        return;
    // (The muzzle's world position as the matrix holds it when the shot leaves: the barrel's
    // animation may move it.)
    I3D_frame *muzzle = m_barrels[m_barrel].muzzle;
    const S_vector &pos = muzzle->GetMatrix()(3);
    S_vector dir;
    float dirW;
    bool alongMuzzle = true;
    if (m_gunner->IsLocalPlayer() && !GetGame()->IsFirstPersonView()) {
        // Third person: at what the camera looks at, unless that is within 3 m of the muzzle;
        // nothing met: along the camera.
        S_vector4 cameraPos, cameraDir;
        GetGame()->GetCameraModule()->GetCameraPos(0, &cameraPos);
        GetGame()->GetCameraModule()->GetCameraDir(0, &cameraDir);
        const S_vector ray(cameraDir.x * AIM_RAY_LENGTH, cameraDir.y * AIM_RAY_LENGTH, cameraDir.z * AIM_RAY_LENGTH);
        const S_vector from(cameraDir.x * AIM_RAY_START + cameraPos.x, cameraPos.y + cameraDir.y * AIM_RAY_START,
                            cameraPos.z + cameraDir.z * AIM_RAY_START);
        S_vector hit;
        if (!g_collisionWorld.RayTest(from, ray, &hit, nullptr, AIM_RAY_EXCLUDE_TYPES, AIM_RAY_EXCLUDE_FLAGS)) {
            dir = XYZ(cameraDir);
            dirW = cameraDir.w;
            alongMuzzle = false;
        } else {
            const S_vector to(hit.x - pos.x, hit.y - pos.y, hit.z - pos.z);
            const float square = SquareXZY(to);
            if (square > AIM_MIN_SQUARE) {
                dir = to;
                dirW = 0.0f;                            // (the original's is not set; the message sends none)
                NormalizeBy(dir, square);
                alongMuzzle = false;
            }
        }
    }
    if (alongMuzzle) {
        dir = muzzle->GetWorldDir();
        dirW = muzzle->GetMatrix().m[2][3];
    }
    // Fix: an item without a component neither spreads nor waits (the original reads through null).
    auto *component = static_cast<C_WeaponComp *>(m_item->GetDef()->GetComponent());
    SpreadDirection(dir, component ? component->GetAimedDispersion() : 0.0f);
    Shoot(m_barrel, pos, dir, true);
    if (m_launcher)
        Recoil(dir, FIRE_RECOIL);
    if (m_sender) {
        const S_vector4 pos4 = {pos.x, pos.y, pos.z, muzzle->GetMatrix().m[3][3]};
        const S_vector4 dir4 = {dir.x, dir.y, dir.z, dirW};
        m_sender(SEAT_WEAPON_MSG_SHOT, &pos4, &dir4, m_barrel, m_seat, m_senderContext);
    }
    m_delay = component ? static_cast<int>(component->GetAttackDelay()) : 0;
    ShowFlash(m_barrel);
    m_item->Use(1.0f);
    if (static_cast<size_t>(++m_barrel) >= m_barrels.size())
        m_barrel = 0;
    if ((g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL && g_teamManager.GetPlayerTeam()->IsMember(m_gunner))
        m_gunner->SetDisguise(FIRE_DISGUISE);
    m_gunner->MakeNoise();
}

bool C_SeatWeapon::FireShot(const S_vector4 *pos, const S_vector4 *dir, int barrel, int unused)   // 0x004c3dc0
{
    (void)unused;
    if (m_item->GetAmount() == 0.0f || !m_gunner)
        return false;
    // Fix: a barrel the weapon does not have is refused (the message's 2 bits index them; the
    // original reads past the barrels).
    if (barrel < 0 || static_cast<size_t>(barrel) >= m_barrels.size())
        return false;
    Shoot(barrel, XYZ(*pos), XYZ(*dir), false);
    ShowFlash(barrel);
    m_item->Use(1.0f);
    if (m_launcher)
        Recoil(XYZ(*dir), FIRE_SHOT_RECOIL);
    return true;
}

void C_SeatWeapon::PlayShot(int barrel)                 // 0x004c4010
{
    (void)barrel;                                       // (Quirk kept: the client fires its own next barrel)
    if (!m_gunner)
        return;
    I3D_frame *muzzle = m_barrels[m_barrel].muzzle;
    const S_vector pos = muzzle->GetMatrix()(3);
    S_vector dir = muzzle->GetMatrix()(2);
    NormalizeBy(dir, SquareYZX(dir));
    Shoot(m_barrel, pos, dir, false);
    ShowFlash(m_barrel);
    m_item->Use(1.0f);
    if (m_launcher)
        Recoil(dir, PLAY_SHOT_RECOIL);
    if (static_cast<size_t>(++m_barrel) >= m_barrels.size())
        m_barrel = 0;
}

void C_SeatWeapon::Shoot(int barrel, const S_vector &pos, const S_vector &dir, bool cameraFollow)
{
    const S_SeatWeaponBarrel &shot = m_barrels[barrel];
    if (shot.animation) {
        static_cast<I3D_model *>(m_model)->SetAnimation(shot.animation, barrel, 1.0f, 0);
        m_model->Update();
    }
    if (m_launcher)
        g_shotManager.FireProjectile(m_gunner, m_item, pos, dir, m_projectileFrame, -1, true, cameraFollow, 0);
    else
        g_shotManager.FireBulletFrom(m_gunner, m_item, pos, dir, shot.muzzle, shot.ejector, true, 0,
                                     m_item->GetDef()->GetShellSound());
    if (g_gameRole & GAME_ROLE_CLIENT)
        m_item->PlayShotSound(m_gunner, m_barrels[barrel].muzzle, false);
}

void C_SeatWeapon::ShowFlash(int barrel)
{
    I3D_frame *muzzle = m_barrels[barrel].muzzle;
    g_particlesModule.Create(m_flashEffect, muzzle->GetMatrix(), muzzle, true, false);
}

void C_SeatWeapon::Recoil(const S_vector &dir, float factor)
{
    if (m_owner->GetType() != ACTOR_CAR)
        return;
    auto *car = static_cast<C_Car *>(m_owner);
    C_Vehicle *vehicle = car->GetVehicle();
    const float mass = vehicle->GetCurrentMass();
    const S_vector impulse(dir.x * mass * factor, dir.y * mass * factor, dir.z * mass * factor);
    if (!car->IsSimulatedHere())
        return;
    // Flag 1: no push, only the turn (about the vehicle's x and z). (Quirk kept: at the lever origin
    // itself there is no turn either: the vehicle is only woken up.)
    vehicle->ApplyImpulse(impulse, XYZ(vehicle->GetUnknown1B0()), 1, S_vector(1.0f, 0.0f, 1.0f));
}

// --- reloading (part 3b) ----------------------------------------------------------------------------

void C_SeatWeapon::StartReload(C_ItemContainer *store)  // 0x004c6400
{
    (void)store;
    if (m_flags & SEAT_WEAPON_RELOADING)
        return;
    // From the owner's container 0; an enemy gunner keeps his last magazine.
    int ammoUid;
    if (!m_item->Reload(m_owner, 0, &ammoUid, m_gunner->GetRole() == HUMAN_ROLE_ENEMY))
        return;
    m_flags |= SEAT_WEAPON_RELOADING;
    const int time = static_cast<C_WeaponComp *>(m_item->GetDef()->GetComponent())->GetReloadTimeMs();
    m_delay = time;
    m_reloadLeft = time;
    m_reloadTime = time;
    if (g_gameRole & GAME_ROLE_CLIENT)
        PlayReloadSound();
    if (m_sender)
        m_sender(SEAT_WEAPON_MSG_RELOAD, nullptr, nullptr, 0, m_seat, m_senderContext);
}

void C_SeatWeapon::Reload(C_ItemContainer *store)       // 0x004c6270
{
    (void)store;
    if (m_flags & SEAT_WEAPON_RELOADING)
        return;
    // Fix: without a gunner (he got out meanwhile) not an enemy's (the original reads through null).
    int ammoUid;
    if (!m_item->Reload(m_owner, 0, &ammoUid, m_gunner && m_gunner->GetRole() == HUMAN_ROLE_ENEMY))
        return;
    m_flags |= SEAT_WEAPON_RELOADING;
    m_delay = static_cast<C_WeaponComp *>(m_item->GetDef()->GetComponent())->GetReloadTimeMs();
}

void C_SeatWeapon::ShowReload(C_ItemContainer *store)   // 0x004c62e0
{
    (void)store;
    if (m_flags & SEAT_WEAPON_RELOADING)
        return;
    // (Quirk kept: the item's result does not matter. Fix: no gunner, as Reload.)
    int ammoUid;
    m_item->Reload(m_owner, 0, &ammoUid, m_gunner && m_gunner->GetRole() == HUMAN_ROLE_ENEMY);
    m_flags |= SEAT_WEAPON_RELOADING;
    const int time = static_cast<C_WeaponComp *>(m_item->GetDef()->GetComponent())->GetReloadTimeMs();
    m_delay = time;
    m_reloadLeft = time;
    m_reloadTime = time;
    if (g_gameRole & GAME_ROLE_CLIENT)
        PlayReloadSound();
}

void C_SeatWeapon::PlayReloadSound()
{
    S_SoundRequest request;
    request.flags = SOUND_SILENT_TO_AI;
    request.attachTo = m_barrels[0].frame;
    request.chapter = SOUND_CHAPTER_WEAPON_MANIPULATION;
    request.group = static_cast<C_WeaponComp *>(m_item->GetDef()->GetComponent())->GetReloadSound();
    GetSoundModule()->Play(request);
}

// --- aiming (part 3b) -------------------------------------------------------------------------------

bool C_SeatWeapon::CanAim(float *pos)                   // 0x004c6230
{
    const S_vector &target = *reinterpret_cast<const S_vector *>(pos);
    return !m_failed && CanTiltTo(target) && CanTurnTo(target);
}

bool C_SeatWeapon::CanTurnTo(const S_vector &pos)       // 0x004c5a30
{
    // Without a right limit the traverse goes all round.
    if (m_rightLimit == NO_LIMIT)
        return true;
    // The angle from VERT's heading to the target's (flat; signed by VERT's x axis), from the turn.
    const S_matrix &matrix = m_vert->GetMatrix();
    S_vector to(pos.x - matrix(3).x, 0.0f, pos.z - matrix(3).z);
    NormalizeBy(to, to.x * to.x + to.z * to.z);
    S_vector aim = m_vert->GetMatrix()(2);
    NormalizeBy(aim, SquareYZX(aim));
    S_vector heading(aim.x, 0.0f, aim.z);
    NormalizeBy(heading, heading.z * heading.z + heading.x * heading.x);
    float angle = heading.AngleTo(to);
    const S_matrix &axes = m_vert->GetMatrix();
    S_vector side(axes.m[0][0], 0.0f, axes.m[0][2]);
    NormalizeBy(side, side.z * side.z + side.x * side.x);
    if ((to.z * side.z + side.x * to.x) + side.y * to.y < 0.0f)
        angle *= -1.0f;
    const float turn = angle + m_turn;
    return turn < m_rightLimit && turn > m_leftLimit;
}

bool C_SeatWeapon::CanTiltTo(const S_vector &pos)       // 0x004c5f60
{
    // The target's elevation seen from VERT, against the barrel's less its tilt. (Quirk kept: a
    // barrel without "down" (NO_LIMIT) has nothing in reach.)
    const S_vector &at = m_vert->GetMatrix()(3);
    S_vector to(pos.x - at.x, pos.y - at.y, pos.z - at.z);
    NormalizeBy(to, SquareYZX(to));
    const float target = static_cast<float>(
        std::atan2(static_cast<double>(to.y), static_cast<double>(std::sqrt(to.z * to.z + to.x * to.x))));
    S_vector aim = m_vert->GetMatrix()(2);
    NormalizeBy(aim, SquareYZX(aim));
    const double elevation =
        std::atan2(static_cast<double>(aim.y), static_cast<double>(std::sqrt(aim.z * aim.z + aim.x * aim.x)));
    const float tilt = target - static_cast<float>(elevation - static_cast<double>(m_tilt));
    return tilt < m_upLimit && tilt > m_downLimit;
}

// --- the accessors ----------------------------------------------------------------------------------

C_ItemDef *C_SeatWeapon::GetItemDef()                   // 0x004c57b0 (__fastcall)
{
    return m_failed ? nullptr : m_item->GetDef();
}

int C_SeatWeapon::GetItemText()                         // 0x004c57d0 (__fastcall)
{
    return m_failed ? -1 : m_item->GetDef()->GetTextId();
}

S_vector C_SeatWeapon::GetOrigin()                      // 0x004c5800
{
    return m_failed ? S_vector(0.0f, 0.0f, 0.0f) : m_vert->GetWorldPos();
}

S_vector C_SeatWeapon::GetAim()                         // 0x004c5870
{
    if (m_failed)
        return S_vector(0.0f, 0.0f, 0.0f);
    S_vector dir = m_vert->GetMatrix()(2);
    NormalizeBy(dir, SquareYZX(dir));
    return dir;
}

int C_SeatWeapon::GetRounds()                           // 0x004c6550 (__fastcall)
{
    return static_cast<int>(m_item->GetAmount());
}

void C_SeatWeapon::SetRounds(int rounds)                // 0x004c6560
{
    m_item->SetLoadedRounds(static_cast<float>(rounds));
}

bool C_SeatWeapon::IsEmpty()                            // 0x004c3100 (__fastcall)
{
    return m_item->GetAmount() == 0.0f;
}

bool C_SeatWeapon::UnknownC8(int *left, int *total, int *text)   // 0x004c7670
{
    if (!m_reloadLeft)
        return false;
    *left = m_reloadLeft;
    *total = m_reloadTime;
    *text = RELOAD_TEXT;
    return true;
}

// (Inline in the original.)
void C_SeatWeapon::GetLimits(float *up, float *down, float *left, float *right) const
{
    *up = m_upLimit;
    *down = m_downLimit;
    *left = m_leftLimit;
    *right = m_rightLimit;
}

uint32_t C_SeatWeapon::GetRefillTime() const
{
    return m_refillTime;
}

void C_SeatWeapon::SetRefillTime(uint32_t time)
{
    m_refillTime = time;
}

int C_SeatWeapon::GetRefillBelow() const
{
    return m_refillBelow;
}

void C_SeatWeapon::SetRefillBelow(int rounds)
{
    m_refillBelow = rounds;
}

void C_SeatWeapon::SetMessageSender(T_MessageSender sender, C_Car *context)
{
    m_sender = sender;
    m_senderContext = context;
}

// --- the saved game ---------------------------------------------------------------------------------

bool C_SeatWeapon::SaveGame(C_chunk &chunk)             // 0x004c6660
{
    chunk.Begin(CHUNK_SEAT_WEAPON_STATE);
    chunk.Write(m_tilt);
    chunk.Write(m_turn);
    chunk.Write(m_flags);
    chunk.Write(m_barrel);
    chunk.Write(m_delay);
    chunk.Write(m_reloadLeft);
    chunk.Write(m_reloadTime);
    chunk.Ascend();
    chunk.Begin(CHUNK_SEAT_WEAPON_ROUNDS);
    const float rounds = m_item->GetAmount();
    chunk.Write(rounds);
    chunk.Ascend();
    return true;
}

bool C_SeatWeapon::LoadGame(C_chunk &chunk)             // 0x004c68b0
{
    // (A header that cannot be read ends the loading; the original handles the chunk before's id
    // again and leaves the outer chunk.)
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        if (id == CHUNK_SEAT_WEAPON_STATE) {
            chunk.Read(m_tilt);
            chunk.Read(m_turn);
            chunk.Read(m_flags);
            chunk.Read(m_barrel);
            chunk.Read(m_delay);
            chunk.Read(m_reloadLeft);
            chunk.Read(m_reloadTime);
        } else if (id == CHUNK_SEAT_WEAPON_ROUNDS) {
            // Fix: a short chunk leaves the rounds as they are (the original sets them to whatever
            // its stack held).
            float rounds;
            if (chunk.Read(rounds))
                m_item->SetLoadedRounds(rounds);
        }
        chunk.Ascend();
    }
    // The turret turned by the angles read, from where it is (at rest after Init).
    S_quat turn;
    turn.Make(S_vector(0.0f, -1.0f, 0.0f), m_turn);
    m_hort->SetRot(QuatMultiply(m_hort->GetRot(), turn));
    m_hort->Update();
    S_quat tilt;
    tilt.Make(S_vector(1.0f, 0.0f, 0.0f), m_tilt);
    m_vert->SetRot(QuatMultiply(m_vert->GetRot(), tilt));
    m_vert->Update();
    if (m_aim) {
        m_aim->SetRot(m_vert->GetRot());
        m_aim->Update();
    }
    m_model->Update();
    return true;
}

// --- the network messages ---------------------------------------------------------------------------

bool __fastcall SeatWeaponSendMessage(int kind, const S_vector4 *pos, const S_vector4 *dir, int barrel, int seat,
                                      C_Car *car)       // 0x004c1230
{
    C_NetObjectMessage *message;
    if (kind == SEAT_WEAPON_MSG_SHOT) {
        auto *shot = new C_SeatWeaponShotMessage;       // _aligned_malloc(0x60, 16) in the original
        shot->seat = seat;
        shot->barrel = barrel;
        shot->pos = *pos;
        shot->dir = *dir;
        message = shot;
    } else if (kind == SEAT_WEAPON_MSG_RELOAD) {
        auto *reload = new C_SeatWeaponReloadMessage;   // _aligned_malloc(0x34, 16)
        reload->seat = seat;
        message = reload;
    } else {
        return true;
    }
    message->fromClient = !(g_gameRole & GAME_ROLE_SERVER);
    C_NetObject *object = g_netReplication->FindObject(car->GetNetId(), false);
    // Fix: no net object, no message (the original queues it through null).
    if (!object) {
        delete message;
        return true;
    }
    object->QueueMessage(message, g_netReplication->GetLocalId(), true);
    return true;
}

C_NetObjectMessage *__fastcall MakeSeatWeaponMessage(void *car, C_BitStream *stream, bool write)   // 0x004c14b0
{
    (void)car;
    if (write) {
        stream->WriteBits(MSG_TYPE_COUNT, MSG_TYPE_BITS);
        return nullptr;
    }
    switch (stream->ReadBits(MSG_TYPE_BITS)) {
    case SEAT_WEAPON_MSG_SHOT:
        return new C_SeatWeaponShotMessage;
    case SEAT_WEAPON_MSG_RELOAD:
        return new C_SeatWeaponReloadMessage;
    case SEAT_WEAPON_MSG_ROUNDS:
        return new C_SeatWeaponRoundsMessage;
    case SEAT_WEAPON_MSG_AMMO:
        return new C_SeatWeaponAmmoMessage;
    default:
        return nullptr;
    }
}

void C_SeatWeaponShotMessage::Write(C_BitStream *stream)   // 0x004c1760
{
    stream->WriteBits(static_cast<uint32_t>(type), MSG_TYPE_BITS);
    stream->WriteBits(static_cast<uint32_t>(seat), 3);
    stream->WriteBits(static_cast<uint32_t>(barrel), 2);
    // A client's to the server: where from and which way (the server's copy for the clients has none).
    if (!(g_gameRole & GAME_ROLE_SERVER)) {
        stream->WriteBits(FloatBits(dir.x), 32);
        stream->WriteBits(FloatBits(dir.y), 32);
        stream->WriteBits(FloatBits(dir.z), 32);
        auto *msg = static_cast<C_NetMessage *>(stream);
        msg->WriteQuantized(&pos.x, msg->GetSender(-1), 7);
    }
}

void C_SeatWeaponShotMessage::Read(C_BitStream *stream)    // 0x004c1810
{
    seat = static_cast<int>(stream->ReadBits(3));
    barrel = static_cast<int>(stream->ReadBits(2));
    if (g_gameRole & GAME_ROLE_SERVER) {
        dir.x = BitsFloat(stream->ReadBits(32));
        dir.y = BitsFloat(stream->ReadBits(32));
        dir.z = BitsFloat(stream->ReadBits(32));
        auto *msg = static_cast<C_NetMessage *>(stream);
        msg->ReadQuantized(&pos.x, msg->GetSender(-1), 7);
    }
}

void C_SeatWeaponReloadMessage::Write(C_BitStream *stream)   // 0x004c1920
{
    stream->WriteBits(static_cast<uint32_t>(type), MSG_TYPE_BITS);
    stream->WriteBits(static_cast<uint32_t>(seat), 3);
}

void C_SeatWeaponReloadMessage::Read(C_BitStream *stream)    // 0x004c1970
{
    seat = static_cast<int>(stream->ReadBits(3));
}

void C_SeatWeaponRoundsMessage::Write(C_BitStream *stream)   // 0x004c1890
{
    stream->WriteBits(static_cast<uint32_t>(type), MSG_TYPE_BITS);
    stream->WriteBits(static_cast<uint32_t>(seat), 3);
    stream->WriteBits(static_cast<uint32_t>(rounds), 10);
}

void C_SeatWeaponRoundsMessage::Read(C_BitStream *stream)    // 0x004c18f0
{
    seat = static_cast<int>(stream->ReadBits(3));
    rounds = static_cast<int>(stream->ReadBits(10));
}

void C_SeatWeaponAmmoMessage::Write(C_BitStream *stream)     // 0x004c16d0
{
    stream->WriteBits(static_cast<uint32_t>(type), MSG_TYPE_BITS);
    stream->WriteBits(static_cast<uint32_t>(uid), 16);
    stream->WriteBits(static_cast<uint32_t>(typeId), 8);
}

void C_SeatWeaponAmmoMessage::Read(C_BitStream *stream)      // 0x004c1730
{
    uid = static_cast<int16_t>(stream->ReadBits(16));
    typeId = static_cast<int>(stream->ReadBits(8));
}
