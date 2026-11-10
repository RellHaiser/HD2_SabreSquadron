// S_CarParts: the car's damage parts (original 0x004e08c0 S_CarParts, 0x004e0930 Setup, 0x004e09d0
// Gather, 0x004e0dc0 Clear, 0x004e0ea0 Reset, 0x004e0ee0 Release, 0x004e0f70 SaveGame, 0x004e1230
// LoadGame, 0x004e1990 Tick, 0x004e19c0 GetFrameInt, 0x004e1fe0 WreckOnce; the zones' vector
// 0x004e2240 / 0x004e2270 / 0x004e2600; the hits: 0x004e1dc0 Place, 0x004e1530 GetHitMaterial,
// 0x004e1820 ExplosiveAt, 0x004e18c0 HitSeats, 0x004e1a20 Hit; the effects: 0x004e1f40
// SendPartEffect, 0x004e1f70 StartPartEffect, 0x004e2000 StartWreckEffects). Not ported (no
// callers, no data references; tier 1): 0x004e1680, an older zone hit (it shows the seats' boxes
// by the zones' count).
#include "game/car.h"

#include <malloc.h>                                     // _aligned_malloc, _aligned_free

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/game.h"                                  // GetGame
#include "game/particles.h"                             // g_particlesModule
#include "game/resource_cache.h"                        // g_modelCache
#include "game/shots.h"                                 // g_materials
#include "game/sound_module.h"
#include "game/sound_table.h"                           // SOUND_CHAPTER_EXPLOSION
#include "ls3d/i3d.h"

namespace {

// Saved-game chunks of the damage parts (inside the car's CHUNK_CAR_PARTS).
enum : uint16_t {
    CHUNK_PARTS_EXPLOSIVE = 10000,      // an explosive part's index, started (1 byte), fuse, effect
    CHUNK_PARTS_ZONE = 0x2711,          // a zone's index, HP
    CHUNK_PARTS_STATE = 0x2712,         // wrecked, the fuse
};

// The wreck's effects without the "particle" dummy's.
constexpr int DEFAULT_FIRE_EFFECT = 0x60;
constexpr int DEFAULT_SMOKE_EFFECT = 0x5F;

// The material of a seat's hit box (flesh, as the humans' bodies).
constexpr int FLESH_MATERIAL = 0x42;

// The frame the ray from `from` along `dir` meets of `frame`'s (null none).
I3D_frame *RayTest(const S_vector &from, const S_vector &dir, I3D_frame *frame)
{
    I3D_collision_info info;
    return GetGame()->GetScene()->IntersectFrame(from, dir, frame, &info, I3DCOL_VISUAL_GEOMETRY, 0, 0, 0);
}

}  // namespace

S_CarParts::S_CarParts() = default;                     // 0x004e08c0 (the member defaults)

void S_CarParts::Setup(C_Car *owner)                    // 0x004e0930
{
    car = owner;
    model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (!model)
        return;
    const std::string name = std::string("DM") + static_cast<I3D_model *>(car->GetFrame())->GetFileName();
    if (g_modelCache.OpenInto(model, name.c_str()) != I3D_OK) {
        model->Release();
        model = nullptr;
        return;
    }
    Gather();
    model->SetOn(false);
}

void S_CarParts::Gather()                               // 0x004e09d0 (__fastcall)
{
    // Fix: nothing without the damage model (the original reads its frame list through null: a car
    // without one crashed when its mission started).
    if (!model)
        return;
    // The seats' hit boxes: "SEAT<nn>" visuals.
    for (int seat = 0; seat < car->GetSeatCount(); ++seat) {
        char name[32];
        std::sprintf(name, "SEAT%02d", seat);
        seatFrames.push_back(model->FindChildFrame(name, FRAME_MASK_VISUAL));
    }
    // The model's frames, from the last.
    for (int i = model->NumModelFrames(); i-- > 0;) {
        I3D_frame *frame = model->GetModelFrame(i);
        const char *name = frame->GetShortName();
        if (std::strstr(name, "HOR") || std::strstr(name, "VERT")) {
            turretFrames.push_back(frame);
        } else if (std::strstr(name, "D_ZONE")) {
            zones.push_back({frame, GetFrameInt(frame, "HP"), GetFrameInt(frame, "matid")});
        } else if (std::strstr(name, "EXPL_D")) {
            auto *part = static_cast<S_CarExplosive *>(_aligned_malloc(sizeof(S_CarExplosive), 16));
            part->frame = frame;
            part->fuse = GetFrameInt(frame, "EXPL") * 1000;
            // Fix: no effect yet (the original leaves the allocation's garbage, which Clear and the
            // wreck's effects destroy when it is not negative).
            part->effect = -1;
            part->effectId = GetFrameInt(frame, "PARTID");
            part->started = 0;
            explosives.push_back(part);
        }
    }
    // The wreck's effects.
    particleFrame = model->FindChildFrame("particle", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    wreckSound = GetFrameInt(particleFrame, "SID");
    fireEffect = GetFrameInt(particleFrame, "PFIRE");
    smokeEffect = GetFrameInt(particleFrame, "PSMOKE");
    if (fireEffect == -1)
        fireEffect = DEFAULT_FIRE_EFFECT;
    if (smokeEffect == -1)
        smokeEffect = DEFAULT_SMOKE_EFFECT;
}

void S_CarParts::Clear()                                // 0x004e0dc0 (__fastcall)
{
    turretFrames.clear();
    zones.clear();
    seatFrames.clear();
    while (!explosives.empty()) {
        S_CarExplosive *part = explosives.back();
#ifndef HD2_DEDICATED   // no particles on the server
        if (part->effect >= 0)
            g_particlesModule.Destroy(part->effect);
#endif
        _aligned_free(part);
        explosives.pop_back();
    }
    particleFrame = nullptr;
}

void S_CarParts::Reset()                                // 0x004e0ea0 (__fastcall)
{
    Clear();
    Gather();
    fuse = 0;
    wrecked = false;
#ifndef HD2_DEDICATED   // no particles on the server
    if (fireHandle != -1)
        g_particlesModule.Destroy(fireHandle);
    if (smokeHandle != -1)
        g_particlesModule.Destroy(smokeHandle);
#endif
}

void S_CarParts::Release()                              // 0x004e0ee0
{
    if (model)
        model->Release();
    turretFrames.clear();
    while (!explosives.empty()) {
        _aligned_free(explosives.back());
        explosives.pop_back();
    }
    model = nullptr;
}

// --- the saved game ------------------------------------------------------------------------------------

bool S_CarParts::SaveGame(C_chunk &chunk)               // 0x004e0f70
{
    for (int i = static_cast<int>(explosives.size()); i-- > 0;) {
        const S_CarExplosive *part = explosives[i];
        chunk.Begin(CHUNK_PARTS_EXPLOSIVE);
        chunk.Write(i);
        chunk.Write(&part->started, 1);
        chunk.Write(part->fuse);
        chunk.Write(part->effect);
        chunk.Ascend();
    }
    for (int i = static_cast<int>(zones.size()); i-- > 0;) {
        chunk.Begin(CHUNK_PARTS_ZONE);
        chunk.Write(i);
        chunk.Write(zones[i].hp);
        chunk.Ascend();
    }
    chunk.Begin(CHUNK_PARTS_STATE);
    chunk.Write(wrecked);
    chunk.Write(fuse);
    chunk.Ascend();
    return true;
}

bool S_CarParts::LoadGame(C_chunk &chunk)               // 0x004e1230
{
    int index = INT_MAX;                                // (the original's: the chunk's address)
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_PARTS_EXPLOSIVE:
            chunk.Read(index);
            if (index >= 0 && index < static_cast<int>(explosives.size())) {
                S_CarExplosive *part = explosives[index];
                chunk.Read(&part->started, 1);
                chunk.Read(part->fuse);
                chunk.Read(part->effect);
                chunk.Ascend();
            }
            break;
        case CHUNK_PARTS_ZONE:
            chunk.Read(index);
            if (index >= 0 && index < static_cast<int>(zones.size())) {
                chunk.Read(zones[index].hp);
                chunk.Ascend();
            }
            break;
        case CHUNK_PARTS_STATE:
            chunk.Read(wrecked);
            chunk.Read(fuse);
            chunk.Ascend();
            break;
        default:
            break;
        }
    }
    return true;
}

// --- the fuse ------------------------------------------------------------------------------------------

void S_CarParts::Tick(int time)                         // 0x004e1990
{
    if (wrecked || fuse == 0)
        return;
    fuse -= time;
    if (fuse < 1) {
        fuse = 0;
        WreckOnce();
    }
}

void S_CarParts::WreckOnce()                            // 0x004e1fe0 (__fastcall)
{
    if (wrecked)
        return;
    car->Wreck(false);
    wrecked = true;
}

int S_CarParts::GetFrameInt(const I3D_frame *frame, const char *key)   // 0x004e19c0
{
    if (!frame)
        return -1;
    const char *at = std::strstr(frame->GetUserProps(), key);
    return at ? std::atoi(at + std::strlen(key)) : -1;
}

// --- the hits ------------------------------------------------------------------------------------------

void S_CarParts::Place()                                // 0x004e1dc0 (__fastcall)
{
    if (!model)
        return;
    I3D_frame *carFrame = car->GetFrame();
    S_quat rot;
    rot.Make(carFrame->GetMatrix());
    model->SetPosRot(carFrame->GetMatrix()(3), rot);
    model->LinkTo(GetGame()->GetScene()->GetPrimarySector(), 0);
    for (size_t i = turretFrames.size(); i-- > 0;) {
        I3D_frame *turret = turretFrames[i];
        I3D_frame *own = carFrame->FindChildFrame(turret->GetShortName(), FRAME_MASK_ANY);
        if (!own)
            continue;
        turret->SetRot(own->GetRot());
        turret->Update();
    }
    model->Update();
}

void S_CarParts::HideExplosives()                       // (inline in Hit and GetHitMaterial)
{
    for (size_t i = explosives.size(); i-- > 0;)
        explosives[i]->frame->SetOn(false);
}

void S_CarParts::ShowOccupiedSeats()                    // (inline in Hit and GetHitMaterial)
{
    for (size_t i = seatFrames.size(); i-- > 0;) {
        if (I3D_frame *box = seatFrames[i])
            box->SetOn(car->GetSeatOccupant(static_cast<int>(i)) != nullptr);
    }
}

int S_CarParts::GetHitMaterial(const S_vector &pos, const S_vector &dir)   // 0x004e1530
{
    if (!model)
        return -1;
    HideExplosives();
    Place();
    ShowOccupiedSeats();
    model->SetOn(true);
    I3D_frame *hit = RayTest(pos - dir, dir, model);
    model->SetOn(false);
    if (!hit)
        return -1;
    for (size_t i = seatFrames.size(); i-- > 0;) {
        if (hit == seatFrames[i])
            return FLESH_MATERIAL;
    }
    return GetFrameInt(hit, "matid");
}

int S_CarParts::ExplosiveAt(const S_vector &from, const S_vector &dir)   // 0x004e1820
{
    model->SetOn(true);
    for (int i = static_cast<int>(explosives.size()); i-- > 0;) {
        I3D_frame *frame = explosives[i]->frame;
        frame->SetOn(true);
        if (RayTest(from, dir, frame) == frame)
            return i;
    }
    model->SetOn(false);
    return -1;
}

void S_CarParts::HitSeats(const S_vector &from, const S_vector &dir, float amount, C_Actor *attacker)   // 0x004e18c0
{
    model->SetOn(true);
    for (int i = static_cast<int>(seatFrames.size()); i-- > 0;) {
        I3D_frame *box = seatFrames[i];
        if (!box)
            continue;
        C_Actor *occupant = car->GetSeatOccupant(i);
        if (!occupant)
            continue;
        box->SetOn(true);
        if (RayTest(from, dir, box) == box)
            occupant->Hit(HIT_BULLET, amount, from, dir, attacker, 0);
    }
    model->SetOn(false);
}

void S_CarParts::Hit(int type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker)   // 0x004e1a20
{
    if (wrecked || !model)
        return;
    if (type != HIT_SCRIPT_KILL && type != HIT_BULLET && type != HIT_PROJECTILE && type != HIT_EXPLOSION)
        return;
    Place();
    const S_vector from = pos - dir;
    HideExplosives();
    ShowOccupiedSeats();
    I3D_frame *hit = nullptr;
    if (type == HIT_EXPLOSION) {
        float nearest = 1e6f;
        for (size_t i = zones.size(); i-- > 0;) {
            const S_vector d = zones[i].frame->GetWorldPos() - pos;
            const float square = (d.z * d.z + d.y * d.y) + d.x * d.x;
            if (!(square > nearest)) {
                nearest = square;
                hit = zones[i].frame;
            }
        }
    } else {
        model->SetOn(true);
        hit = RayTest(from - dir, dir, model);
        model->SetOn(false);
    }
    if (!hit)
        return;
    for (size_t i = seatFrames.size(); i-- > 0;) {
        if (hit != seatFrames[i])
            continue;
        if (C_Actor *occupant = car->GetSeatOccupant(static_cast<int>(i)))
            occupant->Hit(HIT_BULLET, amount, pos, dir, attacker, 0);
        return;
    }
    S_CarZone *zone = nullptr;
    for (size_t i = zones.size(); i-- > 0 && !zone;) {
        if (hit == zones[i].frame)
            zone = &zones[i];
    }
    if (!zone) {
        HitSeats(from, dir, amount, attacker);
        return;
    }
    // Fix: a zone's material outside the table (-1: none) absorbs nothing (the original reads past
    // the table).
    const bool known = zone->material >= 0 && static_cast<uint32_t>(zone->material) < g_materialCount;
    const float through = amount - (known ? static_cast<float>(g_materials[zone->material].absorption) : 0.0f);
    if (through <= 0.0f)
        return;
    zone->hp = static_cast<int>(static_cast<float>(zone->hp) - through);
    if (zone->hp >= 0)
        return;
    zone->hp = 0;
    HitSeats(from, dir, through, attacker);
    if (type == HIT_BULLET) {
        const int index = ExplosiveAt(from, dir);
        if (index < 0)
            return;
        S_CarExplosive *part = explosives[index];
        if (part->started)
            return;
        if (part->fuse != 0) {
            if (fuse == 0 || fuse > part->fuse)
                fuse = part->fuse;
            part->started = 1;
            SendPartEffect(index);
            return;
        }
    }
    WreckOnce();
}

// --- the effects ---------------------------------------------------------------------------------------

void S_CarParts::SendPartEffect(int part)               // 0x004e1f40
{
    C_CarPartEffectMessage msg;
    msg.part = part;
    car->SendMessage(msg);
}

void S_CarParts::StartPartEffect(int part)              // 0x004e1f70
{
    if (part < 0 || part >= static_cast<int>(explosives.size()))
        return;
    S_CarExplosive *explosive = explosives[part];
    if (explosive->effectId == -1)
        return;
    Place();
#ifdef HD2_DEDICATED   // the server creates no particles (the id is -1); it still reads the matrix
    (void)explosive->frame->GetMatrix();
    explosive->effect = -1;
#else
    explosive->effect = g_particlesModule.Create(explosive->effectId, explosive->frame->GetMatrix(), car->GetFrame(), true,
                                                 false);
#endif
}

void S_CarParts::StartWreckEffects()                    // 0x004e2000 (__fastcall)
{
    for (size_t i = explosives.size(); i-- > 0;) {
        S_CarExplosive *part = explosives[i];
        if (part->effect >= 0) {
#ifndef HD2_DEDICATED   // no particles on the server
            g_particlesModule.Destroy(part->effect);
#endif
            part->effect = -1;
        }
    }
    Place();
    I3D_frame *carFrame = car->GetFrame();
    I3D_frame *at = particleFrame ? particleFrame : carFrame;
#ifdef HD2_DEDICATED   // the server creates no particles (the ids are -1); it still reads the matrix twice
    (void)at->GetMatrix();
    fireHandle = -1;
    (void)at->GetMatrix();
    smokeHandle = -1;
#else
    fireHandle = g_particlesModule.Create(fireEffect, at->GetMatrix(), carFrame, true, false);
    smokeHandle = g_particlesModule.Create(smokeEffect, at->GetMatrix(), carFrame, true, false);
#endif
    if (wreckSound < 1)
        return;
    S_SoundRequest request;
    request.flags = SOUND_SILENT_TO_AI;
    request.attachTo = at;
    request.chapter = SOUND_CHAPTER_EXPLOSION;
    request.group = wreckSound;                         // (the original's posW is stack garbage)
    GetSoundModule()->Play(request);
}
