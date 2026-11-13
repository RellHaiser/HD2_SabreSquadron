// The heavy fire support (see heavy_fire_support.h): the air attacks and the artillery.
#include "game/heavy_fire_support.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/math.h"
#include "net/bitstream.h"

namespace {

// The module's message (channel MODULE_HEAVY_FIRE_SUPPORT): bit 0 an air attack, 1 artillery.
enum : uint32_t {
    MESSAGE_AIR_ATTACK = 0,
    MESSAGE_ARTILLERY = 1,
};

// The radio messages: the dubbing samples of the request (air attack / artillery) and of the
// arrival.
constexpr int SPEECH_ARTILLERY = 90005600;
constexpr int SPEECH_AIR_ATTACK = 90005601;
const char *const SPEECH_AIR_ATTACK_FILE = "90005601.wav";
const char *const SPEECH_AIR_ARRIVAL_FILE = "90005603.wav";
const char *const SPEECH_ARTILLERY_FILE = "90005600";
const char *const SPEECH_ARTILLERY_ARRIVAL_FILE = "90005602";
const char *const SPEECH_FRAME_NAME = "airsupport_speech";

// The blasts' effects (particles) and sound.
constexpr int EFFECT_BLAST_0 = 0x2B;
constexpr int EFFECT_BLAST_1 = 0x30;
constexpr int EFFECT_BLAST_2 = 0x31;
constexpr int SOUND_GROUP_BLAST = 3;                    // SOUND_CHAPTER_EXPLOSION

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

// The ground 500 m below `from`.
bool FindGround(const S_vector &from, S_vector *hit)
{
    return CollisionRayTest(from, S_vector(0.0f, -500.0f, 0.0f), hit, nullptr, 0xFFFFFFFF, 0) != nullptr;
}

// One of the blasts: an explosion effect and its sound on the ground below `from`.
void Blast(const S_vector &from, uint32_t soundFlags)
{
    S_vector hit;
    if (!FindGround(from, &hit))
        return;
    int effect = -1;
    switch (std::rand() % 3) {
    case 0:
        effect = EFFECT_BLAST_0;
        break;
    case 1:
        effect = EFFECT_BLAST_1;
        break;
    case 2:
        effect = EFFECT_BLAST_2;
        break;
    default:
        break;
    }
    g_particlesModule.CreateAt(effect, hit, S_vector(0.0f, 1.0f, 0.0f), nullptr, true, false);
    S_SoundRequest sound;
    sound.pos = S_vector(hit.x, hit.y + 1.0f, hit.z);
    sound.flags = soundFlags;
    sound.attachTo = nullptr;
    sound.chapter = SOUND_CHAPTER_EXPLOSION;
    sound.group = SOUND_GROUP_BLAST;
    GetSoundModule()->Play(sound);
}

// The explosion on the ground below `target` (the server's).
void Strike(const S_vector &target, float range, float damage, C_Human *caller)
{
    S_vector hit;
    if (!FindGround(target, &hit))
        return;
    S_Explosion explosion;
    explosion.pos = S_vector(hit.x, hit.y + 1.0f, hit.z);
    explosion.range = range;
    explosion.damage = damage;
    explosion.unknown18 = false;
    explosion.source = caller;
    explosion.Execute();
}

// The caller of a strike message (-1: none; a human only).
C_Human *ReadCaller(uint32_t id)
{
    if (id == 0xFFFFFFFF)
        return nullptr;
    C_Actor *actor = g_actorsModule.GetActor(static_cast<int>(id));
    // Fix: an id without an actor (the original reads its type through null).
    if (!actor || actor->GetType() != ACTOR_HUMAN)
        return nullptr;
    return static_cast<C_Human *>(actor);
}

// The caller's id in the strike messages. (The original's test is reversed: it sends -1 for
// a caller and reads the id through null without one; kept for a caller, Fix: -1 without.)
uint32_t CallerId(const C_Human *caller)
{
    (void)caller;
    return 0xFFFFFFFF;
}

} // namespace

C_HeavyFireSupport g_heavyFireSupport(MODULE_HEAVY_FIRE_SUPPORT, "HeavyFireSupportModule");   // 0x0089b2c0 (static init 0x00471f60)

I_HeavyFireSupport *GetHeavyFireSupport()              // 0x00471fb0
{
    return &g_heavyFireSupport;
}

// ------------------------------------------------------------------------------ air attack

void S_AirSupportKind::SetDefaults()                    // 0x00472210
{
    model = "";
    speed = 30.0f;
    soundGroup = 0;
    bombsPerPlane = 2;
    damage = 1000.0f;
    range = 10.0f;
}

C_AirStrike::S_Plane::~S_Plane()                        // 0x00471750
{
    if (model) {
        model->Release();
        model = nullptr;
    }
    if (sound != -1)
        GetSoundModule()->Stop(&sound);
}

C_AirStrike::C_AirStrike(const S_vector4 &target, const S_vector4 &dir, const S_AirSupportKind &kind, int planes,
                         int team, C_Human *caller)     // 0x00470530
{
    S_vector way = XYZ(dir);
    NormalizeBy(way, (way.y * way.y + way.z * way.z) + way.x * way.x);
    m_target = target;
    m_time = 15000;
    m_speed = kind.speed;
    m_dir = ToVector4(way, dir.w);
    // The lead plane starts 15 s from the target.
    const float t = static_cast<float>(m_time) * 0.001f;
    m_pos.x = m_target.x - t * m_dir.x * m_speed;
    m_pos.y = m_target.y - t * m_dir.y * m_speed;
    m_pos.z = m_target.z - t * m_dir.z * m_speed;
    m_pos.w = dir.w;
    m_bombsPerPlane = kind.bombsPerPlane;
    m_range = kind.range;
    m_damage = kind.damage;
    m_team = team;
    m_caller = caller;
    // (The original copies the model's name with sprintf, the name as the format.)
    const char *modelName = kind.model.c_str();

    for (int i = 0; i < planes; ++i) {
        S_Plane *plane = new S_Plane;
        m_planes.push_back(plane);
        if (!IsClient())
            continue;
        plane->model = I3DGetDriver()->CreateFrame(FRAME_MODEL);
        g_modelCache.OpenInto(static_cast<I3D_model *>(plane->model), modelName);
        if (i == 0) {
            plane->model->LinkTo(GetGame()->GetScene()->GetPrimarySector(), 0);
            plane->model->SetPos(XYZ(m_pos));
            plane->model->SetDir(XYZ(m_dir), 0.0f);
        } else {
            // Behind the plane before: the odd ones a span and a half aside and back, the even
            // ones three spans to the other side.
            plane->model->LinkTo(m_planes[i - 1]->model, 0);
            const float span = plane->model->GetBound().radius * 1.5f;
            const float x = i % 2 != 0 ? span : span * -2.0f;
            const float z = i % 2 != 0 ? -span : 0.0f;
            plane->offset = {x, 0.0f, z, 0.0f};
            plane->model->SetPosInline(XYZ(plane->offset));
            plane->model->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
        }
        plane->model->Update();
        // The engine.
        S_SoundRequest sound;
        sound.pos = S_vector(0.0f, 0.0f, 0.0f);
        sound.flags = SOUND_SILENT_TO_AI | SOUND_KEEP_SLOT | SOUND_LOOP;
        sound.attachTo = plane->model;
        sound.chapter = SOUND_CHAPTER_VEHICLE_NOT_CAR;
        sound.group = kind.soundGroup;
        plane->sound = GetSoundModule()->Play(sound);
        if (I3D_sound *engine = GetSoundModule()->GetSound(plane->sound)) {
            engine->SetVelocity(S_vector(m_dir.x * m_speed, m_dir.y * m_speed, m_dir.z * m_speed));
            engine->Play(static_cast<int>(RandFloat() * 1000.0f));
            engine->Update();
        }
    }

    // The caller's radio: the request.
    m_speech = nullptr;
    if (IsClient() && caller && ISndGetDriver()->IsInitialized()) {
        m_speech = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
        g_soundCache.OpenInto(m_speech, SPEECH_AIR_ATTACK_FILE);
        m_speech->SetSoundType(I3DSOUND_POINT);
        m_speech->SetVolume(1.0f);
        m_speech->SetName(SPEECH_FRAME_NAME);
        m_speech->LinkTo(caller->GetBodyFrame(), 0);
        m_speech->SetPosInline(S_vector(0.0f, -0.5f, 0.0f));
        m_speech->SetRange(4.0f, 15.0f, 1.0f, 0.0f);
        m_speech->SetOn(true);
        m_speechTimer = GetSoundModule()->GetDubbingSampleSize(SPEECH_AIR_ATTACK) + 1000;
    }
}

C_AirStrike::~C_AirStrike()                             // 0x00471090
{
    for (size_t i = m_planes.size(); i-- > 0;)
        delete m_planes[i];
    m_planes.clear();
    if (m_speech)
        m_speech->Release();
}

bool C_AirStrike::TickServer(int timeDelta)             // 0x00471150
{
    return IsClient() ? true : Update(timeDelta);
}

bool C_AirStrike::Update(int timeDelta)                 // 0x00471170
{
    const int64_t before = m_time;
    m_time -= timeDelta;

    // The radio: the planes' arrival when the request is over; then the radio goes when
    // silent.
    if (m_speechTimer < 1) {
        if (m_speech && !m_speech->IsPlaying(0)) {
            m_speech->SetOn(false);
            m_speech->Release();
            m_speech = nullptr;
        }
    } else {
        m_speechTimer -= timeDelta;
        if (m_speechTimer < 1) {
            m_speechTimer = 0;
            if (m_caller && IsClient() && m_speech) {
                g_soundCache.OpenInto(m_speech, SPEECH_AIR_ARRIVAL_FILE);
                m_speech->SetSoundType(I3DSOUND_POINT);
                m_speech->SetVolume(0.5f);
                m_speech->SetName(SPEECH_FRAME_NAME);
                m_speech->SetRange(3.0f, 10.0f, 1.0f, 0.0f);
                m_speech->SetOn(true);
            }
        }
    }

    // The planes. (The lead plane's w keeps a word of the original's stack.)
    const float t = static_cast<float>(m_time) * 0.001f;
    m_pos.x = m_target.x - t * m_dir.x * m_speed;
    m_pos.y = m_target.y - t * m_dir.y * m_speed;
    m_pos.z = m_target.z - t * m_dir.z * m_speed;
    // Fix: no planes (the original reads the first of an empty list).
    if (!m_planes.empty() && m_planes[0]->model) {
        I3D_frame *lead = m_planes[0]->model;
        lead->SetPosInline(XYZ(m_pos));
        lead->Update();
        // The others weave behind (not the last one: kept). The FPU's sine and cosine are
        // extended: double here.
        const float time = static_cast<float>(m_time);
        for (size_t i = 1; i + 1 < m_planes.size(); ++i) {
            S_Plane *plane = m_planes[i];
            if (!plane->model)
                continue;
            const float n = static_cast<float>(i);
            const double sx = std::sin(static_cast<double>(0.0008f * time + static_cast<float>(3 * i)));
            const float cy = static_cast<float>(std::cos(static_cast<double>(time * 0.001f + n)));
            const float sz = static_cast<float>(std::sin(static_cast<double>(n * 2.5f + time * 0.0007f)));
            const S_vector pos(static_cast<float>(sx + plane->offset.x), cy + plane->offset.y, sz + plane->offset.z);
            plane->model->SetPosInline(pos);
            plane->model->Update();
        }
    }

    // Over the target (a timer that lands on 0 does it twice: kept).
    if (m_time <= 0 && before >= 0) {
        const S_vector target = XYZ(m_target);
        if (IsClient()) {
            m_blastTimer = 0;
            m_blastsLeft = m_bombsPerPlane * static_cast<int>(m_planes.size());
        }
        if (IsServer())
            Strike(target, m_range, m_damage, m_caller);
    }

    // The clients' blasts around the target, 0..300 ms apart.
    if (m_blastsLeft != 0 && IsClient()) {
        m_blastTimer -= timeDelta;
        if (m_blastTimer < 0) {
            const int r = std::rand();
            const float half = m_range * 0.5f;
            m_blastTimer += r % 300;
            m_blastsLeft--;
            const float dz = half - RandFloat() * m_range;
            const float x = (half - RandFloat() * m_range) + m_target.x;
            Blast(S_vector(x, m_target.y, dz + m_target.z), SOUND_SILENT_TO_AI);
        }
    }
    return m_time >= -15000;
}

// --------------------------------------------------------------------------------- artillery

C_ArtilleryStrike::C_ArtilleryStrike(const S_vector4 &target, int team, C_Human *caller)   // 0x004719f0 (DS 0x0048d490)
    : m_target(target), m_team(team), m_caller(caller)
{
#ifndef HD2_DEDICATED   // no sounds on the server
    // The caller's radio: the request (by the request file's name; the game is asked for but
    // not used).
    if (IsClient() && caller) {
        S_SoundRequest sound;
        sound.pos = S_vector(0.0f, 1.5f, 0.0f);
        sound.flags = SOUND_SILENT_TO_AI;
        sound.attachTo = caller->GetFrame();
        sound.fileName = SPEECH_ARTILLERY_FILE;
        sound.nearRange = 2.0f;
        sound.farRange = 15.0f;
        sound.volume = 0.9f;
        GetSoundModule()->Play(sound);
        m_speechTimer = GetSoundModule()->GetDubbingSampleSize(SPEECH_ARTILLERY) + 1000;
    }
#endif
}

bool C_ArtilleryStrike::TickServer(int timeDelta)       // 0x00471b30
{
    return IsClient() ? true : Update(timeDelta);
}

bool C_ArtilleryStrike::Update(int timeDelta)           // 0x00471b50
{
    const int64_t before = m_time;
    m_time -= timeDelta;

    // The radio: the shells' arrival when the request is over.
    if (m_speechTimer > 0) {
        m_speechTimer -= timeDelta;
        if (m_speechTimer < 1) {
            m_speechTimer = 0;
            if (m_caller && IsClient()) {
                // (The original sets the scene's +0x1F8 as the frame first.)
                S_SoundRequest sound;
                sound.pos = S_vector(0.0f, 1.5f, 0.0f);
                sound.flags = SOUND_SILENT_TO_AI;
                sound.attachTo = m_caller->GetFrame();
                sound.fileName = SPEECH_ARTILLERY_ARRIVAL_FILE;
                sound.nearRange = 2.0f;
                sound.farRange = 10.0f;
                sound.volume = 0.5f;
                GetSoundModule()->Play(sound);
            }
        }
    }

    // The shells hit (on every side: the explosion decides itself what it does where; a
    // timer that lands on 0 hits twice: kept).
    if (m_time <= 0 && before >= 0) {
        const S_vector target = XYZ(m_target);
        if (IsClient()) {
            m_blastTimer = 0;
            m_blastsLeft = m_blastCount;
        }
        Strike(target, m_range, m_damage, m_caller);
    }

    // The clients' blasts around the point, 0..1000 ms apart.
    if (m_blastsLeft != 0 && IsClient()) {
        m_blastTimer -= timeDelta;
        if (m_blastTimer < 0) {
            const int r = std::rand();
            const float half = m_range * 0.5f;
            m_blastTimer += r % 1000;
            m_blastsLeft--;
            const float dz = half - RandFloat() * m_range;
            const float x = (half - RandFloat() * m_range) + m_target.x;
            Blast(S_vector(x, m_target.y, dz + m_target.z), 0);
        }
    }
    return m_time >= -7000;
}

// ------------------------------------------------------------------------------- the module

C_HeavyFireSupport::C_HeavyFireSupport(int id, const char *name)   // 0x00471fc0
    : C_Module(id, name)
{
    // (At static initialisation: without the data archives open the defaults stay.)
    LoadTable();
}

bool C_HeavyFireSupport::LoadTable()                    // 0x004732b0
{
    C_chunk chunk;
    if (!chunk.Open("tables\\airsupport.tbl", false))
        return false;
    uint16_t id = 0;
    if (!chunk.Descend(id))
        return false;
    for (S_AirSupportKind &kind : m_kinds)
        kind.SetDefaults();
    // A chunk per kind; its model (1000) starts it, the values after it are its. (Fix: values
    // before the first model and kinds past 16 are skipped; the original writes them before
    // and past the table.)
    int index = -1;
    while (!chunk.AtEnd()) {
        if (!chunk.Descend(id))
            return false;
        while (!chunk.AtEnd()) {
            uint16_t field = 0;
            if (!chunk.Descend(field))
                return false;
            S_AirSupportKind *kind = index >= 0 && index < KIND_COUNT ? &m_kinds[index] : nullptr;
            switch (field) {
            case 1000: {
                char modelName[1000];
                chunk.ReadString(modelName, sizeof modelName);
                ++index;
                if (index < KIND_COUNT)
                    m_kinds[index].model = modelName;
                break;
            }
            case 2000:
                if (kind)
                    chunk.Read(kind->speed);
                break;
            case 3000:
                if (kind)
                    chunk.Read(kind->soundGroup);
                break;
            case 4000:
                if (kind)
                    chunk.Read(kind->bombsPerPlane);
                break;
            case 5000:
                if (kind)
                    chunk.Read(kind->range);
                break;
            case 6000:
                if (kind)
                    chunk.Read(kind->damage);
                break;
            default:
                break;
            }
            chunk.Ascend();
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    chunk.Close();
    return true;
}

bool *C_HeavyFireSupport::BusyFlag(int index)
{
    // Fix: not past the flags (see m_busy; teams other than 0 / 1 reach past them).
    return index >= 0 && index < 4 ? &m_busy[index] : nullptr;
}

int C_HeavyFireSupport::RequestAirAttack(const S_AirAttackRequest &request)   // 0x00472e50
{
    if (!IsServer())
        return REQUEST_NOT_SERVER;
    bool *busy = BusyFlag(request.team);
    if (busy && *busy)
        return REQUEST_BUSY;
    if (request.kind < 0 || request.kind >= KIND_COUNT)
        return REQUEST_BAD_KIND;
    const S_vector4 target = {request.x, request.height, request.z, 0.0f};
    const S_vector4 dir = {request.dirX, 0.0f, request.dirZ, 0.0f};
    m_airStrikes.push_back(new C_AirStrike(target, dir, m_kinds[request.kind], request.planes, request.team,
                                           request.caller));
    if (busy)
        *busy = true;
    C_BitStream *msg = g_netGame.CreateMessage(MODULE_HEAVY_FIRE_SUPPORT, NET_DELIVERY_RELIABLE);
    msg->WriteBits(MESSAGE_AIR_ATTACK, 1);
    msg->WriteBits(FloatBits(target.x), 32);
    msg->WriteBits(FloatBits(target.y), 32);
    msg->WriteBits(FloatBits(target.z), 32);
    msg->WriteBits(FloatBits(dir.x), 32);
    msg->WriteBits(FloatBits(dir.y), 32);
    msg->WriteBits(FloatBits(dir.z), 32);
    msg->WriteBits(static_cast<uint32_t>(request.kind), 32);
    msg->WriteBits(static_cast<uint32_t>(request.planes), 32);
    msg->WriteBits(CallerId(request.caller), 32);
    g_netGame.SendNetMessage(msg);
    return REQUEST_SENT;
}

int C_HeavyFireSupport::RequestArtillery(const S_ArtilleryRequest &request)   // 0x00472ce0
{
    if (!IsServer())
        return REQUEST_NOT_SERVER;
    bool *busy = BusyFlag(2 + request.team);
    if (busy && *busy)
        return REQUEST_NOT_SERVER;
    const S_vector4 target = {request.x, 300.0f, request.z, 0.0f};
    m_artillery.push_back(new C_ArtilleryStrike(target, request.team, request.caller));
    if (busy)
        *busy = true;
    C_BitStream *msg = g_netGame.CreateMessage(MODULE_HEAVY_FIRE_SUPPORT, NET_DELIVERY_RELIABLE);
    msg->WriteBits(MESSAGE_ARTILLERY, 1);
    msg->WriteBits(FloatBits(target.x), 32);
    msg->WriteBits(FloatBits(target.y), 32);
    msg->WriteBits(FloatBits(target.z), 32);
    msg->WriteBits(CallerId(request.caller), 32);
    g_netGame.SendNetMessage(msg);
    return REQUEST_NOT_SERVER;
}

void C_HeavyFireSupport::RemoveAirStrike(int index)     // 0x00472350
{
    C_AirStrike *strike = m_airStrikes[index];
    if (bool *busy = BusyFlag(strike->GetTeam()))
        *busy = false;
    delete strike;
    m_airStrikes[index] = m_airStrikes.back();
    m_airStrikes.pop_back();
}

void C_HeavyFireSupport::RemoveArtillery(int index)     // 0x004723f0
{
    C_ArtilleryStrike *strike = m_artillery[index];
    if (bool *busy = BusyFlag(2 + strike->GetTeam()))
        *busy = false;
    delete strike;
    m_artillery[index] = m_artillery.back();
    m_artillery.pop_back();
}

void C_HeavyFireSupport::Update(int timeDelta)          // 0x00472510
{
    for (size_t i = m_airStrikes.size(); i-- > 0;) {
        if (!m_airStrikes[i]->Update(timeDelta))
            RemoveAirStrike(static_cast<int>(i));
    }
    for (size_t i = m_artillery.size(); i-- > 0;) {
        if (!m_artillery[i]->Update(timeDelta))
            RemoveArtillery(static_cast<int>(i));
    }
}

void C_HeavyFireSupport::Tick(int timeDelta)            // 0x00472c60
{
    for (size_t i = m_airStrikes.size(); i-- > 0;) {
        if (!m_airStrikes[i]->TickServer(timeDelta))
            RemoveAirStrike(static_cast<int>(i));
    }
    for (size_t i = m_artillery.size(); i-- > 0;) {
        if (!m_artillery[i]->TickServer(timeDelta))
            RemoveArtillery(static_cast<int>(i));
    }
}

void C_HeavyFireSupport::StartMission(bool /*server*/)  // 0x00472250
{
    for (bool &busy : m_busy)
        busy = false;
}

void C_HeavyFireSupport::StopMission(bool /*server*/)   // 0x00472270
{
    for (size_t i = m_airStrikes.size(); i-- > 0;)
        delete m_airStrikes[i];
    m_airStrikes.clear();
    for (size_t i = m_artillery.size(); i-- > 0;)
        delete m_artillery[i];
    m_artillery.clear();
}

bool C_HeavyFireSupport::OnMessage(C_BitStream *msg)    // 0x00472590
{
    if (msg->ReadBits(1) == MESSAGE_AIR_ATTACK) {
        S_vector4 target = {};
        target.x = BitsFloat(msg->ReadBits(32));
        target.y = BitsFloat(msg->ReadBits(32));
        target.z = BitsFloat(msg->ReadBits(32));
        S_vector4 dir = {};
        dir.x = BitsFloat(msg->ReadBits(32));
        dir.y = BitsFloat(msg->ReadBits(32));
        dir.z = BitsFloat(msg->ReadBits(32));
        const uint32_t kind = msg->ReadBits(32);
        const int planes = static_cast<int>(msg->ReadBits(32));
        const uint32_t callerId = msg->ReadBits(32);
        // The server has its own (a local game's too).
        if (!IsServer()) {
            C_Human *caller = ReadCaller(callerId);
            // Fix: a kind past the table (the original reads past it).
            if (kind < KIND_COUNT)
                m_airStrikes.push_back(new C_AirStrike(target, dir, m_kinds[kind], planes, 2, caller));
        }
    } else {
        S_vector4 target = {};
        target.x = BitsFloat(msg->ReadBits(32));
        target.y = BitsFloat(msg->ReadBits(32));
        target.z = BitsFloat(msg->ReadBits(32));
        const uint32_t callerId = msg->ReadBits(32);
        if (!IsServer())
            m_artillery.push_back(new C_ArtilleryStrike(target, 2, ReadCaller(callerId)));
    }
    return true;
}
