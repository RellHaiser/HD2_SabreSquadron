// Trains (C_Train; original 0x005bbdb0..0x005c0ce0: the actor 0x005bbdb0..0x005befb0, the saved
// game and the movement 0x005befb0..).
#include "game/train.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/frame_volume.h"
#include "game/game.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// ReadProperties' chunks.
constexpr uint16_t PROPERTY_VERSION = 1;
constexpr uint16_t PROPERTY_NAME = 8;
constexpr uint16_t PROPERTY_WAGON = 10;
constexpr uint16_t PROPERTY_WAGON_LENGTH = 0x14;
constexpr uint16_t PROPERTY_WAGON_GAP = 0x1E;
// The saved game's chunks.
constexpr uint16_t CHUNK_RUNNING = 1000;
constexpr uint16_t CHUNK_STOPPED = 2000;

// The wagons' boxes.
constexpr uint32_t WAGON_VOLUME_FLAGS = 0x40;
constexpr uint32_t WAGON_VOLUME_TYPE = 0x2000;
// HitHuman's sweep: what it hits (types) and leaves out (flags).
constexpr uint32_t SWEEP_TYPES = 0x2002;
constexpr uint32_t SWEEP_FLAGS = 0xFFFFFFFF;

constexpr float WHISTLE_NEAR_RANGE = 48.0f;             // m from the camera: the near whistle
constexpr float MIN_MOVE = 0.001f;                      // m: below, a move without force does nothing
constexpr float SPEED_TO_REACH = 1.38888896f;           // km/h to the m the checkpoints are marked ahead (5 s)
constexpr float PI_F = 3.14159274f;

// The wheels' volume: 0 up to 1 km/h, rising by 0.1 per km/h, 0.9 from 10 km/h.
constexpr float WHEEL_SOUND_MIN_SPEED = 1.0f;
constexpr float WHEEL_SOUND_FULL_SPEED = 10.0f;
constexpr float WHEEL_SOUND_FULL_VOLUME = 0.9f;
constexpr float BRAKE_FADE_RATE = 0.0005f;              // per ms

// The crossheads' last place along the slide and whether they were going forward (0x008ac5b4,
// 0x008ac5b0): shared by all the trains (quirk kept).
float g_crossheadZ = 0.0f;
bool g_crossheadForward = false;

// The trains' creator (0x008ac5ac).
C_TrainCreator g_trainCreator;

// The length from a square the caller summed, as the SDK's inlined helper: 0 when its bits are
// below 1e-8's, exactly 1 for a near-unit square, the square root otherwise.
float LengthOf(float square)
{
    int32_t bits;
    std::memcpy(&bits, &square, sizeof bits);
    if (bits < 0x322bcc77)
        return 0.0f;
    if (!(std::fabs(square - 1.0f) < 1e-08f))
        return std::sqrt(square);
    return 1.0f;
}

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

// A positional sound `name` linked to `parent` at `pos`, its range set, playing (`on`) or not;
// its frame in the sound module to *frame. Null when the sound driver is off or the file is
// missing.
I3D_sound *LoadSound(const char *name, I3D_frame *parent, const S_vector &pos, bool loop, bool on,
                     float nearRange, float farRange, S_SoundFrame **frame)   // 0x005bc830
{
    auto *sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    // Fix: no frame (the original releases it through null).
    if (!sound)
        return nullptr;
    const HRESULT result =
        ISndGetDriver()->IsInitialized() ? g_soundCache.OpenInto(sound, name) : static_cast<HRESULT>(0x80000009);
    if (result != I3D_OK) {
        sound->Release();
        return nullptr;
    }
    sound->LinkTo(parent, 0);
    sound->SetPosInline(pos);
    sound->SetSoundType(I3DSOUND_POINT);
    sound->SetLoop(loop);
    sound->SetRange(nearRange, farRange, 0.5f, 0.5f);
    sound->SetOn(on);
    sound->Update();
    S_SoundFrame *registered = GetSoundModule()->RegisterSoundFrame(sound, false);
    if (frame)
        *frame = registered;
    return sound;
}

void FreeSound(I3D_sound *&sound)                       // 0x005bc950
{
    if (!sound)
        return;
    sound->SetOn(false);
    sound->Release();
    sound = nullptr;
}

void PlaySound(I3D_sound *sound)
{
    if (!sound)
        return;
    sound->Play(0);
    sound->SetOn(true);
}

// The wagon's frame at `pos` facing `dir`, its body rolling with the time (0.005 rad for the
// first wagon, phase 0, else 0.0085) about the same heading; the frame moved so that the body is
// at `pos`.
void PlaceWagon(I3D_frame *frame, I3D_frame *body, const S_vector &pos, const S_vector &dir, float phase,
                int time)                               // 0x005bfe10
{
    const float sway = phase == 0.0f ? 0.005f : 0.0085f;
    frame->SetDir(dir, 0.0f);
    // Fix: a wagon without its body is placed by its frame (the original turns the body through
    // null).
    if (body) {
        const float angle = static_cast<float>(static_cast<double>(time) * 0.0025f) + phase;
        body->SetDir(dir, static_cast<float>(std::sin(static_cast<double>(angle))) * sway);
    }
    frame->Update();
    const S_vector framePos = frame->GetWorldPos();
    const S_vector bodyPos = body ? body->GetWorldPos() : framePos;
    frame->SetPos(S_vector(framePos.x - bodyPos.x + pos.x, framePos.y - bodyPos.y + pos.y,
                           framePos.z - bodyPos.z + pos.z));
    frame->Update();
}

// A sphere of `radius` swept from `from` to `to`: a human it meets is killed (run over) and
// returned. (The original also passes the train, unused.)
C_Actor *HitHuman(const S_vector &from, const S_vector &to, float radius)   // 0x005c0950
{
    S_vector hitPos;
    S_vector hitNormal;
    S_CollisionVolume *hit =
        CollisionSphereTest(from, to - from, radius, &hitPos, &hitNormal, SWEEP_TYPES, SWEEP_FLAGS);
    if (!hit || !(hit->flags & 0xC0) || !hit->frame)
        return nullptr;
    S_FrameData *data = hit->frame->GetData();
    if (!data || !data->actor || data->actor->GetType() != ACTOR_HUMAN)
        return nullptr;
    const S_vector zero(0.0f, 0.0f, 0.0f);
    data->actor->Hit(HIT_THROWN_OBJECT, -1.0f, zero, zero, nullptr, 0);
    return data->actor;
}

// The wagon's box follows its frame: the inverse matrix, the sphere's centre (a dummy's box: in
// world space; else the frame's bound, quirk kept: in the frame's space), moved; then the humans
// on the box's centre line (z from its front to its back) run over, the sphere a quarter of the
// box's width and height times the frame's own x and y scale.
void UpdateWagonVolume(S_FrameVolume *wagon)            // 0x005c0a30
{
    S_CollisionVolume &volume = wagon->volume;
    I3D_frame *frame = volume.frame;
    const S_matrix &world = frame->GetMatrix();
    wagon->invMatrix = world;
    const S_matrix copy = world;
    wagon->invMatrix.Inverse(copy);
    I3D_bbox box;
    if (frame->GetType() == FRAME_DUMMY)
        box = static_cast<I3D_dummy *>(frame)->GetBBox();
    else
        box = frame->GetBound().bbox;
    const S_vector center((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
                          (box.max.z + box.min.z) * 0.5f);
    // (The centre's w: a stale stack value in the original.)
    volume.center = frame->GetType() == FRAME_DUMMY ? center * frame->GetMatrix() : center;
    g_collisionWorld.MoveVolume(&volume);
    const S_vector front = S_vector(center.x, center.y, box.min.z) * frame->GetMatrix();
    const S_vector back = S_vector(center.x, center.y, box.max.z) * frame->GetMatrix();
    const S_vector &scale = frame->GetScale();
    const float size = (std::fabs(box.max.x - box.min.x) + std::fabs(box.max.y - box.min.y)) * 0.25f;
    HitHuman(front, back, size * ((scale.y + scale.x) * 0.5f));
}

}  // namespace

C_Train::C_Train()                                      // 0x005bcfb0
    : C_Actor(ACTOR_TRAIN)
{
}

C_Train::~C_Train()                                     // 0x005bd0f0
{
    ClearWagons();
    // (The original leaves the wheels and the path's points to StopMission.)
}

void C_Train::Start()                                   // 0x005bc970
{
    C_game *game = GetGame();
    if (!game || !game->GetScene() || !game->GetScene()->GetActiveCamera())
        return;
    const S_vector &pos = m_frame->GetWorldPos();
    const S_vector &camera = game->GetScene()->GetActiveCamera()->GetWorldPos();
    const S_vector d = camera - pos;
    PlaySound(LengthOf((d.z * d.z + d.y * d.y) + d.x * d.x) <= WHISTLE_NEAR_RANGE ? m_whistleNear : m_whistleFar);
}

void C_Train::Update(int frameTime)                     // 0x005bcf80
{
    if (!m_running)
        return;
    m_time += frameTime;
    Move(frameTime, false);
}

bool C_Train::SetFrame(I3D_frame *frame)                // 0x005bd1a0
{
    if (!frame || frame->GetType() != FRAME_MODEL)
        return false;
    auto *wagon = new S_Wagon();                        // _aligned_malloc(0x50, 16) in the original
    wagon->name = frame->GetName();
    m_wagons.push_back(wagon);
    return C_Actor::SetFrame(frame);
}

bool C_Train::ReadProperties(C_chunk &chunk)            // 0x005be360
{
    ClearWagons();
    uint16_t id;
    if (chunk.RemainingSize() == 0 || !chunk.Descend(id) || id != PROPERTY_VERSION)
        return false;
    // (Quirk kept: a short gap chunk leaves the version chunk's id and the low half of its size
    // there, the original's header buffer.)
    const uint32_t versionSize = static_cast<uint32_t>(chunk.RemainingSize() + 6);
    int version = 0;                                    // (the original goes on with a stack value)
    chunk.Read(version);
    chunk.Ascend();
    if (version != 3 && version != 2)
        return false;
    uint32_t gap = (versionSize & 0xFFFF) << 16 | PROPERTY_VERSION;
    uint32_t length = 0;                                // (an unset local in the original)
    S_Wagon *wagon = nullptr;
    char text[1000];
    // (The original goes on with the last chunk's id after a header it cannot read.)
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case PROPERTY_NAME:
            chunk.ReadString(text, sizeof text);
            chunk.Ascend();
            break;
        case PROPERTY_WAGON:
            chunk.ReadString(text, sizeof text);
            chunk.Ascend();
            wagon = new S_Wagon();
            wagon->name = text;
            m_wagons.push_back(wagon);
            break;
        case PROPERTY_WAGON_LENGTH:
            chunk.Read(length);
            chunk.Ascend();
            if (wagon)
                std::memcpy(&wagon->length, &length, sizeof length);
            break;
        case PROPERTY_WAGON_GAP:
            chunk.Read(gap);
            chunk.Ascend();
            if (wagon)
                std::memcpy(&wagon->gap, &gap, sizeof gap);
            break;
        default:
            break;
        }
    }
    return true;
}

void C_Train::StartMission(bool server)                 // 0x005bd820 (DS 0x005d17f0)
{
    Activate(server);
    if (m_started)
        return;
    m_running = false;
    m_speed = 0.0f;
    m_jarTimer = 0;
    m_brakeTimer = 0;
    m_brakeFade = 0.0f;
    m_trainLength = 0.0f;
    for (size_t i = m_wagons.size(); i-- > 0;) {
        S_Wagon *wagon = m_wagons[i];
        if (!wagon)
            continue;
        I3D_scene *scene = GetGame()->GetScene();
        wagon->frame = scene->FindFrame(wagon->name.c_str(), FRAME_MASK_USER);
        // Quirk kept: a wagon missing stops it here, the train not started (no sounds).
        if (!wagon->frame)
            return;
        if (i != m_wagons.size() - 1)
            m_trainLength = wagon->length + m_trainLength;
        m_trainLength = wagon->gap + m_trainLength;
        I3D_frame *frame = wagon->frame;
        frame->LinkTo(scene->GetPrimarySector(), I3DLINK_KEEP_WORLD);
        frame->Update();
        const S_vector &pos = frame->GetPos();
        wagon->savedPos = {pos.x, pos.y, pos.z, frame->GetPosW()};
        wagon->savedRot = frame->GetRot();
        wagon->body = frame->FindChildFrame("BODY", FRAME_MASK_ANY);
        // The wheels: the right side's, then the left's, numbered from 0 to the first missing.
        for (int side = 2; side-- > 0;) {
            I3D_frame *wheel;
            int number = -1;
            do {
                ++number;
                char name[16];
                std::snprintf(name, sizeof name, side == 0 ? "WHL%02d" : "WHR%02d", number);
                wheel = frame->FindChildFrame(name, FRAME_MASK_ANY);
                if (!wheel || wheel->GetType() != FRAME_VISUAL)
                    continue;
                const I3D_bbox &bound = static_cast<I3D_visual *>(wheel)->GetLocalBound().bbox;
                S_Wheel w;
                w.wheel = wheel;
                w.radius = (bound.max.y - bound.min.y) * 0.5f;
                std::snprintf(name, sizeof name, side == 0 ? "THL%02d" : "THR%02d", number);
                w.rod = wheel->FindChildFrame(name, FRAME_MASK_ANY);
                std::snprintf(name, sizeof name, side == 0 ? "DTHL%02d" : "DTHR%02d", number);
                w.connectingRod = wheel->FindChildFrame(name, FRAME_MASK_ANY);
                if (w.connectingRod)
                    w.crosshead = w.connectingRod->FindChildFrame(side == 0 ? "STHL00" : "STHR00", FRAME_MASK_ANY);
                m_wheels.push_back(w);
            } while (wheel);
        }
        I3D_frame *hull = frame->FindChildFrame("BODYCOLB00", FRAME_MASK_DUMMY);
        if (!hull)
            hull = frame->FindChildFrame("BODY", FRAME_MASK_DUMMY);
        if (!hull)
            hull = frame;
        wagon->volume = new S_FrameVolume();            // _aligned_malloc(0xa0, 16) in the original
        wagon->volume->Init(hull, WAGON_VOLUME_FLAGS, WAGON_VOLUME_TYPE, this);
    }
    SetSounds(true);
    m_started = true;
    m_time = 1;
}

void C_Train::StopMission(bool server)                  // 0x005be1b0
{
    Deactivate(server);
    if (!m_started)
        return;
    SetSounds(false);
    m_started = false;
    for (size_t i = m_wagons.size(); i-- > 0;) {
        S_Wagon *wagon = m_wagons[i];
        if (I3D_frame *frame = wagon->frame) {
            frame->SetPosInline(S_vector(wagon->savedPos.x, wagon->savedPos.y, wagon->savedPos.z));
            frame->SetRot(wagon->savedRot);
            frame->Update();
        }
        if (wagon->volume) {
            g_collisionWorld.RemoveVolume(&wagon->volume->volume);
            delete wagon->volume;
        }
        wagon->volume = nullptr;
    }
    m_wheels.clear();
    ClearPath();
}

void C_Train::RestartMission(bool)                      // 0x005befe0 (DS 0x005d3650)
{
    StopMission(true);
    StartMission(true);
}

bool C_Train::SaveGame(C_chunk &chunk)                  // 0x005bf000
{
    C_Actor::SaveGame(chunk);
    if (!m_running) {
        chunk.Begin(CHUNK_STOPPED);
    } else {
        chunk.Begin(CHUNK_RUNNING);
        // Fix: no track saves an empty name (the original reads it through null).
        C_Track *track = m_player.GetTrack();
        chunk.WriteSizedString(track ? track->GetName() : "");
        chunk.Write(m_player.GetKeyIndex());
        chunk.Write(static_cast<int>(m_wagons.size()));
        for (S_Wagon *wagon : m_wagons) {
            // Fix: a wagon without its frame saves 0 (the original reads it through null).
            const S_vector pos = wagon->frame ? wagon->frame->GetWorldPos() : S_vector(0.0f, 0.0f, 0.0f);
            chunk.Write(&pos, 12);
        }
    }
    chunk.Ascend();
    return true;
}

bool C_Train::LoadGame(C_chunk &chunk)                  // 0x005bf250
{
    C_Actor::LoadGame(chunk);
    uint16_t id = 0;                                    // (an unset local in the original)
    chunk.Descend(id);
    if (id == CHUNK_RUNNING) {
        uint32_t length = 0;
        chunk.Read(&length, 2);
        char name[0x80] = {};
        if (length != 0 && length < 0x80) {
            chunk.Read(name, static_cast<int>(length));
        } else if (length != 0) {
            chunk.Read(name, 0x7F);
            // Quirk kept: the rest is skipped by reads of n - 1, n - 2, ... 0 bytes, n the bytes
            // left over (each only when the chunk still holds it). Fix: into a buffer (the
            // original reads them into one byte on the stack).
            std::vector<char> skip(length - 0x7F);
            for (uint32_t left = length - 0x7F; left != 0; --left)
                chunk.Read(skip.data(), static_cast<int>(left - 1));
        }
        name[0x7F] = '\0';
        m_player.SetTrack(g_trackModule.FindTrack(name));
        m_running = true;
        int key = m_player.GetKeyIndex();
        chunk.Read(key);
        m_player.SetKeyIndex(key);
        if (chunk.RemainingSize() != 0) {
            int count = 0;
            chunk.Read(count);
            S_vector pos(0.0f, 0.0f, 0.0f);             // (an unset local in the original)
            for (int i = 0; i < count; ++i) {
                chunk.Read(&pos, 12);
                // Fix: places past the wagons, or for a wagon without its frame, are dropped.
                if (i < static_cast<int>(m_wagons.size()) && m_wagons[i]->frame)
                    m_wagons[i]->frame->SetPos(pos);
            }
        }
        BuildPath();
    }
    chunk.Ascend();
    return true;
}

// --- the sounds ---------------------------------------------------------------------------------

void C_Train::SetSounds(bool load)                      // 0x005bca90 (DS 0x005d0a90)
{
    if (!load) {
        for (size_t i = m_wagons.size(); i-- > 0;)
            FreeSound(m_wagons[i]->wheelSound);
        FreeSound(m_steam);
        FreeSound(m_whistleNear);
        FreeSound(m_whistleFar);
        FreeSound(m_brakeLoop);
        FreeSound(m_brake);
        FreeSound(m_jar);
        for (int set = CHUFF_SETS; set-- > 0;) {
            for (int i = CHUFF_SOUNDS; i-- > 0;)
                FreeSound(m_chuffSets[set].sounds[i]);
        }
        m_chuffSet = -1;
        return;
    }
    const S_vector origin(0.0f, 0.0f, 0.0f);
    for (size_t i = m_wagons.size(); i-- > 0;) {
#ifndef HD2_DEDICATED   // no sounds on the server (the loops and the names are still made)
        S_Wagon *wagon = m_wagons[i];
        if (wagon->wheelSound)
            continue;
        const bool last = i == m_wagons.size() - 1;
        const char *name = i == 0 ? "l_hheels.wav" : last ? "l_eheels.wav" : "l_rheels.wav";
        wagon->wheelSound = LoadSound(name, wagon->body ? wagon->body : wagon->frame, origin, true, true, 10.0f,
                                      last ? 70.0f : 50.0f, &wagon->wheelSoundFrame);
        if (wagon->wheelSound) {
            wagon->wheelSound->SetVolume(0.0f);
            wagon->wheelSound->Update();
            wagon->wheelSoundFrame->baseVolume = 0.0f;
            wagon->wheelSoundFrame->SetSectorFactor(wagon->wheelSoundFrame->sectorFactor);
        }
#endif
    }
#ifndef HD2_DEDICATED   // no sounds on the server
    const S_vector top(0.0f, 3.0f, 0.0f);
    const S_vector bottom(0.0f, -1.0f, 0.0f);
    m_steam = LoadSound("l_steam.wav", m_frame, origin, true, true, 4.0f, 20.0f, &m_steamFrame);
    m_whistleNear = LoadSound("l_whisnr.wav", m_frame, top, false, false, 32.0f, 54.0f, &m_whistleNearFrame);
    m_whistleFar = LoadSound("l_whisfr.wav", m_frame, top, false, false, 100.0f, 350.0f, &m_whistleFarFrame);
    m_brakeLoop = LoadSound("l_braklp.wav", m_frame, bottom, true, false, 48.0f, 64.0f, &m_brakeLoopFrame);
    m_brake = LoadSound("l_brake.wav", m_frame, bottom, false, false, 32.0f, 48.0f, &m_brakeFrame);
    m_jar = LoadSound("l_jar.wav", m_frame, bottom, false, false, 48.0f, 64.0f, &m_jarFrame);
#endif
    for (int set = CHUFF_SETS; set-- > 0;) {
        S_ChuffSet &chuffs = m_chuffSets[set];
        int count = 0;
        for (int i = CHUFF_SOUNDS; i-- > 0;) {
            chuffs.sounds[i] = nullptr;
            char name[268];
            std::snprintf(name, sizeof name, "l_sts%d%c.wav", set + 1, 'a' + i);
#ifndef HD2_DEDICATED   // no sounds on the server
            chuffs.sounds[i] = LoadSound(name, m_frame, origin, false, false, 5.0f, 50.0f, &chuffs.frames[i]);
#endif
            if (chuffs.sounds[i])
                ++count;
        }
        chuffs.minSpeed = static_cast<float>(set * 12);
        chuffs.count = count;
        chuffs.next = 0;
    }
    m_chuffSet = -1;
}

void C_Train::PlayChuff()                               // 0x005bf4e0
{
    if (m_chuffSet == -1)
        return;
    S_ChuffSet &chuffs = m_chuffSets[m_chuffSet];
    if (chuffs.count == 0)
        return;
    if (++chuffs.next >= chuffs.count)
        chuffs.next = 0;
    // (Quirk kept: the count is of the sounds loaded, the index of all four.)
    PlaySound(chuffs.sounds[chuffs.next]);
}

// --- the path -----------------------------------------------------------------------------------

void C_Train::FollowTrack(const char *name)             // 0x005bd490
{
    float trainLength = 0.0f;
    for (size_t i = m_wagons.size(); i-- > 0;) {
        if (const S_Wagon *wagon = m_wagons[i])
            trainLength = trainLength + wagon->length + wagon->gap;
    }
    m_player.SetKeyIndex(0);
    if (name)
        m_player.SetTrack(g_trackModule.FindTrack(name));
    BuildPath();
    C_Track *track = m_player.GetTrack();
    if (track) {
        float covered = 0.0f;
        for (int i = 0; i < track->NumKeys(); ++i) {
            if (i != 0) {
                const S_vector d = track->GetKey(i - 1).pos - track->GetKey(i).pos;
                covered = LengthOf((d.z * d.z + d.y * d.y) + d.x * d.x) + covered;
            }
            if (covered > trainLength) {
                m_player.SetKeyIndex(i);
                break;
            }
            if (i + 1 == track->NumKeys())
                return;
        }
    }
    // Fix: without a track (with keys), wagons or the first one's frame it is not placed (the
    // original reads through null).
    S_Wagon *front = m_wagons.empty() ? nullptr : m_wagons[0];
    if (track && track->NumKeys() != 0 && front && front->frame) {
        bool normalize;
        S_vector dir = front->frame->GetLocalDirUnnormalized(&normalize);
        if (normalize)
            NormalizeBy(dir, (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x);
        PlaceWagon(front->frame, front->body, track->GetKey(m_player.GetKeyIndex()).pos, dir, 0.0f, 0);
    }
    Move(1000, false);
}

void C_Train::BuildPath()                               // 0x005bc2a0
{
    ClearPath();
    C_Track *track = m_player.GetTrack();
    if (!track || track->NumKeys() == 0)
        return;
    C_Checkpoints *network = GetGame()->GetCheckpoints();
    S_Checkpoint *start = nullptr;
    int key = 0;
    for (; key < track->NumKeys(); ++key) {
        const S_vector &pos = track->GetKey(key).pos;
        start = network->Get(network->FindNearest(pos, 5.0f, -1.0f));
        if (start) {
            const S_vector d = start->pos - pos;
            if ((d.z * d.z + d.y * d.y) + d.x * d.x < 4.0f)
                break;
        }
    }
    if (key == track->NumKeys())
        return;
    m_path.push_back(start);
    while (S_Checkpoint *next = FindNextCheckpoint())
        m_path.push_back(next);
}

S_Checkpoint *C_Train::FindNextCheckpoint() const       // 0x005bbe70
{
    const S_Checkpoint *last = m_path.back();
    const S_Checkpoint *previous = m_path.size() >= 2 ? m_path[m_path.size() - 2] : nullptr;
    S_vector heading;
    if (!previous) {
        heading = m_initialRot.GetDir();
    } else {
        heading = last->pos - previous->pos;
        NormalizeBy(heading, (heading.z * heading.z + heading.y * heading.y) + heading.x * heading.x);
    }
    float best = -1.0f;
    S_Checkpoint *next = nullptr;
    C_Checkpoints *network = GetGame()->GetCheckpoints();
    for (int i = last->GetLinkCount(); i-- > 0;) {
        S_Checkpoint *candidate = network->Get(const_cast<S_Checkpoint *>(last)->GetLink(i)->target);
        if (previous && candidate == previous)
            continue;
        S_vector d = candidate->pos - last->pos;
        NormalizeBy(d, (d.z * d.z + d.y * d.y) + d.x * d.x);
        const float dot = (heading.z * d.z + heading.y * d.y) + heading.x * d.x;
        if (!previous) {
            // Quirk kept: without a previous point the last link looked at (link 0) is taken.
            if (dot > best)
                best = dot;
            next = candidate;
        } else if (dot > 0.5f && dot > best) {
            best = dot;
            next = candidate;
        }
    }
    return next;
}

void C_Train::ClearPath()                               // 0x005bc7c0
{
    m_path.clear();
    m_firstMarked = -1;
    m_lastMarked = -1;
}

void C_Train::UpdateCrossings()                         // 0x005bc5a0
{
    const float reach = m_speed * SPEED_TO_REACH;
    const float reachSquare = reach * reach;
    for (size_t i = static_cast<uint32_t>(m_lastMarked) + 1; i < m_path.size(); ++i) {
        const S_vector d = m_frame->GetWorldPos() - m_path[i]->pos;
        if (!((d.z * d.z + d.y * d.y) + d.x * d.x < reachSquare))
            break;
        m_path[i]->flags |= CHECKPOINT_FLAG_UNDER_CAR;
        m_lastMarked = static_cast<int>(i);
        if (m_firstMarked == -1)
            m_firstMarked = static_cast<int>(i);
    }
    if (m_firstMarked == -1)
        return;
    const float lengthSquare = m_trainLength * m_trainLength;
    for (size_t i = static_cast<uint32_t>(m_firstMarked); i < m_path.size(); ++i) {
        const S_vector d = m_frame->GetWorldPos() - m_path[i]->pos;
        if (!((d.z * d.z + d.y * d.y) + d.x * d.x > lengthSquare))
            return;
        m_path[i]->flags &= ~CHECKPOINT_FLAG_UNDER_CAR;
        if (static_cast<size_t>(++m_firstMarked) >= m_path.size())
            m_firstMarked = static_cast<int>(m_path.size());
    }
}

void C_Train::ClearWagons()                             // 0x005be780
{
    for (size_t i = m_wagons.size(); i-- > 0;)
        delete m_wagons[i];
    m_wagons.clear();
}

// --- the movement -------------------------------------------------------------------------------

void C_Train::UpdateWheels(uint32_t, float distance, int)   // 0x005bf580
{
    bool crossheadChecked = false;
    for (size_t i = m_wheels.size(); i-- > 0;) {
        S_Wheel &w = m_wheels[i];
        I3D_frame *wheel = w.wheel;
        const float angle = distance / w.radius;
        wheel->SetRot(wheel->GetRot().RotateByMatrix(RotateXMatrix(angle)));
        wheel->Update();
        if (w.rod) {
            const S_matrix &parent = wheel->GetParent()->GetMatrix();
            S_vector z(parent.m[2][0], parent.m[2][1], parent.m[2][2]);
            NormalizeBy(z, (z.z * z.z + z.y * z.y) + z.x * z.x);
            w.rod->SetDir(z, 0.0f);
        }
        // Fix: a connecting rod without its crosshead is left (the original reads it through null).
        if (!w.connectingRod || !w.crosshead)
            continue;
        // The connecting rod from its pin on the wheel to the crosshead on the slide (at
        // m_crossheadHeight in the rod's grandparent's space, ahead of the pin by the rod's length).
        const S_vector &end = w.crosshead->GetPos();
        const float rodLength = LengthOf((end.x * end.x + end.y * end.y) + end.z * end.z);
        S_matrix space = w.connectingRod->GetParent()->GetLocalMatrix();
        S_vector pin = w.connectingRod->GetPos();
        pin *= space;
        const float dy = pin.y - m_crossheadHeight;
        S_vector slide(pin.x, m_crossheadHeight, std::sqrt(rodLength * rodLength - dy * dy) + pin.z);
        if (!crossheadChecked) {
            // The crosshead turning back at either end of its stroke: a chuff.
            if (g_crossheadZ < slide.z && !g_crossheadForward) {
                g_crossheadForward = true;
                PlayChuff();
            } else if (g_crossheadZ > slide.z && g_crossheadForward) {
                g_crossheadForward = false;
                PlayChuff();
            }
            crossheadChecked = true;
            g_crossheadZ = slide.z;
        }
        const S_matrix copy = space;
        space.Inverse(copy);
        slide *= space;
        S_vector dir = slide - w.connectingRod->GetPos();
        NormalizeBy(dir, (dir.z * dir.z + dir.y * dir.y) + dir.x * dir.x);
        // (Quirk kept: the up vector goes through the whole inverse matrix, its translation too.)
        S_vector up(0.0f, 1.0f, 0.0f);
        up *= space;
        w.connectingRod->SetLocalDir3(dir, up);
        w.connectingRod->Update();
        const S_matrix &parent = wheel->GetParent()->GetMatrix();
        S_vector z(parent.m[2][0], parent.m[2][1], parent.m[2][2]);
        NormalizeBy(z, (z.x * z.x + z.y * z.y) + z.z * z.z);
        w.crosshead->SetDir(z, 0.0f);
        w.crosshead->Update();
    }
}

void C_Train::Move(uint32_t time, bool force)           // 0x005bff10
{
    // Fix: no wagons, no body on the first or no track with two keys: nothing (the original reads
    // through null or past the keys).
    if (!m_started || m_wagons.empty())
        return;
    S_Wagon *front = m_wagons[0];
    if (!front || !front->frame || !front->body || !m_player.GetTrack() || m_player.GetTrack()->NumKeys() < 2)
        return;
    bool decelerating = false;
    const S_vector next = m_player.AdvanceAlongKeys(front->body->GetWorldPos(), static_cast<int>(time), &decelerating);
    const S_vector d = next - front->body->GetWorldPos();
    const float distance = LengthOf((d.x * d.x + d.z * d.z) + d.y * d.y);
    const float speed = time != 0 ? distance / (static_cast<float>(time) * 0.001f) * 3.6f : 0.0f;
    const bool moved = distance > MIN_MOVE || force;
    if (moved) {
        // (The actor frame's local z axis is taken and dropped: only its local matrix rebuilt.)
        bool normalize;
        m_frame->GetLocalDirUnnormalized(&normalize);
        UpdateWheels(time, distance, 0);
        int key = 0;
        S_vector rear = m_player.FindPointAtDistance(m_player.GetKeyIndex(), next, front->length, &key);
        S_vector dir = next - rear;
        NormalizeBy(dir, (dir.x * dir.x + dir.z * dir.z) + dir.y * dir.y);
        PlaceWagon(front->frame, front->body, next, dir, 0.0f, static_cast<int>(m_time));
        UpdateWagonVolume(front->volume);
        for (size_t i = 1; i < m_wagons.size(); ++i) {
            const S_Wagon *ahead = m_wagons[i - 1];
            if (!ahead)
                return;
            const S_vector coupling = m_player.FindPointAtDistance(key, rear, ahead->gap, &key);
            S_Wagon *wagon = m_wagons[i];
            if (!wagon || !wagon->frame)
                return;
            rear = m_player.FindPointAtDistance(key, coupling, wagon->length, &key);
            S_vector heading = coupling - rear;
            NormalizeBy(heading, (heading.z * heading.z + heading.y * heading.y) + heading.x * heading.x);
            PlaceWagon(wagon->frame, wagon->body, coupling, heading, static_cast<float>(static_cast<int>(i)) * PI_F,
                       static_cast<int>(m_time));
            UpdateWagonVolume(wagon->volume);
        }
        int set = 0;
        for (int k = CHUFF_SETS; k-- > 0;) {
            if (m_chuffSets[k].minSpeed < speed) {
                set = k;
                break;
            }
        }
        if (decelerating && m_brakeLoop) {
            if (!m_brakeLoop->IsOn()) {
                m_brakeLoop->SetVolume(1.0f);
                m_brakeLoopFrame->SetSectorFactor(m_brakeLoopFrame->sectorFactor);
                m_brakeLoop->SetOn(true);
                m_brakeFade = 0.0f;
            }
            m_brakeTimer -= static_cast<int>(time);
            if (m_brakeTimer <= 0) {
                m_brakeTimer += std::rand() % 2000 + 1500;
                PlaySound(m_brake);
            }
            m_jarTimer -= static_cast<int>(time);
            if (m_jarTimer <= 0) {
                m_jarTimer += std::rand() % 4000 + 6000;
                PlaySound(m_jar);
            }
        } else if (m_brakeLoop && m_brakeFade <= 1.0f) {
            m_brakeFade = static_cast<float>(time) * BRAKE_FADE_RATE + m_brakeFade;
            if (m_brakeFade > 1.0f) {
                m_brakeFade = 1.0f;
                m_brakeLoop->SetOn(false);
            } else {
                m_brakeLoop->SetVolume(1.0f - m_brakeFade);
                m_brakeLoopFrame->SetSectorFactor(m_brakeLoopFrame->sectorFactor);
            }
        }
        m_chuffSet = set;
    }
    float volume;
    if (speed >= WHEEL_SOUND_FULL_SPEED)
        volume = WHEEL_SOUND_FULL_VOLUME;
    else if (speed > WHEEL_SOUND_MIN_SPEED)
        volume = (speed - WHEEL_SOUND_MIN_SPEED) * 0.1f;
    else
        volume = 0.0f;
    for (size_t i = m_wagons.size(); i-- > 0;) {
        S_Wagon *wagon = m_wagons[i];
        if (!wagon->wheelSound)
            continue;
        wagon->wheelSound->SetVolume(volume);
        wagon->wheelSoundFrame->baseVolume = volume;
        wagon->wheelSoundFrame->SetSectorFactor(wagon->wheelSoundFrame->sectorFactor);
    }
    if (moved) {
        m_speed = speed;
        UpdateCrossings();
    }
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_TrainCreator::Create()                       // 0x005bbde0
{
    return new C_Train();                               // _aligned_malloc(0x8250, 16) in the original
}

void C_TrainCreator::Destroy(C_Actor *actor)            // 0x005bbe50
{
    delete actor;
}

// Registers the creator (static initialiser 0x005bbdc0, thunk 0x005bbdb0).
static const bool g_trainCreatorRegistered = [] {
    g_actorCreators[ACTOR_TRAIN] = &g_trainCreator;
    return true;
}();
