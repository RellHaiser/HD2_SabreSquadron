// C_Junkers88: see junkers88.h.
#include "game/junkers88.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/frame_box_volume.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_controller.h"                      // HUMAN_CONTROLLER_PLAYER
#include "game/human_movement.h"                        // AngleBetween2D
#include "game/particles.h"                             // g_particlesModule
#include "game/pending.h"                               // RandomFloat, GetGEffects, g_texts
#include "game/profile.h"                               // g_gameProfile
#include "game/resource_cache.h"                        // g_modelCache, g_soundCache, g_animationCache
#include "game/seat_weapon.h"
#include "game/sound_module.h"
#include "game/world.h"                                 // GetCameraModule
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"                                // ISndGetDriver
#include "script/script_module.h"                       // g_scriptModule

namespace {

I3D_scene *Scene() { return GetGame()->GetScene(); }

// The saved game's chunks (all inside CHUNK_SAVE).
constexpr uint16_t CHUNK_SAVE = 1000;
constexpr uint16_t CHUNK_SHOWN = 2000;                  // empty: the frame was shown
constexpr uint16_t CHUNK_VELOCITY = 3000;
constexpr uint16_t CHUNK_CAMERA = 4000;
constexpr uint16_t CHUNK_FIGHTERS = 5000;
constexpr uint16_t CHUNK_DEBRIS = 6000;
constexpr uint16_t CHUNK_HEALTH = 7000;
constexpr uint16_t CHUNK_DAMAGE_RATE = 8000;
constexpr uint16_t CHUNK_LEFT_ENGINE = 9000;
constexpr uint16_t CHUNK_RIGHT_ENGINE = 10000;
constexpr uint16_t CHUNK_FALL = 12000;
constexpr uint16_t CHUNK_TRACERS = 13000;
constexpr uint16_t CHUNK_DAMAGE_PARTS = 14000;
constexpr uint16_t CHUNK_ENGINE_PARTS = 15000;
constexpr uint16_t CHUNK_OUTER_PARTS = 16000;
constexpr uint16_t CHUNK_STAGES = 17000;
constexpr uint16_t CHUNK_HITS = 20000;
constexpr uint16_t CHUNK_FIREPOWER = 0x4E84;
constexpr uint16_t CHUNK_INTERCEPTOR_HEALTH = 0x4EE8;
// The properties.
constexpr uint16_t PROP_HEALTH = 100;
constexpr uint16_t PROP_FIGHTER_HEALTH = 200;
constexpr uint16_t PROP_FIGHTER_DAMAGE = 300;
constexpr uint16_t PROP_UNKNOWN_400 = 400;
constexpr uint16_t PROP_UNKNOWN_500 = 500;

constexpr int EFFECT_SMOKE = 0x10;
constexpr int EFFECT_CRASH_A = 0x67;                    // a plane hitting the ground
constexpr int EFFECT_CRASH_B = 0x68;
// GEffects SetHierarchyOn: the sounds and the models.
constexpr uint32_t SOUNDS_AND_MODELS = 0x88;
constexpr int JUNKERS_CUTSCENE = 31;                    // shot down
constexpr const char *FIGHTER_MODEL = "la_Bf109Canyon";
constexpr float PI = 3.14159274f;
constexpr float INV_TWO_PI = 0.159154937f;

// The parts' names (0x0083e5c8..0x0083e698).
const char *const ENGINE_NAMES[2] = {"engine_l00d", "engine_r00d"};
const char *const OUTER_NAMES[4] = {"left_wing01d", "right_wing01d", "engine_l00d", "engine_r00d"};
// Parts that hang on an engine (three each, left then right).
const char *const DEPENDENT_NAMES[6] = {"left_wing00d", "flap_l00d", "listy_l00d",
                                        "right_wing00d", "flap_r00d", "listy_r00d"};
const char *const BLADE_NAMES[2] = {"listy_l00d", "listy_r00d"};
// A part and the eight thrown off with its last stage.
const char *const THROWN_ROWS[4][9] = {
    {"left_wing00d", "engine_l00d", "left_wing01d", "left_wing02d", "flap_l01d", "flap_l00d", "", "", "listy_l00d"},
    {"right_wing00d", "engine_r00d", "right_wing01d", "right_wing02d", "flap_r01d", "flap_r00d", "flap_r00ad",
     "flap_r01ad", "listy_r00d"},
    {"left_wing01d", "flap_l01d", "left_wing02d", "right_wing02d", "elevator_left01d", "", "", "", ""},
    {"right_wing01d", "flap_r01d", "right_wing02d", "left_wing02d", "elevator_right01d", "", "", "", ""},
};
// A fighter's guns (the propeller's hub too) and the parts it hides.
const char *const GUN_NAMES[5] = {"weapon_r00", "weapon_l00", "weapons_r01", "weapon_l01", "vrtule"};
const char *const HIDDEN_NAMES[2] = {"carriage_l00", "carriage_r00"};

// The seats' flags (0x0083e868): the last StartMission's, shared by every Junkers (the file's
// values until then).
uint32_t g_seatFlags[C_Junkers88::SEATS] = {1, 0x102, 0x20, 0x20, 0};

// S_vector::Normalize as the inline copies compile here: `square` as the caller summed it.
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

// S_vector::Magnitude inline: 0 for a square below 1e-8 (its bits), 1 near 1.
float LengthOf(float square)
{
    uint32_t bits;
    std::memcpy(&bits, &square, sizeof bits);
    if (static_cast<int32_t>(bits) < 0x322bcc77)
        return 0.0f;
    if (!(std::fabs(square - 1.0f) < 1e-08f))
        return std::sqrt(square);
    return 1.0f;
}

// A frame's normalised world z axis, summed as the original's inline copies do.
S_vector WorldDir(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    S_vector d(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeBy(d, (d.y * d.y + d.z * d.z) + d.x * d.x);
    return d;
}

S_vector4 WorldPos4(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

S_vector Vec(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }
float Time(int ms) { return static_cast<float>(static_cast<uint32_t>(ms)); }

// A sound frame: `file` loaded under `parent` at `pos`, positional, (near, far, 0.5, 0.5),
// `loop`, full volume, a random pitch, `on`, played from `playAt` ms; null when the sound driver
// is off or the file does not load.
void LoadSound(I3D_sound *&sound, const C_str &file, I3D_frame *parent, const S_vector &pos, float nearRange,
               float farRange, bool loop, bool on, int playAt)   // 0x005f45e0
{
    sound = nullptr;
    if (!ISndGetDriver()->IsInitialized())
        return;
    sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    if (!sound)
        return;
    if (g_soundCache.OpenInto(sound, file.c_str()) != 0) {
        sound->Release();
        sound = nullptr;
        return;
    }
    sound->LinkTo(parent, 0);
    sound->SetPosInline(pos);
    sound->SetSoundType(I3DSOUND_POINT);
    sound->SetRange(nearRange, farRange, 0.5f, 0.5f);
    sound->SetLoop(loop);
    sound->Play(playAt);
    sound->SetVolume(1.0f);
    sound->SetFrequency(1.25f - RandomFloat(0.5f));
    sound->SetOn(on);
    sound->SetActive(on, false);
    sound->Update();
}

// `name` on `model`'s animation stage (the cached animation set, its loop mode set; 0x005f6f80).
bool PlayAnimation(I3D_model *model, int stage, const char *name, int mode)
{
    I3D_animation_set *anim = nullptr;
    if (g_animationCache.Get(name, &anim) != 0)
        return false;
    anim->SetPlayMode(mode);
    model->SetAnimation(anim, stage, 1.0f, 0);
    anim->Release();
    return true;
}

// A sound of the sound tables at `pos` (attached to `frame`; 0x005f6ed0).
void PlayTableSound(int chapter, int group, const S_vector4 &pos, I3D_frame *frame, int channel)
{
    S_SoundRequest request;
    request.pos = Vec(pos);
    request.posW = pos.w;
    request.flags = SOUND_SILENT_TO_AI;
    request.attachTo = frame;
    request.chapter = chapter;
    request.group = group;
    request.channel = channel;
    GetSoundModule()->Play(request);
}

}  // namespace

C_Junkers88 *g_junkers88;                               // 0x008ac9e4
int g_attackingFighters;                               // 0x008ac9e8
C_Junkers88Creator g_junkers88Creator;                  // 0x008ac9e0

C_Actor *FindJunkersTargetFor(C_Human *human, I3D_frame **frame)   // 0x005f0e40
{
    return g_junkers88 ? g_junkers88->FindTarget(human, frame) : nullptr;
}

// --- the actor ---------------------------------------------------------------------------------------

C_Junkers88::C_Junkers88() : C_Actor(ACTOR_JUNKERS88)   // 0x005ef180
{
    g_junkers88 = this;
    m_name = g_texts.Get(0x1BFE);
}

C_Junkers88::~C_Junkers88()                             // 0x005ef470
{
    g_junkers88 = nullptr;
    // (The lists' items are StopMission's.)
}

void C_Junkers88::RestartMission(bool)                  // 0x005edde0
{
    if (g_gameRole & GAME_ROLE_CLIENT)
        StopMission(false);
    if (g_gameRole & GAME_ROLE_SERVER) {
        StopMission(true);
        StartMission(true);
    }
    if (g_gameRole & GAME_ROLE_CLIENT)
        StartMission(false);
}

bool C_Junkers88::ReadProperties(C_chunk &chunk)        // 0x005f29f0
{
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        // Fix: a short chunk keeps the value (the original takes an uninitialised local).
        switch (id) {
        case PROP_HEALTH:
            chunk.Read(m_maxHealth);
            chunk.Ascend();
            break;
        case PROP_FIGHTER_HEALTH:
            chunk.Read(m_fighterHealth);
            chunk.Ascend();
            break;
        case PROP_FIGHTER_DAMAGE:
            chunk.Read(m_fighterDamage);
            chunk.Ascend();
            break;
        case PROP_UNKNOWN_400:
            chunk.Read(m_gunFov);
            chunk.Ascend();
            break;
        case PROP_UNKNOWN_500:
            chunk.Read(m_unknown32C);
            chunk.Ascend();
            break;
        default:
            // Quirk kept: entered, not left.
            break;
        }
    }
    return true;
}

void C_Junkers88::StartMission(bool server)             // 0x005ef6d0
{
    Activate(server);
    if (server)
        return;
    ResetChatter();
    m_hitsByOthers = 0;
    m_bladeParts[1] = m_bladeParts[0] = nullptr;
    m_engineParts[1] = m_engineParts[0] = nullptr;
    m_propeller[0] = m_frame->FindChildFrame("vrtule_l00", FRAME_MASK_ANY);
    m_propeller[1] = m_frame->FindChildFrame("vrtule_r00", FRAME_MASK_ANY);
    m_body = m_frame->FindChildFrame("BODY", FRAME_MASK_ANY);
    LoadSounds();
    InitSeats();
    InitSeatWeapons();
    InitParts();
    InitCamera();
    InitPropellers();
    m_health = m_maxHealth;
    m_damageRate = 0.0f;
    m_engineDown[0] = m_engineDown[1] = false;
    ShowDamage(0);
    GetGEffects()->SetHierarchyOn(m_frame, false, SOUNDS_AND_MODELS);
    m_slowTime = 0.0f;
    // The tracers' model.
    I3D_frame *odds = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    m_trace = nullptr;
    if (g_modelCache.OpenInto(static_cast<I3D_model *>(odds), "WeaponOdds") >= 0) {
        m_trace = odds->FindChildFrame("trace", FRAME_MASK_VISUAL);
        // Fix: without one there are no tracers (the original adds a reference through null).
        if (m_trace)
            m_trace->AddRef();
    }
    odds->Release();
    g_gameProfile.m_hudOption->shown = true;
    m_firepower = 1.0f;
    m_interceptorHealth = 1.0f;
}

void C_Junkers88::StopMission(bool server)              // 0x005efc10
{
    Deactivate(server);
    if (server)
        return;
    EmptySeats();
    ReleaseSounds();
    ReleaseParts();
    ReleaseCamera();
    ReleaseSeatWeapons();
    ShowDamage(-1);
    for (size_t i = m_fighters.size(); i-- > 0;)
        delete m_fighters[i];
    m_fighters.clear();
    m_body = nullptr;
    if (g_scriptModule.IsCutsceneRunning() && g_scriptModule.GetCutscene() == -1)
        g_scriptModule.EndCutscene();
    for (size_t i = m_tracers.size(); i-- > 0;) {
        if (m_tracers[i]->frame)
            m_tracers[i]->frame->Release();
        delete m_tracers[i];
    }
    m_tracers.clear();
    for (size_t i = m_tracerPool.size(); i-- > 0;) {
        if (m_tracerPool[i]->frame)
            m_tracerPool[i]->frame->Release();
        delete m_tracerPool[i];
    }
    m_tracerPool.clear();
    if (m_trace) {
        m_trace->Release();
        m_trace = nullptr;
    }
    Scene()->GetActiveCamera()->SetFOV(GetGame()->GetCutsceneFOV());
}

// --- the chatter -----------------------------------------------------------------------------------

void C_Junkers88::ResetChatter()                        // 0x005f1390
{
    for (S_Chatter &group : m_chatter)
        group.sounds.clear();
    const auto add = [](S_Chatter &group, std::initializer_list<int> ids) {
        for (int id : ids)
            group.sounds.push_back(id);
    };
    m_chatter[5].minDelay = 3000;
    m_chatter[5].range = 6500;
    for (int i = 0; i < 12; ++i)
        m_chatter[5].sounds.push_back(0x55D5FFC + i);
    m_chatter[0].minDelay = 2000;
    m_chatter[0].range = 2000;
    add(m_chatter[0], {0x55D6017, 0x55D6018, 0x55D6019, 0x55D601A, 0x55D601B, 0x55D601C, 0x55D601D, 0x55D601E});
    m_chatter[1].minDelay = 2000;
    m_chatter[1].range = 2000;
    add(m_chatter[1], {0x55D601F, 0x55D6020, 0x55D6008, 0x55D6009, 0x55D600A, 0x55D600D});
    m_chatter[2].minDelay = 2000;
    m_chatter[2].range = 2000;
    add(m_chatter[2], {0x55D6022, 0x55D6023, 0x55D6024, 0x55D6028, 0x55D6029, 0x55D600E, 0x55D600F, 0x55D6010,
                       0x55D6011, 0x55D6012});
    m_chatter[3].minDelay = 300;
    m_chatter[3].range = 300;
    add(m_chatter[3], {0x55D6025, 0x55D6026, 0x55D6027, 0x55D6013, 0x55D6014, 0x55D6015, 0x55D6016});
    // (Group 4's pause is not set.)
    add(m_chatter[4], {0x55D6021, 0x55D600B, 0x55D600C});
}

void C_Junkers88::Chatter(int group)                    // 0x005f17c0
{
    S_Chatter &chatter = m_chatter[group];
    if (chatter.sounds.empty() || chatter.delay != 0)
        return;
    int sound = -1;
    // Fix: group 4 has no pause (the original divides by its leftover allocation).
    const int random = std::rand();
    chatter.delay = (chatter.range ? random % chatter.range : 0) + chatter.minDelay;
    if (group == 5) {
        // A fighter between 20 and 60 m: its bearing (12 sectors from behind) picks the sound.
        const S_vector4 at = WorldPos4(m_frame);
        std::vector<S_Fighter *> nearby;
        for (S_Fighter *fighter : m_fighters) {
            if (fighter->down)
                continue;
            const S_vector &p = fighter->model->GetWorldPos();
            const float dx = at.x - p.x, dy = at.y - p.y, dz = at.z - p.z;
            const float square = (dz * dz + dy * dy) + dx * dx;
            if (square <= 400.0f || 3600.0f <= square)
                continue;
            nearby.push_back(fighter);
        }
        if (nearby.empty()) {
            chatter.delay = 0;
        } else {
            const S_vector &p = nearby[static_cast<uint32_t>(std::rand()) % nearby.size()]->model->GetWorldPos();
            const float toX = p.x - at.x, toZ = p.z - at.z;
            const S_matrix &m = m_frame->GetMatrix();
            float hx = m.m[2][0], hz = m.m[2][2];
            const float hy = m.m[2][1];
            const float square = (hy * hy + hz * hz) + hx * hx;
            if (!(std::fabs(square - 1.0f) < 1e-08f)) {
                if (!(square < 1e-08f)) {
                    hx = hx * (1.0f / std::sqrt(square));
                    hz = hz * (1.0f / std::sqrt(square));
                } else if (hx != 0.0f) {
                    hx = hx < 0.0f ? -1.0f : 1.0f;
                } else if (hz != 0.0f) {
                    hz = hz < 0.0f ? -1.0f : 1.0f;
                }
            }
            float heading[2] = {hx, hz};
            const float h = std::sqrt(hz * hz + hx * hx);
            if (1e-08f < h) {
                const float inv = 1.0f / h;
                heading[0] = hx * inv;
                heading[1] = inv * hz;
            }
            float to[2] = {toX, toZ};
            const float t = std::sqrt(toZ * toZ + toX * toX);
            if (1e-08f < t) {
                const float inv = 1.0f / t;
                to[0] = toX * inv;
                to[1] = inv * toZ;
            }
            sound = static_cast<int>(((PI - AngleBetween2D(heading, to)) * INV_TWO_PI) * 12.0f);
            if (sound > 11)
                sound = 11;
            else if (sound < 0)
                sound = 0;
        }
    } else {
        sound = chatter.next;
        if (static_cast<int>(chatter.sounds.size()) <= ++chatter.next)
            chatter.next = 0;
        if (group == 0)
            m_chatter[1].delay = 1500;
    }
    if (sound < 0)
        return;
    const C_fstr file("%d.wav", chatter.sounds[sound]);
    S_SoundRequest request;
    request.fileName = file.c_str();
    request.flags = SOUND_SILENT_TO_AI | SOUND_AMBIENT;
    GetSoundModule()->Play(request);
}

// --- the sounds and the propellers -----------------------------------------------------------------

void C_Junkers88::LoadSounds()                          // 0x005ef840
{
    const S_vector origin(0.0f, 0.0f, 0.0f);
    I3D_sector *sector = Scene()->GetPrimarySector();
    LoadSound(m_sounds[0], C_str("ju88mtr1.wav"), m_propeller[1], origin, 3.0f, 100.0f, true, true, 0);
    LoadSound(m_sounds[1], C_str("ju88mtr2.wav"), m_propeller[0], origin, 3.0f, 100.0f, true, true, 0);
    LoadSound(m_sounds[2], C_str("DMGMTR01.wav"), sector, origin, 3.0f, 100.0f, true, false, 0);
    LoadSound(m_sounds[3], C_str("DMGMTR02.wav"), sector, origin, 3.0f, 100.0f, true, false, 0);
    LoadSound(m_sounds[4], C_str("expmtr01.wav"), sector, origin, 100.0f, 200.0f, false, false, 0);
    LoadSound(m_sounds[5], C_str("expmtr02.wav"), sector, origin, 100.0f, 200.0f, false, false, 0);
}

void C_Junkers88::ReleaseSounds()                       // 0x005efb80
{
    for (I3D_sound *&sound : m_sounds) {
        if (sound) {
            sound->Release();
            sound = nullptr;
        }
    }
}

void C_Junkers88::InitPropellers()                      // 0x005f0a50
{
    m_blur[0] = m_frame->FindChildFrame("bluur_l00", FRAME_MASK_ANY);
    m_blur[1] = m_frame->FindChildFrame("bluur_r00", FRAME_MASK_ANY);
}

void C_Junkers88::TurnPropellers()                      // 0x005f7bb0
{
    const float t = static_cast<float>(GetGame()->GetTime());
    const S_vector axis(0.0f, 0.0f, 1.0f);
    const auto spin = [&axis](I3D_frame *frame, float angle) {
        if (frame && frame->IsOn()) {
            frame->SetWorldDir(axis, angle);
            frame->Update();
        }
    };
    spin(m_blur[0], t * 0.001f);
    spin(m_blur[1], -(t * 0.0008f));
    if (m_bladeParts[1]) {
        spin(m_bladeParts[1]->frames[1], t * 0.02f);
        spin(m_bladeParts[1]->frames[2], t * 0.002f);
    }
    if (m_bladeParts[0]) {
        spin(m_bladeParts[0]->frames[1], t * 0.016f);
        spin(m_bladeParts[0]->frames[2], t * 0.0016f);
    }
}

void C_Junkers88::EngineFire(bool right, bool sounds)   // 0x005f6ca0
{
    I3D_frame *frame = right ? m_propeller[1] : m_propeller[0];
    I3D_sound *damage = right ? m_sounds[2] : m_sounds[3];
    I3D_sound *explosion = right ? m_sounds[4] : m_sounds[5];
    if (frame) {
        g_particlesModule.CreateAt(EFFECT_SMOKE, frame->GetWorldPos(), S_vector(0.0f, 0.0f, -1.0f), frame, true, false);
        if (!sounds)
            return;
        for (I3D_sound *sound : {damage, explosion}) {
            if (sound)
                sound->LinkTo(frame, 0);
        }
        for (I3D_sound *sound : {damage, explosion}) {
            if (sound)
                sound->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
        }
    }
    if (sounds) {
        if (damage)
            damage->SetOn(true);
        if (explosion) {
            explosion->SetOn(true);
            explosion->SetVolume(4.0f);
        }
    }
}

void C_Junkers88::EngineDown(S_Part *part)              // 0x005f6e20
{
    S_Part *engine;
    if (part->blades == 0) {
        m_blur[0]->SetOn(false);
        if (m_sounds[1])
            m_sounds[1]->SetOn(false);
        EngineFire(false, true);
        // Quirk kept: the left blades fell, the right engine's part is taken (and back).
        engine = m_engineParts[1];
        m_engineDown[0] = true;
    } else {
        m_blur[1]->SetOn(false);
        if (m_sounds[0])
            m_sounds[0]->SetOn(false);
        EngineFire(m_propeller[1] != nullptr, true);
        engine = m_engineParts[0];
        m_engineDown[1] = true;
    }
    if (engine->stage == 0)
        EngineFell(engine);
}

void C_Junkers88::EngineFell(S_Part *part)              // 0x005f7e10
{
    for (size_t i = m_damage.parts.size(); i-- > 0;) {
        if (m_damage.parts[i] != part)
            continue;
        --m_damage.left;
        m_damage.parts[i] = m_damage.parts.back();
        m_damage.parts.pop_back();
        ++m_outer.left;
        part->hideIntact = true;
        m_outer.parts.push_back(part);
        part->stage = 1;
        return;
    }
}

void C_Junkers88::ShowDamage(int level)                 // 0x005f70d0
{
    auto *model = static_cast<I3D_model *>(m_frame);
    if (level == -1) {
        model->SetAnimation(nullptr, 1, 1.0f, 0);
        return;
    }
    PlayAnimation(model, 1, C_fstr("#adamage%d", level).c_str(), 2);
    if (level == 4) {
        // Quirk kept: the left engine smokes at the right propeller when the left one is there.
        if (!m_engineDown[0]) {
            EngineFire(m_propeller[0] != nullptr, false);
            m_engineDown[0] = true;
        }
        if (!m_engineDown[1]) {
            EngineFire(m_propeller[1] != nullptr, false);
            m_engineDown[1] = true;
        }
    }
}

// --- the seats -------------------------------------------------------------------------------------

C_SeatWeapon *C_Junkers88::SeatWeapon(int seat) const
{
    // (One a seat after StartMission; out of range: none.)
    return seat >= 0 && static_cast<size_t>(seat) < m_seatWeapons.size() ? m_seatWeapons[seat] : nullptr;
}

namespace {

// The digits after "SEAT" at the start of the user properties, as bits.
uint32_t SeatFlags(const I3D_frame *seat)
{
    uint32_t flags = 0;
    const char *props = seat->GetUserProps();
    if (std::strncmp(props, "SEAT", 4) == 0) {
        for (const char *c = props + 4; *c != '\0' && *c != ',' && *c != ' '; ++c)
            flags |= 1u << ((static_cast<unsigned char>(*c) - '0') & 31);
    }
    return flags;
}

}  // namespace

void C_Junkers88::InitSeats()                           // 0x005f2cc0
{
    for (int seat = SEATS; seat-- > 0;) {
        m_seats[seat] = S_Seat();
        m_seats[seat].frame = m_frame->FindChildFrame(C_fstr("SEAT%02d", seat).c_str(), FRAME_MASK_DUMMY);
        g_seatFlags[seat] = 0;
        // Fix: a model without the seat's dummy has no flags (the original reads through null).
        if (m_seats[seat].frame)
            g_seatFlags[seat] = SeatFlags(m_seats[seat].frame);
    }
}

void C_Junkers88::EmptySeats()                          // 0x005f2de0
{
    for (int seat = SEATS; seat-- > 0;) {
        if (C_Actor *occupant = m_seats[seat].occupant) {
            occupant->GetFrame()->LinkTo(Scene()->GetPrimarySector(), 1);
            ExitSeat(seat);
        }
    }
}

void C_Junkers88::InitSeatWeapons()                     // 0x005f81b0
{
    for (int seat = 0; seat < GetSeatCount(); ++seat) {
        if (m_seats[seat].frame)
            m_seats[seat].flags |= SeatFlags(m_seats[seat].frame);
        m_seatWeapons.push_back(nullptr);
        if (!(m_seats[seat].flags & 0x100))
            continue;
        auto *weapon = new C_SeatWeapon();              // _aligned_malloc(0x180, 16) in the original
        if (weapon->Init(this, m_frame, m_frame, seat))
            m_seatWeapons[seat] = weapon;
        else
            delete weapon;
    }
}

void C_Junkers88::ReleaseSeatWeapons()                  // 0x005f8300
{
    for (size_t i = m_seatWeapons.size(); i-- > 0;)
        delete m_seatWeapons[i];
    m_seatWeapons.clear();
}

bool C_Junkers88::SetSeatBlocked(int seat, bool blocked)   // 0x005f2e70
{
    if (seat < 0 || seat >= GetSeatCount() || m_seats[seat].blocked == blocked)
        return false;
    m_seats[seat].blocked = blocked;
    return true;
}

void C_Junkers88::SetSeatOccupant(int seat, C_Actor *occupant)   // 0x005f2ec0
{
    if (seat < 0 || seat >= GetSeatCount() || !occupant)
        return;
    m_seats[seat].occupant = occupant;
    I3D_frame *frame = occupant->GetFrame();
    frame->LinkTo(m_seats[seat].frame, 0);
    frame->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    frame->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    frame->Update();
    if (C_SeatWeapon *weapon = SeatWeapon(seat))
        weapon->SetGunner(static_cast<C_Human *>(occupant));
}

C_Actor *C_Junkers88::GetSeatOccupant(int seat)         // 0x005f2fa0
{
    return seat >= 0 && seat < GetSeatCount() ? m_seats[seat].occupant : nullptr;
}

int C_Junkers88::FindSeat(const C_Actor *occupant)      // 0x005f2fd0
{
    for (int seat = SEATS; seat-- > 0;) {
        if (m_seats[seat].occupant == occupant)
            return seat;
    }
    return -1;
}

C_Actor *C_Junkers88::LeaveSeat(int seat)               // 0x005f3000
{
    if (seat < 0 || seat >= GetSeatCount())
        return nullptr;
    C_Actor *occupant = m_seats[seat].occupant;
    m_seats[seat].occupant = nullptr;
    if (C_SeatWeapon *weapon = SeatWeapon(seat))
        weapon->SetGunner(nullptr);
    m_seats[seat].blocked = false;
    return occupant;
}

bool C_Junkers88::ExitSeat(int seat)                    // 0x005f30c0
{
    if (seat < 0 || seat >= GetSeatCount())
        return false;
    m_seats[seat].occupant = nullptr;
    if (C_SeatWeapon *weapon = SeatWeapon(seat))
        weapon->SetGunner(nullptr);
    return true;
}

uint32_t C_Junkers88::GetSeatFlags(int seat)            // 0x005f2e30
{
    return seat >= 0 && seat < GetSeatCount() ? g_seatFlags[seat] : 0;
}

I3D_frame *C_Junkers88::GetSeatCamera(int seat)         // 0x005f3110
{
    if (seat < 0 || seat >= GetSeatCount())
        return nullptr;
    // (Out of the seat meanwhile, so that its own dummies are not found.)
    C_Actor *occupant = m_seats[seat].occupant;
    if (occupant)
        occupant->GetFrame()->LinkTo(nullptr, 0);
    I3D_frame *camera;
    if (GetGame()->IsFirstPersonView())
        camera = m_seats[seat].frame->FindChildFrame("cameraFPV*", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    else
        camera = m_camera->model->GetChild(0);
    if (occupant) {
        occupant->GetFrame()->LinkTo(m_seats[seat].frame, 0);
        occupant->GetFrame()->Update();
    }
    return camera;
}

int C_Junkers88::GetSeatText(int seat)                  // 0x005f3060
{
    if (seat < 0 || seat >= GetSeatCount())
        return -1;
    const char *found = std::strstr(m_seats[seat].frame->GetUserProps(), "name");
    return found ? std::atoi(found + 4) : -1;
}

bool C_Junkers88::HasSeatWeapon(int seat)               // 0x005f8370
{
    return static_cast<uint32_t>(seat) < m_seatWeapons.size() && m_seatWeapons[seat] != nullptr;
}

int C_Junkers88::GetSeatWeaponRounds(int seat)          // 0x005f83b0
{
    return HasSeatWeapon(seat) ? m_seatWeapons[seat]->GetRounds() : 0;
}

S_vector C_Junkers88::GetSeatWeaponAim(int seat)        // 0x005f84b0
{
    return HasSeatWeapon(seat) ? m_seatWeapons[seat]->GetAim() : S_vector(0.0f, 0.0f, 0.0f);
}

S_vector C_Junkers88::GetSeatWeaponOrigin(int seat)     // 0x005f8440
{
    return HasSeatWeapon(seat) ? m_seatWeapons[seat]->GetOrigin() : S_vector(0.0f, 0.0f, 0.0f);
}

float C_Junkers88::UnknownAC(int seat)                  // 0x005f8500
{
    // (The original returns the seat weapon's 0 and, without one, the flag in the FPU's place.)
    return HasSeatWeapon(seat) ? m_seatWeapons[seat]->UnknownAC() : 0.0f;
}

C_ItemDef *C_Junkers88::GetSeatWeaponDef(int seat)      // 0x005f8180
{
    // Fix: the original asks HasSeatWeapon and calls through the seat's entry whatever it says.
    return HasSeatWeapon(seat) ? m_seatWeapons[seat]->GetItemDef() : nullptr;
}

void C_Junkers88::ApplySeatControls(int seat, float *axes, float *buttons)   // 0x005f83e0
{
    if (!GetGame()->IsFirstPersonView()) {
        auto *occupant = static_cast<C_Human *>(GetSeatOccupant(seat));
        if (!occupant || occupant->GetControllerType() == HUMAN_CONTROLLER_PLAYER)
            return;
    }
    if (C_SeatWeapon *weapon = SeatWeapon(seat))
        weapon->ApplyControls(axes, buttons);
}

void C_Junkers88::ShowSeatView(int seat, bool on)       // 0x005f8490
{
    if (C_SeatWeapon *weapon = SeatWeapon(seat))
        weapon->ShowView(on);
}

bool C_Junkers88::UnknownC8(int seat, int *a, int *b, int *c)   // 0x005f8570
{
    C_SeatWeapon *weapon = SeatWeapon(seat);
    return weapon && weapon->UnknownC8(a, b, c);
}

bool C_Junkers88::IsSeatFirstPersonOnly(int seat)       // 0x005f8540
{
    return seat >= 0 && seat < GetSeatCount();
}

// --- the parts ---------------------------------------------------------------------------------------

C_Junkers88::S_Part::S_Part(I3D_frame *damaged, I3D_frame *model, bool engine_, bool outer_)   // 0x005f6840
    : engine(engine_), outer(outer_)
{
    frames[1] = damaged;
    // The intact frame: the name without its 'd'; the stump: with an 'f' instead.
    std::string name = damaged->GetShortName();
    name.erase(name.size() - 1);
    frames[0] = model->FindChildFrame(name.c_str(), FRAME_MASK_ANY);
    if (!frames[0])
        return;
    frames[2] = model->FindChildFrame((name + "f").c_str(), FRAME_MASK_ANY);
    // The other stages where the intact frame is, hidden.
    const S_vector pos = frames[0]->GetWorldPos();
    const S_quat rot = frames[0]->GetRot();
    frames[1]->SetPos(pos);
    frames[1]->SetRot(rot);
    frames[1]->SetOn(false);
    frames[1]->Update();
    stages = 1;
    if (frames[2]) {
        frames[2]->SetPos(pos);
        frames[2]->SetRot(rot);
        frames[2]->SetOn(false);
        frames[2]->Update();
        stages = 2;
    }
}

I3D_frame *C_Junkers88::S_Part::Shown() const           // 0x005f6ae0
{
    if (stage < 0 || stage > 2)
        return nullptr;
    I3D_frame *frame = frames[stage];
    return frame && frame->IsOn() ? frame : nullptr;
}

bool C_Junkers88::S_Part::HasNext() const               // 0x005f6b10
{
    if (stage == 0)
        return frames[1] != nullptr;
    if (stage == 1)
        return frames[2] != nullptr;
    return false;
}

void C_Junkers88::S_Part::Save(C_chunk &chunk) const    // 0x005f6700
{
    chunk.Write(stage);
    chunk.Write(hideIntact);
}

void C_Junkers88::S_Part::Load(C_chunk &chunk)          // 0x005f6750
{
    int saved = 0;
    chunk.Read(saved);
    chunk.Read(hideIntact);
    stage = saved;
    // Quirk kept: only a frame already shown is switched on.
    if (I3D_frame *frame = Shown())
        frame->SetOn(true);
}

void C_Junkers88::InitParts()                           // 0x005f07e0
{
    m_damage.left = m_engines.left = m_outer.left = 0;
    m_frame->EnumFrames(&EnumPart, this, FRAME_MASK_VISUAL, nullptr);
    m_damage.initial = m_damage.left;
    m_engines.initial = m_engines.left;
    m_outer.initial = m_outer.left;
}

uint32_t __stdcall C_Junkers88::EnumPart(I3D_frame *frame, void *junkers)   // 0x005f0830
{
    // (A copy of the name; at most 1023 characters in the original.)
    const std::string name = frame->GetShortName();
    if (name.size() >= 2 && name[name.size() - 1] == 'd')
        static_cast<C_Junkers88 *>(junkers)->AddPart(frame);
    return 0;
}

void C_Junkers88::AddPart(I3D_frame *frame)             // 0x005efe80
{
    const char *name = frame->GetShortName();
    const auto in = [name](const char *const *names, int count) {
        for (int i = count; i-- > 0;) {
            if (_stricmp(names[i], name) == 0)
                return i;
        }
        return -1;
    };
    const bool engine = in(ENGINE_NAMES, 2) != -1;
    const bool outer = in(OUTER_NAMES, 4) != -1;
    const int dependent = in(DEPENDENT_NAMES, 6);
    const int engineIndex = in(ENGINE_NAMES, 2);
    const int blades = in(BLADE_NAMES, 2);
    auto *part = new S_Part(frame, m_frame, engine, outer);
    if (dependent == -1) {
        m_damage.parts.push_back(part);
        m_damage.left += part->stages;
        m_engines.left += engine;
        m_outer.left += outer;
        if (engineIndex != -1) {
            m_engineParts[engineIndex] = part;
            // The parts hanging on this engine (made before it).
            for (size_t i = m_dependent.size(); i-- > 0;) {
                for (int k = engineIndex * 3; k < engineIndex * 3 + 3; ++k) {
                    if (_stricmp(DEPENDENT_NAMES[k], m_dependent[i]->frames[1]->GetShortName()) == 0)
                        part->hanging.push_back(m_dependent[i]);
                }
            }
        }
    } else {
        m_dependent.push_back(part);
        // On the engine of its side (made before it).
        const char *engineName = ENGINE_NAMES[dependent / 3];
        for (size_t i = m_damage.parts.size(); i-- > 0;) {
            if (_stricmp(engineName, m_damage.parts[i]->frames[1]->GetShortName()) == 0)
                m_damage.parts[i]->hanging.push_back(part);
        }
    }
    if (blades != -1) {
        part->blades = blades;
        m_bladeParts[blades] = part;
        // (Their intact frame hidden: the blur shows them turning.)
        part->frames[0]->SetOn(false);
        part->frames[0]->Update();
    }
    // The parts thrown off with another one's last stage (rows from the last).
    for (int row = 4; row-- > 0;) {
        const char *const *names = THROWN_ROWS[row];
        for (int k = 9; k-- > 1;) {
            if (_stricmp(name, names[k]) == 0) {
                // Thrown with the row's part (made before it): then the next row.
                for (size_t i = m_parts.size(); i-- > 0;) {
                    if (_stricmp(m_parts[i]->frames[1]->GetShortName(), names[0]) == 0) {
                        m_parts[i]->thrown.push_back(part);
                        break;
                    }
                }
                break;
            }
            if (_stricmp(names[0], name) == 0) {
                // The row's part: those thrown with it (made before it).
                for (size_t i = m_parts.size(); i-- > 0;) {
                    if (_stricmp(m_parts[i]->frames[1]->GetShortName(), names[k]) == 0) {
                        part->thrown.push_back(m_parts[i]);
                        break;
                    }
                }
            }
        }
    }
    part->index = static_cast<int>(m_parts.size());
    m_parts.push_back(part);
}

void C_Junkers88::ReleaseParts()                        // 0x005f08a0
{
    for (size_t i = m_parts.size(); i-- > 0;)
        delete m_parts[i];
    m_parts.clear();
    m_damage.parts.clear();
    m_damage.broken.clear();
    m_engines.parts.clear();
    m_engines.broken.clear();
    m_outer.parts.clear();
    m_outer.broken.clear();
    m_dependent.clear();
    // (The debris' frames stay where they fell.)
    for (size_t i = m_debris.size(); i-- > 0;)
        delete m_debris[i];
    m_debris.clear();
    // The model back as it was (the parts' frames).
    char file[1024];
    std::strncpy(file, static_cast<I3D_model *>(m_frame)->GetFileName(), sizeof file - 1);
    file[sizeof file - 1] = '\0';
    g_modelCache.OpenInto(static_cast<I3D_model *>(m_frame), file);
}

void C_Junkers88::Advance(S_Part *part)                 // 0x005f6b40
{
    I3D_frame *current = nullptr, *next = nullptr;
    bool changed = true;
    if (part->stage == 0) {
        current = part->frames[0];
        next = part->frames[1];
        part->stage = 1;
    } else if (part->stage == 1) {
        part->stage = 2;
        if (part->hideIntact) {
            part->frames[0]->SetOn(false);
            part->frames[0]->Update();
        }
        current = part->frames[1];
        next = part->frames[2];
    } else {
        changed = false;
    }
    if (changed) {
        if (current) {
            if (part->blades != -1) {
                EngineDown(part);
                part->blades = -1;
            }
            current->SetOn(false);
            current->Update();
        }
        if (next) {
            // The next stage's own pieces fly off; the current one's children move onto it.
            for (int k = next->NumChildren(); k-- > 0;)
                ThrowDebris(next->GetChild(k));
            if (current) {
                for (int k = current->NumChildren(); k-- > 0;) {
                    I3D_frame *child = current->GetChild(k);
                    child->LinkTo(next, 1);
                    child->Update();
                }
            }
            next->SetOn(true);
            next->Update();
        }
    }
    for (size_t i = part->hanging.size(); i-- > 0;) {
        while (part->hanging[i]->stage < part->stage)
            Advance(part->hanging[i]);
    }
    if (part->stage == 2) {
        for (size_t i = part->thrown.size(); i-- > 0;) {
            if (I3D_frame *frame = part->thrown[i]->Shown())
                ThrowDebris(frame);
        }
    }
}

void C_Junkers88::BreakPart(int index, std::vector<S_Part *> &from, std::vector<S_Part *> &done,
                            std::vector<S_Part *> &outer, std::vector<S_Part *> &engines, int times)   // 0x005f8040
{
    S_Part *part = from[index];
    for (int i = times; i != 0; --i)
        Advance(part);
    std::vector<S_Part *> *into = &done;
    if (part->HasNext()) {
        if (part->outer)
            into = &outer;
        else if (part->engine)
            into = &engines;
        else
            return;
    }
    into->push_back(part);
    // (Found again from the last; else the index.)
    int at = index;
    for (size_t i = from.size(); i-- > 0;) {
        if (from[i] == part) {
            at = static_cast<int>(i);
            break;
        }
    }
    from[at] = from.back();
    from.pop_back();
}

void C_Junkers88::UpdateDamage(int frameTime)           // 0x005f71e0
{
    if (m_health == 0.0f)
        return;
    // Dropped: the copy protection (the 0x008ca840 / 0x008caf40 check) set the damage to 1000 a
    // second after two minutes in a cracked copy.
    if (m_damageRate == 0.0f)
        return;
    if (m_health != 0.0f)
        Chatter(1);
    // The damage look in five steps of the health.
    const float step = 1.0f / (m_maxHealth * 0.2f);
    const int before = static_cast<int>(5.0f - step * m_health);
    float health = m_health - static_cast<float>(frameTime) * m_firepower * m_damageRate * 0.001f;
    if (health < 0.0f)
        health = 0.0f;
    m_health = health;
    const int after = static_cast<int>(5.0f - step * health);
    if (after != before && after <= 4)
        ShowDamage(after);
    // Under 75 %: the parts break down to a quarter of them at 0.
    const float threeQuarters = m_maxHealth * 0.75f;
    if (threeQuarters > m_health) {
        const float share = m_health / threeQuarters - 1.0f;
        const int target = static_cast<int>(share * static_cast<float>(static_cast<int>(m_damage.initial * 0.75f)) +
                                             static_cast<float>(m_damage.initial));
        while (target < m_damage.left) {
            --m_damage.left;
            // Fix: none left to break (the original divides by 0).
            if (m_damage.parts.empty())
                break;
            const uint32_t pick = static_cast<uint32_t>(std::rand()) % m_damage.parts.size();
            BreakPart(static_cast<int>(pick), m_damage.parts, m_damage.broken, m_outer.parts, m_engines.parts, 1);
        }
    }
    // Under 25 %: the engines.
    const float quarter = m_maxHealth * 0.25f;
    if (quarter > m_health) {
        const float share = m_health / quarter - 1.0f;
        const int target = static_cast<int>(share * static_cast<float>(static_cast<int>(m_engines.initial * 0.3f)) +
                                             static_cast<float>(m_engines.initial));
        while (target < m_engines.left) {
            --m_engines.left;
            if (!m_engines.parts.empty()) {
                const uint32_t pick = static_cast<uint32_t>(std::rand()) % m_engines.parts.size();
                BreakPart(static_cast<int>(pick), m_engines.parts, m_engines.broken, m_outer.parts,
                          m_engines.broken, 1);
            } else {
                // An engine that has not broken yet: both stages at once.
                for (size_t i = m_damage.parts.size(); i-- > 0;) {
                    if (m_damage.parts[i]->engine) {
                        BreakPart(static_cast<int>(i), m_damage.parts, m_engines.broken, m_outer.parts,
                                  m_engines.broken, 2);
                        break;
                    }
                }
            }
        }
    }
    if (m_health != 0.0f)
        return;
    // Shot down: an outer part breaks off, the bomber falls.
    Chatter(4);
    --m_outer.left;
    if (!m_outer.parts.empty()) {
        const uint32_t pick = static_cast<uint32_t>(std::rand()) % m_outer.parts.size();
        BreakPart(static_cast<int>(pick), m_outer.parts, m_outer.broken, m_outer.broken, m_outer.broken, 1);
    } else {
        for (size_t i = m_damage.parts.size(); i-- > 0;) {
            if (m_damage.parts[i]->outer) {
                BreakPart(static_cast<int>(i), m_damage.parts, m_outer.broken, m_outer.broken, m_outer.broken, 2);
                break;
            }
        }
    }
    ShowDamage(-1);
    ShootDown();
    m_fall.Start();
    for (size_t i = m_fighters.size(); i-- > 0;)
        m_fighters[i]->Disengage();
    g_scriptModule.RunCutscene(JUNKERS_CUTSCENE);
}

void C_Junkers88::ShootDown()                           // 0x005f6ff0
{
    PlayAnimation(static_cast<I3D_model *>(m_frame), 1, C_fstr("#jushotdwn%d", std::rand() % 8 + 1).c_str(), 1);
    GetGame()->SetPlayerInputEnabled(false);
    if (GetGame()->IsFirstPersonView())
        GetGame()->ShowFirstPersonView(false);
    GetGame()->GetCameraModule()->SetTarget(m_body, 0x2D);
}

// --- the debris ------------------------------------------------------------------------------------

C_Junkers88::S_Debris::S_Debris(I3D_frame *piece, const S_vector4 &throwVelocity)   // 0x005f5960
    : velocity(throwVelocity), frame(piece)
{
    frame->LinkTo(Scene()->GetPrimarySector(), 1);
    frame->Update();
    floor = frame->GetPos().y;
    const float a = RandomFloat(4.0f) - 2.0f;
    const float b = RandomFloat(4.0f) - 2.0f;
    const float c = RandomFloat(4.0f) - 2.0f;
    // (The original's w is a stack leftover.)
    spin = {c, b, a, 0.0f};
}

C_Junkers88::S_Debris::S_Debris(C_chunk &chunk)         // 0x005f57c0 (DS 0x006184e0)
{
    chunk.Read(&velocity, 12);
    chunk.Read(&spin, 12);
    chunk.Read(floor);
    char name[256];
    int16_t length = 0;
    chunk.Read(&length, 2);
    if (length == 0) {
        name[0] = '\0';
    } else if (length < 0xFF) {
        chunk.Read(name, length);
    } else {
        chunk.Read(name, 0xFE);
        name[0xFE] = '\0';
        for (int rest = length - 0xFE; rest-- > 0;) {
            char skipped;
            chunk.Read(&skipped, 1);
        }
    }
    S_vector pos;
    chunk.Read(&pos, 12);
    float file[4];                                      // {s, x, y, z}
    chunk.Read(file, 16);
    frame = I3DGetDriver()->FindFrame(Scene(), name);
    if (!frame)
        return;
    frame->LinkTo(Scene()->GetPrimarySector(), 1);
    frame->SetPosInline(pos);
    frame->SetRot(S_quat(file[0], file[1], file[2], file[3]));
    frame->Update();
}

void C_Junkers88::S_Debris::Save(C_chunk &chunk) const  // 0x005f5640
{
    chunk.Write(&velocity, 12);
    chunk.Write(&spin, 12);
    chunk.Write(floor);
    const char *name = frame->GetName();
    const auto length = static_cast<uint16_t>(std::strlen(name) + 1);
    chunk.Write(&length, 2);
    chunk.Write(name, length);
    chunk.Write(&frame->GetMatrix().m[3][0], 12);
    S_quat rot;
    rot.Make(frame->GetMatrix());
    const float file[4] = {rot.s, rot.v.x, rot.v.y, rot.v.z};
    chunk.Write(file, 16);
}

bool C_Junkers88::S_Debris::Update(int frameTime)       // 0x005f5a30
{
    const float t = static_cast<float>(frameTime) * 0.001f;
    velocity.y = velocity.y - t * 9.81f;
    const S_vector &at = frame->GetPos();
    const float y = velocity.y * t + at.y;
    frame->SetPosInline(S_vector(t * velocity.x + at.x, y, at.z + t * velocity.z));
    if (!(floor - 500.0f <= y))
        return false;
    const S_matrix turn = (RotateXMatrix(t * spin.x) * RotateYMatrix(t * spin.y)) * RotateZMatrix(t * spin.z);
    frame->SetRot(frame->GetRot().RotateByMatrix(turn));
    frame->Update();
    return true;
}

void C_Junkers88::ThrowDebris(I3D_frame *piece)         // 0x005f0580
{
    const float a = RandomFloat(6.0f) - 3.0f;
    const float b = RandomFloat(2.0f) + 4.0f;
    const float c = RandomFloat(6.0f) - 3.0f;
    // (The original's w is a stack leftover.)
    const S_vector4 velocity = {m_velocity.x * 0.8f + c, m_velocity.y * 0.8f + b, m_velocity.z * 0.8f + a, 0.0f};
    m_debris.push_back(new S_Debris(piece, velocity));  // _aligned_malloc(0x30, 16) in the original
}

void C_Junkers88::UpdateDebris(int frameTime)           // 0x005f75f0
{
    for (size_t i = m_debris.size(); i-- > 0;) {
        if (m_debris[i]->Update(frameTime))
            continue;
        delete m_debris[i];
        m_debris[i] = m_debris.back();
        m_debris.pop_back();
    }
}

// --- the tracers -----------------------------------------------------------------------------------

C_Junkers88::S_Tracer::S_Tracer(I3D_frame *model, const S_vector &pos, const S_vector &dir)   // 0x005f91b0
{
    frame = I3DGetDriver()->CreateVisual(model->GetVisualType());
    frame->Duplicate(model);
    frame->LinkTo(Scene()->GetPrimarySector(), 0);
    frame->SetScale(S_vector(5.0f, 5.0f, 25.0f));
    Place(pos, dir);
}

C_Junkers88::S_Tracer::S_Tracer(I3D_frame *model, C_chunk &chunk)   // 0x005f8f70
{
    frame = I3DGetDriver()->CreateVisual(model->GetVisualType());
    frame->Duplicate(model);
    frame->LinkTo(Scene()->GetPrimarySector(), 0);
    frame->SetScale(S_vector(5.0f, 5.0f, 25.0f));
    // Fix: a short chunk keeps the defaults (the original takes uninitialised locals).
    S_vector pos(0.0f, 0.0f, 0.0f), dir(0.0f, 0.0f, 1.0f);
    chunk.Read(&pos, 12);
    chunk.Read(&dir, 12);
    chunk.Read(distance);
    frame->SetPosInline(pos);
    frame->SetWorldDir(dir, 0.0f);
    frame->Update();
}

void C_Junkers88::S_Tracer::Place(const S_vector &pos, const S_vector &dir)   // 0x005f8c50
{
    frame->SetPosInline(pos);
    frame->SetWorldDir(dir, 0.0f);
    frame->Update();
    distance = 500.0f;
    // (The ray is the unit direction: something within a metre.)
    S_vector hit;
    if (g_collisionWorld.RayTestStatic(pos, dir, &hit, nullptr, 0)) {
        const float dx = hit.x - pos.x, dy = hit.y - pos.y, dz = hit.z - pos.z;
        distance = LengthOf((dz * dz + dy * dy) + dx * dx);
    }
}

void C_Junkers88::S_Tracer::Reuse(const S_vector &pos, const S_vector &dir)   // 0x005f8c20
{
    if (frame)
        frame->SetOn(true);
    Place(pos, dir);
}

bool C_Junkers88::S_Tracer::Update(int frameTime)       // 0x005f88c0
{
    const float step = static_cast<float>(frameTime) * 0.5f;
    distance = distance - step;
    if (distance <= 0.0f)
        return false;
    const float a = RandomFloat(0.4f) - 0.2f;
    const float b = RandomFloat(0.4f) - 0.2f;
    const float c = RandomFloat(0.4f) - 0.2f;
    bool normalize;
    S_vector dir = frame->GetLocalDirUnnormalized(&normalize);
    if (normalize)
        NormalizeBy(dir, (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x);
    const S_vector &at = frame->GetPos();
    const S_vector pos((dir.x * step + at.x) + c, (dir.y * step + at.y) + b, (dir.z * step + at.z) + a);
    frame->SetPosInline(pos);
    const S_vector &camera = Scene()->GetActiveCamera()->GetWorldPos();
    const float dx = camera.x - pos.x, dy = camera.y - pos.y, dz = camera.z - pos.z;
    if ((dz * dz + dy * dy) + dx * dx < 625.0f)
        PlayTableSound(4, 0x0B, {pos.x, pos.y, pos.z, 0.0f}, nullptr, 0);
    frame->Update();
    return true;
}

void C_Junkers88::S_Tracer::Save(C_chunk &chunk) const  // 0x005f8d70
{
    chunk.Write(&frame->GetMatrix().m[3][0], 12);
    const S_vector dir = WorldDir(frame);
    chunk.Write(&dir, 12);
    chunk.Write(distance);
}

void C_Junkers88::Fire(const S_vector &pos, const S_vector &dir)   // 0x005f85a0
{
    S_Tracer *tracer;
    if (m_tracerPool.empty()) {
        // Fix: without the tracers' model none (the original copies through null).
        if (!m_trace)
            return;
        tracer = new S_Tracer(m_trace, pos, dir);
    } else {
        tracer = m_tracerPool.back();
        m_tracerPool.pop_back();
        tracer->Reuse(pos, dir);
    }
    m_tracers.push_back(tracer);
}

void C_Junkers88::UpdateTracers(int frameTime)          // 0x005f87f0
{
    for (size_t i = m_tracers.size(); i-- > 0;) {
        if (m_tracers[i]->Update(frameTime))
            continue;
        if (m_tracers[i]->frame)
            m_tracers[i]->frame->SetOn(false);
        m_tracerPool.push_back(m_tracers[i]);
        m_tracers[i] = m_tracers.back();
        m_tracers.pop_back();
    }
}

// --- the fall --------------------------------------------------------------------------------------------

void C_Junkers88::S_Fall::Init(I3D_frame *model, float sphere)   // 0x005f3300
{
    frame = model->FindChildFrame("BODY", FRAME_MASK_ANY);
    radius = sphere;
    // (Active from the start: the model's body may hit the ground at any time.)
    falling = true;
    pos = WorldPos4(model);
}

void C_Junkers88::S_Fall::Start()                       // 0x005f34a0
{
    if (!frame)
        return;
    pos = WorldPos4(frame);
    falling = true;
}

bool C_Junkers88::S_Fall::Update()                      // 0x005f3360
{
    if (!falling || !frame) {
        if (frame)
            pos = WorldPos4(frame);
        return true;
    }
    const S_vector from = Vec(pos);
    const S_vector &at = frame->GetWorldPos();
    const S_vector move(at.x - pos.x, at.y - pos.y, at.z - pos.z);
    pos = WorldPos4(frame);
    S_vector hit;
    if (!CollisionSphereTest(from, move, radius, &hit, nullptr, 0xFFFFFFFF, 0))
        return true;
    // (The original's w is a stack leftover.)
    pos = {hit.x, hit.y, hit.z, pos.w};
    frame->SetPos(hit);
    frame->Update();
    falling = false;
    return false;
}

void C_Junkers88::S_Fall::Save(C_chunk &chunk) const    // 0x005f31f0
{
    chunk.Write(&pos, 12);
    chunk.Write(falling);
}

void C_Junkers88::S_Fall::Load(C_chunk &chunk)          // 0x005f3230
{
    chunk.Read(&pos, 12);
    chunk.Read(falling);
}

// --- the camera and the paths ------------------------------------------------------------------------

void C_Junkers88::InitCamera()                          // 0x005efdd0
{
    m_camera = new S_Path(m_frame, "#aircraftdum", "#aircraftA");
    m_fall.Init(m_frame, 3.0f);
}

void C_Junkers88::ReleaseCamera()                       // 0x005efe50
{
    delete m_camera;
    m_camera = nullptr;
}

C_Junkers88::S_Path::S_Path(I3D_frame *owner_, const char *modelFile, const char *animName)   // 0x005f5f20
    : owner(owner_), name(animName ? animName : "")
{
    I3D_frame *frame = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    model = static_cast<I3D_model *>(frame);
    follow = true;
    if (g_modelCache.OpenInto(model, modelFile) != 0) {
        model->Release();
        model = nullptr;
        return;
    }
    SetCallback(&PathNote);
    model->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    model->SetRot(S_quat(1.0f, 0.0f, 0.0f, 0.0f));
    index = 0;
    Next(1);
}

C_Junkers88::S_Path::~S_Path()                          // 0x005f60e0
{
    if (model)
        model->Release();
}

void C_Junkers88::S_Path::SetCallback(I3D_CALLBACK *callback)   // 0x005f60b0
{
    if (I3D_frame *dummy = model->GetChild(0))
        dummy->SetCallback(callback, 0x1000);
}

void C_Junkers88::S_Path::Update(int frameTime)         // 0x005f6100
{
    if (!model)
        return;
    // Fix: without an animation nothing moves (the original calls through null).
    I3D_animation_set *anim = model->GetAnimation(0);
    if (!anim)
        return;
    const int length = anim->GetEndTime();
    const int time = model->GetAnimTime(0);
    if (time + frameTime >= length) {
        model->Tick(length - time - 1);
        Next(time - length + frameTime);
    } else {
        model->Tick(frameTime);
    }
    model->Update();
    if (!follow || !owner->GetParent())
        return;
    I3D_frame *dummy = model->GetChild(0);
    if (!dummy)
        return;
    S_matrix inverse;
    inverse.Inverse(owner->GetParent()->GetMatrix());
    owner->SetLocalMatrix(dummy->GetMatrix() * inverse);
}

void C_Junkers88::S_Path::Next(int carry)               // 0x005f62a0
{
    ++index;
    const C_fstr file("%s%d", name.c_str(), index);
    I3D_animation_set *anim = nullptr;
    if (g_animationCache.Get(file.c_str(), &anim) != 0)
        return;
    model->SetAnimation(anim, 0, 1.0f, 0);
    // (The original keeps the cache's extra reference.)
    anim->Release();
    model->SetAnimTime(0, 0);
    model->Tick(carry);
    model->Update();
}

void C_Junkers88::S_Path::Save(C_chunk &chunk) const    // 0x005f5c40
{
    const char *text = name.c_str();
    const auto length = static_cast<uint16_t>(std::strlen(text) + 1);
    chunk.Write(&length, 2);
    chunk.Write(text, length);
    chunk.Write(index);
    chunk.Write(follow);
    chunk.Write(model->GetAnimTime(0));
}

void C_Junkers88::S_Path::Load(C_chunk &chunk)          // 0x005f5d20
{
    char text[256];
    chunk.ReadSizedString(text, 0xFF);
    name = text;
    chunk.Read(index);
    chunk.Read(follow);
    int time = 0;
    chunk.Read(time);
    // The animation again, its notes off meanwhile.
    --index;
    SetCallback(&NoPathNote);
    Next(time);
    SetCallback(&PathNote);
}

int __stdcall C_Junkers88::PathNote(I3D_frame *frame, uint32_t message, uintptr_t note, uintptr_t)   // 0x005f54c0
{
    if (!g_junkers88 || !(message & 0x1000) || !note)
        return 0;
    const auto *text = reinterpret_cast<const char *>(note);
    if (std::strstr(text, "fire_start"))
        g_junkers88->StartAttack(frame->GetOwnerModel());
    else if (std::strstr(text, "fire_end"))
        g_junkers88->EndAttack(frame->GetOwnerModel());
    return 0;
}

// --- the fighters --------------------------------------------------------------------------------------

C_Junkers88::S_Fighter *C_Junkers88::FindFighter(const I3D_frame *model) const   // 0x005f0ca0
{
    for (size_t i = m_fighters.size(); i-- > 0;) {
        if (m_fighters[i]->model == model)
            return m_fighters[i];
    }
    return nullptr;
}

C_Junkers88::S_Fighter *C_Junkers88::FindFighterByPath(const I3D_frame *pathModel) const   // 0x005f0c50
{
    for (size_t i = m_fighters.size(); i-- > 0;) {
        if (m_fighters[i]->path && m_fighters[i]->path->model == pathModel)
            return m_fighters[i];
    }
    return nullptr;
}

void C_Junkers88::StartAttack(const I3D_frame *pathModel)   // 0x005f0cf0
{
    S_Fighter *fighter = FindFighterByPath(pathModel);
    m_damageRate = m_fighterDamage + m_damageRate;
    // Fix: a note of no fighter's path (the original calls through null).
    if (fighter)
        fighter->StartAttack();
}

void C_Junkers88::EndAttack(const I3D_frame *pathModel) // 0x005f0d20
{
    S_Fighter *fighter = FindFighterByPath(pathModel);
    const float rate = m_damageRate - m_fighterDamage;
    m_damageRate = rate < 0.0f ? 0.0f : rate;
    if (fighter)
        fighter->StopAttack();
}

void C_Junkers88::SetState(int count)                   // 0x005f0a90
{
    if (!m_fighters.empty())
        return;
    std::vector<int> paths;
    for (int path = 1; path < 10; ++path)
        paths.push_back(path);
    float factor = 1.0f;
    if (g_gameProfile.m_playMode->unknown0C == 0)
        factor = 0.7f;
    else if (g_gameProfile.m_playMode->unknown0C == 1)
        factor = 0.85f;
    while (count-- != 0) {
        // Fix: more than nine: the original divides by 0.
        if (paths.empty())
            break;
        const size_t n = paths.size();
        const uint32_t pick = static_cast<uint32_t>(std::rand()) % n;
        m_fighters.push_back(new S_Fighter(paths[pick], FIGHTER_MODEL, factor * m_fighterHealth));
        paths[pick] = paths.back();
        paths.pop_back();
    }
}

void C_Junkers88::Hit(E_HitType, float amount, const S_vector &, const S_vector &, C_Actor *attacker,
                      int flags)                        // 0x005f0d70
{
    const auto *part = reinterpret_cast<const I3D_frame *>(static_cast<intptr_t>(flags));
    // Fix: a hit without the part (or an attacker) counts as nothing (the original reads
    // through null).
    if (!part || part->GetOwnerModel() == m_frame)
        return;
    auto *human = static_cast<C_Human *>(attacker);
    if (!human || human->GetControllerType() != HUMAN_CONTROLLER_PLAYER) {
        if (m_hitsByOthers > 1)
            return;
        ++m_hitsByOthers;
    }
    S_Fighter *fighter = FindFighter(part->GetOwnerModel());
    if (!fighter)
        return;
    const float factor = m_interceptorHealth == 0.0f ? 0.0f : 1.0f / m_interceptorHealth;
    fighter->Damage(factor * amount);
    g_junkers88->Chatter(2);
}

C_Actor *C_Junkers88::FindTarget(C_Human *human, I3D_frame **frame)   // 0x005f0e60
{
    if (static_cast<int>(GetGame()->GetTime()) < 10000)
        return nullptr;
    if (m_health == 0.0f)
        return nullptr;
    std::vector<S_Fighter *> seen;
    const S_vector4 from4 = WorldPos4(human->GetBodyFrame());
    const S_vector from = Vec(from4);
    for (size_t i = m_fighters.size(); i-- > 0;) {
        S_Fighter *fighter = m_fighters[i];
        // (Fix: a fighter without its model is no target; the original reads through null.)
        if (fighter->down || !fighter->model)
            continue;
        const S_vector at = fighter->model->GetWorldPos();
        S_vector d(at.x - from.x, at.y - from.y, at.z - from.z);
        NormalizeBy(d, (d.z * d.z + d.y * d.y) + d.x * d.x);
        S_vector view = human->GetViewDir();
        NormalizeBy(view, (view.y * view.y + view.z * view.z) + view.x * view.x);
        if (!(0.0f < (view.z * d.z + view.y * d.y) + d.x * view.x))
            continue;
        S_vector hitPos, hitNormal;
        const S_CollisionVolume *hit = CollisionRayTest(from, at - from, &hitPos, &hitNormal, 0, 0);
        if (hit && hit->actor && hit->actor == this)
            seen.push_back(fighter);
    }
    S_Fighter *nearest = nullptr;
    float best = 3.40282347e+38f;
    for (size_t i = seen.size(); i-- > 0;) {
        const S_vector &at = seen[i]->model->GetWorldPos();
        const float dx = at.x - from.x, dy = at.y - from.y, dz = at.z - from.z;
        const float square = (dz * dz + dy * dy) + dx * dx;
        if (square < best) {
            nearest = seen[i];
            best = square;
        }
    }
    if (!nearest)
        return nullptr;
    *frame = nearest->model;
    return this;
}

C_Junkers88::S_Fighter::S_Fighter(int index, const char *modelFile, float hitPoints)   // 0x005f5310
{
    health = hitPoints;
    attacking = down = false;
    smoke[0] = smoke[1] = -1;
    if (!CreateModel(index, modelFile))
        return;
    LoadSounds();
    path = new S_Path(model, "#airfightdum", C_fstr("#airfight_0%d_A", index).c_str());
    PlayAnimation(model, 1, "#adamage0", 2);
    PlayAnimation(model, 2, "#bfcanygrup", 2);
    InitVolume();
    g_collisionWorld.AddVolume(&volume);
    inWorld = true;
    fireTime = hitTime[0] = hitTime[1] = 0;
    InitGuns();
}

C_Junkers88::S_Fighter::S_Fighter(C_chunk &chunk, const char *modelFile)   // 0x005f40e0
{
    chunk.Read(pathIndex);
    if (!CreateModel(pathIndex, modelFile))
        return;
    // Fix: a short chunk keeps the defaults (the original leaves the rest uninitialised).
    chunk.Read(health);
    chunk.Read(attacking);
    chunk.Read(down);
    chunk.Read(inWorld);
    chunk.Read(smoke[0]);
    chunk.Read(smoke[1]);
    chunk.Read(fireTime);
    chunk.Read(hitTime[0]);
    chunk.Read(hitTime[1]);
    LoadSounds();
    S_Path *flight = new S_Path(model, "#airfightdum", C_fstr("#airfight_0%d_A", pathIndex).c_str());
    flight->Load(chunk);
    if (!down)
        flight->SetCallback(&PathNote);
    int time = 0;
    chunk.Read(time);
    if (!down) {
        PlayAnimation(model, 1, "#adamage0", 2);
    } else {
        char name[256];
        chunk.ReadSizedString(name, 0xFF);
        PlayAnimation(model, 1, name, 1);
    }
    model->Tick(time);
    InitVolume();
    fall.Load(chunk);
    if (inWorld)
        g_collisionWorld.AddVolume(&volume);
    InitGuns();
    path = flight;
}

C_Junkers88::S_Fighter::~S_Fighter()                    // 0x005f5540
{
    if (inWorld)
        g_collisionWorld.RemoveVolume(&volume);
    for (int particle : smoke) {
        if (particle != -1)
            g_particlesModule.Finish(particle);
    }
    delete path;
    if (model)
        model->Release();
    for (I3D_sound *&sound : guns) {
        if (sound) {
            sound->Release();
            sound = nullptr;
        }
    }
    if (motor) {
        motor->Release();
        motor = nullptr;
    }
}

bool C_Junkers88::S_Fighter::CreateModel(int index, const char *modelFile)   // 0x005f4910
{
    pathIndex = index;
    model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    model->SetName(C_fstr("interceptor_%d", index).c_str());
    model->LinkTo(g_junkers88->GetFrame(), 0);
    if (g_modelCache.OpenInto(model, modelFile) == 0)
        return true;
    model->Release();
    model = nullptr;
    return false;
}

void C_Junkers88::S_Fighter::LoadSounds()               // 0x005f4740
{
    const S_vector origin(0.0f, 0.0f, 0.0f);
    gun = nullptr;
    const C_fstr motorFile("motor%d.wav", std::rand() % 4 + 1);
    LoadSound(motor, motorFile, model, origin, 20.0f, 400.0f, true, false, std::rand() % 5000);
    if (motor)
        motor->SetVolume(1.0f);
    if (motor)
        motor->SetRange(20.0f, 400.0f, 0.3f, 0.3f);
    for (int i = 1; i <= 5; ++i) {
        const C_fstr gunFile("kul_d%d.wav", i);
        I3D_sound *&sound = guns[i - 1];
        LoadSound(sound, gunFile, model, origin, 10.0f, 300.0f, false, false, std::rand() % 5000);
        if (sound)
            sound->SetRange(10.0f, 300.0f, 0.3f, 0.3f);
    }
}

void C_Junkers88::S_Fighter::InitVolume()               // 0x005f4e50 (DS 0x00617910)
{
    fall.Init(model, 2.0f);
    // A sphere around the model's hierarchy, of the bomber's.
    MakeFrameBoxVolume(volume, invMatrix, model, COLLISION_TYPE_LIGHT_OBJECT, g_junkers88);
    volume.flags = COLLISION_SHAPE_SPHERE;
}

void C_Junkers88::S_Fighter::InitGuns()                 // 0x005f5100
{
    for (int i = 5; i-- > 0;) {
        if (I3D_frame *frame = model->FindChildFrame(GUN_NAMES[i], FRAME_MASK_ANY))
            gunFrames.push_back(frame);
    }
    for (int i = 2; i-- > 0;) {
        if (I3D_frame *frame = model->FindChildFrame(HIDDEN_NAMES[i], FRAME_MASK_ANY)) {
            frame->SetOn(false);
            frame->Update();
        }
    }
}

bool C_Junkers88::S_Fighter::Update(int frameTime)      // 0x005f3a70
{
    S_vector before(0.0f, 0.0f, 0.0f), velocity(0.0f, 0.0f, 0.0f);
    bool moved = false;
    if (model)
        before = model->GetWorldPos();
    if (path)
        path->Update(frameTime);
    if (model) {
        model->Tick(frameTime);
        const S_vector &after = model->GetWorldPos();
        moved = true;
        const float k = 1.0f / (static_cast<float>(frameTime) * 0.001f);
        velocity = S_vector((after.x - before.x) * k, (after.y - before.y) * k, (after.z - before.z) * k);
    }
    if (motor && !motor->IsOn())
        motor->SetOn(true);
    if (attacking) {
        if (!gun || !gun->IsPlaying(0)) {
            gun = guns[std::rand() % 5];
            if (gun) {
                gun->SetOn(true);
                gun->Play(0);
            }
        }
        Fire(frameTime);
        HitBomber(frameTime);
    }
    if (moved) {
        if (gun)
            gun->SetVelocity(velocity);
        if (motor)
            motor->SetVelocity(velocity);
    }
    if (down) {
        // The fall's animation over: gone.
        if (I3D_animation_set *anim = model->GetAnimation(1)) {
            if (anim->GetEndTime() <= model->GetAnimTime(1)) {
                model->SetAnimation(nullptr, 1, 1.0f, 0);
                for (int &particle : smoke) {
                    if (particle != -1) {
                        g_particlesModule.Finish(particle);
                        particle = -1;
                    }
                }
                return false;
            }
        }
        if (!fall.Update()) {
            g_particlesModule.CreateAt(EFFECT_CRASH_A, Vec(fall.pos), S_vector(0.0f, 1.0f, 0.0f), nullptr, true, false);
            g_particlesModule.CreateAt(EFFECT_CRASH_B, Vec(fall.pos), S_vector(0.0f, 1.0f, 0.0f), nullptr, true, false);
            return false;
        }
    }
    if (inWorld) {
        // Quirk kept: the box's centre the model's bound's (in its own space), the radius as w.
        const I3D_bound_volume &bound = model->GetBound();
        invMatrix.Inverse(model->GetMatrix());
        volume.center = bound.center;
        volume.centerW = bound.radius;
        g_collisionWorld.MoveVolume(&volume);
    }
    return true;
}

void C_Junkers88::S_Fighter::Fire(int frameTime)        // 0x005f36e0
{
    fireTime -= frameTime;
    while (fireTime < 0) {
        fireTime += 200;
        // Fix: a model without guns fires none (the original divides by 0).
        if (gunFrames.empty())
            continue;
        I3D_frame *barrel = gunFrames[static_cast<uint32_t>(std::rand()) % gunFrames.size()];
        const S_vector dir = WorldDir(barrel);
        g_junkers88->Fire(barrel->GetWorldPos(), dir);
    }
}

void C_Junkers88::S_Fighter::HitBomber(int frameTime)   // 0x005f38f0
{
    hitTime[0] -= frameTime;
    while (hitTime[0] < 0) {
        hitTime[0] += std::rand() % 100 + 200;
        I3D_frame *bomber = g_junkers88->GetFrame();
        const float a = RandomFloat(1.0f) - 0.5f;
        const float b = RandomFloat(1.0f) - 0.5f;
        const float c = RandomFloat(1.0f) - 0.5f;
        // (The original's w is a stack leftover.)
        PlayTableSound(4, 0x12, {a, b, c, 0.0f}, bomber, 1);
    }
    hitTime[1] -= frameTime;
    while (hitTime[1] < 0) {
        hitTime[1] += std::rand() % 125 + 250;
        I3D_frame *bomber = g_junkers88->GetFrame();
        const float a = RandomFloat(1.0f) - 0.5f;
        const float b = RandomFloat(1.0f) - 0.5f;
        const float c = RandomFloat(1.0f) - 0.5f;
        PlayTableSound(4, 0x0B, {a, b, c, 0.0f}, bomber, 0);
    }
}

void C_Junkers88::S_Fighter::Damage(float amount)       // 0x005f34e0
{
    health = health - amount;
    if (health <= 0.0f) {
        health = 0.0f;
        ShootDown();
    }
}

void C_Junkers88::S_Fighter::ShootDown()                // 0x005f3510
{
    if (down)
        return;
    Disengage();
    if (g_junkers88->m_health != 0.0f)
        g_junkers88->Chatter(3);
    PlayAnimation(model, 1, C_fstr("#bfshotdwn%d", std::rand() % 3 + 1).c_str(), 1);
    fall.Start();
    I3D_frame *wings[2] = {model->FindChildFrame("left_wing", FRAME_MASK_ANY),
                           model->FindChildFrame("right_wing", FRAME_MASK_ANY)};
    for (int i = 0; i < 2; ++i) {
        if (wings[i]) {
            smoke[i] = g_particlesModule.CreateAt(EFFECT_SMOKE, wings[i]->GetWorldPos(), S_vector(0.0f, 0.0f, -1.0f),
                                                  wings[i], true, false);
        }
    }
    down = true;
    inWorld = false;
    g_collisionWorld.RemoveVolume(&volume);
}

void C_Junkers88::S_Fighter::StartAttack()              // 0x005f45a0
{
    attacking = true;
    ++g_attackingFighters;
    if (g_junkers88->m_health != 0.0f)
        g_junkers88->Chatter(0);
}

void C_Junkers88::S_Fighter::StopAttack()               // 0x005f3e10
{
    attacking = false;
    --g_attackingFighters;
}

void C_Junkers88::S_Fighter::Disengage()                // 0x005f3e30
{
    // (Fix: a fighter whose model did not load has no path; the original calls through it.)
    if (!path)
        return;
    if (attacking)
        g_junkers88->EndAttack(path->model);
    path->SetCallback(&NoPathNote);
}

void C_Junkers88::S_Fighter::Save(C_chunk &chunk) const // 0x005f3e70
{
    chunk.Write(pathIndex);
    chunk.Write(health);
    chunk.Write(attacking);
    chunk.Write(down);
    chunk.Write(inWorld);
    chunk.Write(smoke[0]);
    chunk.Write(smoke[1]);
    chunk.Write(fireTime);
    chunk.Write(hitTime[0]);
    chunk.Write(hitTime[1]);
    path->Save(chunk);
    chunk.Write(model->GetAnimTime(0));
    if (down) {
        // The fall's animation by its file name.
        const char *file = model->GetAnimation(1)->GetFileName();
        if (!file)
            file = "";
        if (const char *slash = std::strrchr(file, '\\'))
            file = slash + 1;
        const auto length = static_cast<uint16_t>(std::strlen(file) + 1);
        chunk.Write(&length, 2);
        chunk.Write(file, length);
    }
    fall.Save(chunk);
}

// --- the update ----------------------------------------------------------------------------------------

void C_Junkers88::Tick(int timeDelta)                   // 0x005f76a0
{
    for (size_t i = m_seatWeapons.size(); i-- > 0;) {
        if (m_seatWeapons[i])
            m_seatWeapons[i]->Tick(timeDelta);
    }
    if (m_health != 0.0f)
        g_junkers88->Chatter(5);
}

void C_Junkers88::Update(int frameTime)                 // 0x005f7710
{
    if (g_scriptModule.IsCutsceneRunning() && g_scriptModule.GetCutscene() != JUNKERS_CUTSCENE)
        return;
    if (!m_frame->IsOn())
        return;
    for (int i = 6; i-- > 0;) {
        S_Chatter &chatter = m_chatter[i];
        if (chatter.delay != 0) {
            chatter.delay -= frameTime;
            if (chatter.delay < 0)
                chatter.delay = 0;
        }
    }
    const S_vector4 before = WorldPos4(m_frame);
    if (m_camera->follow) {
        m_camera->Update(frameTime);
    } else if (m_slowTime != 0.0f) {
        // Down: the camera slows over 7 s, its milliseconds' fractions kept.
        const float t = Time(frameTime);
        float left = m_slowTime - t;
        if (left <= 0.0f)
            left = 0.0f;
        m_slowTime = left;
        const float slowed = m_slowTime * 0.000142857141f * t;
        int ms = static_cast<int>(slowed);
        const float fraction = (slowed - static_cast<float>(ms)) + m_slowFraction;
        m_slowFraction = fraction;
        if (fraction > 1.0f) {
            const int whole = static_cast<int>(fraction);
            ms += whole;
            m_slowFraction = fraction - static_cast<float>(whole);
        }
        m_camera->Update(ms);
    }
    m_frame->Tick(frameTime);
    m_frame->Update();
    for (size_t i = m_seatWeapons.size(); i-- > 0;) {
        if (m_seatWeapons[i])
            m_seatWeapons[i]->Update(frameTime);
    }
    // The fighters fade in between 9 and 11 s of the game.
    const auto time = static_cast<int>(GetGame()->GetTime());
    float alpha = 0.0f;
    if (time > 11000)
        alpha = 1.0f;
    else if (time > 9000)
        alpha = 1.0f - static_cast<float>(11000 - time) * 0.0005f;
    for (size_t i = m_fighters.size(); i-- > 0;) {
        // (Fix: a fighter without its model only updates; the original calls through null.)
        if (m_fighters[i]->model)
            m_fighters[i]->model->SetHierarchyAlpha(alpha);
        if (m_fighters[i]->Update(frameTime))
            continue;
        delete m_fighters[i];
        m_fighters[i] = m_fighters.back();
        m_fighters.pop_back();
        PlayTableSound(6, 5, {0.0f, 0.0f, 0.0f, 0.0f}, Scene()->GetActiveCamera(), 4);
    }
    if (frameTime != 0) {
        const float k = 1000.0f / Time(frameTime);
        const S_vector &at = m_frame->GetWorldPos();
        m_velocity = {(at.x - before.x) * k, (at.y - before.y) * k, (at.z - before.z) * k, before.w};
    }
    UpdateDamage(frameTime);
    UpdateDebris(frameTime);
    TurnPropellers();
    UpdateTracers(frameTime);
    if (m_fall.Update())
        return;
    // On the ground: the crash, the camera slowing.
    g_particlesModule.CreateAt(EFFECT_CRASH_A, Vec(m_fall.pos), S_vector(0.0f, 1.0f, 0.0f), nullptr, true, false);
    g_particlesModule.CreateAt(EFFECT_CRASH_B, Vec(m_fall.pos), S_vector(0.0f, 1.0f, 0.0f), nullptr, true, false);
    PlayTableSound(6, 5, {0.0f, 0.0f, 0.0f, 0.0f}, Scene()->GetActiveCamera(), 4);
    static_cast<I3D_model *>(m_frame)->SetAnimation(nullptr, 1, 1.0f, 0);
    m_camera->follow = false;
    m_slowTime = 7000.0f;
    m_slowFraction = 0.0f;
}

// --- the saved game -------------------------------------------------------------------------------------

void C_Junkers88::SaveParts(C_chunk &chunk, uint16_t id, const S_PartGroup &group) const   // 0x005ede30
{
    chunk.Begin(id);
    chunk.Write(group.left);
    for (const std::vector<S_Part *> *list : {&group.parts, &group.broken}) {
        chunk.Write(static_cast<int>(list->size()));
        for (const S_Part *part : *list)
            chunk.Write(part->index);
    }
    chunk.Ascend();
}

void C_Junkers88::LoadParts(C_chunk &chunk, S_PartGroup &group)   // 0x005ee010
{
    chunk.Read(group.left);
    for (std::vector<S_Part *> *list : {&group.parts, &group.broken}) {
        int count = 0;
        chunk.Read(count);
        for (int i = 0; i < count; ++i) {
            int index = 0;
            chunk.Read(index);
            // Fix: an index out of range is skipped (the original takes what lies there).
            if (index >= 0 && static_cast<size_t>(index) < m_parts.size())
                list->push_back(m_parts[index]);
        }
    }
    chunk.Ascend();
}

bool C_Junkers88::SaveGame(C_chunk &chunk)              // 0x005ee1e0
{
    C_Actor::SaveGame(chunk);
    chunk.Begin(CHUNK_SAVE);
    if (m_frame->IsOn()) {
        chunk.Begin(CHUNK_SHOWN);
        chunk.Ascend();
    }
    chunk.WriteChunk(CHUNK_VELOCITY, &m_velocity, 12);
    chunk.Begin(CHUNK_CAMERA);
    m_camera->Save(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_FIGHTERS);
    chunk.Write(static_cast<int>(m_fighters.size()));
    for (const S_Fighter *fighter : m_fighters)
        fighter->Save(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_DEBRIS);
    chunk.Write(static_cast<int>(m_debris.size()));
    for (const S_Debris *debris : m_debris)
        debris->Save(chunk);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_FIREPOWER, m_firepower);
    chunk.WriteChunk(CHUNK_INTERCEPTOR_HEALTH, m_interceptorHealth);
    // Quirk kept: written twice.
    chunk.WriteChunk(CHUNK_HEALTH, m_health);
    chunk.WriteChunk(CHUNK_HEALTH, m_health);
    chunk.WriteChunk(CHUNK_DAMAGE_RATE, m_damageRate);
    chunk.Begin(CHUNK_LEFT_ENGINE);
    chunk.Write(m_engineDown[0]);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_RIGHT_ENGINE, m_engineDown[1]);
    chunk.Begin(CHUNK_FALL);
    m_fall.Save(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_TRACERS);
    chunk.Write(static_cast<int>(m_tracers.size()));
    for (const S_Tracer *tracer : m_tracers)
        tracer->Save(chunk);
    chunk.Ascend();
    SaveParts(chunk, CHUNK_DAMAGE_PARTS, m_damage);
    SaveParts(chunk, CHUNK_ENGINE_PARTS, m_engines);
    SaveParts(chunk, CHUNK_OUTER_PARTS, m_outer);
    chunk.WriteChunk(CHUNK_HITS, m_hitsByOthers);
    chunk.Begin(CHUNK_STAGES);
    for (const S_Part *part : m_parts)
        part->Save(chunk);
    chunk.Ascend();
    chunk.Ascend();
    return true;
}

bool C_Junkers88::LoadGame(C_chunk &chunk)              // 0x005ee830
{
    C_Actor::LoadGame(chunk);
    uint16_t id = 0;
    if (!chunk.Descend(id) || id != CHUNK_SAVE) {
        chunk.Ascend();
        return true;
    }
    const auto readLeaf = [&chunk](auto &value) {
        chunk.Read(value);
        chunk.Ascend();
    };
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_SHOWN:
            GetGEffects()->SetHierarchyOn(m_frame, true, SOUNDS_AND_MODELS);
            chunk.Ascend();
            break;
        case CHUNK_VELOCITY:
            // (The w the original takes is a stack leftover.)
            chunk.Read(&m_velocity, 12);
            chunk.Ascend();
            break;
        case CHUNK_CAMERA:
            m_camera->Load(chunk);
            chunk.Ascend();
            break;
        case CHUNK_FIGHTERS: {
            int count = 0;
            chunk.Read(count);
            for (int i = 0; i < count; ++i)
                m_fighters.push_back(new S_Fighter(chunk, FIGHTER_MODEL));   // _aligned_malloc(0x120, 16)
            chunk.Ascend();
            break;
        }
        case CHUNK_DEBRIS: {
            int count = 0;
            chunk.Read(count);
            for (int i = 0; i < count; ++i)
                m_debris.push_back(new S_Debris(chunk));
            chunk.Ascend();
            break;
        }
        case CHUNK_HEALTH:
            readLeaf(m_health);
            break;
        case CHUNK_DAMAGE_RATE:
            readLeaf(m_damageRate);
            break;
        case CHUNK_LEFT_ENGINE:
            readLeaf(m_engineDown[0]);
            break;
        case CHUNK_RIGHT_ENGINE:
            readLeaf(m_engineDown[1]);
            break;
        case CHUNK_FALL:
            m_fall.Load(chunk);
            chunk.Ascend();
            break;
        case CHUNK_TRACERS: {
            int count = 0;
            chunk.Read(count);
            for (int i = 0; i < count; ++i) {
                // Fix: without the tracers' model the tracers are skipped (the original copies
                // through null).
                if (m_trace)
                    m_tracers.push_back(new S_Tracer(m_trace, chunk));
            }
            chunk.Ascend();
            break;
        }
        case CHUNK_DAMAGE_PARTS:
            m_damage.parts.clear();
            m_damage.broken.clear();
            LoadParts(chunk, m_damage);
            break;
        case CHUNK_ENGINE_PARTS:
            m_engines.parts.clear();
            m_engines.broken.clear();
            LoadParts(chunk, m_engines);
            break;
        case CHUNK_OUTER_PARTS:
            m_outer.parts.clear();
            m_outer.broken.clear();
            LoadParts(chunk, m_outer);
            break;
        case CHUNK_STAGES:
            for (S_Part *part : m_parts)
                part->Load(chunk);
            chunk.Ascend();
            break;
        case CHUNK_HITS:
            readLeaf(m_hitsByOthers);
            break;
        case CHUNK_FIREPOWER:
            readLeaf(m_firepower);
            break;
        case CHUNK_INTERCEPTOR_HEALTH:
            readLeaf(m_interceptorHealth);
            break;
        default:
            chunk.Ascend();
            break;
        }
    }
    chunk.Ascend();
    return true;
}

// --- creator -----------------------------------------------------------------------------------------------

C_Actor *C_Junkers88Creator::Create()                   // 0x005edd20
{
    return new C_Junkers88();                           // _aligned_malloc(0x330, 16) in the original
}

void C_Junkers88Creator::Destroy(C_Actor *actor)        // 0x005edda0
{
    delete actor;
}

// Registers the creator (static initialiser 0x005edd00, thunk 0x005edcf0).
static const bool g_junkers88CreatorRegistered = [] {
    g_actorCreators[ACTOR_JUNKERS88] = &g_junkers88Creator;
    return true;
}();
