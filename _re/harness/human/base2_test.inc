// Part 4b of the human harness (HUMAN_BASE builds; included after base_test.inc): the movement base's command
// record (S_MoveRequest), SetOwnerBase, ground damage, the ladder's use sound, PlaySound for others than the active
// player, against the original's. (Its functions that grow or free the movement's containers are left out: the
// original's MSVC6 vector and deque allocate through its own C runtime's heap, which this test never starts.)
namespace {

// ---- the original's sound module: a fake with the slots the movement base calls (its vtable +0x54, +0x5C, +0x90)
struct S_FakeSoundModule {
    void **vt;
    uint8_t rest[0x100];
} g_origSoundModule;
void *g_origSoundVt[64];
void __attribute__((thiscall)) OS_Unexpected(void *)
{
    std::printf("unexpected sound module call in the original\n");
    std::exit(3);
}
int __attribute__((thiscall)) OS_Play(void *self, const S_SoundRequest *request)
{
    LogSoundRequest(self, *request);
    return g_soundSlot;
}
void __attribute__((thiscall)) OS_Stop(void *self, int *slot) { LogSoundStop(self, slot); }
void __attribute__((thiscall)) OS_SetChannelLimit(void *self, int channel, int limit)
{
    Log(self, "SetChannelLimit", {double(channel), double(limit)});
}
void *O_GetSoundModule() { return &g_origSoundModule; }
bool __attribute__((thiscall)) O_IsFirstPersonView(void *) { return g_firstPerson; }
void *__attribute__((thiscall)) O_GetModel(void *, int place) { return place >= 0 && place < 4 ? g_equipmentModel[place] : nullptr; }
void __attribute__((thiscall)) O_OnAnimationEvent(void *self, int event) { Log(self, "OnAnimationEvent", {double(event)}); }
void __attribute__((thiscall)) O_UpdateModelNames(void *self) { Log(self, "UpdateModelNames"); }
I3D_frame *g_handsModel, *g_longGunModel;
int __attribute__((thiscall)) O_CreateAt(void *self, int effect, const S_vector *pos, const S_vector *dir, void *parent, int faceCamera,
                                         int transient)
{
    LogCreateAt(self, effect, *pos, *dir, parent, B(faceCamera), B(transient));
    return 0;
}
// SendExplosionMessage: a method of the original's network module (ECX 0x0089aac0).
void __attribute__((thiscall)) O_SendExplosionMessage(void *, const float *pos, int material)
{
    Log(nullptr, "SendExplosionMessage", {pos[0], pos[1], pos[2], double(material)});
}

// The materials: one table both sides read (g_materials / g_materialCount, the original's 0x0097b9b4 / b8).
const uint32_t kMaterials = 0x70;
S_MaterialDef g_materialTable[kMaterials];

void SetUpBase2()
{
    for (void *&slot : g_origSoundVt)
        slot = reinterpret_cast<void *>(OS_Unexpected);
    g_origSoundVt[0x54 / 4] = reinterpret_cast<void *>(OS_Play);
    g_origSoundVt[0x5C / 4] = reinterpret_cast<void *>(OS_Stop);
    g_origSoundVt[0x90 / 4] = reinterpret_cast<void *>(OS_SetChannelLimit);
    g_origSoundModule.vt = g_origSoundVt;
    g_names[&g_origSoundModule] = g_names[g_fakeSoundModule] = "soundModule";
    RedirectOriginal(0x004336c0, reinterpret_cast<const void *>(O_GetSoundModule));
    RedirectOriginal(0x00451900, reinterpret_cast<const void *>(O_SendExplosionMessage));
    RedirectOriginal(0x0069a780, reinterpret_cast<const void *>(O_IsFirstPersonView));
    RedirectOriginal(0x007917d0, reinterpret_cast<const void *>(O_GetModel));
    RedirectOriginal(0x00791830, reinterpret_cast<const void *>(O_OnAnimationEvent));
    RedirectOriginal(0x00786b30, reinterpret_cast<const void *>(O_UpdateModelNames));
    g_handsModel = NewFrame(FRAME_DUMMY, "handsModel", S_vector(0, 1, 0), Identity(), Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_sector);
    g_longGunModel = NewFrame(FRAME_DUMMY, "longGunModel", S_vector(0, 1, 0), Identity(), Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_sector);
    g_sharedWords.push_back({&g_handsModel->m_frameFlags, "handsModel flags"});
    g_sharedWords.push_back({&g_longGunModel->m_frameFlags, "longGunModel flags"});
    RedirectOriginal(0x00445150, reinterpret_cast<const void *>(O_CreateAt));
    g_names[&g_particlesModule] = g_names[OriginalAt<void *>(0x008999f8)] = "particles";
    std::memset(static_cast<void *>(g_materialTable), 0, sizeof g_materialTable);
    for (uint32_t i = 0; i < kMaterials; ++i)
        g_materialTable[i].footstepSound = i % 7 == 6 ? -1 : static_cast<int>(i % 7);
    g_materials = g_materialTable;
    g_materialCount = kMaterials;
    *OriginalAt<void **>(0x0097b9b4) = g_materialTable;
    *OriginalAt<uint32_t *>(0x0097b9b8) = kMaterials;
}

typedef void(__attribute__((thiscall)) * T_RVoid)(void *);
typedef bool(__attribute__((thiscall)) * T_RChunk)(void *, C_chunk *);
typedef void(__attribute__((thiscall)) * T_MOwner)(void *, void *, void *);
typedef void(__attribute__((thiscall)) * T_MDamage)(void *, const S_MoveContacts *, const S_CollisionVolume *, int);
const uint32_t O_REQUEST = 0x770;                       // the movement's S_MoveRequest (the same offset in both)

// A random command record: every field (the pointers among the test's objects).
void RandomRequest(S_MoveRequest &r)
{
    uint8_t *bytes = reinterpret_cast<uint8_t *>(&r);
    for (size_t i = 0; i + 4 <= sizeof r; i += 4) {
        const float f = U(-50, 50);
        std::memcpy(bytes + i, &f, 4);
    }
    r.anim = I(0, 0xCD);
    r.activity = I(0, 0x4B);
    r.stance = I(0, 2);
    r.gait = I(0, 5);
    r.carryMode = I(0, 6);
    r.action = I(0, 5);
    r.unknown5C = I(0, 2);
    r.hitFlags = static_cast<uint32_t>(I(0, 255));
    r.killer = P(0.5f) ? g_h.human : nullptr;
    r.workingFrame = P(0.5f) ? g_h.frame : nullptr;
    r.interactTarget = nullptr;
    r.interaction = I(0, 3);
    r.interactArg = I(0, 9);
}

// Footsteps: the ground under the foot (the material, or a ray down), the stealth roll, the step's sound by gait and
// stance, the AI's hearing of it, the dust of remote humans' steps on sand. (Surface marks only where Footstep
// returns before the footprint: the original's footprint kinds are an inline table, its marks a pool of visuals.)
void TestFootsteps()
{
    typedef void(__attribute__((thiscall)) * T_MBool)(void *, int);
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomHumanBase();
        C_HumanMovement *m = g_h.movement;
        const bool left = P(0.5f);
        const bool marks = P(0.5f);
        g_surfaceMarksEnabled = marks ? 1 : 0;
        *OriginalAt<uint32_t *>(0x0098ad60) = g_surfaceMarksEnabled;
        if (marks) {
            if (P(0.5f))
                g_h.work->m_request.stance = HUMAN_STANCE_PRONE;
            else
                m->m_flags |= MOVE_FLAG_TINY;
        }
        if (P(0.3f))
            m->m_flags |= MOVE_FLAG_FULL_DETAIL;
        m->m_groundMaterial = P(0.5f) ? -1 : (P(0.3f) ? (P(0.5f) ? 0x29 : 0x2A) : I(0, kMaterials - 1));
        m->m_footstepSurface = I(-3, 3);
        m->m_footstepCount = I(-1, 12);
        m->m_footstepTime = P(0.2f) ? I(998, 1001) : P(0.5f) ? I(0, 999) : I(1000, 3000);
        m->m_animState.anim.sub = P(0.5f) ? 0 : I(1, 2);
        m->m_visual.m_sector = P(0.5f) ? static_cast<I3D_sector *>(static_cast<void *>(g_sector)) : nullptr;
        g_h.work->m_request.gait = I(0, 6);
        g_firstPerson = P(0.3f);
        g_script.stat = P(0.5f) ? I(0, 10) * 10 : I(0, 100);
        g_h.work->m_request.unknown10 = {U(-1, 1), U(-0.3f, 0.3f), U(-1, 1), 0.0f};
        DiffCall("Footstep", [&] { m->Footstep(left); return 0; },
                 [&](void *self) { OriginalAt<T_MBool>(0x0075d670)(MovementOf(self), left); return 0; });
    }
}

// Picking up and dropping a body: the carrier's side (no body: the body's own model, vehicle and transparency are
// part 4c's).
void TestCarrying()
{
    typedef void(__attribute__((thiscall)) * T_MBody)(void *, void *);
    for (int i = 0; i < 2000 / QUICK; ++i) {
        RandomHumanBase();
        g_equipmentModel[EQUIPMENT_PLACE_HANDS] = P(0.6f) ? g_handsModel : nullptr;
        g_equipmentModel[EQUIPMENT_PLACE_LONG_GUN] = P(0.6f) ? g_longGunModel : nullptr;
        g_handsModel->m_frameFlags = P(0.5f) ? g_handsModel->m_frameFlags | 1u : g_handsModel->m_frameFlags & ~1u;
        g_longGunModel->m_frameFlags = P(0.5f) ? g_longGunModel->m_frameFlags | 1u : g_longGunModel->m_frameFlags & ~1u;
        g_h.work->m_request.carryMode = I(0, 6);
        C_HumanMovement *m = g_h.movement;
        m->m_nextAnimState.carryAnim = I(0x50, 0x5C);
        if (i % 2 == 0)
            DiffCall("OnPickUpBody", [&] { m->OnPickUpBody(nullptr); return 0; },
                     [&](void *self) { OriginalAt<T_MBody>(0x0075e3d0)(MovementOf(self), nullptr); return 0; });
        else
            DiffCall("OnDropBody", [&] { m->OnDropBody(nullptr); return 0; },
                     [&](void *self) { OriginalAt<T_MBody>(0x0075e6f0)(MovementOf(self), nullptr); return 0; });
    }
    g_equipmentModel[EQUIPMENT_PLACE_HANDS] = g_equipmentModel[EQUIPMENT_PLACE_LONG_GUN] = nullptr;
}

void TestBase2()
{
    SetUpBase2();
    TestFootsteps();
    TestCarrying();
    const std::string file = TempFile("request.chk"), file2 = TempFile("request_orig.chk");
    S_ContactActor *ladder = Ladder();
    for (int i = 0; i < 8000 / QUICK; ++i) {
        RandomHumanBase();
        C_HumanMovement *m = g_h.movement;
        g_soundSlot = I(0, 30);
        switch (i % 8) {
        case 0:
            RandomRequest(m->m_request);
            DiffCall("S_MoveRequest::Reset", [&] { m->m_request.Reset(); return 0; },
                     [&](void *self) { OriginalAt<T_RVoid>(0x007801a0)(static_cast<uint8_t *>(MovementOf(self)) + O_REQUEST); return 0; });
            break;
        case 1: {
            // Saved, then loaded back into another record (or a chunk of another id, or nothing open).
            RandomRequest(m->m_request);
            const bool open = P(0.95f);
            std::remove(file.c_str());
            std::remove(file2.c_str());
            DiffCall("S_MoveRequest::SaveGame",
                     [&] {
                         C_chunk c;
                         if (open)
                             c.Open(file.c_str(), true);
                         return int(m->m_request.SaveGame(c));
                     },
                     [&](void *self) {
                         C_chunk c;
                         if (open)
                             c.Open(file2.c_str(), true);
                         return int(OriginalAt<T_RChunk>(0x00780990)(static_cast<uint8_t *>(MovementOf(self)) + O_REQUEST, &c));
                     });
            {
                // (The same bytes but the three pointers first in the record, after the chunk's 6-byte head: each
                // side's own objects. LoadGame clears them.)
                std::vector<uint8_t> a = FileBytes(file), b = FileBytes(file2);
                for (size_t k = 6; k < 18 && k < a.size() && k < b.size(); ++k)
                    a[k] = b[k] = 0;
                CHECK(a == b);
            }
            CHECK(!open || FileBytes(file).size() > sizeof(S_MoveRequest));
            if (P(0.1f)) {
                C_chunk c;
                c.Open(file.c_str(), true);
                c.WriteChunk(0x309, &m->m_request, sizeof(S_MoveRequest));
            }
            RandomRequest(m->m_request);
            const bool readOpen = P(0.97f);
            DiffCall("S_MoveRequest::LoadGame",
                     [&] {
                         C_chunk c;
                         if (readOpen)
                             c.Open(file.c_str(), false);
                         return int(m->m_request.LoadGame(c));
                     },
                     [&](void *self) {
                         C_chunk c;
                         if (readOpen)
                             c.Open(file.c_str(), false);
                         return int(OriginalAt<T_RChunk>(0x00780a90)(static_cast<uint8_t *>(MovementOf(self)) + O_REQUEST, &c));
                     });
            break;
        }
        case 2: {
            void *pose = P(0.8f) ? static_cast<void *>(g_h.pose) : nullptr;
            m->m_owner = nullptr;
            DiffCall("SetOwnerBase", [&] { m->SetOwnerBase(g_h.human, static_cast<C_HumanWeaponPose *>(pose)); return 0; },
                     [&](void *self) { OriginalAt<T_MOwner>(0x0075b260)(MovementOf(self), o_human, pose); return 0; });
            m->m_owner = g_h.human;
            break;
        }
        case 3:
        case 4: {
            const int materials[] = {0x10, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x5A, 0x5D, 0x62, 0x63, 0x6F};
            const int material = materials[I(0, 12)];
            const int time = I(5, 120);
            GetGame()->m_missionTime = static_cast<uint32_t>(I(0, 30) * 100);
            if (P(0.5f))
                m->m_animState.anim.anim = P(0.5f) ? 0xBE : 0x2D;
            if (i % 8 == 3) {
                DiffCall("DamageByMaterial", [&] { m->DamageByMaterial(material, time); return 0; },
                         [&](void *self) { OriginalAt<T_MIntInt>(0x0075f340)(MovementOf(self), material, time); return 0; });
            } else {
                // Contacts with volumes of materials (repeats among them), or no contacts and a volume (or none).
                static S_CollisionVolume volumes[6];
                S_MoveContacts contacts;
                std::memset(&contacts, 0, sizeof contacts);
                contacts.count = I(0, 6);
                for (int k = 0; k < 6; ++k) {
                    volumes[k].flags = static_cast<uint32_t>(materials[I(0, 12)]) << 24;
                    contacts.volumes[k] = &volumes[k];
                }
                const bool none = P(0.3f);
                const S_CollisionVolume *volume = P(0.8f) ? &volumes[I(0, 5)] : nullptr;
                DiffCall("DamageByContacts", [&] { m->DamageByContacts(none ? nullptr : &contacts, volume, time); return 0; },
                         [&](void *self) {
                             OriginalAt<T_MDamage>(0x0075f5e0)(MovementOf(self), none ? nullptr : &contacts, volume, time);
                             return 0;
                         });
            }
            break;
        }
        case 5:
        case 6: {
            // The ladder's sound (the climb sound group: none or one).
            m->m_useObject = P(0.8f) ? reinterpret_cast<C_Actor *>(ladder->port) : nullptr;
            const int group = P(0.2f) ? -1 : I(0, 12);
            reinterpret_cast<C_Ladder *>(ladder->port)->m_climbSound = group;
            std::memcpy(ladder->orig + 0x178, &group, 4);
            m->m_useSound = P(0.5f) ? -1 : I(0, 30);
            if (i % 8 == 5)
                DiffCall("StartUseSound", [&] { m->StartUseSound(); return 0; },
                         [&](void *self) { OriginalAt<T_MVoid>(0x0075f240)(MovementOf(self)); return 0; });
            else
                DiffCall("StopUseSound", [&] { m->StopUseSound(); return 0; },
                         [&](void *self) { OriginalAt<T_MVoid>(0x0075f310)(MovementOf(self)); return 0; });
            break;
        }
        default: {
            // (Not the active player, or not a client: PlaySound leaves the sound vector alone.)
            g_script.activeIsOwner = false;
            const int group = I(-1, 40);
            DiffCall("PlaySound", [&] { m->PlaySound(group); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x0075ebd0)(MovementOf(self), group); return 0; });
            break;
        }
        }
    }
}
}  // namespace
