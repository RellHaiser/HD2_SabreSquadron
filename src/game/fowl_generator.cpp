// Flocks of birds (C_FowlGenerator; original 0x005ba100..0x005bbdb0).
#include "game/fowl_generator.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage, g_actorsModule
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"                               // g_gameRole, C_Bird, g_birds, RandomFloat
#include "game/resource_cache.h"                        // g_modelCache
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

// The modes.
enum : int {
    FOWL_MISSION_BIRDS = 0,                             // the mission's birds within the radius
    FOWL_MADE_ON_START = 1,
    FOWL_MADE_AT_MISSION = 2,                           // on the ground below their places
};

// ReadProperties' chunks: 0 the version (must be 1) first, then the others.
enum : uint16_t {
    PROPERTY_VERSION = 0,
    PROPERTY_MODE = 1,
    PROPERTY_RADIUS = 2,
    PROPERTY_DISTANCE = 3,
    PROPERTY_COUNT = 4,
    PROPERTY_MODEL = 5,
};
constexpr int PROPERTIES_VERSION = 1;
constexpr uint16_t CHUNK_STATE = 0x10E1;                // the saved game's

constexpr int CHECK_INTERVAL = 1000;                    // ms
constexpr int PLAYER_SLOTS = 32;                        // the first actors: the players'
constexpr int SOUND_RECENT = 1000;                      // ms within which Start is heard
constexpr int GONE_TIME = 10000;                        // ms added each Update once all are gone
constexpr float JITTER = 0.4f;                          // of the spacing
constexpr float DROP = -100.0f;                         // m the made birds' ground is looked for
constexpr float HEARD_INTENSITY = 0.8f;

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

// The square of the distance from `a` to `b`, summed z, y, x.
float DistanceSquare(const S_vector &a, const S_vector &b)
{
    const S_vector d(a.x - b.x, a.y - b.y, a.z - b.z);
    return (d.z * d.z + d.y * d.y) + d.x * d.x;
}

}  // namespace

C_FowlGenerator::C_FowlGenerator() : C_Actor(ACTOR_FOWL_GENERATOR)   // 0x005ba4d0
{
}

void C_FowlGenerator::RestartMission(bool)              // 0x005ba1c0
{
    StopMission(true);
    StartMission(true);
}

bool C_FowlGenerator::SaveGame(C_chunk &chunk)          // 0x005ba1e0
{
    chunk.Begin(CHUNK_STATE);
    chunk.Write(m_flown);
    if (m_flown)
        chunk.Write(m_time);
    chunk.Ascend();
    return true;
}

bool C_FowlGenerator::LoadGame(C_chunk &chunk)          // 0x005ba300
{
    // (The chunk's id is not checked.) Quirk kept: it is left even when it was not entered.
    uint16_t id;
    chunk.Descend(id);
    m_flown = false;
    bool flown = false;
    chunk.Read(flown);
    if (flown) {
        int time = 0;
        chunk.Read(time);
        m_time = 0;
        Start(true, time);
        Update(time);
    }
    chunk.Ascend();
    return true;
}

void C_FowlGenerator::StartMission(bool server)         // 0x005ba670
{
    if (!server && IsServer())
        return;
    m_time = 0;
    if (m_mode == FOWL_MISSION_BIRDS) {
        // The mission's birds within the radius are taken over (the last one fills the gap).
        const S_vector center = m_frame->GetWorldPos();
        for (size_t i = g_birds.size(); i-- > 0;) {
            const float distance = LengthOf(DistanceSquare(center, g_birds[i]->GetModel()->GetWorldPos()));
            if (distance <= m_radius) {
                m_birds.push_back(g_birds[i]);
                g_birds[i] = g_birds.back();
                g_birds.pop_back();
            }
        }
    }
    if (m_mode == FOWL_MADE_AT_MISSION)
        CreateBirds();
    if (server)
        m_listener = GetSoundModule()->AddListener(m_frame, &C_FowlGenerator::OnSoundHeard);
    m_checkTime = std::rand() % CHECK_INTERVAL;
    m_flown = false;
    // Without birds to take over there is nothing to set off.
    if (m_mode == FOWL_MISSION_BIRDS && m_birds.empty())
        m_flown = true;
}

void __stdcall C_FowlGenerator::OnSoundHeard(const S_HeardSound *sound)   // 0x005ba860
{
    if (!sound->listener)
        return;
    const S_FrameData *data = sound->listener->GetData();
    if (!data || !data->actor)
        return;
    if ((sound->chapter == SOUND_CHAPTER_PUPPET_MOVEMENT || sound->chapter == SOUND_CHAPTER_WEAPON_SHOOTING) &&
        sound->intensity > HEARD_INTENSITY)
        static_cast<C_FowlGenerator *>(data->actor)->Start(true, 0);
}

void C_FowlGenerator::CreateBirds()                     // 0x005ba8b0
{
    if (m_mode == FOWL_MISSION_BIRDS)
        return;
    const float side = std::sqrt(static_cast<float>(m_count));
    const float radius = m_radius;
    const float spacing = (radius + radius) / side;
    const S_vector center = m_frame->GetWorldPos();
    I3D_frame *model = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    if (!model)
        return;
    // The model's name without its extension.
    std::string name = m_model.c_str();
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos)
        name.erase(dot);
    if (g_modelCache.OpenInto(static_cast<I3D_model *>(model), name.c_str()) != I3D_OK) {
        model->Release();
        return;
    }
    const int rows = static_cast<int>(side);
    for (int row = rows; row-- > 0;) {
        const float rowF = static_cast<float>(row);
        const float x = (rowF * spacing + center.x) - radius;
        const float jitter = spacing * JITTER;
        const float jitterRange = jitter + jitter;
        for (int col = rows; col-- > 0;) {
            S_vector pos(x, center.y, (static_cast<float>(col) * spacing + center.z) - radius);
            pos.x = (RandomFloat(jitterRange) - jitter) + pos.x;
            pos.z = (RandomFloat(jitterRange) - jitter) + pos.z;
            I3D_frame *bird = I3DGetDriver()->CreateFrame(FRAME_MODEL);
            if (!bird)
                break;
            static_cast<I3D_model *>(bird)->DuplicateModel(static_cast<I3D_model *>(model));
            GetGame()->GetScene()->AddFrame(bird);
            bird->LinkTo(GetGame()->GetScene()->GetPrimarySector(), 0);
            if (m_mode == FOWL_MADE_AT_MISSION) {
                S_vector ground;
                if (CollisionRayTest(pos, S_vector(0.0f, DROP, 0.0f), &ground, nullptr, 0, 0))
                    pos = ground;
            }
            bird->SetOn(true);
            bird->SetPosInline(pos);
            const float dirX = RandomFloat(1.0f);
            const float dirZ = RandomFloat(1.0f);
            bird->SetWorldDir(S_vector(dirX, 0.0f, dirZ), 0.0f);
            // Quirk kept: the second number is a float passed to %d, which prints the low half of
            // its double (every bird of a row gets the same name).
            const double index = static_cast<float>(rowF * side);
            uint64_t bits;
            std::memcpy(&bits, &index, sizeof bits);
            char birdName[64];
            std::snprintf(birdName, sizeof birdName, "_bird_%d_%d", m_netId, static_cast<int>(static_cast<uint32_t>(bits)));
            bird->SetName(birdName);
            bird->Update();
            m_birds.push_back(new C_Bird(bird, true));  // _aligned_malloc(0x70, 16) in the original
            bird->Release();
        }
    }
    model->Release();
}

void C_FowlGenerator::Start(bool send, int time)        // 0x005bb140
{
    if (m_flown)
        return;
    if (m_mode == FOWL_MADE_ON_START)
        CreateBirds();
    for (size_t i = m_birds.size(); i-- > 0;)
        m_birds[i]->FlyAway();
    if (IsClient() && time < SOUND_RECENT) {
        S_SoundRequest request;
        request.chapter = SOUND_CHAPTER_ACTION;
        const S_matrix &world = m_frame->GetMatrix();
        request.pos = world(3);
        request.posW = world.m[3][3];
        request.group = m_birds.size() > 3 ? 1 : 0;     // (a flock: more than 3)
        request.source = nullptr;
        request.attachTo = nullptr;
        request.flags = SOUND_SILENT_TO_AI;
        GetSoundModule()->Play(request);
    }
    m_flown = true;
    m_time = 0;
    if (send)
        SendActorMessage(CreateActorMessage(m_netId, true));   // (an empty message)
}

void C_FowlGenerator::StopMission(bool server)          // 0x005bb290
{
    for (size_t i = m_birds.size(); i-- > 0;) {
        C_Bird *bird = m_birds[i];
        I3D_frame *model = bird->OwnsModel() ? nullptr : bird->GetModel();
        delete bird;
        // A mission's bird's model (back among the spare ones) shown again where it sat.
        if (model) {
            for (size_t j = g_spareBirdModels.size(); j-- > 0;) {
                if (g_spareBirdModels[j] != model)
                    continue;
                model->SetOn(true);
                model->Update();
                g_spareBirdModels.erase(g_spareBirdModels.begin() + static_cast<std::ptrdiff_t>(j));
            }
        }
    }
    m_birds.clear();
    // (The listener's index is kept.)
    if (m_listener != -1 && server)
        GetSoundModule()->RemoveListener(m_listener);
}

void C_FowlGenerator::Update(int frameTime)             // 0x005bb3c0
{
    if (m_birds.empty())
        return;
    for (size_t i = m_birds.size(); i-- > 0;) {
        if (m_birds[i]->Update(frameTime))
            continue;
        delete m_birds[i];
        m_birds[i] = m_birds.back();
        m_birds.pop_back();
    }
    if (m_flown) {
        m_time += frameTime;
        if (m_birds.empty())
            m_time += GONE_TIME;
    }
}

void C_FowlGenerator::Tick(int timeDelta)               // 0x005bb4d0
{
    m_checkTime -= timeDelta;
    if (m_flown || m_checkTime >= 1)
        return;
    do
        m_checkTime += CHECK_INTERVAL;
    while (m_checkTime < 1);
    const S_vector here = m_frame->GetWorldPos();
    const std::vector<C_Actor *> &actors = g_actorsModule.GetActors();
    // The players near (Fix: the original reads past a list shorter than the 32 slots).
    for (size_t i = 0; i < PLAYER_SLOTS && i < actors.size() && actors[i]; ++i) {
        if (LengthOf(DistanceSquare(actors[i]->GetFrame()->GetWorldPos(), here)) < m_triggerDistance) {
            Start(true, 0);
            break;
        }
    }
    // The player's other humans near.
    for (size_t i = actors.size(); i-- > PLAYER_SLOTS && actors[i];) {
        C_Actor *actor = actors[i];
        if (actor->GetType() != ACTOR_HUMAN || static_cast<C_Human *>(actor)->GetRole() != HUMAN_ROLE_PLAYER)
            continue;
        if (!(m_triggerDistance * m_triggerDistance <= DistanceSquare(actor->GetFrame()->GetWorldPos(), here))) {
            Start(true, 0);
            break;
        }
    }
    // The camera near.
    const S_vector camera = GetGame()->GetScene()->GetActiveCamera()->GetWorldPos();
    if (DistanceSquare(camera, here) < m_triggerDistance * m_triggerDistance)
        Start(true, 0);
}

bool C_FowlGenerator::ReadProperties(C_chunk &chunk)    // 0x005bb7e0
{
    uint16_t id;
    if (chunk.RemainingSize() == 0 || !chunk.Descend(id) || id != PROPERTY_VERSION)
        return false;
    int version = 0;                                    // (the original goes on with a stack value)
    chunk.Read(version);
    chunk.Ascend();
    if (version != PROPERTIES_VERSION)
        return false;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case PROPERTY_MODE:
            chunk.Read(m_mode);
            chunk.Ascend();
            break;
        case PROPERTY_RADIUS: {
            float radius = 0.0f;                        // (the original goes on with stack values)
            chunk.Read(radius);
            chunk.Ascend();
            m_radius = radius;
            break;
        }
        case PROPERTY_DISTANCE: {
            float distance = 0.0f;
            chunk.Read(distance);
            chunk.Ascend();
            m_triggerDistance = distance;
            break;
        }
        case PROPERTY_COUNT: {
            int count = 0;
            chunk.Read(count);
            chunk.Ascend();
            m_count = count;
            break;
        }
        case PROPERTY_MODEL: {
            // Fix: the name read is terminated (the original's buffer is not cleared).
            char model[1000] = {};
            int size = chunk.RemainingSize();
            if (size > 999)
                size = 999;
            chunk.Read(model, size);
            m_model = model;
            chunk.Ascend();
            break;
        }
        default:
            chunk.Ascend();
            break;
        }
    }
    return true;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_FowlGeneratorCreator::Create()               // 0x005ba130
{
    return new C_FowlGenerator();                       // _aligned_malloc(0xd0, 16) in the original
}

void C_FowlGeneratorCreator::Destroy(C_Actor *actor)    // 0x005ba1a0
{
    delete actor;
}

static C_FowlGeneratorCreator g_fowlGeneratorCreator;   // 0x008ac5a8

// Registers the creator (static initialiser 0x005ba110, thunk 0x005ba100).
static const bool g_fowlGeneratorCreatorRegistered = [] {
    g_actorCreators[ACTOR_FOWL_GENERATOR] = &g_fowlGeneratorCreator;
    return true;
}();
