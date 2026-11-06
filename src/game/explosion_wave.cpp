// See explosion_wave.h.
#include "game/explosion_wave.h"

#include <cmath>
#include <cstring>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/camera_module.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"
#include "game/shots.h"
#include "game/versions.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

const uint32_t VOLUME_EXCLUDE = 0xD8000;                // what GetVolumes... leave out
const uint32_t LINE_OF_SIGHT_FLAGS = 0xB10000;
const char MODEL_CHILD[] = "a";                         // 0x00839100
// The joints of a human tried for a line of sight (0x008435b0).
const int EXPOSED_JOINTS[] = {0x18, 0x03, 0x0E, 0x0A, 0x11, 0x15};

std::vector<C_ExplosionWave *> s_waves;                 // 0x008aee54

// Chunks of the saved game.
enum : uint16_t {
    CK_EVENTS = 0x1A85,
    CK_EVENT = 1000,
    CK_WAVE = 0x457,
    CK_WAVE_POS = 1000,
    CK_WAVE_RADIUS = 2000,
    CK_WAVE_DAMAGE = 3000,
    CK_WAVE_RANGE = 4000,
    CK_WAVE_SOURCE = 5000,
    CK_WAVE_FLAG = 0x157C,
    CK_WAVE_SECTOR = 0x1676,
    CK_WAVE_VICTIM = 6000,
    CK_VICTIM = 0x17D4,
    CK_VICTIM_OFFSET = 0x1838,
    CK_VICTIM_FRAME = 0x189C,
    CK_VICTIM_DISTANCE = 0x1900,
};

void FreeVictim(S_WaveVictim *victim)
{
    if (victim->frame)
        victim->frame->Release();
    delete victim;
}

void FreeVictims(S_WaveVictim *victim)                  // 0x006de330, 0x006df510
{
    while (victim) {
        S_WaveVictim *const next = victim->next;
        FreeVictim(victim);
        victim = next;
    }
}

// The squared distance from a point to a box.
float SquaredDistanceToBox(const S_vector &point, const I3D_bbox &box)
{
    float sum = 0.0f;
    if (point.x < box.min.x)
        sum = (point.x - box.min.x) * (point.x - box.min.x);
    else if (point.x > box.max.x)
        sum = (point.x - box.max.x) * (point.x - box.max.x);
    if (point.y < box.min.y)
        sum += (point.y - box.min.y) * (point.y - box.min.y);
    else if (point.y > box.max.y)
        sum += (point.y - box.max.y) * (point.y - box.max.y);
    if (point.z < box.min.z)
        sum += (point.z - box.min.z) * (point.z - box.min.z);
    else if (point.z > box.max.z)
        sum += (point.z - box.max.z) * (point.z - box.max.z);
    return sum;
}

bool IsHumanFrame(I3D_frame *frame)
{
    const S_FrameData *data = frame->GetData();
    return data && data->actor && data->actor->GetType() == ACTOR_HUMAN;
}

// (0x006ddb50; a member in the original)
void WriteVictim(C_chunk &chunk, const S_WaveVictim &victim)
{
    chunk.Begin(CK_VICTIM);
    chunk.Begin(CK_VICTIM_OFFSET);
    chunk.Write(&victim.offset, 12);
    chunk.Ascend();
    if (victim.frame) {
        const char *const name = victim.frame->GetName();
        chunk.Begin(CK_VICTIM_FRAME);
        chunk.Write(name, static_cast<int>(std::strlen(name)) + 1);
        chunk.Ascend();
    }
    chunk.Begin(CK_VICTIM_DISTANCE);
    chunk.Write(&victim.distance, 4);
    chunk.Ascend();
    chunk.Ascend();
}

// (0x006ddd70; a member in the original)
void ReadVictim(C_chunk &chunk, S_WaveVictim &victim)
{
    uint16_t id;
    if (!chunk.Descend(id))
        return;
    while (chunk.HasMore()) {
        if (!chunk.Descend(id))
            break;
        switch (id) {
        case CK_VICTIM_OFFSET:
            chunk.Read(&victim.offset, 12);
            break;
        case CK_VICTIM_FRAME: {
            char name[1000];
            chunk.ReadString(name, sizeof(name));
            victim.frame = GetGame()->GetScene()->FindFrame(name, 0xCFFFF);
            if (victim.frame)                           // (fix: the original's crashes on a frame that is gone)
                victim.frame->AddRef();
            break;
        }
        case CK_VICTIM_DISTANCE:
            chunk.Read(&victim.distance, 4);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
}

}  // namespace

C_Actor *GetFrameOrModelActor(I3D_frame *frame)
{
    if (frame->GetData() && frame->GetData()->actor)
        return frame->GetData()->actor;
    I3D_frame *const model = frame->GetOwnerModel();
    if (model && model->GetType() == FRAME_MODEL && model->GetData() && model->GetData()->actor)
        return model->GetData()->actor;
    return nullptr;
}

C_ExplosionWave::C_ExplosionWave(const S_Explosion &explosion)
    : m_pos{explosion.pos.x, explosion.pos.y, explosion.pos.z, explosion.posW},
      m_damage(explosion.damage),
      m_range(explosion.range),
      m_source(explosion.source),
      m_sameSectorHumans(explosion.unknown18)
{
    const S_vector pos = explosion.pos;
    m_sector = GetGame()->GetScene()->GetSector(pos);

    // The frames in range, each once; of a human, its model's child.
    static std::vector<S_CollisionVolume *> s_volumes;  // 0x008aee40
    g_collisionWorld.GetVolumesInRadius(pos, m_range, VOLUME_EXCLUDE, s_volumes);
    std::vector<I3D_frame *> frames;
    if (const S_CollisionVolume *inside = g_collisionWorld.GetVolumeAt(pos, VOLUME_EXCLUDE))
        frames.push_back(inside->frame);
    for (const S_CollisionVolume *volume : s_volumes) {
        I3D_frame *const frame = volume->frame;
        if (!frame)
            break;
        I3D_frame *const entry = IsHumanFrame(frame) ? frame->FindChildFrame(MODEL_CHILD, 0xFFFF) : frame;
        bool known = false;
        for (I3D_frame *other : frames)
            if (other == entry || other == frame) {
                known = true;
                break;
            }
        if (!known)
            frames.push_back(entry);
    }

    if (GetGame()) {
        if (C_CameraModule *const camera = GetGame()->GetCameraModule())
            camera->SetSpecialMode(CAMERA_MODE_EXPLOSION, &m_pos, static_cast<uint32_t>(static_cast<int>(m_range)));
    }

    for (size_t i = frames.size(); i-- != 0;) {
        I3D_frame *const frame = frames[i];
        S_WaveVictim *const victim = new S_WaveVictim;
        victim->frame = frame;
        frame->AddRef();
        S_matrix inverse;
        inverse.Inverse(frame->GetMatrix());
        const S_vector local = pos * inverse;
        I3D_bound_volume bound;
        const I3D_bbox &box = frame->GetType() == FRAME_DUMMY ? static_cast<I3D_dummy *>(frame)->GetBBox()
                                                              : I3DGetDriver()->GetHierarchyBound(bound, frame)->bbox;
        const float distance = SquaredDistanceToBox(local, box);
        const S_vector offset = frame->GetWorldPos() - pos;
        victim->offset = {offset.x, offset.y, offset.z, 0.0f};  // (the original's w is what was on the stack)
        victim->distance = distance < 0.0f ? 0.0f : distance;
        if (victim->distance > m_range)
            m_range = victim->distance;

        // By distance. Kept: the last one in the list is not compared, so one nearer than it still goes behind it.
        if (!m_victims) {
            m_victims = victim;
            continue;
        }
        S_WaveVictim *previous = nullptr;
        S_WaveVictim *current = m_victims;
        bool placed = false;
        while (current->next) {
            if (victim->distance < current->distance) {
                victim->next = current;
                if (previous)
                    previous->next = victim;
                else
                    m_victims = victim;
                placed = true;
                break;
            }
            previous = current;
            current = current->next;
        }
        if (!placed)
            current->next = victim;
    }
}

C_ExplosionWave::~C_ExplosionWave()
{
    FreeVictims(m_victims);
}

void C_ExplosionWave::Hit(I3D_frame *frame, float amount, const S_vector &dir, bool lessEffects)
{
    if (!(amount > 0.0f))
        return;
    const bool server = (g_gameRole & 1) != 0;
    const S_vector pos(m_pos.x, m_pos.y, m_pos.z);
    C_Actor *const actor = GetFrameOrModelActor(frame);
    if (actor) {
        bool known = false;
        for (const C_Actor *other : m_hit)
            if (other == actor) {
                known = true;
                break;
            }
        if (!known) {
            // Kept: without a source it counts as HIT_SCRIPT_KILL.
            const E_HitType type = m_source ? HIT_EXPLOSION : HIT_SCRIPT_KILL;
            if (actor->GetType() == ACTOR_CAR) {
                if (server)
                    actor->Hit(type, amount, pos, dir, m_source, 0);
            } else {
                const int material = actor->GetMaterialId(frame);
                if (material != -1) {
                    const float absorption = static_cast<float>(static_cast<long long>(g_materials[material].absorption));
                    if (amount > absorption) {
                        amount -= absorption;
                        if (server)
                            actor->Hit(type, amount, pos, dir, m_source, 0);
                    }
                }
            }
            m_hit.push_back(actor);
        }
    }
    const S_FrameData *const data = frame->GetData();
    if (data && data->versionMaster && server)
        data->versionMaster->DamageAll(static_cast<int>(amount), lessEffects ? VERSION_EFFECTS_NONE : VERSION_EFFECTS_SOME);
}

bool C_ExplosionWave::IsHumanExposed(const C_Actor *human) const
{
    I3D_frame *const model = human->GetFrame()->FindChildFrame(MODEL_CHILD, 1);
    if (!model)                                         // (fix: the original's crashes)
        return false;
    I3D_frame *const *const joints = static_cast<const I3D_singlemesh_visual *>(model)->GetJoints();
    const S_vector pos(m_pos.x, m_pos.y, m_pos.z);
    for (const int index : EXPOSED_JOINTS) {
        const S_vector from = joints[index]->GetWorldPos();
        const S_vector toward = pos - from;
        const S_vector dir = toward - toward.NormalizedGame() * 0.05f;
        if (!CollisionRayTest(from, dir, nullptr, nullptr, 0xFFFFFFFF, LINE_OF_SIGHT_FLAGS))
            return true;
    }
    return false;
}

bool C_ExplosionWave::Step(int timeDelta)
{
    if (!m_victims)
        return false;
    int hits = 0;
    m_radius += static_cast<float>(timeDelta) * 0.2f;
    const S_vector pos(m_pos.x, m_pos.y, m_pos.z);
    while (m_victims && m_victims->distance <= m_radius) {
        S_WaveVictim *const victim = m_victims;
        C_Actor *const actor = GetFrameOrModelActor(victim->frame);
        const S_vector offset(victim->offset.x, victim->offset.y, victim->offset.z);
        bool reached;
        if (m_sameSectorHumans && actor && actor->GetType() == ACTOR_HUMAN
            && m_sector == static_cast<C_Human *>(actor)->GetBodySector())
            reached = true;
        else if (actor && actor->GetType() == ACTOR_HUMAN)
            reached = IsHumanExposed(actor);
        else
            reached = !CollisionRayTest(offset + pos, -offset, nullptr, nullptr, 0xFFFFFFFF, LINE_OF_SIGHT_FLAGS);
        if (reached) {
            const float amount = std::fabs((1.0f - victim->distance / m_range) * m_damage);
            Hit(victim->frame, amount, offset.NormalizedGame(), hits > 10);
            ++hits;
        }
        m_victims = victim->next;
        FreeVictim(victim);
    }
    return true;
}

void C_ExplosionWave::Save(C_chunk &chunk) const
{
    chunk.Begin(CK_WAVE);
    chunk.Begin(CK_WAVE_POS);
    chunk.Write(&m_pos, 12);
    chunk.Ascend();
    chunk.WriteChunk(CK_WAVE_RADIUS, m_radius);
    chunk.WriteChunk(CK_WAVE_DAMAGE, m_damage);
    chunk.WriteChunk(CK_WAVE_RANGE, m_range);
    const int source = m_source ? m_source->GetNetId() : -1;
    chunk.WriteChunk(CK_WAVE_SOURCE, source);
    chunk.Begin(CK_WAVE_FLAG);
    chunk.Write(&m_sameSectorHumans, 1);
    chunk.Ascend();
    if (m_sector) {
        const char *const name = reinterpret_cast<const I3D_frame *>(m_sector)->GetName();
        chunk.Begin(CK_WAVE_SECTOR);
        chunk.Write(name, static_cast<int>(std::strlen(name)) + 1);
        chunk.Ascend();
    }
    for (const S_WaveVictim *victim = m_victims; victim; victim = victim->next) {
        chunk.Begin(CK_WAVE_VICTIM);
        WriteVictim(chunk, *victim);
        chunk.Ascend();
    }
    chunk.Ascend();
}

void C_ExplosionWave::Load(C_chunk &chunk)
{
    FreeVictims(m_victims);
    m_victims = nullptr;
    m_sector = nullptr;
    uint16_t id;
    if (!chunk.Descend(id))
        return;
    while (chunk.HasMore()) {
        if (!chunk.Descend(id))
            break;
        switch (id) {
        case CK_WAVE_POS:
            chunk.Read(&m_pos, 12);
            break;
        case CK_WAVE_RADIUS:
            chunk.Read(&m_radius, 4);
            break;
        case CK_WAVE_DAMAGE:
            chunk.Read(&m_damage, 4);
            break;
        case CK_WAVE_RANGE:
            chunk.Read(&m_range, 4);
            break;
        case CK_WAVE_SOURCE: {
            int source = -1;
            chunk.Read(&source, 4);
            m_source = source == -1 ? nullptr : g_actorsModule.GetActorById(source);
            break;
        }
        case CK_WAVE_FLAG:
            chunk.Read(&m_sameSectorHumans, 1);
            break;
        case CK_WAVE_SECTOR: {
            char name[1000];
            chunk.ReadString(name, sizeof(name));
            m_sector = reinterpret_cast<I3D_sector *>(GetGame()->GetScene()->FindFrame(name, 0xFFFF));
            break;
        }
        case CK_WAVE_VICTIM: {
            S_WaveVictim *const victim = new S_WaveVictim;
            // Kept: the original walks the list's head to its end to append, so of a wave with victims left only the last two survive a load.
            // (Fix: those it drops are freed, not leaked.)
            while (m_victims && m_victims->next) {
                S_WaveVictim *const dropped = m_victims;
                m_victims = m_victims->next;
                FreeVictim(dropped);
            }
            if (m_victims)
                m_victims->next = victim;
            else
                m_victims = victim;
            ReadVictim(chunk, *victim);
            break;
        }
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
}

// ---- S_Explosion and the timed events ----------------------------------------------------------------------------------------

void S_Explosion::Execute()
{
    C_ExplosionWave *const wave = new C_ExplosionWave(*this);
    // (What stands right at it goes at once.)
    if (wave->Step(100))
        s_waves.push_back(wave);
    else
        delete wave;
}

void TimedEvents_Update(int timeDelta, bool clear)
{
    if (clear) {
        while (!s_waves.empty()) {
            delete s_waves.back();
            s_waves.pop_back();
        }
        return;
    }
    for (size_t i = s_waves.size(); i-- != 0;) {
        if (!s_waves[i]->Step(timeDelta)) {
            delete s_waves[i];
            s_waves[i] = s_waves.back();
            s_waves.pop_back();
        }
    }
}

void TimedEvents_Clear()
{
    TimedEvents_Update(0, true);
}

bool TimedEvents_Save(C_chunk &chunk)
{
    chunk.Begin(CK_EVENTS);
    for (const C_ExplosionWave *wave : s_waves) {
        chunk.Begin(CK_EVENT);
        wave->Save(chunk);
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

bool TimedEvents_Load(C_chunk &chunk)
{
    TimedEvents_Update(0, true);
    uint16_t id;
    if (!chunk.Descend(id))
        return true;
    while (chunk.HasMore()) {
        if (!chunk.Descend(id))
            break;
        if (id == CK_EVENT) {
            C_ExplosionWave *const wave = new C_ExplosionWave;
            s_waves.push_back(wave);
            wave->Load(chunk);
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}
