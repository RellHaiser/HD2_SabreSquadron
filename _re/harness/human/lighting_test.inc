// Part 12 of the human harness (HUMAN_LIGHTING builds, build_lighting.bat; included by human_test.cpp after
// visual_test.inc): the light level of the model (game/human_lighting.cpp: C_HumanLighting's Init, Update, Sample,
// Approach, SetSector) against the original's, the visual's build under it.

// What the lighting calls: the game's light intensity and the controller's values, logged on both sides.
namespace {
struct S_LightScript {
    float intensity = 0.5f, reference = 1.0f;           // C_Game::GetLightIntensity (occlusion / not)
    bool controllerKnows = false;                       // C_Human::ControllerUnknown5C
    float controllerIntensity = 0.3f, controllerReference = 0.6f;
} g_light;
I3D_frame *g_litModelFrame = nullptr;                   // (SetUpLighting: the model Sample measures for)
bool LitModelOn() { return g_litModelFrame && g_litModelFrame->IsOn(); }
}  // namespace

float C_game::GetLightIntensity(const S_vector &pos, I3D_sector *sector, bool occlusion)
{
    // (With the model's on flag: the measure hides it.)
    LogV(this, ("GetLightIntensity " + NameOf(sector)).c_str(), &pos.x, 3, {double(occlusion), double(LitModelOn())});
    return occlusion ? g_light.intensity : g_light.reference;
}
bool C_Human::ControllerUnknown5C(float *intensity, float *reference) const
{
    Log(this, "ControllerUnknown5C");
    if (g_light.controllerKnows) {
        *intensity = g_light.controllerIntensity;
        *reference = g_light.controllerReference;
    }
    return g_light.controllerKnows;
}

namespace {
#define LT __attribute__((thiscall))
float LT OL_GetLightIntensity(void *self, const float *pos, void *sector, int occlusion)
{
    LogV(self, ("GetLightIntensity " + NameOf(sector)).c_str(), pos, 3, {double(B(occlusion)), double(LitModelOn())});
    return B(occlusion) ? g_light.intensity : g_light.reference;
}
int LT OL_ControllerUnknown5C(void *self, float *intensity, float *reference)
{
    Log(self, "ControllerUnknown5C");
    if (g_light.controllerKnows) {
        *intensity = g_light.controllerIntensity;
        *reference = g_light.controllerReference;
    }
    return g_light.controllerKnows;
}
// The effects module's SetModelLightLevel (the original's slot 0x54, the port's own).
bool LT F_SetModelLightLevel(void *self, I3D_frame *model, float level)
{
    Log(self, ("SetModelLightLevel " + NameOf(model)).c_str(), {level});
    return true;
}

typedef void(LT *T_LVoid)(void *);
typedef void(LT *T_LUpdate)(void *, void *, int, int);
typedef void(LT *T_LSample)(void *, void *, void *, const float *, int);
typedef void(LT *T_LInt)(void *, int);
typedef void(LT *T_LPtr)(void *, void *);
const size_t O_LIGHTING = 0xCD0 + 0xD4;
uint8_t *LightingOf(void *movement) { return static_cast<uint8_t *>(movement) + O_LIGHTING; }

// A sector with lights (their on flag, type +0x164, color +0x130, power +0x168: the fields SetSector reads), the
// model the level is given to (its render stamp +0x11C, its data's first word the human).
I3D_frame *g_litSector = nullptr, *g_litModel = nullptr;
I3D_frame *g_lights[4] = {};
I3D_light *g_lightList[4] = {};
S_FrameData g_litData = {};

void SetUpLighting()
{
    SetUpVisual();
    for (uint32_t at : {0x0078a100u, 0x0078a210u, 0x0078a420u, 0x0078a130u})
        if (!RestoreOriginal(at)) {
            std::printf("lighting: %08x not restored\n", at);
            std::exit(3);
        }
    RedirectOriginal(0x0069eca0, reinterpret_cast<const void *>(OL_GetLightIntensity));
    RedirectOriginal(0x005a6710, reinterpret_cast<const void *>(OL_ControllerUnknown5C));
    g_geffectsVt[0x54 / 4] = g_geffectsVt[VSlot(&C_GEffectsModule::SetModelLightLevel)] = reinterpret_cast<void *>(F_SetModelLightLevel);
    g_names[GetGame()] = g_names[g_origGame] = "game";
    g_litSector = NewFrame(FRAME_DUMMY, "litSector", S_vector(0, 0, 0), Identity(), Box(S_vector(10, 10, 10)), "", g_sector);
    for (int i = 0; i < 4; ++i) {
        g_lights[i] = NewFrame(FRAME_DUMMY, "light" + std::to_string(i), S_vector(0, 3, 0), Identity(), Box(S_vector(0.1f, 0.1f, 0.1f)), "",
                               g_litSector);
        g_lightList[i] = reinterpret_cast<I3D_light *>(g_lights[i]);
    }
    *reinterpret_cast<I3D_light ***>(reinterpret_cast<uint8_t *>(g_litSector) + 0x1A4) = g_lightList;
    g_litModel = NewFrame(FRAME_DUMMY, "litModel", S_vector(0, 1, 0), Identity(), Box(S_vector(0.3f, 0.9f, 0.3f)), "", g_sector);
    g_litData.actor = g_h.human;
    g_litModel->SetData(&g_litData);
    g_litModelFrame = g_litModel;
    // (The model's on flag: Sample hides it for the measure and shows it as it was; compared.)
    g_sharedWords.push_back({&g_litModel->m_frameFlags, "litModel flags"});
}

// The lighting's state, the sector's lights, the stamps, the scripted answers.
void RandomLighting()
{
    RandomVisual();
    C_HumanLighting &l = g_h.movement->m_visual.m_lighting;
    l.m_intensity = U(0, 1);
    l.m_level = P(0.2f) ? U(1.0f, 1.5f) : U(0.05f, 1.0f);
    l.m_shownLevel = P(0.3f) ? l.GetLevel() : U(0, 1.2f);
    l.m_time = P(0.2f) ? I(0, 5) : I(0, 12000);
    l.m_sampleDue = P(0.6f);
    l.m_ambient = U(0, 2);
    l.m_climate = P(0.5f) ? 0.0f : U(0, 1);
    // The lights: how many, on or off, ambient (4) or not, their colors with any channel strongest.
    *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(g_litSector) + 0x1AC) = I(0, 4);
    for (I3D_frame *f : g_lights) {
        uint8_t *b = reinterpret_cast<uint8_t *>(f);
        f->m_frameFlags = P(0.7f) ? (f->m_frameFlags | 1) : (f->m_frameFlags & ~1u);
        *reinterpret_cast<int *>(b + 0x164) = P(0.5f) ? 4 : I(0, 5);
        const float equal = U(0, 1);
        S_vector color = P(0.15f) ? S_vector(equal, equal, equal) : S_vector(U(0, 1), U(0, 1), U(0, 1));
        if (P(0.3f)) {
            // (Two channels alike: which one counts as the strongest.)
            const int k = I(0, 2);
            (&color.x)[k] = (&color.x)[(k + 1) % 3];
        }
        std::memcpy(b + 0x130, &color, 12);
        *reinterpret_cast<float *>(b + 0x168) = U(0, 2);
    }
    // The stamps: the model drawn in the last frame or not.
    const int stamp = I(1, 1000);
    *reinterpret_cast<int *>(g_sceneBytes + 0x11C) = stamp;
    *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(g_litModel) + 0x11C) = P(0.6f) ? stamp : stamp - I(1, 5);
    g_litModel->m_frameFlags = P(0.8f) ? (g_litModel->m_frameFlags | 1) : (g_litModel->m_frameFlags & ~1u);
    g_light.intensity = P(0.1f) ? 0.0f : U(0, 2);
    g_light.reference = P(0.15f) ? 0.0f : U(0, 2);
    g_light.controllerKnows = P(0.4f);
    g_light.controllerIntensity = U(0, 1);
    g_light.controllerReference = P(0.1f) ? 0.0f : U(0, 1);
}

void TestLighting()
{
    SetUpLighting();
    C_HumanLighting &l = g_h.movement->m_visual.m_lighting;
    std::printf("lighting\n");
    for (int i = 0; i < 30000 / QUICK; ++i) {
        RandomLighting();
        switch (i % 5) {
        case 0:
            DiffMove("LightingInit", [&] { l.Init(); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_LVoid>(0x0078a100)(LightingOf(mv)); return 0; }); });
            break;
        case 1: {
            const int time = P(0.1f) ? I(1000, 3000) : I(0, 400);
            const int mode = I(0, 6);
            DiffMove("LightingUpdate", [&] { l.Update(g_litModel, time, mode); return 0; },
                     [&](void *mv) {
                         return IdleOriginal([&] { OriginalAt<T_LUpdate>(0x0078a130)(LightingOf(mv), g_litModel, time, mode); return 0; });
                     });
            break;
        }
        case 2: {
            const S_vector pos(U(0, 60), U(1, 3), U(0, 60));
            const bool always = P(0.5f);
            DiffMove("LightingSample", [&] { l.Sample(reinterpret_cast<I3D_sector *>(g_litSector), g_litModel, pos, always); return 0; },
                     [&](void *mv) {
                         return IdleOriginal([&] {
                             OriginalAt<T_LSample>(0x0078a210)(LightingOf(mv), g_litSector, g_litModel, &pos.x, always);
                             return 0;
                         });
                     });
            break;
        }
        case 3: {
            const int time = I(0, 2000);
            DiffMove("LightingApproach", [&] { l.Approach(time); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_LInt>(0x0078a3c0)(LightingOf(mv), time); return 0; }); });
            break;
        }
        default:
            DiffMove("LightingSetSector", [&] { l.SetSector(reinterpret_cast<I3D_sector *>(g_litSector)); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_LPtr>(0x0078a420)(LightingOf(mv), g_litSector); return 0; }); });
            break;
        }
    }
}

}  // namespace
