// Part 11 of the human harness (HUMAN_VISUAL builds, build_visual.bat; included by human_test.cpp after idle_test.inc):
// the visual (game/human_visual.cpp: the model's glide toward the network positions, its place and direction, the
// shadow, the sector and the light's update, the animation events of the stages, the saved games) against the
// original's. The idle's build under it (HUMAN_IDLE); the lighting (C_HumanLighting) stays part 6's loggers.

// What the visual calls besides part 1-10's fakes: logged on both sides.
bool C_HumanEquipment::ShowHandsItem(bool show, int unused, bool toggle)
{
    Log(this, "ShowHandsItem", {double(show), double(unused), double(toggle)});
    return true;
}
void C_HumanEquipment::Unknown7938f0(int arg) { Log(this, "Unknown7938f0", {double(arg)}); }
void C_HumanEquipment::UpdateLensFlare(bool on) { Log(this, "UpdateLensFlare", {double(on)}); }
#ifndef HUMAN_MORPH   // (game/human_morph.cpp's own in HUMAN_MORPH builds)
void C_HumanMorph::Update(int time) { Log(this, "MorphUpdate", {double(time)}); }
#endif
void C_HumanWeaponPose::FitProneBody() { Log(this, "FitProneBody"); }
void C_HumanWork::SetViewDir(const S_vector &dir) { LogV(this, "SetViewDir", &dir.x, 3); }
namespace {
#define VT __attribute__((thiscall))
int VT OV_ShowHandsItem(void *self, int show, int unused, int toggle)
{
    Log(self, "ShowHandsItem", {double(B(show)), double(unused), double(B(toggle))});
    return 1;
}
void VT OV_Unknown7938f0(void *self, int arg) { Log(self, "Unknown7938f0", {double(arg)}); }
void VT OV_UpdateLensFlare(void *self, int on) { Log(self, "UpdateLensFlare", {double(B(on))}); }
void VT OV_MorphUpdate(void *self, int time) { Log(self, "MorphUpdate", {double(time)}); }
void VT OV_FitProneBody(void *self) { Log(self, "FitProneBody"); }
void VT OV_SetViewDir(void *self, const float *dir) { LogV(self, "SetViewDir", dir, 3); }
}  // namespace

namespace {

typedef void(__attribute__((thiscall)) * T_VVoid)(void *);
typedef void(__attribute__((thiscall)) * T_VInt)(void *, int);
typedef void(__attribute__((thiscall)) * T_VPtr)(void *, const void *);
typedef void(__attribute__((thiscall)) * T_VPtrInt)(void *, const void *, int);
typedef void(__attribute__((thiscall)) * T_VPtrFloat)(void *, const void *, float);
typedef bool(__attribute__((thiscall)) * T_VChunk)(void *, void *);
const size_t O_VISUAL = 0xCD0;
uint8_t *VisualOf(void *movement) { return static_cast<uint8_t *>(movement) + O_VISUAL; }

I3D_frame *g_place7 = nullptr;                          // (the object of place 7: SetUpVisual)

void SetUpVisual()
{
    SetUpIdle();
    // The original's visual functions part 6 and part 10 redirected to loggers run for real here.
    for (uint32_t at : {0x007583b0u, 0x00758270u, 0x0075a840u, 0x0075ab60u, 0x0075b0c0u, 0x00759fe0u, 0x007583c0u, 0x00758c90u,
                        0x00758c50u, 0x00759e50u, 0x007590c0u})
        if (!RestoreOriginal(at)) {
            std::printf("visual: %08x not restored\n", at);
            std::exit(3);
        }
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00791f20, reinterpret_cast<const void *>(OV_ShowHandsItem)},
        {0x007938f0, reinterpret_cast<const void *>(OV_Unknown7938f0)},
        {0x00793830, reinterpret_cast<const void *>(OV_UpdateLensFlare)},
        {0x00781b10, reinterpret_cast<const void *>(OV_MorphUpdate)},
        {0x007a8980, reinterpret_cast<const void *>(OV_FitProneBody)},
        {0x0077faf0, reinterpret_cast<const void *>(OV_SetViewDir)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // Undefined in the original, not compared: the w of m_unknown70 (ResetState copies it from a stack temp), of
    // m_glideTo and m_step (SetTarget's on-along path writes them whole from temps whose w is not set).
    for (size_t at : {0x7Cu, 0xACu, 0xBCu})
        g_skipWords.insert({"m_visual", at});
    // The object of place 7 the activity's end uses up: restored for the original's call and its count compared.
    g_place7 = NewFrame(FRAME_DUMMY, "place7", S_vector(0, 0, 0), Identity(), Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_sector);
    g_sharedFrames.push_back(g_place7);
    ShareBlock(&g_place7->m_refCount, 4, "place7 refCount");
    // The ladder's frame (a climber faces it: SetDirection reads its direction), on both sides' actors.
    reinterpret_cast<C_Actor *>(Ladder()->port)->m_frame = Ladder()->frame;
    *reinterpret_cast<I3D_frame **>(Ladder()->orig + 0x20) = Ladder()->frame;
}

// A random human for the visual: the idle's state, the visual's own, the events of the stages.
void RandomVisual()
{
    RandomIdle();
    C_HumanMovement *m = g_h.movement;
    C_HumanVisual &v = m->m_visual;
    v.m_movement = m;
    v.m_locomotion = &m->m_locomotion;
    v.m_owner = g_h.human;
    v.m_unknown0C = I(0, 4);
    v.m_initialized = P(0.7f);
    v.m_sector = P(0.2f) ? nullptr : reinterpret_cast<I3D_sector *>(g_sector);
    v.m_sectorTime = I(0, 1200);
    v.m_showHandsTime = P(0.5f) ? -1 : I(0, 2000);
    v.m_glideTime = I(-10, 150);
    v.m_glideDuration = P(0.2f) ? -1 : I(1, 300);
    const S_vector4 &p = g_h.human->m_position;
    const auto around = [&](float r) { return S_vector4{p.x + U(-r, r), p.y + U(-0.2f, 0.2f), p.z + U(-r, r), P(0.5f) ? 1.0f : 0.0f}; };
    v.m_previousTarget = around(1.0f);
    v.m_unknown40 = around(1.0f);
    v.m_target = around(0.6f);
    v.m_newTarget = P(0.5f);
    v.m_unknown70 = around(1.0f);
    v.m_glideFrom = around(0.6f);
    v.m_glideTo = around(0.6f);
    v.m_step = {U(-0.3f, 0.3f), U(-0.05f, 0.05f), U(-0.3f, 0.3f), 0.0f};
    v.m_unknownC0 = U(0, 60);
    v.m_flags = P(0.5f) ? 0 : I(1, 3);
    v.m_glideDone = P(0.3f);
    v.m_moveSoundTime = I(0, 600);
    v.m_networkDriven = P(0.4f);
    // The mixer's moves (none under way: the standing flag), the detached body, the seat, the weapon in the hands.
    m->m_mixer.m_addMove2Duration = P(0.6f) ? MOVE_NO_TIME : I(0, 500);
    m->m_mixer.m_addMoveDuration = P(0.6f) ? MOVE_NO_TIME : I(0, 500);
    m->m_mixer.m_moveToDuration = P(0.6f) ? MOVE_NO_TIME : I(0, 500);
    m->m_mixer.m_slideDuration = P(0.6f) ? MOVE_NO_TIME : I(0, 500);
    m->m_mixer.m_extraMoveDuration = P(0.6f) ? MOVE_NO_TIME : 0;
    m->m_detached = P(0.1f);
    m->m_unknown240 = {U(-1, 1), U(-1, 1), U(-1, 1), 0.0f};   // (a network position clears it)
    if (P(0.2f))
        m->m_seatMode = P(0.5f) ? 2 : 4;
    C_HumanWeaponPose *pose = reinterpret_cast<C_HumanWeaponPose *>(g_h.pose);
    pose->m_weapon = P(0.5f) ? g_handsModel : nullptr;
    pose->m_hand = static_cast<uint32_t>(I(0, 2)) | (P(0.2f) ? 0x80000000u : 0u);
    // A door in use (the door event opens or closes it) and the pick-up's states.
    if (P(0.15f)) {
        m->m_interaction = 1;
        m->m_vehicle = reinterpret_cast<C_Actor *>(ContactNamed("door")->port);
    }
    if (P(0.2f))
        m->m_unknownA2C = P(0.5f) ? 2 : 10;
    // The used-up object of place 7 (released once, twice or three times): its reference count compared.
    g_place7->m_refCount = static_cast<int16_t>(I(3, 5));
    g_equipmentModel[7] = P(0.6f) ? g_place7 : nullptr;
    // The ladder's step sound (none: -1), the same on the port's ladder and the original's copy (+0x174).
    C_Ladder *ladder = reinterpret_cast<C_Ladder *>(Ladder()->port);
    ladder->m_stepSound = P(0.3f) ? -1 : I(0, 9);
    std::memcpy(Ladder()->orig + 0x174, &ladder->m_stepSound, 4);
    // The events of the stages ProcessAnimEvents handles (every kind, now and then several).
    for (int t = 0; t < 3; ++t) {
        S_MixerTrack &trk = m->m_mixer.m_tracks[t];
        trk.eventCount = P(0.4f) ? 0 : I(1, MIXER_TRACK_EVENTS - 1);
        for (int e = 0; e < trk.eventCount; ++e)
            trk.events[e] = I(0, 25);
    }
}

void TestVisualSmall()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanVisual &v = m->m_visual;
    std::printf("visual: small\n");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomVisual();
        const auto original = [](uint32_t at) {
            return [at](void *mv) { return IdleOriginal([&] { OriginalAt<T_VVoid>(at)(VisualOf(mv)); return 0; }); };
        };
        switch (i % 10) {
        case 0:
            DiffMove("VisualResetState", [&] { v.ResetState(); return 0; }, original(0x00758080));
            break;
        case 1:
            DiffMove("UpdateStandingFlag", [&] { v.UpdateStandingFlag(); return 0; }, original(0x00758410));
            break;
        case 2: {
            const S_vector4 pos = {g_h.human->m_position.x + U(-1, 1), g_h.human->m_position.y, g_h.human->m_position.z + U(-1, 1), 1.0f};
            const bool restart = P(0.5f);
            DiffMove("SetTarget", [&] { v.SetTarget(pos, restart); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VPtrInt>(0x007586f0)(VisualOf(mv), &pos, restart); return 0; }); });
            break;
        }
        case 3: {
            const int time = I(0, 150);
            DiffMove("Glide", [&] { v.Glide(time); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VInt>(0x00758a20)(VisualOf(mv), time); return 0; }); });
            break;
        }
        case 4: {
            const int time = I(0, 150);
            DiffMove("VisualUpdatePosition", [&] { v.UpdatePosition(time); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VInt>(0x00758c50)(VisualOf(mv), time); return 0; }); });
            break;
        }
        case 5: {
            S_vector4 pos = {g_h.human->m_position.x + U(-0.5f, 0.5f), g_h.human->m_position.y + U(-0.1f, 0.1f),
                             g_h.human->m_position.z + U(-0.5f, 0.5f), U(0, 1)};
            if (P(0.2f)) {
                // (Where the model already is: nothing to move.)
                const S_vector at = g_h.frame->GetWorldPos();
                pos = {at.x, at.y, at.z, 1.0f};
            }
            if (P(0.3f)) {
                // (In a seat: a car or a static weapon keep the body; another actor does not.)
                const E_ActorType types[] = {ACTOR_CAR, ACTOR_STATIC_WEAPON, ACTOR_BARREL};
                const E_ActorType type = types[I(0, 2)];
                reinterpret_cast<C_Actor *>(g_carBytes)->m_type = type;
                *reinterpret_cast<int *>(g_carBytes + 0x18) = type;
                g_h.human->m_vehicle = reinterpret_cast<C_Actor *>(g_carBytes);
            }
            if (P(0.5f)) {
                DiffMove("SetNetworkPosition", [&] { v.SetNetworkPosition(pos); return 0; },
                         [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VPtr>(0x00759e50)(VisualOf(mv), &pos); return 0; }); });
            } else {
                DiffMove("VisualSetPosition", [&] { v.SetPosition(pos); return 0; },
                         [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VPtr>(0x0075a280)(VisualOf(mv), &pos); return 0; }); });
            }
            break;
        }
        case 6: {
            const float yaw = U(0, 6.2831f);
            S_vector4 dir = {std::sin(yaw), P(0.3f) ? U(-0.2f, 0.2f) : 0.0f, std::cos(yaw), U(-0.3f, 0.3f)};
            if (P(0.2f)) {
                // (The way the model already faces, its roll unchanged: nothing to turn.)
                const S_vector d = g_h.frame->GetWorldDir();
                dir = {d.x, d.y, d.z, 0.0f};
                m->m_unknown254 = m->m_unknown250;
            }
            const float roll = P(0.5f) ? 0.0f : U(-0.3f, 0.3f);
            if (P(0.5f)) {
                DiffMove("SetNetworkDirection", [&] { v.SetNetworkDirection(dir); return 0; },
                         [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VPtr>(0x00759fe0)(VisualOf(mv), &dir); return 0; }); });
            } else {
                DiffMove("VisualSetDirection", [&] { v.SetDirection(dir, roll); return 0; },
                         [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VPtrFloat>(0x0075a520)(VisualOf(mv), &dir, roll); return 0; }); });
            }
            break;
        }
        case 7: {
            const bool on = P(0.5f);
            if (P(0.5f)) {
                DiffMove("SetNetworkDriven", [&] { v.SetNetworkDriven(on); return 0; },
                         [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VInt>(0x007583c0)(VisualOf(mv), on); return 0; }); });
            } else {
                DiffMove("Unknown7585d0", [&] { v.Unknown7585d0(on); return 0; },
                         [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VInt>(0x007585d0)(VisualOf(mv), on); return 0; }); });
            }
            break;
        }
        case 8:
            if (P(0.4f)) {
                // (Seen in the first person: the game's flag and the original's inline bytes, as RandomMove sets them.)
                g_firstPerson = true;
                g_origFirstPerson[0x58] = P(0.5f);
                g_origFirstPerson[0x59] = !g_origFirstPerson[0x58];
                g_script.activeIsOwner = P(0.7f);
            }
            DiffMove("ProcessAnimEvents", [&] { v.ProcessAnimEvents(); return 0; }, original(0x00759180));
            break;
        default: {
            const int time = P(0.1f) ? 0 : I(1, 200);
            DiffMove("VisualUpdate", [&] { v.Update(time); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_VInt>(0x00758c90)(VisualOf(mv), time); return 0; }); });
            break;
        }
        }
    }
}

// The shadow the effects module makes (CreateShadow, the original's slot 0x78 and the port's): a frame of its own, or
// none.
I3D_frame *g_visualShadow = nullptr;
bool g_shadowMade = true;
I3D_frame *__attribute__((thiscall)) F_CreateShadow(void *self, I3D_frame *model)
{
    Log(self, ("CreateShadow " + NameOf(model)).c_str());
    return g_shadowMade ? g_visualShadow : nullptr;
}

// The model's set-ups (Init, Done, Reset, InitModel, DoneModel), the shadow, the saved games.
void TestVisualLife()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanVisual &v = m->m_visual;
    g_visualShadow = NewFrame(FRAME_DUMMY, "visualShadow", S_vector(0, 0, 0), Identity(), Box(S_vector(0.5f, 0.1f, 0.5f)), "", g_sector);
    // (Its state compared: on or off, its references, its links, the value the visual gives it at +0x1FC.)
    g_sharedFrames.push_back(g_visualShadow);
    ShareBlock(&g_visualShadow->m_frameFlags, 4, "shadow flags");
    ShareBlock(&g_visualShadow->m_refCount, 4, "shadow refCount");
    ShareBlock(&Fake(g_visualShadow)->links, 4, "shadow links");
    ShareBlock(reinterpret_cast<uint8_t *>(g_visualShadow) + 0x1FC, 4, "shadow +0x1FC");
    g_geffectsVt[0x78 / 4] = g_geffectsVt[VSlot(&C_GEffectsModule::CreateShadow)] = reinterpret_cast<void *>(F_CreateShadow);
    std::printf("visual: life\n");
    const std::string file = TempFile("visual.chk"), file2 = TempFile("visual_orig.chk");
    for (int i = 0; i < 16000 / QUICK; ++i) {
        RandomVisual();
        g_shadowMade = P(0.8f);
        if (P(0.5f))
            m->m_shadow = P(0.7f) ? g_visualShadow : nullptr;
        // (The shadow's references (Remove releases once or twice) and whether it is shown.)
        g_visualShadow->m_refCount = static_cast<int16_t>(I(1, 4));
        g_visualShadow->m_frameFlags = P(0.5f) ? (g_visualShadow->m_frameFlags | 1) : (g_visualShadow->m_frameFlags & ~1u);
        const auto original = [](uint32_t at) {
            return [at](void *mv) { return IdleOriginal([&] { OriginalAt<T_VVoid>(at)(VisualOf(mv)); return 0; }); };
        };
        switch (i % 8) {
        case 0:
            g_undefinedTrack0Weight = true;
            g_skipWords.insert({"m_mixer", 0xC8});      // (track 0's stages' crossfade: the same unset byte)
            g_skipWords.insert({"m_mixer", 0xEC});
            DiffMove("VisualInit", [&] { v.Init(); return 0; }, original(0x00758270));
            g_undefinedTrack0Weight = false;
            g_skipWords.erase({"m_mixer", 0xC8});
            g_skipWords.erase({"m_mixer", 0xEC});
            break;
        case 1:
            DiffMove("VisualDone", [&] { v.Done(); return 0; }, original(0x007583b0));
            break;
        case 2:
            g_undefinedTrack0Weight = true;
            g_skipWords.insert({"m_mixer", 0xC8});      // (track 0's stages' crossfade: the same unset byte)
            g_skipWords.insert({"m_mixer", 0xEC});
            DiffMove("VisualReset", [&] { v.Reset(); return 0; }, original(0x0075b0c0));
            g_undefinedTrack0Weight = false;
            g_skipWords.erase({"m_mixer", 0xC8});
            g_skipWords.erase({"m_mixer", 0xEC});
            break;
        case 3:
            if (P(0.5f))
                DiffMove("InitModel", [&] { v.InitModel(); return 0; }, original(0x00758220));
            else
                DiffMove("DoneModel", [&] { v.DoneModel(); return 0; }, original(0x00758250));
            break;
        case 4:
            DiffMove("RebuildShadow", [&] { v.RebuildShadow(); return 0; }, original(0x00759050));
            break;
        case 5:
            DiffMove("RemoveShadow", [&] { v.RemoveShadow(); return 0; }, original(0x007590c0));
            break;
        case 6: {
            std::remove(file.c_str());
            std::remove(file2.c_str());
            const bool open = P(0.95f);
            DiffMove("VisualSaveGame",
                     [&] {
                         C_chunk c;
                         if (open)
                             c.Open(file.c_str(), true);
                         return int(v.SaveGame(c));
                     },
                     [&](void *mv) {
                         return IdleOriginal([&] {
                             C_chunk c;
                             if (open)
                                 c.Open(file2.c_str(), true);
                             return int(OriginalAt<T_VChunk>(0x0075a840)(VisualOf(mv), &c));
                         });
                     });
            CHECK(FileBytes(file) == FileBytes(file2));
            CHECK(!open || FileBytes(file).size() > 100);
            break;
        }
        default: {
            // A save of another state loaded back (or an unknown chunk, another id, a short lighting record, nothing open).
            std::remove(file.c_str());
            {
                C_chunk c;
                c.Open(file.c_str(), true);
                const int kind = I(0, 9);
                c.Begin(kind == 0 ? 0x22C : 0x22B);
                std::vector<uint8_t> light(kind == 1 ? 8 : 0x1C);
                for (uint8_t &b : light)
                    b = static_cast<uint8_t>(I(0, 255));
                c.WriteChunk(0x00, light.data(), static_cast<int>(light.size()));
                for (uint16_t id : {0x03, 0x04, 0x05, 0x07, 0x08, 0x09, 0x0D, 0x19, 0x1A, 0x1B, 0x33})
                    if (P(0.8f))
                        c.WriteChunk(id, I(-5, 300));
                for (uint16_t id : {0x0A, 0x0B, 0x0C, 0x12})
                    if (P(0.8f)) {
                        const float xyz[3] = {U(-50, 50), U(-5, 5), U(-50, 50)};
                        c.WriteChunk(id, xyz, 12);
                    }
                c.Ascend();
            }
            const bool open = P(0.95f);
            // (The targets' w: the original reads 12 bytes into a stack temp and copies 16, its w undefined.)
            for (size_t at : {0x3Cu, 0x4Cu, 0x5Cu})
                g_skipWords.insert({"m_visual", at});
            DiffMove("VisualLoadGame",
                     [&] {
                         C_chunk c;
                         if (open)
                             c.Open(file.c_str(), false);
                         return int(v.LoadGame(c));
                     },
                     [&](void *mv) {
                         return IdleOriginal([&] {
                             C_chunk c;
                             if (open)
                                 c.Open(file.c_str(), false);
                             return int(OriginalAt<T_VChunk>(0x0075ab60)(VisualOf(mv), &c));
                         });
                     });
            for (size_t at : {0x3Cu, 0x4Cu, 0x5Cu})
                g_skipWords.erase({"m_visual", at});
            break;
        }
        }
    }
}

void TestVisual()
{
    SetUpVisual();
    TestVisualSmall();
    TestVisualLife();
}

}  // namespace
