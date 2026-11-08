// The vehicles' particle effects and their emitter pool (original 0x004d5a50 InitCarEffects, the
// car's emitter callbacks 0x004d5b10 / 0x004d62c0, the pool 0x004ed0c0 / 0x004ed130; the pool's
// append 0x004ecff0 is the vector's push_back here).
#include "game/car_effects.h"

#include <cstring>

#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

S_CarEffectDef *g_carEffects;                           // (LoadMaterialTables fills them)
uint32_t g_carEffectCount;
std::vector<S_CarEmitter> g_carEmitters;

namespace {

// A property's value: a float's bits, a pointer.
uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
uint32_t Bits(const void *pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

// The effect's properties on the emitter's visual, its velocities kept in the emitter (`all`: also
// property 0x22).
void SetProperties(S_CarEmitter &emitter, const S_CarEffectDef &def, bool all)
{
    I3D_particle_visual *visual = emitter.visual;
    visual->SetRenderState(0x14, 0);
    visual->SetRenderState(0x13, def.flags & 0x107);
    visual->SetRenderState(0, def.unknown70);
    visual->SetRenderState(1, def.unknown74);
    visual->SetRenderState(2, def.unknown78);
    visual->SetRenderState(3, def.unknown7C);
    visual->SetRenderState(4, Bits(def.unknown80));
    visual->SetRenderState(5, Bits(def.unknown84));
    visual->SetRenderState(0x1A, Bits(def.unknownD4));
    visual->SetRenderState(0xB, Bits(def.unknownA4));
    visual->SetRenderState(0xC, Bits(def.unknownA8));
    if (all)
        visual->SetRenderState(0x22, Bits(def.unknownAC));
    visual->SetRenderState(6, Bits(def.unknown88));
    visual->SetRenderState(7, static_cast<uint32_t>(static_cast<int>(def.unknown8C)));
    visual->SetRenderState(0xD, Bits(def.velocity.x));
    visual->SetRenderState(0xE, Bits(def.velocity.y));
    visual->SetRenderState(0xF, Bits(def.velocity.z));
    visual->SetRenderState(0x15, 1);
    emitter.velocity = {def.velocity.x, def.velocity.y, def.velocity.z, 0.0f};
    visual->SetRenderState(0x10, Bits(def.velocity2.x));
    visual->SetRenderState(0x11, Bits(def.velocity2.y));
    visual->SetRenderState(0x12, Bits(def.velocity2.z));
    visual->SetRenderState(0x16, 1);
    emitter.velocity2 = {def.velocity2.x, def.velocity2.y, def.velocity2.z, 0.0f};
    visual->SetRenderState(0x18, Bits(def.unknownC8));
    visual->SetRenderState(0x19, Bits(def.unknownCC));
    visual->SetRenderState(8, def.unknown90);
    visual->SetRenderState(9, def.unknown94);
    visual->SetRenderState(0xA, Bits(def.unknownA0));
    visual->SetRenderState(0x1D, Bits(def.color1));
    visual->SetRenderState(0x1E, Bits(def.color2));
}

// The visual's keys of the effect's texture rectangle selected.
void SelectKeys(I3D_particle_visual *visual, const S_CarEffectDef &def)
{
    for (int k = visual->NumKeys(); k-- > 0;) {
        const float *rect = visual->GetKeyRect(k);
        if (def.rect[0] == rect[0] && def.rect[1] == rect[1] && def.rect[2] == rect[2] && def.rect[3] == rect[3])
            visual->SelectKey(k);
    }
}

}  // namespace

// 30 emitters, each on the first effect from 1 on that sets up (one empty emitter without
// effects: false).
bool InitCarEffects()                                                          // 0x004d5a50
{
    FreeCarEffectEmitters();
    if (g_carEffectCount < 1) {
        S_CarEmitter none = {};
        none.effect = -1;
        g_carEmitters.push_back(none);
        return false;
    }
    for (int left = 30; left-- > 0;) {
        S_CarEmitter emitter = {};
        emitter.visual = static_cast<I3D_particle_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_EMITTER));
        uint32_t effect = 1;
        while (effect < g_carEffectCount && !SetupCarEmitter(&emitter, effect))
            ++effect;
        if (effect >= g_carEffectCount) {
            emitter.visual->Release();
            return true;
        }
        emitter.active = false;
        emitter.stopping = false;
        g_carEmitters.push_back(emitter);
    }
    return true;
}

void FreeCarEffectEmitters()                                                   // 0x004ed0c0
{
    for (size_t i = g_carEmitters.size(); i-- > 0;) {
        if (I3D_particle_visual *visual = g_carEmitters[i].visual) {
            visual->LinkTo(nullptr, 0);
            visual->Release();
        }
    }
    g_carEmitters.clear();
    g_carEmitters.shrink_to_fit();
}

// The taken emitters run; a let-go one stops emitting and is free (unlinked) once its particles
// are gone.
void UpdateCarEffectEmitters(int timeDelta)                                    // 0x004ed130
{
    for (size_t i = g_carEmitters.size(); i-- > 0;) {
        S_CarEmitter &emitter = g_carEmitters[i];
        if (!emitter.active)
            continue;
        emitter.visual->Tick(timeDelta);
        if (!emitter.stopping)
            continue;
        emitter.visual->SetEmitting(false);
        if (emitter.visual->NumParticles() < 1) {
            emitter.stopping = false;
            emitter.active = false;
            emitter.visual->LinkTo(nullptr, 0);
        }
    }
}

// The effect's texture (with its alpha file when it names one) on the emitter, unlinked, at scale
// 1 facing +z, its properties, a key for every effect of the same texture (its rectangle), the
// effect's own selected.
bool __fastcall SetupCarEmitter(S_CarEmitter *emitter, uint32_t effect)        // 0x004d5b10
{
    if (effect >= g_carEffectCount)
        return false;
    const S_CarEffectDef &def = g_carEffects[effect];
    const bool alpha = std::strlen(def.alphaTexture) >= 4;
    IImage *image;
    if (GetIGraph()->LoadTexture(def.texture, alpha ? def.alphaTexture : nullptr, alpha ? 4 : 2, &image) != 0)
        return false;
    emitter->effect = static_cast<int>(effect);
    I3D_particle_visual *visual = emitter->visual;
    visual->SetTexture(image);
    visual->SetName("Car_particle");
    visual->LinkTo(nullptr, 0);
    visual->SetScale(S_vector(1.0f, 1.0f, 1.0f));
    visual->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    visual->Update();
    visual->SetDefaults();
    SetProperties(*emitter, def, true);
    for (int k = visual->NumKeys(); k-- > 0;)
        visual->RemoveKey(k);
    for (uint32_t i = g_carEffectCount; i-- > 0;) {
        const S_CarEffectDef &other = g_carEffects[i];
        if (std::strcmp(def.texture, other.texture) != 0)
            continue;
        I3D_emitter_key key = {};
        std::memcpy(key.rect, other.rect, sizeof key.rect);
        key.unknown10 = key.unknown14 = key.unknown18 = 1;
        key.unknown20 = 2000;
        key.unknown30 = 8;
        visual->AddKey(key, -1);
    }
    SelectKeys(visual, def);
    visual->Update();
    image->Release();
    return true;
}

// The slot's emitter switched to the effect in place when it has the effect's texture: its
// properties (but 0x22: Quirk kept), its keys of the effect selected, taken and emitting.
int __fastcall ReuseCarEmitter(int slot, uint32_t effect)                      // 0x004d62c0
{
    if (effect >= g_carEffectCount)
        return -1;
    const S_CarEffectDef &def = g_carEffects[effect];
    S_CarEmitter &emitter = g_carEmitters[slot];
    if (std::strcmp(emitter.visual->GetTextureFileName(), def.texture) != 0)
        return -1;
    emitter.effect = static_cast<int>(effect);
    SetProperties(emitter, def, false);
    SelectKeys(emitter.visual, def);
    emitter.visual->Update();
    emitter.active = true;
    emitter.stopping = false;
    emitter.visual->SetEmitting(true);
    return slot;
}
