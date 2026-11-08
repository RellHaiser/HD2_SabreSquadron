#include "game/sound_module.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actor.h"
#include "game/game.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_table.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

// Constructed by the static initializer 0x00433580.
C_SoundModule g_soundModule(MODULE_SOUND, "Sound");

C_SoundModule *GetSoundModule()
{
    return &g_soundModule;
}

namespace {

constexpr int FADE_IN_TIME = 2000;                      // ms of silence after a mission (re)start
constexpr int SECTOR_UPDATE_PERIOD = 1000;              // ms between sector checks of moving sound frames
constexpr float LISTENER_EAR_HEIGHT = 0.25f;            // above an AI listener's frame
constexpr float UNDERWATER_DAMPING = 0.4f;
constexpr float CLOSED_PORTAL_DAMPING = 0.5f;
constexpr float MAX_OPEN_AREA = 40.0f;                  // m^2
constexpr float PI = 3.1415927f;

enum : uint16_t {
    CHUNK_SOUND_MODULE = 2000,
    CHUNK_MUSIC_TRACK = 0xC1C,
    CHUNK_OBSOLETE_FLAG = 4000,
    CHUNK_MUSIC_POSITION = 5000,
};

int s_sectorUpdateTimer = 0;                            // 0x00899940
bool s_warnedNoSample = false;                          // 0x0089993c
bool s_warnedNullSample = false;                        // 0x0089993d

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

// Events of the module run in the server pass, or on a pure client.
bool RunsInPass(bool server) { return server || !IsServer(); }

// Distance from `a` to `b`; `factor` receives how much of a sound gets from
// sector `sectorB` into `sectorA` (sectors are looked up when null): sounds
// between the outside and a sector are damped by that sector's opening
// (0x00433330; a sixth, unused argument there, which PortalDistance passes on).
float SectorDistance(float &factor, const S_vector &a, const S_vector &b, I3D_sector *sectorA,
                     I3D_sector *sectorB)
{
    I3D_scene *scene = GetGame()->GetScene();
    if (!sectorA)
        sectorA = scene->GetSector(a);
    if (!sectorB)
        sectorB = scene->GetSector(b);
    factor = 1.0f;
    if (sectorA != sectorB) {
        I3D_sector *outdoor = scene->GetPrimarySector();
        I3D_sector *indoor = sectorA == outdoor ? sectorB : sectorB == outdoor ? sectorA : nullptr;
        const auto *acoustics = indoor ? static_cast<const S_SectorAcoustics *>(indoor->GetUserData()) : nullptr;
        if (acoustics)
            factor = acoustics->outdoorFactor;
    }
    return (a - b).Magnitude();
}

// Like SectorDistance(), but between two indoor sectors the sound only gets
// through one sector in between at most, damped by closed portals; FLT_MAX if it
// does not get through (0x00432be0).
float PortalDistance(float &factor, const S_vector &a, const S_vector &b, I3D_sector *sectorA,
                     I3D_sector *sectorB)
{
    I3D_scene *scene = GetGame()->GetScene();
    if (!sectorA)
        sectorA = scene->GetSector(a);
    if (!sectorB)
        sectorB = scene->GetSector(b);
    factor = 1.0f;
    const float distance = (a - b).Magnitude();
    if (sectorA == sectorB)
        return distance;

    I3D_sector *outdoor = scene->GetPrimarySector();
    if (sectorA == outdoor || sectorB == outdoor) {
        // (The original returns the distance scaled by the damping.)
        const float d = SectorDistance(factor, a, b, sectorA, sectorB);
        return d * factor;
    }

    // Sectors next to A, and how open the way into each is.
    std::vector<I3D_sector *> neighbours;
    std::vector<float> openness;
    const auto visit = [&](I3D_portal *portal, I3D_sector *other) {
        const bool open = portal->IsOn();
        if (other == sectorB) {
            if (!open)
                factor = CLOSED_PORTAL_DAMPING;
            return true;
        }
        neighbours.push_back(other);
        openness.push_back(open ? 1.0f : CLOSED_PORTAL_DAMPING);
        return false;
    };
    for (int i = sectorA->NumPortals(); i-- > 0;) {
        I3D_portal *portal = sectorA->GetPortal(i);
        if (visit(portal, portal->GetConnected()))
            return distance;
    }
    for (int i = sectorA->NumIncomingPortals(); i-- > 0;) {
        I3D_portal *portal = sectorA->GetIncomingPortal(i);
        if (visit(portal, portal->GetOwner()))
            return distance;
    }

    // A sector next to B that is also next to A.
    const auto meet = [&](I3D_portal *portal, I3D_sector *other) {
        for (std::size_t j = neighbours.size(); j-- > 0;) {
            if (neighbours[j] != other)
                continue;
            factor *= openness[j];
            // (sic: the original applies A's openness again for a closed portal of B)
            if (!portal->IsOn())
                factor *= openness[j] * CLOSED_PORTAL_DAMPING;
            return true;
        }
        return false;
    };
    for (int i = sectorB->NumPortals(); i-- > 0;) {
        I3D_portal *portal = sectorB->GetPortal(i);
        if (meet(portal, portal->GetConnected()))
            return distance;
    }
    for (int i = sectorB->NumIncomingPortals(); i-- > 0;) {
        I3D_portal *portal = sectorB->GetIncomingPortal(i);
        if (meet(portal, portal->GetOwner()))
            return distance;
    }
    factor = 1.0f;
    return FLT_MAX;
}

// Intensity of a sound `distance` away: 1 up to the near range, falling to 0 at
// the far range.
float Intensity(float distance, float nearRange, float farRange)
{
    return distance > nearRange ? 1.0f - (distance - nearRange) / (farRange - nearRange) : 1.0f;
}

// Where an AI listener hears from.
S_vector EarPosition(I3D_frame *frame)
{
    S_vector pos = frame->GetWorldPos();
    pos.y += LISTENER_EAR_HEIGHT;
    return pos;
}

S_vector ToVector(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}

C_Human *AsHuman(C_Actor *actor)
{
    return actor && actor->GetType() == ACTOR_HUMAN ? static_cast<C_Human *>(actor) : nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// S_SoundFrame

S_SoundFrame::S_SoundFrame(I3D_sound *frameSound, bool track)
    : sound(frameSound),
      trackSector(track),
      sector(GetGame()->GetScene()->GetSector(frameSound->GetWorldPos())),
      baseVolume(frameSound->GetVolumeInline())
{
    sound->AddRef();
}

S_SoundFrame::~S_SoundFrame()
{
    sound->Release();
}

void S_SoundFrame::SetSectorFactor(float factor)
{
    sectorFactor = factor;
    sound->SetVolume(factor * baseVolume);
    sound->Update();
}

// ---------------------------------------------------------------------------
// construction, module events

C_SoundModule::C_SoundModule(int id, const char *name)
    : C_Module(id, name)
{
    for (int channel = 0; channel < SOUND_CHANNELS; ++channel)
        SetChannelLimit(channel, 3);
}

// Only the containers are freed (0x004335d0); sounds are released by StopAll()
// before the engine shuts down.
C_SoundModule::~C_SoundModule() = default;

void C_SoundModule::LoadMissionData(bool server)
{
    if (RunsInPass(server))
        StopAll(true, true);
}

void C_SoundModule::FreeMissionData(bool server)
{
    if (RunsInPass(server))
        StopAll(true, false);
}

void C_SoundModule::StartMission(bool server)
{
    if (!RunsInPass(server))
        return;
    StopAll(true, true);
    g_soundTable.Load();
    InitSectorAcoustics();
    m_listenerSector = nullptr;
    m_fadeInTime = FADE_IN_TIME;
    ISndGetDriver()->SetVolume(0.0f);
}

void C_SoundModule::StopMission(bool server)
{
    if (!RunsInPass(server))
        return;
    StopAll(true, false);
    FreeSectorAcoustics();
}

void C_SoundModule::RestartMission(bool)
{
    StopAll(false, false);
    m_listenerSector = nullptr;
    m_fadeInTime = FADE_IN_TIME;
    ISndGetDriver()->SetVolume(0.0f);
}

bool C_SoundModule::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_SOUND_MODULE);
    int position = 0;
    if (IsMusicPlaying())
        m_music->GetPosition(&position);
    chunk.WriteChunk(CHUNK_MUSIC_TRACK, m_musicTrack);
    chunk.WriteChunk(CHUNK_MUSIC_POSITION, position);
    chunk.Ascend();
    return true;
}

bool C_SoundModule::LoadGame(C_chunk &chunk)
{
    uint16_t id = 0;
    if (!chunk.Descend(id))
        return true;
    if (id == CHUNK_SOUND_MODULE) {
        uint16_t sub = 0;
        while (!chunk.AtEnd() && chunk.Descend(sub)) {
            switch (sub) {
            case CHUNK_MUSIC_TRACK:
                chunk.Read(m_musicTrack);
                break;
            case CHUNK_OBSOLETE_FLAG: {
                uint8_t ignored = 0;
                chunk.Read(ignored);
                break;
            }
            case CHUNK_MUSIC_POSITION: {
                int position = 0;
                chunk.Read(position);
                chunk.Ascend();
                SetBackgroundMusic(m_musicTrack, position);
                FadeBackgroundMusic(1.0f, 3000);
                continue;
            }
            default:
                break;
            }
            chunk.Ascend();
        }
    }
    chunk.Ascend();
    return true;
}

void C_SoundModule::Update(int timeDelta)
{
    if (IsClient()) {
        // Finished sounds free their slots (unless the slot is kept).
        for (int i = static_cast<int>(m_slots.size()); i-- > 0;) {
            S_SoundSlot *slot = m_slots[i];
            if (!slot || !slot->active)
                continue;
            if (!slot->sound) {
                slot->active = false;
                m_freeSlots.push_back(i);
                continue;
            }
            if (!slot->sound->IsPlaying(0)) {
                if (!(slot->flags & SOUND_KEEP_SLOT)) {
                    int index = i;
                    Stop(&index);
                }
            } else if (slot->flags & SOUND_FADE_IN) {
                float volume = timeDelta * 0.001f + slot->sound->GetVolumeInline();
                if (slot->volume < volume) {
                    volume = slot->volume;
                    slot->flags &= ~SOUND_FADE_IN;
                }
                slot->sound->SetVolume(volume);
                slot->sound->Update();
            }
        }

        // Background music fade (and the second half of CrossfadeMusic()).
        if (m_musicFade != m_musicFadeTarget) {
            const float step = timeDelta * m_musicFadeSpeed;
            if (m_musicFade <= m_musicFadeTarget) {
                m_musicFade += step;
                if (m_musicFade >= m_musicFadeTarget)
                    m_musicFade = m_musicFadeTarget;
            } else {
                m_musicFade -= step;
                if (m_musicFade <= m_musicFadeTarget) {
                    m_musicFade = m_musicFadeTarget;
                    if (m_crossfadePending) {
                        m_crossfadePending = false;
                        SetBackgroundMusic(m_crossfadeTrack, m_crossfadePosition);
                        FadeBackgroundMusic(1.0f, m_crossfadeTime);
                    }
                }
            }
            if (IsMusicPlaying())
                m_music->SetVolume(m_musicVolume * m_musicFade);
        }

        if (m_overlayFade != m_overlayFadeTarget) {
            const float previous = m_overlayFade;
            m_overlayFade -= timeDelta * m_overlayFadeSpeed;
            if (previous <= m_overlayFadeTarget) {
                if (m_overlayFade >= m_overlayFadeTarget)
                    m_overlayFade = m_overlayFadeTarget;
            } else if (m_overlayFade <= m_overlayFadeTarget) {
                m_overlayFade = m_overlayFadeTarget;
            }
            if (IsOverlayPlaying())
                m_overlay->SetVolume(m_musicVolume * m_overlayFade);
        }

        // The overlay track has ended: bring the background music back up.
        if (m_overlay && (m_overlay->GetState() == ISNDSTREAM_STATE_FINISHED || !m_overlay->IsPlaying())) {
            if (m_overlay->IsPlaying()) {
                m_overlay->Stop();
                m_overlay->Close();
            }
            m_overlay->Release();
            m_overlay = nullptr;
            FadeBackgroundMusic(1.0f, 10000);
        }
        // The background music loops.
        if (m_music && m_music->GetState() == ISNDSTREAM_STATE_FINISHED) {
            m_music->SetPosition(0);
            m_music->Play();
        }
    }

    C_game *game = GetGame();
    if (!game)
        return;
    I3D_scene *scene = game->GetScene();

    // Moving sound frames are checked for a sector change once a second.
    s_sectorUpdateTimer += timeDelta;
    if (s_sectorUpdateTimer > SECTOR_UPDATE_PERIOD) {
        s_sectorUpdateTimer = (s_sectorUpdateTimer - SECTOR_UPDATE_PERIOD - 1) % SECTOR_UPDATE_PERIOD + 1;
        if (m_listenerSector) {
            for (std::size_t i = m_soundFrames.size(); i-- > 0;) {
                S_SoundFrame *entry = m_soundFrames[i];
                if (!entry->trackSector)
                    continue;
                I3D_sector *sector = scene->GetSector(entry->sound->GetWorldPos());
                if (sector == entry->sector)
                    continue;
                entry->sector = sector;
                float factor = 1.0f;
                SectorDistance(factor, S_vector(0, 0, 0), S_vector(0, 0, 0), sector, m_listenerSector);
                entry->SetSectorFactor(factor);
            }
        }
    }

    I3D_sector *listenerSector = game->GetCameraModule()->GetListenerSector();
    if (listenerSector != m_listenerSector) {
        for (std::size_t i = m_soundFrames.size(); i-- > 0;) {
            S_SoundFrame *entry = m_soundFrames[i];
            float factor = 1.0f;
            SectorDistance(factor, S_vector(0, 0, 0), S_vector(0, 0, 0), entry->sector, listenerSector);
            entry->SetSectorFactor(factor);
        }
        m_listenerSector = listenerSector;
    }

    if (m_fadeInTime && (m_fadeInTime -= timeDelta) < 1)
        m_fadeInTime = 0;
    if (!m_masterVolumeApplied) {
        if (m_fadeInTime) {
            // (Integer division in the original: the sound stays off for the whole
            // fade-in instead of rising.)
            float fade = 0.0f;
            if (m_fadeInTime < FADE_IN_TIME)
                fade = static_cast<float>((FADE_IN_TIME - m_fadeInTime) / FADE_IN_TIME);
            ISndGetDriver()->SetVolume(fade * m_masterVolume);
            return;
        }
        ISndGetDriver()->SetVolume(m_masterVolume);
        m_masterVolumeApplied = true;
    }
}

void C_SoundModule::StopAll(bool releaseTableSounds, bool keepMusic)
{
    if (!IsClient() && !g_gameProfile.IsCooperativeGame())
        return;

    for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
        for (int &slot : m_channelSlots[channel])
            slot = -1;
        m_channelNext[channel] = 0;
    }
    for (std::size_t i = m_slots.size(); i-- > 0;) {
        if (S_SoundSlot *slot = m_slots[i]) {
            ReleaseSlotSound(slot);
            delete slot;
        }
    }
    m_slots.clear();
    for (std::size_t i = m_soundFrames.size(); i-- > 0;)
        delete m_soundFrames[i];
    m_soundFrames.clear();
    m_freeSlots.clear();
    m_listeners.clear();

    if (m_music && !keepMusic) {
        SetBackgroundMusic(-1, 0);
        m_music->Release();
        m_music = nullptr;
    }
    if (m_overlay) {
        m_overlay->Release();
        m_overlay = nullptr;
    }
    if (!keepMusic) {
        m_musicFade = 1.0f;
        m_musicFadeTarget = 1.0f;
        m_crossfadePending = false;
    }
    m_overlayFade = 1.0f;
    m_overlayFadeTarget = 1.0f;
    if (releaseTableSounds)
        g_soundTable.ReleaseSounds();
    m_masterVolumeApplied = false;
}

// ---------------------------------------------------------------------------
// sector acoustics

void C_SoundModule::InitSectorAcoustics()
{
    GetGame()->GetScene()->GetPrimarySector()->EnumFrames(AddSectorAcoustics, this, ENUMF_SECTOR, 0);
}

uint32_t __stdcall C_SoundModule::AddSectorAcoustics(I3D_frame *frame, void *context)
{
    auto *module = static_cast<C_SoundModule *>(context);
    auto *sector = static_cast<I3D_sector *>(frame);
    auto *acoustics = new S_SectorAcoustics{sector, 0.0f, 0.1f};
    sector->SetUserData(acoustics);

    // Area of the openings to the outside (closed ones count a tenth).
    I3D_sector *outdoor = GetGame()->GetScene()->GetPrimarySector();
    const auto addOpening = [&](I3D_portal *portal) {
        const float radius = portal->GetRadius();
        acoustics->openArea += radius * radius * PI * (portal->IsOn() ? 1.0f : 0.1f);
    };
    for (int i = sector->NumPortals(); i-- > 0;) {
        I3D_portal *portal = sector->GetPortal(i);
        if (portal->GetConnected() == outdoor)
            addOpening(portal);
    }
    for (int i = sector->NumIncomingPortals(); i-- > 0;) {
        I3D_portal *portal = sector->GetIncomingPortal(i);
        if (portal->GetOwner() == outdoor)
            addOpening(portal);
    }
    if (acoustics->openArea > MAX_OPEN_AREA)
        acoustics->openArea = MAX_OPEN_AREA;
    acoustics->outdoorFactor += (1.0f - (MAX_OPEN_AREA - acoustics->openArea) * 0.025f) * 0.9f;
    module->m_sectorAcoustics.push_back(acoustics);
    return 0;
}

void C_SoundModule::FreeSectorAcoustics()
{
    for (std::size_t i = m_sectorAcoustics.size(); i-- > 0;) {
        if (S_SectorAcoustics *acoustics = m_sectorAcoustics[i]) {
            acoustics->sector->SetUserData(nullptr);
            delete acoustics;
        }
    }
    m_sectorAcoustics.clear();
}

S_SoundFrame *C_SoundModule::RegisterSoundFrame(I3D_sound *sound, bool trackSector)
{
    for (std::size_t i = m_soundFrames.size(); i-- > 0;) {
        S_SoundFrame *entry = m_soundFrames[i];
        if (entry->sound != sound)
            continue;
        entry->trackSector = trackSector;
        if (trackSector)
            entry->sector = GetGame()->GetScene()->GetSector(sound->GetWorldPos());
        entry->baseVolume = sound->GetVolumeInline();
        entry->SetSectorFactor(entry->sectorFactor);
        return entry;
    }
    auto *entry = new S_SoundFrame(sound, trackSector);
    m_soundFrames.push_back(entry);
    return entry;
}

// ---------------------------------------------------------------------------
// effects

void C_SoundModule::SetChannelLimit(int channel, int limit)
{
    m_channelLimit[channel] = limit;
    if (m_channelNext[channel] >= limit)
        m_channelNext[channel] = 0;
    m_channelSlots[channel].resize(limit, -1);
}

S_SoundGroup *C_SoundModule::GetGroup(int chapter, int group) const
{
    return g_soundTable.GetGroup(chapter, group);
}

int C_SoundModule::GetDubbingSampleSize(int id)
{
    return g_soundTable.GetDubbingSampleSize(id);
}

int C_SoundModule::AllocSlot()
{
    // The index stays in m_freeSlots until the caller takes it.
    if (!m_freeSlots.empty() && m_slots[m_freeSlots.back()])
        return m_freeSlots.back();
    m_slots.push_back(new S_SoundSlot);
    const int index = static_cast<int>(m_slots.size()) - 1;
    m_freeSlots.push_back(index);
    return index;
}

void C_SoundModule::ReleaseSlotSound(S_SoundSlot *slot)
{
    if (I3D_sound *sound = slot->sound) {
        sound->SetVolume(0.0f);
        sound->SetActive(false, true);
        sound->Update();
        sound->Release();
    }
    slot->sound = nullptr;
}

int C_SoundModule::Play(const S_SoundRequest &request)
{
    I3D_scene *scene = GetGame()->GetScene();
    S_vector soundPos = request.pos;
    if (request.attachTo)
        soundPos += request.attachTo->GetWorldPos();
    const S_matrix &listener = scene->GetActiveCamera()->GetMatrix();
    const S_vector listenerPos = listener(3);
    const float distance2 = (listenerPos - soundPos).Square();
    // (The w component of the camera's matrix, 1 in practice, scales the volume.)
    const float listenerW = listener.m[3][3];

    if (!request.fileName) {
        if (static_cast<unsigned>(request.chapter) >= static_cast<unsigned>(g_soundTable.NumChapters()))
            return -1;
        const S_SoundChapter *chapter = g_soundTable.GetChapter(request.chapter);
        if (static_cast<unsigned>(request.group) >= chapter->groups.size())
            return -1;
    }

    const int index = AllocSlot();
    S_SoundSlot *slot = m_slots[index];
    S_SoundSample *sample = nullptr;
    S_SoundGroup *group = nullptr;
    if (!request.fileName) {
        const S_SoundChapter *chapter = g_soundTable.GetChapter(request.chapter);
        if (!chapter)
            return -1;
        group = chapter->groups[request.group];
        if (!group || group->samples.empty())
            return -1;
        sample = group->samples[std::rand() % static_cast<int>(group->samples.size())];
        slot->sample = sample;
        if (!sample)
            return -1;
        slot->farRange = sample->farRange * request.farScale;
        slot->nearRange = sample->nearRange * request.nearScale;
        slot->chapter = request.chapter;
        slot->volume = sample->volume * request.volume;
        slot->unknown28 = sample->unknown18;
        slot->flags = request.flags;
        if (!request.source)
            slot->flags |= SOUND_SILENT_TO_AI;
    } else {
        slot->farRange = request.farScale * request.farRange;
        slot->nearRange = request.nearScale * request.nearRange;
        slot->chapter = request.chapter;
        slot->volume = request.volume;
        slot->unknown28 = 0.0f;
        slot->flags = SOUND_SILENT_TO_AI;
        // The original left the sample of a previous use here, which put this
        // file's sound into that sample's pool when it stopped.
        slot->sample = nullptr;
    }
    slot->pos = {soundPos.x, soundPos.y, soundPos.z, request.posW};
    slot->attachTo = request.attachTo;
    slot->active = true;
    slot->source = request.source;
    m_freeSlots.pop_back();

    // Audible from the listener?
    bool audible = distance2 <= slot->farRange * slot->farRange;
    bool checkUnderwater = audible;
    if (audible && request.chapter == SOUND_CHAPTER_WEAPON_SHOOTING) {
        C_Human *shooter = AsHuman(slot->source);
        I3D_sector *sourceSector = shooter ? shooter->GetSector() : nullptr;
        I3D_sector *listenerSector = GetGame()->GetCameraModule()->GetListenerSector();
        float factor = 1.0f;
        if (sourceSector && listenerSector &&
            SectorDistance(factor, soundPos, listenerPos, sourceSector, listenerSector) > slot->farRange) {
            audible = false;
            checkUnderwater = false;
        }
    }
    if (checkUnderwater && GetCameraModule()->IsUnderwater()) {
        switch (request.chapter) {
        case SOUND_CHAPTER_PUPPET_MOVEMENT:
        case SOUND_CHAPTER_OBJECT_MANIPULATION:
        case SOUND_CHAPTER_WEAPON_MANIPULATION:
        case SOUND_CHAPTER_BULLET_HIT:
        case SOUND_CHAPTER_FALLING_HIT:
        case SOUND_CHAPTER_VERSION:
        case SOUND_CHAPTER_SPEECH:
            // Sounds of swimmers are not heard under water.
            if (C_Human *human = AsHuman(request.source)) {
                if (human->IsInWater())
                    audible = false;
            }
            break;
        default:
            slot->volume *= UNDERWATER_DAMPING;
            break;
        }
    }

    const bool alwaysAudible = request.fileName ? (request.flags & SOUND_AMBIENT) != 0 : group->alwaysAudible;
    if (request.flags & SOUND_ANY_RANGE)
        audible = true;

    const bool play = IsClient() && (audible || alwaysAudible || (request.flags & (SOUND_KEEP_SLOT | SOUND_LOOP))) &&
                      !(request.flags & SOUND_AI_ONLY);
    if (play) {
        char fileName[0x104];
        std::snprintf(fileName, sizeof(fileName), "%s", request.fileName ? request.fileName : sample->fileName.c_str());
        if (!std::strstr(fileName, ".wav"))
            std::strncat(fileName, ".wav", sizeof(fileName) - std::strlen(fileName) - 1);

        if (ISndGetDriver()->IsInitialized()) {
            if (sample && !sample->pool.empty()) {
                // A finished sound of the same sample.
                slot->sound = sample->pool.back();
                sample->pool.pop_back();
            } else if (sample && sample->master) {
                slot->sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
                slot->sound->Open(sample->master->GetFileName(), sample->master->GetOpenFlags());
            } else {
                slot->sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
                const HRESULT result = g_soundCache.OpenInto(slot->sound, fileName);
                if (sample && result == I3D_OK) {
                    // Later sounds of this sample are opened from the cached copy.
                    sample->master = nullptr;
                    g_soundCache.Get(fileName, &sample->master);
                }
            }
            I3D_sound *sound = slot->sound;
            if (!sound)
                return -1;

            sound->LinkTo(request.attachTo ? request.attachTo : scene->GetPrimarySector(), 0);
            sound->SetPosInline(request.pos);
            sound->SetSoundType((request.flags & SOUND_AMBIENT) || alwaysAudible ? I3DSOUND_AMBIENT : I3DSOUND_POINT);
            sound->SetRange(slot->nearRange, slot->farRange, 0.4f, 0.3f);
            sound->SetVolume(request.flags & SOUND_FADE_IN ? 0.0f : listenerW * slot->volume);
            sound->SetLoop((request.flags & SOUND_LOOP) != 0);
            sound->Play(0);
            sound->SetActive(true, false);

            if (request.channel != SOUND_CHANNEL_NONE) {
                // The channel's oldest sound makes room.
                const int channel = request.channel;
                int &place = m_channelSlots[channel][m_channelNext[channel]];
                if (place != -1)
                    Stop(&place);
                place = index;
                if (++m_channelNext[channel] >= m_channelLimit[channel])
                    m_channelNext[channel] = 0;
                slot->flags |= SOUND_KEEP_SLOT;
            }
        }
    }

    if (IsServer() && !(request.flags & SOUND_SILENT_TO_AI))
        NotifyListeners(index, (request.flags & SOUND_AI_OCCLUSION) != 0);

    if (!ISndGetDriver()->IsInitialized()) {
        int stopped = index;
        Stop(&stopped);
        return -1;
    }
    return index;
}

int C_SoundModule::PlayTableSound(void *owner, const S_SoundRequest &request)
{
    if (!owner)
        return -1;
    g_soundTable.Load();
    // (sic: the group index is checked against the number of chapters)
    if (static_cast<unsigned>(request.group) >= static_cast<unsigned>(g_soundTable.NumChapters()))
        return -1;
    const S_SoundChapter *chapter = g_soundTable.GetChapter(request.chapter);
    if (!chapter || static_cast<unsigned>(request.group) >= chapter->groups.size())
        return -1;

    const int index = AllocSlot();
    S_SoundSlot *slot = m_slots[index];
    // Stop() and Update() clear the sound of every slot they free, so this
    // only succeeds for a slot that was never cleaned up.
    if (!slot->sound)
        return -1;

    // Samples are chosen by their cumulative probability.
    const int roll = std::rand() % 100;
    const S_SoundGroup *group = chapter->groups[request.group];
    if (!group)
        return -1;
    std::size_t pick = 0;
    for (std::size_t i = 0; i < group->samples.size(); ++i) {
        if (group->samples[i] && roll < group->samples[i]->probability) {
            pick = i;
            break;
        }
    }
    if (pick >= group->samples.size()) {
        s_warnedNoSample = true;
        return -1;
    }
    S_SoundSample *sample = group->samples[pick];
    if (!sample) {
        s_warnedNullSample = true;
        return -1;
    }

    slot->farRange = sample->farRange;
    slot->nearRange = sample->nearRange;
    slot->pos = {request.pos.x, request.pos.y, request.pos.z, request.posW};
    slot->attachTo = request.attachTo;
    slot->source = request.source;
    slot->chapter = request.chapter;
    slot->unknown28 = sample->unknown18;
    slot->active = true;
    m_freeSlots.pop_back();

    char fileName[0x104];
    std::snprintf(fileName, sizeof(fileName), "%s", sample->fileName.c_str());
    if (!std::strstr(fileName, ".wav"))
        std::strncat(fileName, ".wav", sizeof(fileName) - std::strlen(fileName) - 1);
    if (!ISndGetDriver()->IsInitialized())
        return -1;

    I3D_sound *sound = slot->sound;
    g_soundCache.OpenInto(sound, fileName);
    I3D_sector *outdoor = GetGame()->GetScene()->GetPrimarySector();
    outdoor->AddSound(sound);
    sound->LinkTo(outdoor, 0);
    sound->SetRange(sample->nearRange, sample->farRange, 0.4f, 0.3f);
    sound->SetPosInline(request.pos);
    sound->SetSoundType(I3DSOUND_AMBIENT);
    sound->SetVolume(sample->volume);
    sound->SetActive(true, true);
    sound->SetLoop((request.flags & SOUND_LOOP) != 0);
    return index;
}

void C_SoundModule::Stop(int *slotIndex)
{
    const int index = *slotIndex;
    if (index == -1)
        return;
    if (index >= 0 && index < static_cast<int>(m_slots.size()) && m_slots[index] && m_slots[index]->active) {
        S_SoundSlot *slot = m_slots[index];
        if (ISndGetDriver()->IsInitialized() && slot->sound) {
            slot->sound->SetOn(false);
            slot->sound->SetVolume(0.0f);
            if (slot->sample) {
                // Kept for the sample's next Play().
                slot->sample->pool.push_back(slot->sound);
                slot->sample = nullptr;
            } else {
                slot->sound->Release();
            }
            slot->sound = nullptr;
        }
        slot->active = false;
        m_freeSlots.push_back(index);
    }
    *slotIndex = -1;
}

I3D_sound *C_SoundModule::GetSound(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_slots.size()))
        return nullptr;
    const S_SoundSlot *slot = m_slots[index];
    return slot && slot->active ? slot->sound : nullptr;
}

// ---------------------------------------------------------------------------
// AI hearing

int C_SoundModule::AddListener(I3D_frame *frame, T_SoundHeardProc callback)
{
    if (!IsServer())
        return -1;
    const S_FrameData *data = frame ? frame->GetData() : nullptr;
    const S_Listener listener = {frame, AsHuman(data ? data->actor : nullptr), callback};
    for (std::size_t i = m_listeners.size(); i-- > 0;) {
        if (!m_listeners[i].frame) {
            m_listeners[i] = listener;
            return static_cast<int>(i);
        }
    }
    m_listeners.push_back(listener);
    return static_cast<int>(m_listeners.size()) - 1;
}

bool C_SoundModule::RemoveListener(int index)
{
    if (!IsServer() || index < 0 || index >= static_cast<int>(m_listeners.size()) || !m_listeners[index].callback)
        return false;
    m_listeners[index] = S_Listener{nullptr, nullptr, nullptr};
    return true;
}

void C_SoundModule::NotifyListeners(int index, bool occluded)
{
    // Nobody hears anything while the mission fades in.
    if (m_fadeInTime || index >= static_cast<int>(m_slots.size()))
        return;
    const S_SoundSlot *slot = m_slots[index];
    if (!slot)
        return;

    const S_vector soundPos = ToVector(slot->pos);
    S_HeardSound heard = {};
    heard.pos = soundPos;
    heard.posW = slot->pos.w;
    heard.chapter = slot->chapter;
    heard.source = slot->source;
    I3D_sector *sourceSector = slot->source ? slot->source->GetSector() : nullptr;

    for (std::size_t i = m_listeners.size(); i-- > 0;) {
        const S_Listener &listener = m_listeners[i];
        if (!listener.callback || !listener.frame)
            continue;
        const S_vector ear = EarPosition(listener.frame);
        float distance = (soundPos - ear).Magnitude();
        if (distance > slot->farRange)
            continue;
        heard.listener = listener.frame;
        if (!occluded) {
            heard.intensity = Intensity(distance, slot->nearRange, slot->farRange);
        } else {
            float factor = 1.0f;
            if (listener.human)
                distance = PortalDistance(factor, soundPos, ear, sourceSector, listener.human->GetSector());
            if (distance > slot->farRange)
                continue;
            heard.intensity = distance > slot->nearRange ? Intensity(distance, slot->nearRange, slot->farRange) * factor
                                                         : 1.0f;
        }
        listener.callback(&heard);
    }
}

bool C_SoundModule::PlayDubbing(C_Human *owner, I3D_frame *frame, int dubbing, float nearRange, float farRange,
                                bool showSubtitle)
{
    if (!IsClient())
        return false;
    if (!dubbing) {
        StopFrameSpeech(owner ? owner->GetFrame() : frame);
        return true;
    }
    const int slot = FindFreeSpeechSlot();
    if (owner)
        AttachSpeechSlot(slot, owner);
    else
        AttachSpeechSlot(slot, frame);
    const bool isActivePlayer = owner == g_teamManager.GetPlayerTeam()->GetActivePlayer();
    PlaySpeechSlot(slot, dubbing, nearRange, farRange, showSubtitle, isActivePlayer);
    return true;
}

bool C_SoundModule::PlayDubbingWithNoise(C_Human *owner, I3D_frame *frame, int dubbing, float nearRange,
                                         float farRange, bool showSubtitle, uint32_t flags)
{
    if (!IsClient())
        return false;
    if (!dubbing) {
        StopFrameSpeech(owner ? owner->GetFrame() : frame);
    } else if (!(flags & SOUND_AI_ONLY)) {
        const int slot = FindFreeSpeechSlot();
        if (owner)
            AttachSpeechSlot(slot, owner);
        else
            AttachSpeechSlot(slot, frame);
        const bool isActivePlayer = IsClient() && owner == g_teamManager.GetPlayerTeam()->GetActivePlayer();
        PlaySpeechSlot(slot, dubbing, nearRange, farRange, showSubtitle, isActivePlayer);
    }

    if (!(flags & SOUND_SILENT_TO_AI)) {
        const S_matrix &body = owner->GetBodyFrame()->GetMatrix();
        const S_vector pos = body(3);
        S_HeardSound heard = {};
        heard.pos = pos;
        heard.posW = body.m[3][3];
        heard.chapter = SOUND_CHAPTER_SPEECH;
        heard.source = owner;
        for (std::size_t i = m_listeners.size(); i-- > 0;) {
            const S_Listener &listener = m_listeners[i];
            if (!listener.callback || !listener.frame)
                continue;
            const S_vector ear = EarPosition(listener.frame);
            float distance = (pos - ear).Magnitude();
            if (distance > farRange)
                continue;
            heard.listener = listener.frame;
            if (!(flags & SOUND_AI_OCCLUSION)) {
                heard.intensity = Intensity(distance, nearRange, farRange);
            } else {
                float factor = 1.0f;
                distance = PortalDistance(factor, pos, ear, nullptr, nullptr);
                if (distance > farRange)
                    continue;
                heard.intensity = distance > nearRange ? Intensity(distance, nearRange, farRange) * factor : 1.0f;
            }
            listener.callback(&heard);
        }
    }
    return true;
}

void C_SoundModule::SetDopplerWobble(bool on)
{
    const float speed = std::cos(static_cast<float>(GetGame()->GetTime()) * 0.01f) * 50.0f + 25.0f;
    const S_vector velocity = on ? S_vector(speed, speed, speed) : S_vector(0, 0, 0);
#ifndef HD2_DEDICATED   // no sound listener in LS3DRF.dll
    GetGame()->GetScene()->GetSoundListener()->SetVelocity(velocity);
#endif
}

// ---------------------------------------------------------------------------
// music

bool C_SoundModule::IsMusicPlaying() const
{
    return m_music && m_music->IsPlaying();
}

bool C_SoundModule::IsOverlayPlaying() const
{
    return m_overlay && m_overlay->IsPlaying();
}

bool C_SoundModule::OpenStream(ISND_stream *&stream, const char *fileName, float volume, bool loop)
{
    if (!IsClient())
        return true;
    if (!stream) {
#ifndef HD2_DEDICATED   // no streams in LS3DRF.dll
        if (fileName)
            stream = GetIGraph()->CreateStream();
#endif
        if (!stream)
            return false;
    }
    if (stream->IsPlaying()) {
        stream->Stop();
        stream->Close();
    }
    if (fileName && *fileName) {
        if (stream->Open(C_fstr("sounds\\%s", fileName), loop ? ISNDSTREAM_LOOP : 0) == I3D_OK) {
            stream->SetVolume(volume);
            stream->Play();
        }
    }
    return true;
}

void C_SoundModule::SetBackgroundMusic(int track, int position)
{
    g_soundTable.Load();
    OpenStream(m_music, nullptr, 0.0f, true);
    m_crossfadePending = false;
    m_musicTrack = track;
    const S_SoundGroup *group = g_soundTable.GetGroup(SOUND_CHAPTER_MUSIC, track);
    if (!group || group->samples.empty())
        return;
    const unsigned pick = static_cast<unsigned>(std::rand()) % group->samples.size();
    m_musicFadeTarget = 1.0f;
    m_musicFade = 0.0f;
    OpenStream(m_music, group->samples[pick]->fileName, m_musicVolume * m_musicFade, true);
    if (position && m_music && m_music->IsPlaying())
        m_music->SetPosition(position);
}

void C_SoundModule::FadeBackgroundMusic(float volume, int time)
{
    if (time < 1)
        time = 1;
    m_musicFadeTarget = volume;
    m_musicFadeSpeed = std::fabs((m_musicFade - volume) / time);
}

void C_SoundModule::FadeOverlay(float volume, int time)
{
    if (time < 1)
        time = 1;
    m_overlayFadeTarget = volume;
    m_overlayFadeSpeed = (m_overlayFade - volume) / time;
}

void C_SoundModule::PlayMusic(int track)
{
    if ((IsOverlayPlaying() && track != -1) || m_crossfadePending)
        return;
    if (track == -1) {
        if (m_overlay) {
            if (m_overlay->IsPlaying()) {
                m_overlay->Stop();
                m_overlay->Close();
            }
            m_overlay->Release();
            m_overlay = nullptr;
        }
        return;
    }
#ifndef HD2_DEDICATED   // no streams in LS3DRF.dll
    if (!m_overlay)
        m_overlay = GetIGraph()->CreateStream();
#endif
    const S_SoundGroup *group = g_soundTable.GetGroup(SOUND_CHAPTER_MUSIC, track);
    if (!group || group->samples.empty())
        return;
    const unsigned pick = static_cast<unsigned>(std::rand()) % group->samples.size();
    const bool ok = OpenStream(m_overlay, group->samples[pick]->fileName, 0.0f, false);
    m_overlayFade = 0.0f;
    if (ok) {
        // Turn the background music down while the track plays.
        FadeOverlay(1.0f, 2000);
        FadeBackgroundMusic(0.3f, 2000);
    }
}

void C_SoundModule::CrossfadeMusic(int track, int fadeTime, int position)
{
    const int half = static_cast<int>(fadeTime * 0.5f);
    FadeOverlay(0.01f, half);
    FadeBackgroundMusic(0.01f, half);
    m_crossfadeTime = half;
    m_crossfadePending = true;
    m_crossfadeTrack = track;
    m_crossfadePosition = position;
}

int C_SoundModule::GetBackgroundMusicPosition() const
{
    int position = 0;
    if (m_music)
        m_music->GetPosition(&position);
    return position;
}

float C_SoundModule::SetMusicVolume(float volume)
{
    if (IsClient()) {
        m_musicVolume = volume;
        if (IsMusicPlaying())
            m_music->SetVolume(m_musicFade * m_musicVolume);
    }
    return m_musicVolume;
}

float C_SoundModule::SetMasterVolume(float volume)
{
    if (IsClient()) {
        m_masterVolume = volume;
        ISndGetDriver()->SetVolume(volume);
    }
    return m_masterVolume;
}
