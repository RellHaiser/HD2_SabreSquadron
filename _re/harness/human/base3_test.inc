// Part 4c of the human harness (HUMAN_BASE builds; included after base2_test.inc): the movement base's functions
// on its sound vector and remote action deque, and its resets, against the original's. The original's operator
// new / delete and rand (its own C runtime's, never started here) are redirected to the test's; the port's sound
// vector is copied into the original's MSVC6 vector (+0x194: allocator, first +0x198, last +0x19C, end +0x1A0)
// before each call of the original's and compared after it. The remote action deque stays empty on both sides (its
// MSVC6 form is not rebuilt).
namespace {

const uint32_t O_SOUNDS = 0x194;
void *O_New(size_t size) { return std::malloc(size ? size : 1); }
void O_Delete(void *p) { std::free(p); }
int O_Rand() { return std::rand(); }
void __attribute__((thiscall)) O_MorphInit(void *self, void *) { Log(self, "MorphInit"); }
void __attribute__((thiscall)) O_MorphDone(void *self) { Log(self, "MorphDone"); }
void __attribute__((thiscall)) O_SetBlinking(void *self, int on) { Log(self, "SetBlinking", {double(B(on))}); }
void O_SurfaceMarksClear() { Log(nullptr, "SurfaceMarks_Clear"); }

// The port's sound vector as it was before the call (the raw copies of the movement restore only its header: the
// port's call may have changed what it points to).
std::vector<I3D_sound *> g_soundsBefore;
// ... into the original's copy (a new buffer: the original's push_back may grow and free it).
void SoundsToOriginal()
{
    const std::vector<I3D_sound *> &sounds = g_soundsBefore;
    void **buffer = nullptr;
    if (!sounds.empty()) {
        buffer = static_cast<void **>(std::malloc(sounds.size() * sizeof(void *)));
        for (size_t i = 0; i < sounds.size(); ++i)
            buffer[i] = sounds[i];
    }
    void **vector = reinterpret_cast<void **>(o_movement + O_SOUNDS);
    vector[0] = nullptr;
    vector[1] = buffer;
    vector[2] = buffer + sounds.size();
    vector[3] = buffer + sounds.size();
}
// The original's sound vector after its call, against the port's.
bool SoundsAgree()
{
    void **vector = reinterpret_cast<void **>(o_movement + O_SOUNDS);
    auto *first = static_cast<void **>(vector[1]), *last = static_cast<void **>(vector[2]);
    const std::vector<I3D_sound *> &sounds = g_h.movement->m_sounds;
    if (static_cast<size_t>(last - first) != sounds.size())
        return false;
    for (size_t i = 0; i < sounds.size(); ++i)
        if (first[i] != sounds[i])
            return false;
    return true;
}

// A second sound (UpdateSounds moves the last one into a finished one's place: they must differ).
S_FakeSound g_sound2;

// What the resets set, set to something else first.
void ScrambleResetFields(C_HumanMovement *m)
{
    m->m_unknown1BC = I(1, 9);
    m->m_animProfileTime = I(1, 99);
    m->m_physicsProfileTime = I(1, 99);
    m->m_footstepSurface = I(-5, 5);
    m->m_animState.carryVariant = I(1, 3);
    m->m_animState.activityVariant = I(1, 3);
    m->m_unknown220 = I(1, 9);
    m->m_unknown254 = U(-1, 1);
    m->m_carried = P(0.5f);
    m->m_unknown9E0 = I(0, 9);
    m->m_ground = P(0.5f) ? &g_truck : nullptr;
    m->m_unknown96C = P(0.5f);
    m->m_unknown9B0 = I(0, 500);
    m->m_unknownA24 = I(1, 9);
    m->m_unknownA2C = I(0, 500);
    m->m_unknown224 = P(0.5f);
    m->m_useSound = I(-1, 9);
    m->m_bloodFrame = P(0.5f) ? g_h.frame : nullptr;
    m->m_interactionTime = I(0, 500);
    m->m_seat = I(0, 5);
    m->m_unknown240 = {U(-5, 5), U(-5, 5), U(-5, 5), 0.0f};
    m->m_unknown190 = I(1, 9);
    m->m_inUpdate = P(0.5f);
    m->m_unknown9C9 = P(0.5f);
    m->m_nextStance2 = I(0, 3);
}

void SetUpBase3()
{
    FakeSound();
    g_sound2.vt = g_sound.vt;
    g_names[&g_sound2] = "swimSound2";
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(O_New));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(O_Delete));
    RedirectOriginal(0x007e6393, reinterpret_cast<const void *>(O_Rand));
    RedirectOriginal(0x00780e20, reinterpret_cast<const void *>(O_MorphInit));
    RedirectOriginal(0x00780f10, reinterpret_cast<const void *>(O_MorphDone));
    RedirectOriginal(0x007816b0, reinterpret_cast<const void *>(O_SetBlinking));
    RedirectOriginal(0x0078ac30, reinterpret_cast<const void *>(O_SurfaceMarksClear));
    g_toOriginalExtra = SoundsToOriginal;
    FakeSound();
}

void TestBase3()
{
    SetUpBase3();
    typedef void(__attribute__((thiscall)) * T_MInt1)(void *, int);
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomHumanBase();
        C_HumanMovement *m = g_h.movement;
        // Sounds playing (the movement sounds' bank gives the fake one), the active player or not.
        m->m_sounds.clear();
        for (int k = I(0, 3); k > 0; --k)
            m->m_sounds.push_back(static_cast<I3D_sound *>(static_cast<void *>(P(0.5f) ? &g_sound : &g_sound2)));
        g_script.activeIsOwner = P(0.7f);
        g_script.soundPlays = P(0.8f);
        g_script.soundPlaying = P(0.5f);
        m->m_remoteActions.clear();
        m->m_trailTime = P(0.5f) ? 0 : I(1, 500);
        g_soundsBefore = m->m_sounds;
        g_firstPerson = P(0.4f);
        switch (i % 6) {
        case 0:
        case 1: {
            const int group = P(0.2f) ? I(-1, 40) : I(4, 20);
            DiffCall("PlaySound", [&] { m->PlaySound(group); return 0; },
                     [&](void *self) { OriginalAt<T_MInt1>(0x0075ebd0)(MovementOf(self), group); return 0; });
            break;
        }
        case 2: {
            const bool stopAll = P(0.3f);
            DiffCall("UpdateSounds", [&] { m->UpdateSounds(stopAll); return 0; },
                     [&](void *self) { OriginalAt<T_MInt1>(0x0075ef00)(MovementOf(self), stopAll); return 0; });
            break;
        }
        case 3:
            ScrambleResetFields(m);
            DiffCall("ResetBase", [&] { m->ResetBase(); return 0; },
                     [&](void *self) { OriginalAt<T_MVoid>(0x0075b2a0)(MovementOf(self)); return 0; });
            break;
        case 4:
            ScrambleResetFields(m);
            DiffCall("RestartBase", [&] { m->RestartBase(); return 0; },
                     [&](void *self) { OriginalAt<T_MVoid>(0x00763650)(MovementOf(self)); return 0; });
            break;
        default: {
            const int time = I(5, 120);
            DiffCall("ProcessRemoteActions (none)", [&] { m->ProcessRemoteActions(time); return 0; },
                     [&](void *self) { OriginalAt<T_MInt1>(0x0075f690)(MovementOf(self), time); return 0; });
            break;
        }
        }
        CHECK(SoundsAgree());
    }
    g_h.movement->m_sounds.clear();
}
}  // namespace
