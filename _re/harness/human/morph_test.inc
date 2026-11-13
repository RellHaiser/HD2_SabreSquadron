// Part 13 of the human harness (HUMAN_MORPH builds, build_morph.bat; included by human_test.cpp after
// lighting_test.inc): the morph (game/human_morph.cpp: blinking, breathing by the exhaustion and its sounds,
// expressions, the hands' grips, the dead face, the weights given to the model's morph channels, the saved games)
// against the original's. The morph is compiled through morph_statics.cpp; its tables of Tables\morph.sav and the
// original's are filled alike.

// What the morph asks of the human: logged on both sides.
namespace {
struct S_MorphScript {
    int stamina = 100;                                  // C_Human::GetStaminaPercent
    bool holdingBreath = false;                         // C_Human::IsPlayerHoldingBreath
    int targets = 4;                                    // I3D_morph::NumTargets
} g_morphScript;
}  // namespace

int C_Human::GetStaminaPercent() const
{
    Log(this, "GetStaminaPercent");
    return g_morphScript.stamina;
}
bool C_Human::IsPlayerHoldingBreath() const
{
    Log(this, "IsPlayerHoldingBreath");
    return g_morphScript.holdingBreath;
}

extern void *const g_portMorphWeights;
extern void *const g_portMorphFlags;
extern bool *const g_portMorphLoaded;
extern const unsigned g_morphWeightsSize;
extern const unsigned g_morphFlagsSize;

namespace {
#define MT __attribute__((thiscall))
int MT OH_GetStaminaPercent(void *self)
{
    Log(self, "GetStaminaPercent");
    return g_morphScript.stamina;
}
int MT OH_IsPlayerHoldingBreath(void *self)
{
    Log(self, "IsPlayerHoldingBreath");
    return g_morphScript.holdingBreath;
}

// The model's morph visual (a child of the model whose visual type is 'SMMR') and its morph interface at +0x120
// (COM: stdcall, this on the stack): the weights set logged.
I3D_frame *g_morphVisual = nullptr, *g_leftEye = nullptr, *g_rightEye = nullptr;
void *g_morphFrameVt[128];
void *g_morphIfaceVt[4];
uint32_t __stdcall FMV_GetVisualType(I3D_frame *) { return I3DVISUAL_SINGLEMORPH; }
int __stdcall FMI_NumTargets(void *) { return g_morphScript.targets; }
void __stdcall FMI_SetWeight(void *self, int channel, int target, float weight)
{
    Log(self, "MorphSetWeight", {double(channel), double(target), weight});
}

const uint32_t O_MORPH_WEIGHTS = 0x00984130, O_MORPH_FLAGS = 0x00983550, O_MORPH_LOADED = 0x0098a345;
const size_t O_MORPH = 0x610;
uint8_t *MorphOf(void *movement) { return static_cast<uint8_t *>(movement) + O_MORPH; }

typedef void(MT *T_MoVoid)(void *);
typedef void(MT *T_MoInt)(void *, int);
typedef void(MT *T_MoPtr)(void *, void *);
typedef void(MT *T_MoBool2)(void *, int, int);
typedef bool(MT *T_MoChunk)(void *, void *);

// The tables: random weights, valid or not, the channels each state sets; the same bytes on both sides.
void FillMorphTables()
{
    float *w = static_cast<float *>(g_portMorphWeights);
    for (unsigned state = 0; state < g_morphWeightsSize / 0x108; ++state) {
        float *rec = w + state * (0x108 / 4);
        for (int k = 0; k < 64; ++k)
            rec[k] = U(0, 1);
        reinterpret_cast<int *>(rec)[64] = P(0.85f) ? 1 : I(0, 2);
        reinterpret_cast<int *>(rec)[65] = I(0, 5);
    }
    uint8_t *f = static_cast<uint8_t *>(g_portMorphFlags);
    for (unsigned i = 0; i < g_morphFlagsSize; ++i)
        f[i] = static_cast<uint8_t>(P(0.7f) ? 1 : 0);
    std::memcpy(OriginalAt<void *>(O_MORPH_WEIGHTS), g_portMorphWeights, g_morphWeightsSize);
    std::memcpy(OriginalAt<void *>(O_MORPH_FLAGS), g_portMorphFlags, g_morphFlagsSize);
    *g_portMorphLoaded = true;
    *OriginalAt<bool *>(O_MORPH_LOADED) = true;
}

void SetUpMorph()
{
    SetUpLighting();
    // The original's morph functions earlier parts redirected to loggers run for real here (those redirected in this
    // build's set-ups; the others never were).
    for (uint32_t at : {0x00780e20u, 0x00780f10u, 0x007816b0u, 0x00781f50u, 0x007816f0u, 0x00781710u, 0x00781730u, 0x00782310u,
                        0x00782730u, 0x00782cd0u, 0x007815b0u, 0x00781770u, 0x00781b10u})
        RestoreOriginal(at);
    // Its time constants (0x0098a328..0x0098a340): the original's static initialisers, never run in this copy.
    for (uint32_t at = 0x00780cb0; at <= 0x00780dd0; at += 0x30)
        OriginalAt<void (*)()>(at)();
    RedirectOriginal(0x005a62a0, reinterpret_cast<const void *>(OH_GetStaminaPercent));
    RedirectOriginal(0x005a6430, reinterpret_cast<const void *>(OH_IsPlayerHoldingBreath));
    // The morph visual under the model, the eyes the dead face turns (their local matrices compared).
    g_morphVisual = NewFrame(FRAME_VISUAL, "morphVisual", S_vector(0, 0, 0), Identity(), Box(S_vector(0.3f, 0.9f, 0.3f)), "", g_h.frame);
    std::memcpy(g_morphFrameVt, *reinterpret_cast<void ***>(g_morphVisual), sizeof g_morphFrameVt);
    g_morphFrameVt[0x04 / 4] = reinterpret_cast<void *>(FMV_GetVisualType);
    *reinterpret_cast<void ***>(g_morphVisual) = g_morphFrameVt;
    for (void *&slot : g_morphIfaceVt)
        slot = reinterpret_cast<void *>(FMI_NumTargets);
    g_morphIfaceVt[0x08 / 4] = reinterpret_cast<void *>(FMI_SetWeight);
    *reinterpret_cast<void ***>(reinterpret_cast<uint8_t *>(g_morphVisual) + 0x120) = g_morphIfaceVt;
    g_names[reinterpret_cast<uint8_t *>(g_morphVisual) + 0x120] = "morph";
    g_leftEye = NewFrame(FRAME_DUMMY, "Leye", S_vector(0.03f, 1.6f, 0.08f), Identity(), Box(S_vector(0.01f, 0.01f, 0.01f)), "", g_h.frame);
    g_rightEye = NewFrame(FRAME_DUMMY, "Reye", S_vector(-0.03f, 1.6f, 0.08f), Identity(), Box(S_vector(0.01f, 0.01f, 0.01f)), "", g_h.frame);
    for (I3D_frame *eye : {g_leftEye, g_rightEye}) {
        g_sharedFrames.push_back(eye);
        ShareBlock(&eye->m_localMatrix, sizeof(S_matrix), std::string("eye ") + NameOf(eye));
    }
    FillMorphTables();
}

// The morph's state, the human's answers, the camera of the dead face.
void RandomMorph()
{
    RandomLighting();
    C_HumanMorph &mo = g_h.movement->m_morph;
    mo.m_owner = g_h.human;
    mo.m_morphVisual = P(0.85f) ? g_morphVisual : nullptr;
    mo.m_group = I(0, 4);
    mo.m_head = P(0.8f) ? g_h.frame : nullptr;
    mo.m_dirty = P(0.5f);
    const auto state = [](int hi) { return P(0.1f) ? I(-1, hi + 2) : I(0, hi); };
    mo.m_blinkState = state(5);
    mo.m_blinkPrevious = state(5);
    mo.m_blinkTime = I(0, 300);
    mo.m_blinkDuration = P(0.4f) ? 0 : I(1, 300);
    mo.m_blinkTimer = P(0.2f) ? -1 : I(-10, 5000);
    mo.m_breathState = state(4);
    mo.m_breathPrevious = state(4);
    mo.m_breathTime = I(0, 400);
    mo.m_breathDuration = P(0.4f) ? 0 : I(1, 400);
    mo.m_exhaustTime = P(0.5f) ? -1 : I(0, 25000);
    mo.m_exhaustion = P(0.3f) ? 0 : I(0, 25000);
    mo.m_heavyBreath = P(0.3f);
    mo.m_expressionTime = I(0, 3000);
    mo.m_expressionDuration = P(0.5f) ? 0 : I(1, 3000);
    mo.m_expressionRequest = P(0.5f) ? 100 : I(2, 4);
    mo.m_expressionSpeed = I(-1, 4);
    mo.m_gripState = state(2);
    mo.m_gripPrevious = state(2);
    mo.m_gripTime = I(0, 600);
    mo.m_gripDuration = P(0.4f) ? 0 : I(1, 600);
    mo.m_gripRequest = P(0.5f) ? 100 : I(0, 2);
    mo.m_grip2State = state(2);
    mo.m_grip2Previous = state(2);
    mo.m_grip2Time = I(0, 600);
    mo.m_grip2Duration = P(0.4f) ? 0 : I(1, 600);
    mo.m_grip2Request = P(0.5f) ? 100 : I(0, 2);
    mo.m_breathSound = P(0.4f) ? static_cast<I3D_sound *>(FakeSound()) : nullptr;
    mo.m_breathSoundTime = P(0.3f) ? -1 : I(-10, 2000);
    mo.m_breathVolume = P(0.2f) ? -1.0f : U(0, 1);
    *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(&g_sound) + 0x16C) = U(0, 1);   // (GetVolumeInline)
    // (The integer boundaries now and then: durations and timers of 1, exhaustion at and just past the threshold.)
    const auto small = [](int &v) {
        if (P(0.15f))
            v = I(0, 2);
    };
    for (int *v : {&mo.m_blinkDuration, &mo.m_breathDuration, &mo.m_expressionDuration, &mo.m_gripDuration, &mo.m_grip2Duration,
                   &mo.m_blinkTime, &mo.m_breathTime, &mo.m_gripTime, &mo.m_grip2Time, &mo.m_exhaustTime, &mo.m_exhaustion,
                   &mo.m_breathSoundTime})
        small(*v);
    if (P(0.1f))
        mo.m_exhaustion = I(6069, 6074);                // (PANT_THRESHOLD is 8500 / 1.4: 6071.43)
    // (The bools' padding zeroed: a bool in the low byte of a float-like word passes the float tolerance.)
    for (bool *b : {&mo.m_dirty, &mo.m_heavyBreath})
        std::memset(reinterpret_cast<uint8_t *>(b) + 1, 0, 3);
    g_morphScript.stamina = P(0.3f) ? 100 : I(0, 99);
    g_morphScript.holdingBreath = P(0.2f);
    g_morphScript.targets = I(0, 8);
    g_script.activeIsOwner = P(0.6f);
    g_script.soundLength = P(0.2f) ? 0 : I(1, 2000);
    g_script.soundPlays = P(0.85f);
    if (P(0.3f))
        g_h.movement->m_flags |= MOVE_FLAG_LARGE;
    // The camera the dead face is seen from: by the body or far away, a field of view.
    I3D_frame *camera = *reinterpret_cast<I3D_frame **>(g_sceneBytes + 0x15C);
    const S_vector at = g_h.frame->GetWorldPos();
    const float away = P(0.5f) ? U(0, 2) : U(2, 30);
    camera->SetPosInline(S_vector(at.x + away, at.y + U(0, 2), at.z));
    UpdateTree(camera);
    *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(camera) + 0x120) = U(0.5f, 1.8f);
    // The eyes as the model has them.
    for (I3D_frame *eye : {g_leftEye, g_rightEye}) {
        eye->SetRot(Identity());
        UpdateTree(eye);
    }
}

void TestMorph()
{
    SetUpMorph();
    C_HumanMorph &mo = g_h.movement->m_morph;
    std::printf("morph\n");
    const std::string file = TempFile("morph.chk"), file2 = TempFile("morph_orig.chk");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomMorph();
        const auto original = [](uint32_t at) {
            return [at](void *mv) { return IdleOriginal([&] { OriginalAt<T_MoVoid>(at)(MorphOf(mv)); return 0; }); };
        };
        switch (i % 12) {
        case 0:
            if (P(0.7f)) {
                mo.m_owner = P(0.5f) ? nullptr : mo.m_owner;   // (Init sets it: RandomMorph gives the same value)
                DiffMove("MorphInit", [&] { mo.Init(g_h.human); return 0; },
                         [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_MoPtr>(0x00780e20)(MorphOf(mv), o_human); return 0; }); });
            } else
                DiffMove("MorphRelink", [&] { mo.Relink(g_h.frame); return 0; },
                         [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_MoPtr>(0x00781f50)(MorphOf(mv), g_h.frame); return 0; }); });
            break;
        case 1:
            if (P(0.5f))
                DiffMove("MorphDone", [&] { mo.Done(); return 0; }, original(0x00780f10));
            else
                DiffMove("MorphReset", [&] { mo.Reset(); return 0; }, original(0x00782cd0));
            break;
        case 2: {
            const bool on = P(0.5f);
            DiffMove("SetBlinking", [&] { mo.SetBlinking(on); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_MoInt>(0x007816b0)(MorphOf(mv), on); return 0; }); });
            break;
        }
        case 3: {
            const int which = I(1, 3), speed = I(0, 4);
            const uint32_t at = which == 1 ? 0x007816f0 : which == 2 ? 0x00781710 : 0x00781730;
            DiffMove("MorphSetUnknownN",
                     [&] {
                         if (which == 1)
                             mo.SetUnknown1(speed);
                         else if (which == 2)
                             mo.SetUnknown2(speed);
                         else
                             mo.SetUnknown3(speed);
                         return 0;
                     },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_MoInt>(at)(MorphOf(mv), speed); return 0; }); });
            break;
        }
        case 4: {
            const bool start = P(0.5f), cap = P(0.5f);
            DiffMove("SetExhausted", [&] { mo.SetExhausted(start, cap); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_MoBool2>(0x007815b0)(MorphOf(mv), start, cap); return 0; }); });
            break;
        }
        case 5:
            // (A model without its morph visual: the port's documented fix, the original reads near null.)
            if (!mo.m_morphVisual)
                mo.m_morphVisual = g_morphVisual;
            DiffMove("ShowDeathFace", [&] { mo.ShowDeathFace(); return 0; }, original(0x00781770));
            break;
        case 6: {
            const bool force = P(0.5f);
            if (!mo.m_morphVisual)
                mo.m_morphVisual = g_morphVisual;
            DiffMove("MorphApply", [&] { mo.Apply(force); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_MoInt>(0x00780f30)(MorphOf(mv), force); return 0; }); });
            break;
        }
        case 7:
        case 8: {
            const int time = P(0.1f) ? 0 : I(1, 300);
            DiffMove("MorphUpdate", [&] { mo.Update(time); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_MoInt>(0x00781b10)(MorphOf(mv), time); return 0; }); });
            break;
        }
        case 9: {
            const int time = I(0, 300);
            DiffMove("UpdateBreathSound", [&] { mo.UpdateBreathSound(time); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_MoInt>(0x00782000)(MorphOf(mv), time); return 0; }); });
            break;
        }
        case 10: {
            std::remove(file.c_str());
            std::remove(file2.c_str());
            const bool open = P(0.95f);
            DiffMove("MorphSaveGame",
                     [&] {
                         C_chunk c;
                         if (open)
                             c.Open(file.c_str(), true);
                         return int(mo.SaveGame(c));
                     },
                     [&](void *mv) {
                         return IdleOriginal([&] {
                             C_chunk c;
                             if (open)
                                 c.Open(file2.c_str(), true);
                             return int(OriginalAt<T_MoChunk>(0x00782310)(MorphOf(mv), &c));
                         });
                     });
            CHECK(FileBytes(file) == FileBytes(file2));
            CHECK(!open || FileBytes(file).size() > 100);
            break;
        }
        default: {
            // Made-up saves: the chunks in any order, unknown ones, another id, nothing open.
            std::remove(file.c_str());
            {
                C_chunk c;
                c.Open(file.c_str(), true);
                c.Begin(P(0.1f) ? 0xB1 : 0xB0);
                for (uint16_t id = 0; id <= 0x1E; ++id)
                    if (P(0.7f)) {
                        if (id == 0x1C)
                            c.WriteChunk(id, U(0, 1));
                        else
                            c.WriteChunk(id, P(0.3f) ? I(0, 2) : I(-5, 3000));
                    }
                c.Ascend();
            }
            const bool open = P(0.95f);
            DiffMove("MorphLoadGame",
                     [&] {
                         C_chunk c;
                         if (open)
                             c.Open(file.c_str(), false);
                         return int(mo.LoadGame(c));
                     },
                     [&](void *mv) {
                         return IdleOriginal([&] {
                             C_chunk c;
                             if (open)
                                 c.Open(file.c_str(), false);
                             return int(OriginalAt<T_MoChunk>(0x00782730)(MorphOf(mv), &c));
                         });
                     });
            break;
        }
        }
    }
}

}  // namespace
