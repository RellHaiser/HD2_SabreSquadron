// Shots and the shot manager (original source 0x00448270..0x004559c0).
#include "game/first_person_view.h"
#include "game/shots.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actor.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/physics.h"
#include "game/resource_cache.h"
#include "game/sound_table.h"
#include "game/versions.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/bitstream.h"
#include "script/script.h"

C_ShotManager g_shotManager(MODULE_SHOTS, "ShotMgr");
C_HitZones g_hitZones;

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

constexpr float PI = 3.14159274f;
constexpr float TWO_PI = 6.28318548f;

// Item type of the signal pistol: its bullets are flares.
constexpr int ITEM_SIGNAL_PISTOL = 261;
// Particle effect of a burning flare.
constexpr int EFFECT_FLARE_SPARKS = 23;
// Explosion effects of SendExplosionMessage() on the clients (one at random).
const int g_messageExplosionEffects[3] = {43, 48, 49};   // 0x00839024
constexpr int MESSAGE_EXPLOSION_SOUND = 30;
// Sound group (chapter 4) of a bullet passing the listener.
constexpr int SOUND_GROUP_NEAR_MISS = 11;

// Shot manager messages (module channel MODULE_SHOTS): the type is sent with
// log(1) / log(2) + 1 bits.
constexpr int SHOT_MESSAGE_BITS = 1;
enum : uint32_t {
    SHOT_MESSAGE_EXPLOSION = 0,                             // position, material (8 bits)
};

// Saved-game placeholders (nothing of the effects or shots is saved).
enum : uint16_t {
    CHUNK_DECAL = 0x1A0A,
    CHUNK_BLOOD_DROP = 0x1A0B,
    CHUNK_BLOOD_POOL = 0x1A0C,
    CHUNK_SHOT = 0x1A0D,
};

// Projector visual mode of decals (3) and of a flying blood drop (0); not identified.
constexpr int PROJECTOR_MODE_DECAL = 3;
constexpr int PROJECTOR_MODE_DROP = 0;

// Materials whose hit sound and effect a projectile plays when it passes through.
bool HasProjectileHitEffect(int material)
{
    return (material > 30 && material < 35) || material == 41 || material == 43 || material == 62 ||
           material == 70;
}

bool DecalsEnabled() { return g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_DECALS); }

I3D_scene *GetScene() { return GetGame()->GetScene(); }

// Shots take their shooter's body out of the collision world while they are near it.
// (The original does this for any shooter, through the human's member at +0x98.)
void RemoveShooterCollision(C_Actor *shooter) { static_cast<C_Human *>(shooter)->RemoveCollision(false); }
void RestoreShooterCollision(C_Actor *shooter) { static_cast<C_Human *>(shooter)->RestoreCollision(false); }

// Ray from `from` along `dir` against the rendered geometry of `frame`; on a hit `hit`
// moves to the intersection along `normDir` (the normalized `dir`).
I3D_frame *IntersectVisual(const S_vector &from, const S_vector &dir, const S_vector &normDir, I3D_frame *frame,
                           S_CollisionHit &hit)
{
    I3D_collision_info info;
    I3D_frame *part = GetScene()->IntersectFrame(from, dir, frame, &info, I3DCOL_VISUAL_GEOMETRY, 0, 0, 0);
    if (part) {
        hit.pos = normDir * info.distance + from;
        hit.normal = info.normal;
    }
    return part;
}

// Volumes a shot crosses on its way from `from` along `step`, nearest first.
int QueryShotSegment(const S_vector &from, const S_vector &step, S_CollisionQuery &query)
{
    query.from = from;
    query.dir = step;
    query.excludeTypes = COLLISION_QUERY_SHOT_EXCLUDE_TYPES;
    query.excludeFlags = COLLISION_QUERY_SHOT_EXCLUDE_FLAGS;
    const int count = g_collisionWorld.QuerySegment(query);
    return count;
}
void SortHits(S_CollisionQuery &query)
{
    // (The original's std::sort instantiation 0x0044a910 / 0x0044ae00 with 0x00455650.)
    std::sort(query.hits.begin(), query.hits.end(),
              [](const S_CollisionHit &a, const S_CollisionHit &b) { return a.distance < b.distance; });
}

// Reflects `dir` off a surface with `normal`, keeping its length: factor 2 mirrors it,
// smaller factors keep more of the original direction.
void DeflectVector(S_vector &dir, const S_vector &normal, float factor)   // 0x00448380
{
    const float length = dir.Magnitude();
    dir.Normalize();
    const float d = (dir.y * normal.y + dir.z * normal.z + dir.x * normal.x) * factor;
    dir -= normal * d;
    dir.Normalize();
    dir *= length;
}

// Two axes perpendicular to `dir` (the second one is built from the first). Not called.
[[maybe_unused]] void MakeOrthoBasis(const S_vector &dir, S_vector &right, S_vector &up)   // 0x0044ffd0
{
    right = dir.Cross(S_vector(0.0f, 1.0f, 0.0f));
    right.Normalize();
    right = right.Cross(dir);
    right.Normalize();
    up = right.Cross(dir);
    up.Normalize();
}

// Whether any part of the segment from `pos` along `dir` is in the view of the active
// camera (between its near and far range).
bool IsSegmentInView(const S_vector &pos, const S_vector &dir)   // 0x00450620
{
    I3D_camera *camera = GetScene()->GetActiveCamera();
    if (!camera)
        return false;
    const float right = std::tan(camera->GetFOV() * 0.5f);
    const float top = right / camera->GetAspectRatio();
    const float left = -right;
    const float bottom = -top;
    float nearRange, farRange;
    camera->GetRange(nearRange, farRange);
    const S_matrix m = camera->GetMatrix();

    // Both ends in camera space.
    S_vector d = pos - camera->GetWorldPos();
    S_vector a(m(0).Dot(d), m(1).Dot(d), m.m[2][1] * d.y + m.m[2][2] * d.z + m.m[2][0] * d.x);
    d += dir;
    S_vector b(m(0).Dot(d), m(1).Dot(d), m.m[2][1] * d.y + m.m[2][2] * d.z + m.m[2][0] * d.x);

    // Clip to the depth range.
    const float dz = b.z - a.z;
    if (std::fabs(dz) > 1e-8f) {
        if (a.z < nearRange) {
            if (b.z < nearRange)
                return false;
            const float t = (nearRange - a.z) / dz;
            a.x = (b.x - a.x) * t + a.x;
            a.y = (b.y - a.y) * t + a.y;
            a.z = dz * t + a.z;
        } else if (a.z > farRange) {
            if (b.z > farRange)
                return false;
            const float t = (farRange - a.z) / dz;
            a.x = (b.x - a.x) * t + a.x;
            a.y = (b.y - a.y) * t + a.y;
            a.z = dz * t + a.z;
        }
        if (b.z < nearRange) {
            const float t = -((nearRange - b.z) / dz);
            b.x = (a.x - b.x) * t + b.x;
            b.y = (a.y - b.y) * t + b.y;
            b.z = (a.z - b.z) * t + b.z;
        } else if (b.z > farRange) {
            const float t = -((farRange - b.z) / dz);
            b.x = (a.x - b.x) * t + b.x;
            b.y = (a.y - b.y) * t + b.y;
            b.z = (a.z - b.z) * t + b.z;
        }
    }
    // Project (by the distance; points behind the camera are mirrored).
    if (std::fabs(a.z) > 1e-8f) {
        const float s = std::fabs(1.0f / a.z);
        a.x *= s;
        a.y *= s;
    }
    if (std::fabs(b.z) > 1e-8f) {
        const float s = std::fabs(1.0f / b.z);
        b.x *= s;
        b.y *= s;
    }
    // The projected segment crosses an edge of the view or starts inside it.
    const auto crosses = [](float p, float q, float edge) { return std::signbit(q - edge) != std::signbit(p - edge); };
    const float dx = b.x - a.x;
    if (std::fabs(dx) > 1e-8f) {
        for (const float edge : {left, right}) {
            if (crosses(a.x, b.x, edge)) {
                const float y = (b.y - a.y) * ((edge - a.x) / dx) + a.y;
                if (bottom <= y && y <= top)
                    return true;
            }
        }
    }
    const float dy = b.y - a.y;
    if (std::fabs(dy) > 1e-8f) {
        for (const float edge : {bottom, top}) {
            if (crosses(a.y, b.y, edge)) {
                const float x = ((edge - a.y) / dy) * dx + a.x;
                if (left <= x && x <= right)
                    return true;
            }
        }
    }
    return left <= a.x && a.x < right && bottom <= a.y && a.y < top;
}

// Copy of the tracer billboard: a white, unlit quad.
I3D_billboard_visual *CreateTrace(I3D_frame *traceModel)
{
    auto *trace = static_cast<I3D_billboard_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_BILLBOARD));
    trace->Duplicate(traceModel);
    I3D_material *material = trace->GetMesh()->GetLevel(0)->GetFaceGroup(0)->material;
    material->Flags() |= I3DMATERIAL_TRACE_FLAG;
    material->SetDiffuse(S_vector(1.0f, 1.0f, 1.0f));
    material->SetEmissive(S_vector(0.0f, 0.0f, 0.0f));
    material->SetAmbient(S_vector(0.0f, 0.0f, 0.0f));
    material->SetAlpha(0.0f);
    return trace;
}

// Color of a blood decal at `pos`: the decal's own color scaled by the light there (at
// least 20 %).
S_vector BloodColor(const I3D_projector_visual *visual, const S_vector &pos, I3D_sector *sector)
{
    const float light = GetGame()->GetLightIntensity(pos, sector, true);
    return visual->GetBaseColor() * (light < 0.2f ? 0.2f : light);
}

} // namespace

// ============================================================================ materials

int GetFrameMaterial(const I3D_frame *frame)
{
    if (!frame)
        return MATERIAL_NONE;
    // (The original lower-cases the frame's property text in place.)
    std::string props = frame->GetUserProps();
    for (char &c : props)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const size_t at = props.find("matid");
    if (at == std::string::npos)
        return MATERIAL_NONE;
    return std::atoi(props.c_str() + at + std::strlen("matid"));
}

// ============================================================================ C_Shot

void C_Shot::Init(C_Actor *shooter, const S_vector &pos, const S_vector &velocity, C_WeaponComp *weapon,
                  I3D_frame *model, bool fake)
{
    m_shooter = shooter;
    m_pos = pos;
    m_velocity = velocity;
    m_fake = fake;
    m_weapon = weapon;
    m_ricocheted = false;
    m_damage = weapon->GetDamage();
    if (model) {
        m_visual = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
        m_visual->Duplicate(model);
        m_visual->LinkTo(GetScene()->GetPrimarySector(), 0);
    }
}

void C_Shot::Recycle()
{
    if (m_visual) {
        m_visual->Release();
        m_visual = nullptr;
    }
}

void C_Shot::Release()
{
    if (m_visual)
        m_visual->Release();
    m_visual = nullptr;
}

// ============================================================================ C_BulletShot

void C_BulletShot::Init(C_Actor *shooter, const S_vector &pos, const S_vector &dir, C_FirearmComp *weapon,
                        I3D_frame *traceModel, bool fake, bool flare)
{
    C_Shot::Init(shooter, pos, dir, weapon, nullptr, fake);
    m_velocity *= m_weapon->GetMuzzleVelocity();
    m_distance = 0.0f;
    m_traceDistance = 0;
    m_prevPos = pos;
    m_step.Zero();
    m_burnedOut = false;
    m_flareSparks = -1;
    I3D_frame *dynamicRoot = GetScene()->GetDynamicRoot();
    if (traceModel) {
        m_visual = CreateTrace(traceModel);
        m_visual->LinkTo(dynamicRoot, 0);
        m_visual->SetPosInline(m_prevPos);
        m_visual->SetOn(false);
        m_visual->Update();
    } else if (flare) {
        m_visual = I3DGetDriver()->CreateFrame(FRAME_MODEL);
        g_modelCache.OpenInto(static_cast<I3D_model *>(m_visual), "m_w_flare");
        m_visual->LinkTo(dynamicRoot, 0);
        m_visual->SetOn(true);
        m_visual->Update();
        m_visual->SetPosInline(m_prevPos);
        m_visual->SetScale(S_vector(0.5f, 0.5f, 0.5f));
        m_visual->Update();
        if (I3D_frame *glow = m_visual->FindChildFrame("flare", FRAME_MASK_ANY)) {
            static_cast<I3D_lensflare_visual *>(glow)->SetFlareScale(0.1f);
            glow->Update();
        }
    }
    m_ricocheted = false;
    m_flare = flare;
    m_nearMissPlayed = false;
    m_finished = false;
    m_unknown74 = 0;
}

void C_BulletShot::Recycle()
{
    C_Shot::Recycle();
    if (m_flareSparks != -1)
        g_particlesModule.Destroy(m_flareSparks);
    m_flareSparks = -1;
}

void C_BulletShot::Release()
{
    C_Shot::Release();
    if (m_flareSparks != -1)
        g_particlesModule.Destroy(m_flareSparks);
    m_flareSparks = -1;
}

float C_BulletShot::GetDamageAt(float distance, float damage) const
{
    const auto *firearm = static_cast<const C_FirearmComp *>(m_weapon);
    if (distance <= firearm->GetIdealRange())
        return damage;
    const float loss = (distance - firearm->GetIdealRange()) * firearm->GetDamageFalloff();
    if (damage < loss)
        return 0.0f;
    return damage - loss;
}

void C_BulletShot::CheckNearMiss(const S_vector &from, const S_vector &to)
{
    if (m_nearMissPlayed)
        return;
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    if (player == m_shooter || !player)
        return;
    const S_vector listener = player->GetBodyFrame()->GetWorldPos();
    S_vector segment = to - from;
    S_vector fromListener = from - listener;
    S_vector toListener = to - listener;
    toListener.Normalize();
    fromListener.Normalize();
    segment.Normalize();
    // Only while the bullet passes the listener (comes from before, goes behind).
    if (fromListener.Dot(segment) > 0.0f || toListener.Dot(segment) < 0.0f)
        return;
    const float lengthSq = segment.Square();
    if (lengthSq >= 1e-8f) {
        const S_vector rel = m_pos - listener;
        const float along = rel.Dot(segment) / lengthSq;
        const S_vector closest = rel - segment * along;
        if (closest.Square() >= 9.0f)
            return;                                         // farther than 3 m
    }
    g_shotManager.PlayNearMissSound(listener);
    m_nearMissPlayed = true;
}

void C_BulletShot::Update(int timeDelta)
{
    if (!m_visual || m_step.Square() < 1e-16f || m_burnedOut)
        return;
    if (m_interpolation + timeDelta > 100)
        timeDelta = 100 - m_interpolation;
    m_interpolation += timeDelta;
    const S_vector shown = m_step * (static_cast<float>(m_interpolation) * 0.01f);
    const float length = (m_step * (static_cast<float>(timeDelta) * 0.01f)).Magnitude();
    const float traced = length + static_cast<float>(m_traceDistance);
    m_traceDistance = static_cast<int>(traced);

    // The first 30 m are not shown.
    bool on = false;
    if (traced >= 30.0f) {
        if (m_flare && m_visual) {
            // A flare lights up within 100 m, then burns down.
            auto *glow = static_cast<I3D_lensflare_visual *>(m_visual->FindChildFrame("flare", FRAME_MASK_ANY));
            if (traced <= 100.0f) {
                if (m_flareSparks == -1)
                    m_flareSparks = g_particlesModule.Create(EFFECT_FLARE_SPARKS, m_visual->GetMatrix(), m_visual, true,
                                                             true);
                if (glow) {
                    float scale = glow->GetFlareScale();
                    if (scale < 1.0f) {
                        scale += static_cast<float>(timeDelta * 2) * 0.001f;
                        if (scale > 1.0f)
                            scale = 1.0f;
                    }
                    glow->SetFlareScale(scale);
                    glow->Update();
                }
            } else if (glow) {
                const float scale = glow->GetFlareScale() - static_cast<float>(timeDelta) * 0.0005f;
                if (scale <= 0.0f) {
                    m_burnedOut = true;
                    if (m_flareSparks != -1) {
                        g_particlesModule.ToggleEmission(m_flareSparks, 0);
                        // Mission scripts check whether the player fired the signal flare.
                        if (IsLocalGame() && IsServer())
                            GetGame()->SetSignalPistolFired(true);
                    }
                    m_visual->SetScale(S_vector(0.0f, 0.0f, 0.0f));
                    m_visual->Update();
                    return;
                }
                glow->SetFlareScale(scale);
                glow->Update();
            }
        }
        // The trace covers this frame's part of the step.
        m_visual->SetPos(m_prevPos + shown);
        S_vector dir = shown;
        dir.Normalize();
        m_visual->SetWorldDir(dir, 0.0f);
        m_visual->SetScale(S_vector(1.0f, 1.0f, length));
        on = true;
    }
    m_visual->SetOn(on);
    m_visual->Update();
}

bool C_BulletShot::Tick(int timeDelta)
{
    if (m_finished)
        return true;
    // Flares fly at half speed.
    if (m_flare && m_velocity.y != 0.0f)
        timeDelta = static_cast<int>(static_cast<float>(timeDelta) * 0.5f);

    m_interpolation = 0;
    m_prevPos = m_pos;
    const float dt = static_cast<float>(timeDelta);
    const S_vector step(dt * m_velocity.x * 0.001f, dt * m_velocity.y * 0.001f, dt * m_velocity.z * 0.001f);
    S_vector dir = m_velocity;
    dir.Normalize();

    S_CollisionQuery query;
    if (m_distance < 5.0f)
        RemoveShooterCollision(m_shooter);
    const int count = QueryShotSegment(m_pos, step, query);
    if (m_distance < 5.0f)
        RestoreShooterCollision(m_shooter);
    SortHits(query);

    C_Human *shooter = static_cast<C_Human *>(m_shooter);
    S_vector end = m_pos + step;
    C_Actor *lastActor = nullptr;
    for (int i = 0; i < count; ++i) {
        S_CollisionHit &hit = query.hits[i];
        C_Actor *actor = nullptr;
        I3D_frame *hitFrame = nullptr;
        const float power = GetDamageAt((hit.pos - m_pos).Magnitude() + m_distance, m_damage);
        S_CollisionVolume *volume = hit.volume;
        int material;

        if (!(volume->flags & COLLISION_FLAG_DYNAMIC)) {
            // Level geometry: visible meshes are tested precisely.
            material = GetVolumeMaterial(*volume);
            if ((volume->flags & 0xC0) && volume->frame &&
                (volume->frame->GetType() == FRAME_MODEL || volume->frame->GetType() == FRAME_VISUAL) &&
                volume->frame->GetRenderStamp() == GetScene()->GetRenderStamp()) {
                hitFrame = IntersectVisual(m_pos, step, dir, volume->frame, hit);
                if (!hitFrame)
                    continue;
                if (IsServer() && hitFrame)
                    NotifyFrameHit(hitFrame);
            }
            if (material == MATERIAL_PASS)
                continue;
            if (material == MATERIAL_STOP) {
                m_finished = true;
                end = hit.pos;
                break;
            }
        } else {
            actor = volume->actor;
            if (lastActor && lastActor == actor)
                continue;                                   // one hit per actor
            lastActor = actor;
            if (actor == m_shooter)
                continue;
            if (!actor) {
                // Doors, physics objects and destructible parts.
                hitFrame = IntersectVisual(m_pos, step, dir, volume->frame, hit);
                if (!hitFrame)
                    continue;
                S_FrameData *volumeData = volume->frame->GetData();
                T_PhysicsObject physics = volumeData ? volumeData->physicsObject : g_noPhysicsObject;
                C_VersionKeeper *keeper = hitFrame->GetData() ? hitFrame->GetData()->versionKeeper : nullptr;
                if (keeper) {
                    material = keeper->GetMaterialId();
                    if (IsServer()) {
                        const float absorption = static_cast<float>(g_materials[material].absorption);
                        if (!keeper->Damage(static_cast<int>(power - absorption), &hitFrame, VERSION_EFFECTS_ALL))
                            hitFrame = nullptr;
                    }
                } else {
                    material = GetFrameMaterial(hitFrame);
                    if (physics != g_noPhysicsObject) {
                        if (material == MATERIAL_NONE)
                            material = GetFrameMaterial(volume->frame);
                        GetPhysicsModule()->ApplyImpulse(physics, m_velocity, hit.pos, 0.01f);
                    }
                    if (IsServer() && hitFrame)
                        NotifyFrameHit(hitFrame);
                }
            } else if (m_fake) {
                continue;
            } else if (actor->GetType() == ACTOR_HUMAN) {
                auto *human = static_cast<C_Human *>(actor);
                // In single player the AI members of the player's team do not hit each other.
                if (IsLocalGame() && !shooter->IsLocalPlayer() && human->GetRole() == HUMAN_ROLE_PLAYER &&
                    shooter->GetRole() == HUMAN_ROLE_PLAYER)
                    continue;
                const int zone = g_hitZones.GetHitZone(human, m_pos, step);
                if (zone == HIT_ZONE_NONE) {
                    // A miss: the target notices it is being shot at.
                    if (IsServer() && !m_fake && human->GetHealth() > 0 && human->IsHostileTo(m_shooter))
                        human->NotifyShotAt(m_shooter, m_velocity);
                    continue;
                }
                material = zone == HIT_ZONE_HELMET ? MATERIAL_HELMET : human->GetMaterialId(nullptr);
                if (IsServer()) {
                    const float absorption = static_cast<float>(g_materials[material].absorption);
                    if (absorption < power)
                        human->Hit(HIT_BULLET, power - absorption, hit.pos, dir, m_shooter, zone);
                }
            } else if (actor->GetType() == ACTOR_STATIC_WEAPON) {
                if (IsServer())
                    actor->Hit(HIT_BULLET, power, m_pos, step, m_shooter, 0);
                hitFrame = IntersectVisual(m_pos, step, dir, actor->GetFrame(), hit);
                if (!hitFrame)
                    continue;
                material = actor->GetMaterialId(hitFrame);
            } else if (actor->GetType() == ACTOR_CAR) {
                if (shooter->GetVehicle() == actor)
                    continue;                               // the shooter's own vehicle
                if (IsServer())
                    actor->Hit(HIT_BULLET, power, hit.pos, dir, m_shooter, 0);
                material = static_cast<C_Car *>(actor)->GetHitMaterial(m_pos, dir);
                if (material == MATERIAL_NONE)
                    continue;
                if (IsServer())
                    NotifyFrameHit(actor->GetFrame());
            } else {
                hitFrame = IntersectVisual(m_pos, step, dir, actor->GetFrame(), hit);
                if (!hitFrame)
                    continue;
                material = actor->GetMaterialId(hitFrame);
                if (IsServer())
                    NotifyFrameHit(actor->GetFrame());
                if (IsServer()) {
                    const float absorption = static_cast<float>(g_materials[material].absorption);
                    // (The hit part is passed as the frame for actors other than humans.)
                    if (absorption < power)
                        actor->Hit(HIT_BULLET, power - absorption, hit.pos, dir, m_shooter,
                                   static_cast<int>(reinterpret_cast<intptr_t>(hitFrame)));
                }
            }
        }

        // --- the bullet meets `material` at hit.pos -------------------------------------
        if (material == MATERIAL_NONE) {
            // (The original formats a debug message here, asking to report the frame.)
            m_finished = true;
            end = hit.pos;
            break;
        }
        if (IsClient() && (!actor || actor->GetType() != ACTOR_HUMAN)) {
            g_shotManager.PlayHitSound(m_shooter, material, hit.pos, false, m_fake);
            // Items lying around get no bullet holes.
            I3D_frame *decalFrame = actor && actor->GetType() == ACTOR_ITEM ? nullptr : hitFrame;
            g_shotManager.OnBulletHit(m_shooter, decalFrame, material, hit.pos, hit.normal);
        }
        const S_MaterialDef &def = g_materials[material];
        if (power < static_cast<float>(def.absorption)) {
            // Too weak to go on: stops, or ricochets once unless it hits head-on.
            if (m_ricocheted || dir.Dot(hit.normal) < -0.95f) {
                m_finished = true;
            } else {
                m_ricocheted = true;
                DeflectVector(m_velocity, hit.normal, RandFloat() * 0.1f + 1.1f);
                m_velocity *= 0.2f;
                m_damage *= 0.3f;
            }
            end = hit.pos;
            break;
        }
        if (def.penetrationCost != MATERIAL_IMPENETRABLE && static_cast<float>(def.penetrationCost) <= power) {
            m_damage -= static_cast<float>(def.penetrationCost);
            continue;                                       // goes through
        }
        m_finished = true;
        end = hit.pos;
        break;
    }

    m_velocity.y -= dt * 0.001f * 9.8f;
    if (!m_finished) {
        m_distance += (end - m_pos).Magnitude();
        if (m_distance > 600.0f)
            m_finished = true;
        if (IsClient())
            CheckNearMiss(m_pos, end);
    } else {
        if (!IsClient())
            return true;
        // Clients keep the bullet for one more tick so that its trace reaches the end.
        CheckNearMiss(m_pos, end);
    }
    m_pos = end;
    m_step = m_pos - m_prevPos;
    return false;
}

void C_BulletShot::InitClient(C_Actor *shooter, const S_vector &pos, const S_vector &dir, C_FirearmComp *weapon,
                              I3D_frame *traceModel, bool fake, bool /*flare*/)
{
    // (m_velocity stays the unit direction: TickClient moves at the muzzle velocity.)
    C_Shot::Init(shooter, pos, dir, weapon, nullptr, fake);
    m_flareSparks = -1;
    m_flare = false;
    m_burnedOut = false;
    m_distance = 0.0f;
    m_traceDistance = 0;
    m_prevPos = pos;
    m_step.Zero();
    if (traceModel) {
        I3D_billboard_visual *trace = CreateTrace(traceModel);
        trace->SetUnknownA4(1.0f);
        m_visual = trace;
        m_visual->LinkTo(GetScene()->GetDynamicRoot(), 0);
        m_visual->SetOn(false);
        m_visual->Update();
    }
    m_ricocheted = false;
    m_nearMissPlayed = false;
    m_finished = false;
    m_unknown74 = 0;

    // The bullet ends where a 500 m ray from the muzzle hits.
    const S_vector reach = dir * 500.0f;
    if (S_CollisionVolume *volume = g_collisionWorld.RayTest(pos, reach, &m_target, &m_targetNormal, 0x2DC100,
                                                              0x900000)) {
        m_targetMaterial = GetVolumeMaterial(*volume);
    } else {
        m_target = reach + pos;
        m_targetMaterial = MATERIAL_NONE;
    }
    CheckNearMiss(m_pos, m_target);
}

bool C_BulletShot::TickClient(int timeDelta)
{
    S_vector move = m_target - m_pos;
    const float speed = m_weapon->GetMuzzleVelocity() * static_cast<float>(timeDelta) * 0.001f;
    if (move.Magnitude() <= speed) {
        m_finished = true;
    } else {
        move.Normalize();
        move *= speed;
    }
    m_pos += move;
    if (m_visual) {
        m_visual->SetPos(m_pos);
        m_visual->SetWorldDir(move, 0.0f);
        m_visual->SetScale(S_vector(1.0f, 1.0f, speed));
        m_visual->SetOn(true);
        m_visual->Update();
    }
    if (!m_finished)
        return false;
    if (m_targetMaterial >= 0) {
        g_shotManager.PlayHitSound(m_shooter, m_targetMaterial, m_target, false, true);
        g_shotManager.OnBulletHit(m_shooter, nullptr, m_targetMaterial, m_target, m_targetNormal);
    }
    return true;
}

// ============================================================================ C_ProjectileShot

void C_ProjectileShot::Init(C_Actor *shooter, const S_vector &pos, const S_vector &dir,
                            C_ProjectileWeaponComp *weapon, I3D_frame *model, int trailEffect, bool flySound,
                            bool cameraFollow, bool fake)
{
    C_Shot::Init(shooter, pos, dir, weapon, model, fake);
    m_velocity *= weapon->GetMuzzleVelocity();
    m_trail = -1;
    if (m_visual) {
        m_visual->LinkTo(GetScene()->GetDynamicRoot(), 0);
        m_visual->SetDir(m_velocity, 0.0f);
        m_visual->SetPos(m_pos);
        m_visual->SetOn(true);
        m_visual->Update();
        if (trailEffect >= 0)
            m_trail = g_particlesModule.CreateAt(trailEffect, m_pos, -dir, m_visual, true, true);
    }
    m_distance = 0.0f;
    m_traceDistance = 0;
    m_prevPos = pos;
    m_step.Zero();
    m_unknown7C = 0;
    m_exploded = false;
    m_flightTime = 0;
    m_removeDelay = 0;
    m_cameraFollows = false;
    m_allowCameraFollow = cameraFollow;

    m_flySound = nullptr;
    if (!IsClient() || !flySound || !m_visual)
        return;
    // Whistle of an incoming shell, moving with the projectile.
    m_flySound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    char fileName[32];
    std::sprintf(fileName, "e_incm%d.wav", std::rand() % 3 + 1);
    if (g_soundCache.OpenInto(m_flySound, fileName) != I3D_OK) {
        m_flySound->Release();
        m_flySound = nullptr;
        return;
    }
    m_flySound->SetSoundType(I3DSOUND_POINT);
    m_flySound->SetRange(20.0f, 50.0f, 0.5f, 0.5f);
    m_flySound->SetLoop(true);
    m_flySound->SetOn(true);
    m_flySound->SetVolume(1.0f);
    m_flySound->SetName("FollowBulletSound");
    m_flySound->LinkTo(m_visual, 0);
    m_flySound->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
}

void C_ProjectileShot::Release()
{
    if (m_flySound) {
        m_flySound->Release();
        m_flySound = nullptr;
    }
    if (m_visual) {
        if (m_trail >= 0)
            g_particlesModule.Destroy(m_trail);
        m_trail = -1;
        m_visual->Release();
    }
    if (m_cameraFollows)
        GetCameraModule()->SetSpecialMode(CAMERA_MODE_PROJECTILE, false, nullptr);
    m_visual = nullptr;
    C_Shot::Release();
}

void C_ProjectileShot::Update(int timeDelta)
{
    if (!m_visual || m_step.Square() < 1e-16f)
        return;
    m_flightTime += timeDelta;
    if (m_interpolation + timeDelta > 100)
        timeDelta = 100 - m_interpolation;
    m_interpolation += timeDelta;
    const S_vector shown = m_step * (static_cast<float>(m_interpolation) * 0.01f);
    m_visual->SetPos(m_prevPos + shown);
    S_vector dir = shown;
    dir.Normalize();
    m_visual->SetWorldDir(dir, 0.0f);
    m_visual->SetOn(true);
    m_visual->Update();
    // The camera rides along with the local player's projectile after 300 ms.
    if (m_allowCameraFollow && !m_cameraFollows && m_shooter && static_cast<C_Human *>(m_shooter)->IsLocalPlayer() &&
        m_flightTime > 300) {
        GetCameraModule()->SetSpecialMode(CAMERA_MODE_PROJECTILE, true, m_visual);
        m_cameraFollows = true;
    }
}

bool C_ProjectileShot::Tick(int timeDelta)
{
    if (m_exploded) {
        // Gone; kept for a while if the camera follows it.
        if (m_trail >= 0)
            g_particlesModule.Destroy(m_trail);
        if (m_flySound && m_flySound->IsOn())
            m_flySound->SetOn(false);
        if (m_visual && m_visual->IsOn())
            m_visual->SetOn(false);
        m_trail = -1;
        if (m_removeDelay != 0)
            m_removeDelay -= timeDelta;
        return m_removeDelay < 1;
    }

    m_interpolation = 0;
    m_prevPos = m_pos;
    const float dt = static_cast<float>(timeDelta);
    const S_vector step(dt * m_velocity.x * 0.001f, dt * m_velocity.y * 0.001f, dt * m_velocity.z * 0.001f);
    S_vector dir = m_velocity;
    dir.Normalize();

    S_CollisionQuery query;
    if (m_distance < 5.0f)
        RemoveShooterCollision(m_shooter);
    const int count = QueryShotSegment(m_pos, step, query);
    if (m_distance < 5.0f)
        RestoreShooterCollision(m_shooter);
    SortHits(query);

    auto *weapon = static_cast<C_ProjectileWeaponComp *>(m_weapon);
    C_Human *shooter = static_cast<C_Human *>(m_shooter);
    S_vector end = m_pos + step;
    S_vector endNormal(0.0f, 1.0f, 0.0f);
    int material = MATERIAL_NONE;
    C_Actor *lastActor = nullptr;
    bool explode = false;
    // Share of the explosion damage: halved by thin materials passed through, full for a
    // human, a tenth for a vehicle (which takes the direct hit as well).
    float damageScale = 1.0f;
    const float directDamage = weapon->GetExplosionDamage();
    for (int i = 0; i < count; ++i) {
        S_CollisionHit &hit = query.hits[i];
        I3D_frame *hitFrame = nullptr;
        material = MATERIAL_NONE;
        S_CollisionVolume *volume = hit.volume;

        if (!(volume->flags & COLLISION_FLAG_DYNAMIC)) {
            material = GetVolumeMaterial(*volume);
            if (volume->flags & 0xC0) {
                hitFrame = IntersectVisual(m_pos, step, dir, volume->frame, hit);
                if (hitFrame && IsServer())
                    NotifyFrameHit(hitFrame);
            }
            if (material == MATERIAL_PASS)
                continue;
            if (material == MATERIAL_STOP) {
                // Vanishes without an explosion.
                m_exploded = true;
                m_removeDelay = 0;
                end = hit.pos;
                endNormal = hit.normal;
                break;
            }
            damageScale = 0.5f;
        } else {
            C_Actor *actor = volume->actor;
            if (lastActor && lastActor == actor)
                continue;
            lastActor = actor;
            if (!actor) {
                hitFrame = IntersectVisual(m_pos, step, dir, volume->frame, hit);
                if (!hitFrame)
                    continue;
                S_FrameData *volumeData = volume->frame->GetData();
                T_PhysicsObject physics = volumeData ? volumeData->physicsObject : g_noPhysicsObject;
                C_VersionKeeper *keeper = hitFrame->GetData() ? hitFrame->GetData()->versionKeeper : nullptr;
                if (keeper) {
                    material = keeper->GetMaterialId();
                    if (IsServer() && !keeper->Damage(static_cast<int>(m_damage), &hitFrame, VERSION_EFFECTS_ALL))
                        hitFrame = nullptr;
                } else {
                    material = GetFrameMaterial(hitFrame);
                    if (physics != g_noPhysicsObject) {
                        if (material == MATERIAL_NONE)
                            material = GetFrameMaterial(volume->frame);
                        GetPhysicsModule()->ApplyImpulse(physics, m_velocity, hit.pos, 0.01f);
                    }
                    if (IsServer() && hitFrame)
                        NotifyFrameHit(hitFrame);
                }
            } else if (m_fake) {
                continue;
            } else if (actor->GetType() == ACTOR_HUMAN) {
                auto *human = static_cast<C_Human *>(actor);
                if (g_hitZones.GetHitZone(human, m_pos, step) == HIT_ZONE_NONE) {
                    if (IsServer() && human->GetHealth() > 0)
                        human->NotifyShotAt(m_shooter, m_velocity);
                    continue;
                }
                material = human->GetMaterialId(nullptr);
                explode = true;
                damageScale = 1.0f;
            } else if (actor->GetType() == ACTOR_CAR) {
                if (shooter->GetVehicle() == actor)
                    continue;
                if (IsServer()) {
                    // A direct hit; in single player on difficulty 1 the player's shots count four times.
                    const S_vector at = hit.pos - dir * 0.1f;
                    if (IsLocalGame() && g_gameProfile.GetDifficulty() == 1 && shooter->GetRole() == HUMAN_ROLE_PLAYER &&
                        m_visual)
                        actor->Hit(HIT_PROJECTILE, directDamage * 4.0f, at, dir, m_shooter, 0);
                    else
                        actor->Hit(HIT_PROJECTILE, directDamage, at, dir, m_shooter, 0);
                }
                material = static_cast<C_Car *>(actor)->GetHitMaterial(m_pos, step);
                if (material == MATERIAL_NONE) {
                    I3D_collision_info info;
                    hitFrame = GetScene()->IntersectFrame(m_pos, step, actor->GetFrame(), &info, I3DCOL_VISUAL_GEOMETRY,
                                                          0, 0, 0);
                    if (!hitFrame)
                        continue;
                    material = actor->GetMaterialId(hitFrame);
                    if (material == MATERIAL_NONE)
                        continue;
                }
                if (IsServer())
                    NotifyFrameHit(actor->GetFrame());
                damageScale = 0.1f;
                explode = true;
            } else {
                hitFrame = IntersectVisual(m_pos, step, dir, actor->GetFrame(), hit);
                if (!hitFrame)
                    continue;
                material = actor->GetMaterialId(hitFrame);
                if (IsServer())
                    NotifyFrameHit(actor->GetFrame());
                explode = true;
            }
        }

        // --- the projectile meets `material` at hit.pos ---------------------------------
        const uint32_t cost = g_materials[material].penetrationCost;
        if (cost == MATERIAL_IMPENETRABLE || explode || m_damage < static_cast<float>(static_cast<int>(cost))) {
            explode = true;
            end = hit.pos;
            endNormal = hit.normal;
            break;
        }
        m_damage -= static_cast<float>(static_cast<int>(cost));
        if (IsClient() && HasProjectileHitEffect(material)) {
            g_shotManager.PlayHitSound(m_shooter, material, hit.pos, false, m_fake);
            g_shotManager.PlayHitEffect(material, hit.pos, hit.normal);
        }
    }

    m_velocity.y -= dt * 0.001f * 9.8f;
    if (!m_exploded) {
        m_distance += (end - m_pos).Magnitude();
        if (m_distance > 600.0f) {
            m_exploded = true;
            m_removeDelay = 0;
        }
    } else if (!IsClient()) {
        return true;
    }
    m_pos = end;
    m_step = m_pos - m_prevPos;
    if (explode) {
        if (IsServer())
            g_shotManager.Explode(m_shooter, m_pos + endNormal * 0.2f, weapon->GetExplosionRadius(),
                                  damageScale * weapon->GetExplosionDamage(), weapon->GetExplosionSound());
        if (IsClient())
            g_shotManager.PlayExplosion(m_pos, endNormal, weapon->GetExplosionEffect(), material,
                                        weapon->GetExplosionSound());
        m_exploded = true;
        if (m_cameraFollows)
            m_removeDelay = 1500;                           // the camera lingers at the explosion
    }
    return false;
}

// ============================================================================ C_ThrownShot

void C_ThrownShot::Init(C_Actor *thrower, const S_vector &pos, const S_vector &dir, C_Item *item, bool fake)
{
    m_state = STATE_FLYING;
    auto *weapon = static_cast<C_ProjectileWeaponComp *>(item->GetDef()->GetComponent());
    C_Shot::Init(thrower, pos, dir, weapon, item->GetFrame(0), fake);
    float speed = static_cast<C_WeaponItem *>(item)->GetThrowStrength() * weapon->GetMuzzleVelocity();
    if (speed < 5.0f)
        speed = 5.0f;
    m_velocity *= speed;

    static_cast<I3D_object_visual *>(m_visual)->SetAlpha(1.0f);
    m_visual->LinkTo(GetScene()->GetDynamicRoot(), 0);
    m_visual->SetDir(m_velocity, 0.0f);
    m_visual->SetPos(m_pos);
    m_visual->SetOn(true);
    m_visual->Update();
    m_time = 0;
    m_fuseTime = weapon->GetFuseTime();

    S_RigidBodyParams params;
    params.unknown00 = {0.0f, 1.0f, 0.0f, 0.0f};
    const S_vector &start = m_visual->GetWorldPos();
    params.unknown10 = {start.x, start.y + 10.0f, start.z, 0.0f};
    params.SetVelocity(m_velocity);
    params.weight = 2.0f;
    params.friction = 0.5f;
    params.callback = OnContact;
    params.context = this;
    params.moveCollision = true;
    params.unknown41 = false;
    m_body.SetFrame(m_visual);
    m_body.Setup(params);
    m_body.Start();

    m_bounceSoundTimer = 0;
    m_bounceSound = item->GetDef()->GetCategory();
}

void C_ThrownShot::Release()
{
    m_body.Stop();
    if (m_visual)
        m_visual->Release();
    m_visual = nullptr;
    C_Shot::Release();
}

void C_ThrownShot::Update(int timeDelta)
{
    if (m_state != STATE_FINISHED)
        Simulate(timeDelta);
}

bool C_ThrownShot::Tick(int timeDelta)
{
    if (m_state == STATE_FINISHED)
        return true;
    // Clients simulate the flight every frame (Update), the server in its ticks.
    if (!IsClient())
        Simulate(timeDelta);
    return m_state != STATE_FLYING;
}

void C_ThrownShot::Simulate(int timeDelta)
{
    RemoveShooterCollision(m_shooter);
    m_body.Tick(timeDelta);
    RestoreShooterCollision(m_shooter);

    auto *weapon = static_cast<C_ProjectileWeaponComp *>(m_weapon);
    if (m_state == STATE_IMPACT) {
        m_body.Stop();
        if (IsServer())
            g_shotManager.Explode(m_shooter, m_normal * 0.2f + m_visual->GetWorldPos(), weapon->GetExplosionRadius(),
                                  weapon->GetExplosionDamage(), weapon->GetExplosionSound());
        if (IsClient()) {
            g_shotManager.PlayExplosion(m_visual->GetWorldPos(), m_normal, weapon->GetExplosionEffect(), MATERIAL_NONE,
                                        weapon->GetExplosionSound());
            m_state = STATE_FINISHED;
            return;
        }
    } else {
        m_time += timeDelta;
        if (m_fuseTime > 0 && m_time > m_fuseTime) {
            m_body.Stop();
            if (IsServer())
                g_shotManager.Explode(m_shooter, m_visual->GetWorldPos(), weapon->GetExplosionRadius(),
                                      weapon->GetExplosionDamage(), weapon->GetExplosionSound());
            if (IsClient())
                g_shotManager.PlayExplosion(m_visual->GetWorldPos(), m_normal, weapon->GetExplosionEffect(), m_material,
                                            weapon->GetExplosionSound());
            m_state = STATE_FINISHED;
        }
        if (m_time < 15001)
            return;
    }
    m_state = STATE_FINISHED;                               // (also: lost after 15 s)
}

void C_ThrownShot::PlayBounceSound()
{
    // (m_bounceSoundTimer is never counted down: only the first bounce is heard.)
    if (m_bounceSoundTimer >= 50 || m_bounceSound <= 0)
        return;
    S_SoundRequest sound;
    sound.group = m_bounceSound;
    sound.chapter = SOUND_CHAPTER_FALLING_HIT;
    sound.source = nullptr;
    sound.pos = m_visual->GetWorldPos();
    sound.attachTo = nullptr;
    sound.flags = SOUND_SILENT_TO_AI;
    GetSoundModule()->Play(sound);
    m_bounceSoundTimer = 2000;
}

int __fastcall C_ThrownShot::OnContact(int event, int, void *context, const S_RigidBodyContact *contactPtr)
{
    const S_RigidBodyContact &contact = *contactPtr;
    if (event != RIGID_BODY_EVENT_CONTACT)
        return 0;
    auto *shot = static_cast<C_ThrownShot *>(context);
    auto *thrower = static_cast<C_Human *>(shot->m_shooter);
    // In single player the grenades of the player's AI team mates pass what they touch
    // in the first 200 ms (the thrower's neighbours).
    if (IsLocalGame() && !thrower->IsLocalPlayer() && thrower->GetRole() == HUMAN_ROLE_PLAYER && shot->m_time <= 200)
        return RIGID_BODY_REFUSE;

    const float damage = shot->m_weapon->GetDamage();
    S_CollisionVolume *volume = contact.volume;
    C_Actor *actor = nullptr;
    if (!(volume->flags & COLLISION_FLAG_DYNAMIC)) {
        const int material = GetVolumeMaterial(*volume);
        if (material == MATERIAL_PASS)
            return RIGID_BODY_REFUSE;
        if (material == MATERIAL_STOP) {
            shot->m_state = STATE_FINISHED;
            return 3;                                       // (counts: any value but 1)
        }
        // Flies through materials cheaper than its damage.
        const uint32_t cost = g_materials[material].penetrationCost;
        if (cost != MATERIAL_IMPENETRABLE && static_cast<float>(cost) < damage)
            return RIGID_BODY_REFUSE;
    } else {
        actor = volume->actor;
        if (!actor) {
            S_FrameData *data = volume->frame->GetData();
            C_VersionMaster *master = data ? data->versionMaster : nullptr;
            if (master) {
                // Breaks through the destructible part it touches (fragile ones shatter).
                const S_vector from = contact.normal * 0.1f + contact.pos;
                I3D_collision_info info;
                I3D_frame *part = GetScene()->IntersectFrame(from, -contact.normal, volume->frame, &info, 0, 0, 0, 0);
                if (part) {
                    const int material = GetFrameMaterial(part);
                    // Fixed: the original reads g_materials[-1] for parts without a material.
                    const uint32_t cost =
                        material == MATERIAL_NONE ? MATERIAL_IMPENETRABLE : g_materials[material].penetrationCost;
                    if (cost != MATERIAL_IMPENETRABLE && static_cast<float>(cost) < damage) {
                        if (IsServer())
                            master->DamageFrame(part, material == MATERIAL_FRAGILE ? 10000 : static_cast<int>(damage));
                        return RIGID_BODY_REFUSE;
                    }
                }
            }
        } else {
            if (actor == shot->m_shooter)
                return RIGID_BODY_REFUSE;
            actor->GetMaterialId(volume->frame);            // (result not used)
        }
    }

    if (shot->m_fuseTime != 0) {
        // A bounce.
        if (IsServer() && actor && actor->GetType() == ACTOR_HUMAN)
            actor->Hit(HIT_THROWN_OBJECT, 1.0f, contact.pos, contact.normal, shot->m_shooter, 0);
        if (IsClient())
            shot->PlayBounceSound();
        return 2;                                           // (counts)
    }
    shot->m_state = STATE_IMPACT;                           // an impact grenade
    return 3;                                               // (counts)
}

// ============================================================================ effects

// A cartridge shell flying out of the local player's weapon (0x40 bytes, pooled).
class alignas(16) C_Shell {
public:
    C_Shell() = default;                                    // 0x0044db10
    ~C_Shell() { Release(); }
    C_Shell(const C_Shell &) = delete;
    C_Shell &operator=(const C_Shell &) = delete;

    void Init(C_Human *owner, I3D_frame *model, const S_vector &pos, const S_vector &dir, const S_vector &velocity,
              int soundGroup);                              // 0x00454190
    // True when the shell is done (three bounces or 2 s).
    bool Tick(int timeDelta);                               // 0x00454360
    void Hide();                                            // 0x004546c0
    void Release();                                         // 0x004546a0

private:
    S_vector m_pos = {0, 0, 0};                             // +0x00
    S_vector m_velocity = {0, 0, 0};                        // +0x10
    I3D_frame *m_visual = nullptr;                          // +0x20 kept while pooled
    C_Human *m_owner = nullptr;                             // +0x24
    int m_time = 0;                                         // +0x28 ms
    int m_bounces = 0;                                      // +0x2C
    int m_soundGroup = 0;                                   // +0x30 chapter 5
};

void C_Shell::Init(C_Human *owner, I3D_frame *model, const S_vector &pos, const S_vector &dir,
                   const S_vector &velocity, int soundGroup)
{
    m_pos = pos;
    m_velocity = velocity;
    m_owner = owner;
    m_time = 0;
    m_soundGroup = soundGroup;
    if (!model)
        return;
    if (!m_visual) {
        m_visual = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
        m_visual->Duplicate(model);
    }
    m_visual->LinkTo(nullptr, 0);
    m_visual->SetPosDir(pos, dir, 0.0f);
    if (owner->IsLocalPlayer() && GetGame()->IsFirstPersonView()) {
        // Seen in the first-person view: smaller, and moving with the view.
        m_visual->SetScale(S_vector(0.6f, 0.6f, 0.6f));
        m_visual->LinkTo(GetScene()->GetFirstPersonRoot(), I3DLINK_KEEP_WORLD);
    } else {
        GetScene()->UpdateFrameSector(m_visual, m_visual->GetWorldPos());
    }
    m_visual->SetOn(true);
    m_visual->Update();
    m_bounces = 0;
}

bool C_Shell::Tick(int timeDelta)
{
    if (m_bounces == 3)
        return true;
    const float dt = static_cast<float>(timeDelta);
    m_time += timeDelta;
    const S_vector step = m_velocity * dt * 0.001f;
    m_owner->RemoveCollision(false);
    S_vector hitPos, normal;
    const bool hit = CollisionRayTest(m_pos, step, &hitPos, &normal, 1, 0) != nullptr;
    m_owner->RestoreCollision(false);
    if (!hit) {
        m_pos += step;
    } else if (m_pos == hitPos) {
        m_velocity.Zero();                                  // stuck
    } else {
        ++m_bounces;
        if (m_velocity.Magnitude() < 0.1f) {
            m_velocity.Zero();
        } else {
            m_velocity -= normal * (normal.Dot(m_velocity) * 1.2f);
            if (normal.Dot(m_velocity) < 0.0f)
                m_velocity = -m_velocity;
        }
        S_SoundRequest sound;
        sound.chapter = SOUND_CHAPTER_FALLING_HIT;
        sound.flags = SOUND_SILENT_TO_AI;
        sound.channel = 2;
        sound.pos = m_pos;
        sound.group = m_soundGroup;
        sound.source = nullptr;
        sound.attachTo = nullptr;
        GetSoundModule()->Play(sound);
        m_pos = hitPos;
    }
    if (m_visual) {
        m_visual->SetPos(m_pos);
        m_visual->Update();
    }
    if (m_velocity.Square() != 0.0f)
        m_velocity.y -= dt * 0.001f * 3.8f;
    return m_time > 2000;
}

void C_Shell::Hide()
{
    if (m_visual) {
        m_visual->SetOn(false);
        m_visual->Update();
    }
}

void C_Shell::Release()
{
    if (m_visual)
        m_visual->Release();
    m_visual = nullptr;
}

// The light of a muzzle flash (0x14 bytes): lights the muzzle's sector and the
// neighbouring sectors near it, fading out.
class C_MuzzleLight {
public:
    C_MuzzleLight() = default;
    ~C_MuzzleLight() { Release(); }
    C_MuzzleLight(const C_MuzzleLight &) = delete;
    C_MuzzleLight &operator=(const C_MuzzleLight &) = delete;

    // Lights for `duration` ms; `flash`: the weapon's flash visual, shown for 50 ms.
    void Init(I3D_frame *muzzle, I3D_frame *flash, int duration);   // 0x00453d80
    // True when the light is out.
    bool Tick(int timeDelta);                               // 0x00454060
    void Release();                                         // 0x00454120

private:
    I3D_light *m_light = nullptr;                           // +0x00 "WeaponLight"
    I3D_sector *m_sector = nullptr;                         // +0x04
    I3D_frame *m_flash = nullptr;                           // +0x08 holds a reference while shown
    int m_time = 0;                                         // +0x0C ms
    int m_duration = 0;                                     // +0x10
};

void C_MuzzleLight::Init(I3D_frame *muzzle, I3D_frame *flash, int duration)
{
    m_duration = duration;
    if (!muzzle) {
        m_time = duration;
        m_light = nullptr;
        return;
    }
    m_time = 0;
    m_light = static_cast<I3D_light *>(I3DGetDriver()->CreateFrame(FRAME_LIGHT));
    m_light->LinkTo(muzzle, 0);
    m_light->SetName("WeaponLight");
    m_light->SetMode(I3DLIGHTMODE_WEAPON_FLASH);
    m_light->SetLightType(I3DLIGHT_POINT);
    m_light->SetColor(S_vector(1.0f, 1.0f, 1.0f));
    m_light->SetPower(1.5f);
    m_light->SetSpecularColor(S_vector(1.0f, 1.0f, 1.0f));
    m_light->SetSpecularPower(1.0f);
    m_light->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));

    m_sector = GetScene()->GetSector(muzzle->GetWorldPos());
    m_sector->AddLight(m_light);
    // Sectors with a portal into this one whose origin is within 4 m are lit as well.
    for (int i = 0; i < m_sector->NumIncomingPortals(); ++i) {
        I3D_sector *other = m_sector->GetIncomingPortal(i)->GetOwner();
        if ((other->GetWorldPos() - muzzle->GetWorldPos()).Square() <= 16.0f) {
            other->AddLight(m_light);
            other->Update();
        }
    }
    m_light->SetRange(4.0f);
    m_light->SetScale(S_vector(1.0f, 1.0f, 1.0f));
    m_light->SetAttenuation(0.99f, 0.0f, 0.01f);
    m_light->SetOn(true);
    m_light->Update();

    m_flash = flash;
    if (flash) {
        flash->AddRef();
        flash->SetOn(true);
        flash->SetUnknown50(1.0f);
        flash->Update();
    }
}

bool C_MuzzleLight::Tick(int timeDelta)
{
    if (m_flash && m_time > 50) {
        m_flash->SetOn(false);
        m_flash->Update();
        m_flash->Release();
        m_flash = nullptr;
    }
    if (m_time >= m_duration) {
        if (m_light) {
            m_light->SetOn(false);
            m_light->Update();
        }
        return true;
    }
    const float fade = static_cast<float>(m_time) / static_cast<float>(m_duration);
    m_light->SetPower(1.0f - fade);
    m_light->SetRange(4.0f - fade);
    m_light->Update();
    m_time += timeDelta;
    return false;
}

void C_MuzzleLight::Release()
{
    if (m_light) {
        I3D_sector *const *sectors = m_light->GetSectors();
        for (int i = m_light->NumSectors(); i != 0; --i)
            sectors[i - 1]->RemoveLight(m_light);
        m_light->LinkTo(nullptr, 0);
        m_light->Release();
        m_light = nullptr;
    }
    if (m_flash) {
        m_flash->SetOn(false);
        m_flash->Release();
        m_flash = nullptr;
    }
}

// A pool of blood growing on the ground (0x10 bytes).
class C_BloodPool {
public:
    C_BloodPool() = default;
    ~C_BloodPool() { Release(); }
    C_BloodPool(const C_BloodPool &) = delete;
    C_BloodPool &operator=(const C_BloodPool &) = delete;

    void Init(const S_vector &pos, const S_vector &normal, IImage *texture, float maxSize);   // 0x004546e0
    // Moves the pool to a new place and starts it again.
    void Reuse(const S_vector &pos, const S_vector &normal, float maxSize);   // 0x00454940
    void Tick(int timeDelta);                               // 0x00454b50
    void Release();                                         // 0x00454c40

private:
    I3D_projector_visual *m_visual = nullptr;               // +0x00
    float m_size = 0.0f;                                    // +0x04
    float m_maxSize = 0.0f;                                 // +0x08 randomised by +-0.1
    int m_time = 0;                                         // +0x0C ms towards the next growth, -1 = grown
};

void C_BloodPool::Init(const S_vector &pos, const S_vector &normal, IImage *texture, float maxSize)
{
    if (!texture) {
        m_visual = nullptr;
        return;
    }
    const S_vector at = normal * 0.05f + pos;
    m_visual = static_cast<I3D_projector_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_PROJECTOR));
    m_visual->SetTexture(texture);
    m_visual->SetPos(at);
    m_visual->SetScale(S_vector(0.1f, 0.1f, 0.1f));
    m_visual->SetDir(-normal, (RandFloat() - 0.5f) * TWO_PI);
    m_visual->SetUnknownB4(1);
    m_visual->LinkTo(nullptr, I3DLINK_KEEP_WORLD);
    I3D_sector *sector = GetScene()->GetSector(at);
    m_visual->LinkTo(sector, I3DLINK_KEEP_WORLD);
    m_visual->SetColor(BloodColor(m_visual, at, sector));
    m_visual->SetMode(PROJECTOR_MODE_DECAL);
    m_visual->Update();
    m_time = 0;
    m_size = 0.01f;
    const float r = RandFloat() - 0.5f;
    m_maxSize = static_cast<float>((r + r) * 0.1 + maxSize);
}

void C_BloodPool::Reuse(const S_vector &pos, const S_vector &normal, float maxSize)
{
    // Fixed: the original dereferences the missing visual of a pool made without a texture.
    if (!m_visual)
        return;
    const S_vector at = normal * 0.05f + pos;
    m_visual->SetPos(at);
    m_visual->SetScale(S_vector(0.1f, 0.1f, 0.1f));
    m_visual->SetDir(-normal, (RandFloat() - 0.5f) * TWO_PI);
    I3D_sector *sector = GetScene()->GetSector(at);
    m_visual->LinkTo(sector, I3DLINK_KEEP_WORLD);
    m_visual->SetColor(BloodColor(m_visual, at, sector));
    m_visual->SetMode(PROJECTOR_MODE_DECAL);
    m_visual->Update();
    const float r = RandFloat() - 0.5f;
    m_time = 0;
    m_size = 0.01f;
    m_maxSize = static_cast<float>((r + r) * 0.1 + maxSize);
}

void C_BloodPool::Tick(int timeDelta)
{
    if (m_time == -1 || !m_visual)
        return;
    // Grows by 1 mm every 100 ms up to its size.
    m_time += timeDelta;
    if (m_time > 100) {
        m_size += 0.001f;
        m_visual->SetScale(S_vector(m_size, m_size, 0.1f));
        m_visual->SetMode(PROJECTOR_MODE_DECAL);
        m_visual->Update();
        m_time -= 100;
    }
    if (m_size >= m_maxSize) {
        m_visual->Update();
        m_time = -1;
    }
}

void C_BloodPool::Release()
{
    if (m_visual)
        m_visual->Release();
    m_visual = nullptr;
}

// A drop of blood flying until it lands and leaves a stain (0x30 bytes).
class alignas(16) C_BloodDrop {
public:
    C_BloodDrop() = default;
    ~C_BloodDrop() { Release(); }
    C_BloodDrop(const C_BloodDrop &) = delete;
    C_BloodDrop &operator=(const C_BloodDrop &) = delete;

    void Init(const S_vector &pos, const S_vector &dir, IImage *texture);    // 0x00454c60
    void Reuse(const S_vector &pos, const S_vector &dir, IImage *texture);   // 0x00454e20
    void Tick(int timeDelta);                               // 0x00455240
    void Release();                                         // 0x00455350

private:
    void Land(const S_vector &pos, const S_vector &normal); // 0x00454ff0

    S_vector m_pos = {0, 0, 0};                             // +0x00
    S_vector m_velocity = {0, 0, 0};                        // +0x10 3 m/s +-1
    I3D_projector_visual *m_visual = nullptr;               // +0x20 hidden until it lands
    bool m_landed = false;                                  // +0x24
    int m_time = 0;                                         // +0x28 ms
};

void C_BloodDrop::Init(const S_vector &pos, const S_vector &dir, IImage *texture)
{
    m_pos = pos;
    m_velocity = dir;
    m_velocity.Normalize();
    const float r = RandFloat() - 0.5f;
    m_velocity *= r + r + 3.0f;
    m_landed = false;
    m_time = 0;
    if (!texture) {
        m_visual = nullptr;
        return;
    }
    m_visual = static_cast<I3D_projector_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_PROJECTOR));
    m_visual->SetTexture(texture);
    m_visual->SetOn(false);
    m_visual->Update();
}

void C_BloodDrop::Reuse(const S_vector &pos, const S_vector &dir, IImage *texture)
{
    if (!m_visual) {
        if (!texture) {
            m_landed = true;                                // nothing to show: stays inactive
            return;
        }
        m_visual = static_cast<I3D_projector_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_PROJECTOR));
    }
    m_visual->SetTexture(texture);
    m_visual->SetOn(false);
    m_visual->Update();
    m_pos = pos;
    m_velocity = dir;
    m_velocity.Normalize();
    const float r = RandFloat() - 0.5f;
    m_velocity *= r + r + 3.0f;
    m_landed = false;
    m_time = 0;
}

void C_BloodDrop::Tick(int timeDelta)
{
    if (m_landed || !m_visual)
        return;
    m_time += timeDelta;
    if (m_time > 1000)
        return;                                             // gave up: never shown
    const float dt = static_cast<float>(timeDelta);
    const S_vector step = m_velocity * dt * 0.001f;
    S_vector hitPos, normal;
    if (CollisionRayTest(m_pos, step, &hitPos, &normal, 0xFFFFFFFF, 0)) {
        Land(hitPos, normal);
        return;
    }
    m_pos += step;
    if (m_time > 400)
        m_velocity.y -= dt * 0.001f * 9.8f;
}

void C_BloodDrop::Land(const S_vector &pos, const S_vector &normal)
{
    const S_vector at = normal * 0.02f + pos;
    m_visual->SetMode(PROJECTOR_MODE_DROP);
    m_visual->SetPos(at);
    const float size = (RandFloat() - 0.5f) * 0.1f + 0.2f;
    m_visual->SetScale(S_vector(size, size, 0.05f));
    m_visual->SetWorldDir(-normal, (RandFloat() - 0.5f) * TWO_PI);
    m_visual->SetUnknownB4(1);
    m_visual->SetOn(true);
    m_visual->Update();
    m_visual->LinkTo(nullptr, I3DLINK_KEEP_WORLD);
    I3D_sector *sector = GetScene()->GetSector(at);
    m_visual->LinkTo(sector, I3DLINK_KEEP_WORLD);
    m_visual->SetColor(BloodColor(m_visual, at, sector));
    m_visual->SetMode(PROJECTOR_MODE_DECAL);
    m_visual->Update();
    m_landed = true;
}

void C_BloodDrop::Release()
{
    if (m_visual)
        m_visual->Release();
    m_visual = nullptr;
}

// A bullet hole, scorch mark or blood stain (8 bytes).
struct S_ShotDecal {
    S_ShotDecal() = default;
    ~S_ShotDecal()                                          // 0x0044e4c0
    {
        if (visual)
            visual->Release();
    }
    S_ShotDecal(const S_ShotDecal &) = delete;
    S_ShotDecal &operator=(const S_ShotDecal &) = delete;

    I3D_projector_visual *visual = nullptr;                 // +0x00
    C_str parentName;                                       // +0x04 frame it is linked to ("No Parent": a sector)
};

// ============================================================================ C_HitZones

C_HitZones::C_HitZones()
    : m_joints{1, 6, 13, 14, 9, 10, 20, 21, 16, 17},
      m_names{"c", "f", "l", "m", "h", "i", "s", "t", "o", "p"}
{
}

void C_HitZones::Load()
{
    m_model = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    if (g_modelCache.OpenInto(static_cast<I3D_model *>(m_model), "humanzones") != I3D_OK) {
        m_model->Release();
        m_model = nullptr;
        return;
    }
    for (int i = 0; i < ZONE_COUNT; ++i)
        m_zones[i] = m_model->FindChildFrame(m_names[i], FRAME_MASK_ANY);
    m_model->LinkTo(GetScene()->GetDynamicRoot(), 0);
    m_model->SetOn(false);
}

void C_HitZones::Free()
{
    if (m_model)
        m_model->Release();
    m_model = nullptr;
}

void C_HitZones::Place(I3D_frame *human)
{
    if (!m_model)
        return;
    I3D_frame *const *joints =
        static_cast<I3D_singlemesh_visual *>(human->FindChildFrame("a", FRAME_MASK_VISUAL))->GetJoints();
    for (int i = 0; i < ZONE_COUNT; ++i) {
        m_zones[i]->SetLocalMatrix(joints[m_joints[i]]->GetMatrix());
        m_zones[i]->Update();
    }
}

int C_HitZones::GetHitZone(C_Human *human, const S_vector &pos, const S_vector &dir)
{
    if (!m_model)
        return HIT_ZONE_NONE;
    I3D_frame *const *joints =
        static_cast<I3D_singlemesh_visual *>(human->GetFrame()->FindChildFrame("a", FRAME_MASK_VISUAL))->GetJoints();
    if (!joints)
        return HIT_ZONE_NONE;
    I3D_scene *scene = GetScene();
    I3D_collision_info info;
    for (int i = 0; i < ZONE_COUNT; ++i) {
        // The zone box follows its joint (at its own size).
        I3D_frame *zone = m_zones[i];
        zone->SetLocalMatrix(joints[m_joints[i]]->GetMatrix());
        zone->SetScale(S_vector(1.0f, 1.0f, 1.0f));
        zone->Update();
        if (i == HIT_ZONE_HEAD) {
            // A helmet stops the shot.
            C_Item *headgear = human->GetContainer(ITEM_CONTAINER_HEAD)->GetItem();
            if (headgear &&
                static_cast<C_HeadgearComp *>(headgear->GetDef()->GetComponent())->GetHeadgearType() == 0 &&
                scene->IntersectFrame(pos, dir, headgear->GetFrame(0), &info, I3DCOL_HELMET | I3DCOL_VISUAL_GEOMETRY, 0,
                                      0, 0))
                return HIT_ZONE_HELMET;
        }
        if (scene->IntersectFrame(pos, dir, zone, &info, I3DCOL_HIT_ZONE | I3DCOL_VISUAL_GEOMETRY, 0, 0, 0))
            return i;
    }
    return HIT_ZONE_NONE;
}

void C_HitZones::Print(int zone) const
{
    static const char *const names[] = {
        "body",           "head",        "left hand up",  "left hand down", "right hand up",
        "righ hand down", "left leg up", "left leg down", "right leg up",   "right leg down",
    };
    if (zone == HIT_ZONE_NONE)
        GetGame()->Print("no");
    else if (zone >= 0 && zone < 10)
        GetGame()->Print(names[zone]);
}

// ============================================================================ C_ShotManager

C_ShotManager::C_ShotManager(int id, const char *name)
    : C_Module(id, name), m_bulletPool(16), m_shellPool(16)
{
    m_bulletPool.Grow();
    m_shellPool.Grow();
}

C_ShotManager::~C_ShotManager()
{
    // (Shots and effects still alive are not deleted; the pools free theirs.)
}

void C_ShotManager::StartMission(bool server)
{
    // (The original also reads the performance counter here and ignores it.)
    if (server) {
        g_hitZones.Load();
        return;
    }
    m_maxBloodDrops = 100;
    m_maxBloodPools = IsLocalGame() ? 50 : 20;
    m_maxDecals = 100;
    m_weaponOdds = nullptr;
    m_traceModel = nullptr;
    m_shell3rd = nullptr;
    m_shellFpv = nullptr;
    m_weaponOdds = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    if (g_modelCache.OpenInto(static_cast<I3D_model *>(m_weaponOdds), "WeaponOdds") >= 0) {
        m_traceModel = m_weaponOdds->FindChildFrame("trace", FRAME_MASK_VISUAL);
        m_shell3rd = m_weaponOdds->FindChildFrame("shell3rd", FRAME_MASK_VISUAL);
        m_shellFpv = m_weaponOdds->FindChildFrame("shellFPV", FRAME_MASK_VISUAL);
    }
    m_decals.reserve(m_maxDecals);
    m_bloodDrops.reserve(m_maxBloodDrops);
    m_bloodPools.reserve(m_maxBloodPools);
    LoadTextures();
    m_clientTickTime = 0;
}

void C_ShotManager::StopMission(bool server)
{
    if (server) {
        g_hitZones.Free();
    } else {
        RemoveEffects(false);
        m_shellPool.Clear();
        for (IImage *&texture : m_decalTextures) {
            if (texture)
                texture->Release();
            texture = nullptr;
        }
        for (IImage *&texture : m_splatTextures) {
            if (texture)
                texture->Release();
            texture = nullptr;
        }
        if (m_weaponOdds)
            m_weaponOdds->Release();
        m_weaponOdds = nullptr;
        m_traceModel = nullptr;                             // (the shell models are not reset)
    }
    RemoveShots();
    m_bulletPool.Clear();
}

void C_ShotManager::RestartMission(bool)
{
    RemoveEffects(true);
    RemoveShots();
}

void C_ShotManager::RemoveEffects(bool hideShells)
{
    for (size_t i = m_shells.size(); i-- > 0;) {
        if (hideShells)
            m_shells[i]->Hide();
        m_shellPool.Put(m_shells[i]);
    }
    for (size_t i = m_decals.size(); i-- > 0;)
        delete m_decals[i];
    for (size_t i = m_bloodDrops.size(); i-- > 0;)
        delete m_bloodDrops[i];
    for (size_t i = m_bloodPools.size(); i-- > 0;)
        delete m_bloodPools[i];
    for (size_t i = m_muzzleLights.size(); i-- > 0;)
        delete m_muzzleLights[i];
    m_decals.clear();
    m_bloodDrops.clear();
    m_bloodPools.clear();
    m_muzzleLights.clear();
    m_shells.clear();
}

void C_ShotManager::RecycleBullet(C_BulletShot *bullet)
{
    bullet->Recycle();
    m_bulletPool.Put(bullet);
}

void C_ShotManager::RemoveShots()
{
    for (size_t i = m_shots.size(); i-- > 0;) {
        C_Shot *shot = m_shots[i];
        switch (shot->GetType()) {
        case SHOT_BULLET:
            RecycleBullet(static_cast<C_BulletShot *>(shot));
            break;
        case SHOT_THROWN:
        case SHOT_PROJECTILE:
            delete shot;
            break;
        default:
            break;
        }
    }
    m_shots.clear();
    // Fixed: the original keeps the client bullets (with their possibly deleted shooters)
    // into the next mission.
    for (C_BulletShot *bullet : m_clientBullets)
        RecycleBullet(bullet);
    m_clientBullets.clear();
}

void C_ShotManager::LoadTextures()
{
    IGraph *graph = GetIGraph();
    char fileName[44];
    for (int i = 0; i < 9; ++i) {
        std::sprintf(fileName, "x_decal%02d.tga", i);
        graph->LoadTexture(fileName, 0, 2, &m_decalTextures[i]);
    }
    std::sprintf(fileName, "x_expl%02d.tga", 0);
    graph->LoadTexture(fileName, 0, 2, &m_splatTextures[0]);
    for (int i = 0; i < 6; ++i) {
        std::sprintf(fileName, "x_blood%02d.tga", i);
        graph->LoadTexture(fileName, 0, 2, &m_splatTextures[1 + i]);
    }
}

void C_ShotManager::Tick(int timeDelta)
{
    for (int i = 0; i < static_cast<int>(m_shots.size()); ++i) {
        C_Shot *shot = m_shots[i];
        if (!shot->Tick(timeDelta))
            continue;
        switch (shot->GetType()) {
        case SHOT_BULLET:
            RecycleBullet(static_cast<C_BulletShot *>(shot));
            break;
        case SHOT_THROWN:
        case SHOT_PROJECTILE:
        case SHOT_6:
            delete shot;
            break;
        default:
            break;
        }
        m_shots.erase(m_shots.begin() + i);
        --i;
    }
}

void C_ShotManager::Update(int timeDelta)
{
    if (!IsServer()) {
        // Pure clients run the shots in 100 ms ticks of their own (at most one a frame).
        m_clientTickTime += timeDelta;
        if (m_clientTickTime > 100) {
            m_clientTickTime -= 100;
            Tick(100);
        }
        for (int i = 0; i < static_cast<int>(m_clientBullets.size()); ++i) {
            C_BulletShot *bullet = m_clientBullets[i];
            if (!bullet->TickClient(timeDelta))
                continue;
            RecycleBullet(bullet);
            m_clientBullets.erase(m_clientBullets.begin() + i);
            --i;
        }
    }
    for (size_t i = 0; i < m_shots.size(); ++i)
        m_shots[i]->Update(timeDelta);
    for (int i = 0; i < static_cast<int>(m_shells.size()); ++i) {
        C_Shell *shell = m_shells[i];
        if (!shell->Tick(timeDelta))
            continue;
        shell->Hide();
        m_shellPool.Put(shell);
        m_shells.erase(m_shells.begin() + i);
        --i;
    }
    for (int i = 0; i < static_cast<int>(m_muzzleLights.size()); ++i) {
        if (!m_muzzleLights[i]->Tick(timeDelta))
            continue;
        delete m_muzzleLights[i];
        m_muzzleLights.erase(m_muzzleLights.begin() + i);
        --i;
    }
    // (Pools and drops stay until they are reused or the mission ends.)
    for (C_BloodPool *pool : m_bloodPools)
        pool->Tick(timeDelta);
    for (C_BloodDrop *drop : m_bloodDrops)
        drop->Tick(timeDelta);
}

bool C_ShotManager::SaveGame(C_chunk &chunk)
{
    // Only placeholders: an empty chunk per decal, blood drop, blood pool and shot.
    for (size_t i = 0; i < m_decals.size(); ++i) {
        chunk.Begin(CHUNK_DECAL);
        chunk.Ascend();
    }
    for (size_t i = 0; i < m_bloodDrops.size(); ++i) {
        chunk.Begin(CHUNK_BLOOD_DROP);
        chunk.Ascend();
    }
    for (size_t i = 0; i < m_bloodPools.size(); ++i) {
        chunk.Begin(CHUNK_BLOOD_POOL);
        chunk.Ascend();
    }
    for (size_t i = m_shots.size(); i-- > 0;) {
        chunk.Begin(CHUNK_SHOT);
        m_shots[i]->SaveGame(chunk);
        chunk.Ascend();
    }
    return true;
}

bool C_ShotManager::LoadGame(C_chunk &chunk)
{
    // Skips the placeholders. (Other chunks are entered rather than skipped, as in the
    // original; none are written.)
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0;
        if (!chunk.Descend(id))
            break;
        switch (id) {
        case CHUNK_DECAL:
        case CHUNK_BLOOD_DROP:
        case CHUNK_BLOOD_POOL:
        case CHUNK_SHOT:
            chunk.Ascend();
            break;
        default:
            break;
        }
    }
    return true;
}

bool C_ShotManager::OnMessage(C_BitStream *msg)
{
    if (msg->ReadBits(SHOT_MESSAGE_BITS) == SHOT_MESSAGE_EXPLOSION) {
        S_vector pos;
        static_cast<C_NetMessage *>(msg)->ReadPosition(pos);
        const int material = static_cast<int>(msg->ReadBits(8));
        PlayExplosion(pos, S_vector(0.0f, 1.0f, 0.0f), g_messageExplosionEffects[std::rand() % 3], material,
                      MESSAGE_EXPLOSION_SOUND);
    }
    return true;
}

// --- firing ---------------------------------------------------------------------------

void C_ShotManager::FireBullet(C_Actor *shooter, C_Item *weapon, const S_vector &pos, const S_vector &dir, bool tracer,
                               uint32_t flags, int shellSound)
{
    auto *human = static_cast<C_Human *>(shooter);
    auto *gun = static_cast<C_WeaponItem *>(weapon);
    I3D_frame *ejectDummy = nullptr;
    I3D_frame *muzzle = nullptr;
    if (IsClient()) {
        if (human->IsLocalPlayer() && GetGame()->IsFirstPersonView()) {
            // The first-person weapon of the HUD.
            ejectDummy = GetGame()->GetFirstPersonView()->GetCartridgeDummy();
            muzzle = GetGame()->GetFirstPersonView()->GetBlastDummy();
        } else {
            ejectDummy = gun->GetCartridgeDummy();
            muzzle = gun->GetBlastDummy();
        }
    }
    FireBulletFrom(human, gun, pos, dir, muzzle, ejectDummy, tracer, flags, shellSound);
}

void C_ShotManager::FireBulletFrom(C_Human *shooter, C_WeaponItem *weapon, const S_vector &pos, const S_vector &dir,
                                   I3D_frame *muzzle, I3D_frame *ejectDummy, bool tracer, uint32_t flags,
                                   int shellSound)
{
    const bool flare = weapon->GetTypeId() == ITEM_SIGNAL_PISTOL;
    auto *firearm = static_cast<C_FirearmComp *>(weapon->GetDef()->GetComponent());
    const bool fake = (flags & 0xFF) != 0;
    I3D_frame *trace = tracer ? m_traceModel : nullptr;
    I3D_scene *scene = GetScene();
    if (IsLocalGame() || IsServer() || shooter->IsLocalPlayer() || flare) {
        C_BulletShot *bullet = m_bulletPool.Get();
        bullet->Init(shooter, pos, dir, firearm, trace, fake, flare);
        // Shots from vehicles, and own shots in a network game, take their first step at once.
        if (shooter->GetVehicle() || (!IsLocalGame() && shooter->IsLocalPlayer()))
            bullet->Tick(100);
        m_shots.push_back(bullet);
    } else {
        // A remote player's bullet: only shown if the shooter or the shot can be seen.
        if (shooter->GetFrame()->GetRenderStamp() != scene->GetRenderStamp() && !IsSegmentInView(pos, dir))
            return;
        C_BulletShot *bullet = m_bulletPool.Get();
        bullet->InitClient(shooter, pos, dir, firearm, trace, fake, false);
        m_clientBullets.push_back(bullet);
    }
    if (!IsClient())
        return;
    if (ejectDummy)
        EjectShell(shooter, ejectDummy, shellSound);
    if (muzzle) {
        if (shooter->GetFrame()->GetRenderStamp() == scene->GetRenderStamp())
            g_particlesModule.Create(firearm->GetEffect(), muzzle->GetMatrix(), muzzle, true, false);
        auto *light = new C_MuzzleLight;
        light->Init(muzzle, weapon->GetFlashFrame(), 100);
        m_muzzleLights.push_back(light);
    }
}

void C_ShotManager::FireProjectile(C_Actor *shooter, C_Item *weapon, const S_vector &pos, const S_vector &dir,
                                   I3D_frame *projectile, int trailEffect, bool flySound, bool cameraFollow,
                                   uint32_t flags)
{
    auto *shot = new C_ProjectileShot;
    shot->Init(shooter, pos, dir, static_cast<C_ProjectileWeaponComp *>(weapon->GetDef()->GetComponent()), projectile,
               trailEffect, flySound, cameraFollow, (flags & 0xFF) != 0);
    m_shots.push_back(shot);
}

void C_ShotManager::ThrowItem(C_Actor *thrower, C_Item *item, const S_vector &pos, const S_vector &dir, bool fake)
{
    auto *shot = new C_ThrownShot;
    shot->Init(thrower, pos, dir, item, fake);
    m_shots.push_back(shot);
    if (IsLocalGame())
        g_teamManager.GetPlayerTeam()->NotifyGrenadeThrown(pos, dir, thrower);
    static_cast<C_Human *>(thrower)->RemoveItem(item->GetUid(), true);
}

void C_ShotManager::EjectShell(C_Human *shooter, I3D_frame *ejectDummy, int soundGroup)
{
    if (!m_shell3rd || !ejectDummy || !m_shellFpv)
        return;
    if (shooter != g_teamManager.GetPlayerTeam()->GetActivePlayer())
        return;
    // Out along the dummy's up axis, plus the shooter's own movement.
    S_vector velocity = ejectDummy->GetMatrix()(1);
    velocity.x *= (RandFloat() - 0.5f) * 0.5f + 1.5f;
    velocity.y *= (RandFloat() - 0.5f) + 1.5f;
    const float rz = RandFloat();
    velocity.x += shooter->GetVelocity().x;
    velocity.y += shooter->GetVelocity().y;
    velocity.z = ((rz - 0.5f) * 0.5f + 1.5f) * velocity.z + shooter->GetVelocity().z;
    C_Shell *shell = m_shellPool.Get();
    I3D_frame *model = GetGame()->IsFirstPersonView() ? m_shellFpv : m_shell3rd;
    shell->Init(shooter, model, ejectDummy->GetWorldPos(), ejectDummy->GetWorldDir(), velocity, soundGroup);
    m_shells.push_back(shell);
}

// --- effects --------------------------------------------------------------------------

void C_ShotManager::Explode(C_Actor *source, const S_vector &pos, float range, float damage, int soundGroup)
{
    if (range > 0.0f && damage > 0.0f) {
        S_Explosion explosion;
        explosion.pos = pos;
        explosion.range = range;
        explosion.damage = damage;
        explosion.unknown18 = false;
        explosion.source = source;
        explosion.Execute();
    }
    if (IsLocalGame()) {
        // Only for the AI's ears; the explosion itself is played by PlayExplosion.
        m_sound.chapter = SOUND_CHAPTER_EXPLOSION;
        m_sound.pos = pos;
        m_sound.attachTo = nullptr;
        m_sound.group = soundGroup;
        m_sound.source = source;
        m_sound.flags = SOUND_AI_ONLY | SOUND_AI_OCCLUSION;
        GetSoundModule()->Play(m_sound);
    }
}

void C_ShotManager::PlayExplosion(const S_vector &pos, const S_vector &dir, int effect, int material, int soundGroup)
{
    m_sound.chapter = SOUND_CHAPTER_EXPLOSION;
    m_sound.pos = pos;
    m_sound.flags = SOUND_SILENT_TO_AI;
    m_sound.group = soundGroup;
    m_sound.source = nullptr;
    m_sound.attachTo = nullptr;
    GetSoundModule()->Play(m_sound);
    PlayExplosionEffects(material, effect, pos, dir);
}

void C_ShotManager::PlayExplosionEffects(int material, int effect, const S_vector &pos, const S_vector &dir)
{
    if (material >= 0)
        AddExplosionDecal(material, pos, -dir);
    g_particlesModule.CreateAt(effect, pos, dir, nullptr, true, false);
    if (material >= 0)
        g_particlesModule.CreateAt(g_materials[material].explosionEffect, pos, dir, nullptr, true, false);
}

void C_ShotManager::PlayHitSound(C_Actor *source, int material, const S_vector &pos, bool alternate,
                                 bool silentToAI)
{
    if (!IsClient() || material == MATERIAL_NONE || static_cast<uint32_t>(material) >= g_materialCount)
        return;
    m_sound.chapter = SOUND_CHAPTER_BULLET_HIT;
    m_sound.pos = pos;
    m_sound.group = alternate ? g_materials[material].hitSoundAlternate : g_materials[material].hitSound;
    if (m_sound.group == -1)
        return;
    m_sound.source = source;
    m_sound.flags = silentToAI ? SOUND_SILENT_TO_AI : 0;
    m_sound.attachTo = nullptr;
    m_sound.channel = 1;
    GetSoundModule()->Play(m_sound);
}

void C_ShotManager::OnBulletHit(C_Actor *, I3D_frame *frame, int material, const S_vector &pos,
                                const S_vector &normal)
{
    if (material == MATERIAL_NONE)
        return;
    AddBulletDecal(material, pos, normal, frame);
    PlayHitEffect(material, pos, normal);
}

void C_ShotManager::PlayHitEffect(int material, const S_vector &pos, const S_vector &normal)
{
    C_CameraModule *cameras = GetGame()->GetCameraModule();
    S_vector4 cameraDir, cameraPos;
    cameras->GetCameraDir(0, &cameraDir);
    cameras->GetCameraPos(0, &cameraPos);
    S_vector toHit = pos - S_vector(cameraPos.x, cameraPos.y, cameraPos.z);
    toHit.Normalize();
    if (toHit.Dot(S_vector(cameraDir.x, cameraDir.y, cameraDir.z)) > -0.05f)
        g_particlesModule.CreateAt(g_materials[material].hitEffect, pos, normal, nullptr, true, false);
}

void C_ShotManager::PlayNearMissSound(const S_vector &pos)
{
    if (!IsClient())
        return;
    m_sound.chapter = SOUND_CHAPTER_BULLET_HIT;
    m_sound.pos = pos;
    m_sound.group = SOUND_GROUP_NEAR_MISS;
    m_sound.flags = SOUND_SILENT_TO_AI;
    m_sound.source = nullptr;
    m_sound.attachTo = nullptr;
    m_sound.channel = 0;
    GetSoundModule()->Play(m_sound);
}

// --- decals and blood -------------------------------------------------------------------

S_ShotDecal *C_ShotManager::AllocDecal(bool setUnknownA4)
{
    if (static_cast<int>(m_decals.size()) == m_maxDecals) {
        S_ShotDecal *oldest = m_decals.front();
        m_decals.erase(m_decals.begin());
        return oldest;
    }
    auto *decal = new S_ShotDecal;
    decal->visual = static_cast<I3D_projector_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_PROJECTOR));
    decal->visual->SetMode(PROJECTOR_MODE_DECAL);
    decal->visual->SetUnknownB4(1);
    if (setUnknownA4)
        decal->visual->SetUnknownA4(1);
    return decal;
}

void C_ShotManager::AddBulletDecal(int material, const S_vector &pos, const S_vector &normal, I3D_frame *frame)
{
    if (!DecalsEnabled())
        return;
    const int texture = g_materials[material].decal;
    if (texture < 0 || static_cast<uint32_t>(material) >= g_materialCount || texture >= 9)
        return;
    if (frame) {
        // Only on static object visuals, and not on parts of skinned models.
        const uint32_t type = frame->GetVisualType();
        if (type != I3DVISUAL_OBJECT && type != I3DVISUAL_LMAP && type != I3DVISUAL_MIRROR)
            return;
        if (static_cast<I3D_visual *>(frame)->VisualFlags() & 0x3000)
            return;
        for (I3D_frame *parent = frame->GetParent(); parent; parent = parent->GetParent()) {
            const uint32_t parentType = parent->GetVisualType();
            if (parentType == I3DVISUAL_SINGLEMESH || parentType == I3DVISUAL_SHD ||
                parentType == I3DVISUAL_SINGLEMORPH)
                return;
        }
    }
    if (!m_decalTextures[texture])
        return;

    S_ShotDecal *decal = AllocDecal(true);
    I3D_projector_visual *visual = decal->visual;
    S_vector n = normal;
    n.Normalize();
    visual->SetDir(-n, (RandFloat() - 0.5f) * PI);
    const float size = RandFloat() * 0.01f + 0.015f;
    visual->SetScale(S_vector(size, size, 0.03f));
    visual->SetPos(n * 0.01f + pos);
    visual->SetTexture(m_decalTextures[texture]);
    const float light = GetScene()->GetLightIntensity(visual->GetWorldPos(), nullptr, 1, 0);
    if (light < 0.4f)
        visual->SetColor(visual->GetBaseColor() * 0.4f);
    else if (light < 1.0f)
        visual->SetColor(visual->GetBaseColor() * light);
    else
        visual->SetColor(S_vector(1.0f, 1.0f, 1.0f));
    if (frame) {
        visual->LinkTo(frame, I3DLINK_KEEP_WORLD);
        decal->parentName = frame->GetName();
    } else {
        decal->parentName = "No Parent";
        visual->LinkTo(GetScene()->GetSector(pos), I3DLINK_KEEP_WORLD);
    }
    visual->SetUnknownA4(1);
    visual->Update();
    m_decals.push_back(decal);
}

void C_ShotManager::AddExplosionDecal(int material, const S_vector &pos, const S_vector &dir)
{
    if (!DecalsEnabled())
        return;
    S_vector hitPos, normal;
    if (!CollisionRayTest(pos - dir * 0.3f, dir * 0.5f, &hitPos, &normal, 0xFFFFFFFF, 0xB90000))
        return;
    IImage *texture = m_splatTextures[g_materials[material].explosionDecal];
    if (!texture)
        return;
    S_ShotDecal *decal = AllocDecal(true);
    I3D_projector_visual *visual = decal->visual;
    const float size = (RandFloat() + 1.0f) * 0.5f;
    visual->SetDir(-normal, (RandFloat() - 0.5f) * TWO_PI);
    visual->SetScale(S_vector(size, size, 0.1f));
    visual->SetPos(normal * 0.05f + pos);
    visual->SetTexture(texture);
    const float light = GetScene()->GetLightIntensity(visual->GetWorldPos(), nullptr, 0, 0);
    visual->SetColor(visual->GetBaseColor() * light);
    visual->LinkTo(GetScene()->GetSector(pos), I3DLINK_KEEP_WORLD);
    visual->Update();
    m_decals.push_back(decal);
}

void C_ShotManager::AddBloodSplat(const S_vector &pos, const S_vector &dir)
{
    if (!DecalsEnabled())
        return;
    IImage *texture = m_splatTextures[1 + 4];
    if (!texture)
        return;
    S_vector reach(dir.x, 0.0f, dir.z);
    reach.Normalize();
    reach *= 3.0f;
    S_vector hitPos, normal;
    if (!CollisionRayTest(pos, reach, &hitPos, &normal, 0xFFFFFFFF, 0))
        return;
    S_ShotDecal *decal = AllocDecal(false);
    I3D_projector_visual *visual = decal->visual;
    const float size = RandFloat() * 0.6f + 0.3f;
    visual->SetDir(-normal, (RandFloat() - 0.5f) * TWO_PI);
    visual->SetScale(S_vector(size, size, 0.2f));
    visual->SetPos(normal * 0.01f + hitPos);
    visual->SetTexture(texture);
    const float light = GetScene()->GetLightIntensity(visual->GetWorldPos(), nullptr, 0, 0);
    visual->SetColor(visual->GetBaseColor() * light);
    visual->LinkTo(GetScene()->GetSector(hitPos), I3DLINK_KEEP_WORLD);
    visual->Update();
    m_decals.push_back(decal);
}

void C_ShotManager::AddBloodPool(const S_vector &pos, const S_vector &normal, float maxSize)
{
    if (!DecalsEnabled())
        return;
    C_BloodPool *pool;
    if (m_bloodPools.size() < static_cast<uint32_t>(m_maxBloodPools)) {
        pool = new C_BloodPool;
        pool->Init(pos, normal, m_splatTextures[1 + 5], maxSize);
    } else {
        pool = m_bloodPools.front();                        // the oldest one moves
        m_bloodPools.erase(m_bloodPools.begin());
        pool->Reuse(pos, normal, maxSize);
    }
    m_bloodPools.push_back(pool);
}

void C_ShotManager::SprayBlood(const S_vector &pos, const S_vector &dir)
{
    AddBloodDrop(pos, dir, m_splatTextures[1 + 0]);
}

void C_ShotManager::SprayBloodRandom(const S_vector &pos, const S_vector &dir)
{
    AddBloodDrop(pos, dir, m_splatTextures[1 + 1 + std::rand() % 3]);
}

void C_ShotManager::AddBloodDrop(const S_vector &pos, const S_vector &dir, IImage *texture)
{
    if (!DecalsEnabled() || !texture)
        return;
    C_BloodDrop *drop;
    if (m_bloodDrops.size() < static_cast<uint32_t>(m_maxBloodDrops)) {
        drop = new C_BloodDrop;
        drop->Init(pos, dir, texture);
    } else {
        drop = m_bloodDrops.front();
        m_bloodDrops.erase(m_bloodDrops.begin());
        drop->Reuse(pos, dir, texture);
    }
    m_bloodDrops.push_back(drop);
}

// ============================================================================ melee and messages

void MeleeAttack(C_Actor *attacker, C_Item *weapon, bool backstab, const S_vector &pos, const S_vector &dir)
{
    auto *human = static_cast<C_Human *>(attacker);
    S_vector hitPos, normal;
    if (backstab) {
        // Half a metre ahead, horizontally; kills a human who faces away.
        S_vector reach(dir.x, 0.0f, dir.z);
        reach.Normalize();
        reach *= 0.5f;
        human->RemoveCollision(false);
        S_CollisionVolume *volume = g_collisionWorld.RayTestDynamic(pos, reach, &hitPos, &normal, 0, 0);
        human->RestoreCollision(false);
        if (!volume || !volume->actor || volume->actor->GetType() != ACTOR_HUMAN)
            return;
        auto *target = static_cast<C_Human *>(volume->actor);
        if (target->GetFacingDir().Dot(dir) <= 0.8f)
            return;
        GetGame()->Print("undercut him!!!");
        target->Hit(HIT_BACKSTAB, -1.0f, hitPos, reach, attacker, 0);
        return;
    }

    const float damage = static_cast<C_WeaponComp *>(weapon->GetDef()->GetComponent())->GetDamage();
    const S_vector from = pos;
    const S_vector reach = dir * 1.2f;
    human->RemoveCollision(false);
    S_CollisionVolume *volume = g_collisionWorld.RayTest(from, reach, &hitPos, &normal, 1, 0);
    human->RestoreCollision(false);
    if (!volume)
        return;

    int material = MATERIAL_NONE;
    if (!(volume->flags & COLLISION_FLAG_DYNAMIC) || !(volume->flags & 0xC0) || !volume->frame) {
        material = GetVolumeMaterial(*volume);
    } else {
        C_Actor *actor = volume->actor;
        if (!actor)
            return;
        C_Human *target = nullptr;
        int zone = HIT_ZONE_NONE;
        if (actor->GetType() == ACTOR_HUMAN) {
            target = static_cast<C_Human *>(actor);
            zone = g_hitZones.GetHitZone(target, from, reach);
            g_hitZones.Print(zone);
        } else if (actor->GetType() == ACTOR_MELEE_TRACKER) {
            material = actor->GetMaterialId(nullptr);
            actor->Hit(HIT_MELEE, damage, hitPos, reach, attacker, 0);
        } else if (actor->GetType() == ACTOR_STATIC_WEAPON) {
            // The gunner behind it.
            target = static_cast<C_Human *>(actor->GetSeatOccupant(0));
            if (!target)
                return;
            zone = g_hitZones.GetHitZone(target, from, reach);
        } else {
            I3D_collision_info info;
            I3D_frame *part = GetScene()->IntersectFrame(from, reach, actor->GetFrame(), &info, 0, 0, 0, 0);
            if (!part)
                return;
            material = actor->GetMaterialId(part);
        }
        if (target) {
            if (zone == HIT_ZONE_NONE) {
                target->NotifyShotAt(attacker, reach);
                return;
            }
            material = target->GetMaterialId(nullptr);
            target->Hit(HIT_MELEE, damage, hitPos, reach, attacker, zone);
        }
    }
    if (material != MATERIAL_NONE && g_materials[material].hitEffect != 0)
        g_particlesModule.CreateAt(g_materials[material].hitEffect, hitPos, normal, nullptr, true, false);
}

void SendExplosionMessage(const S_vector &pos, int material)
{
    S_Explosion explosion;
    explosion.pos = pos;
    explosion.range = 5.0f;
    explosion.damage = 1000.0f;
    explosion.unknown18 = false;
    explosion.source = nullptr;
    explosion.Execute();

    // CreateShotMessage (0x00452ca0) and SendShotMessage (0x00452d00), inlined.
    C_BitStream *msg = g_netGame.CreateMessage(MODULE_SHOTS, NET_DELIVERY_RELIABLE);
    msg->WriteBits(SHOT_MESSAGE_EXPLOSION, SHOT_MESSAGE_BITS);
    static_cast<C_NetMessage *>(msg)->WritePosition(pos);
    msg->WriteBits(static_cast<uint32_t>(material), 8);
    g_netGame.SendNetMessage(msg);
}
