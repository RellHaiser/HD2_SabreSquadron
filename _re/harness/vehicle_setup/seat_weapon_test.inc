// The seat weapon (src/game/seat_weapon.cpp; part 3a) in the REAL_SEAT_WEAPON build (build_sw.bat;
// included by harness.cpp before main): the real C_SeatWeapon on the car models' seats. Per model,
// every seat is tried (Init fails cleanly where the model has no weapon there); each weapon set up
// is checked against what the frames' names and user properties say, then its accessors, the
// gunner, a shell, the saved game, Stop, Restart, a failed weapon's release, and the network
// messages. check_sw.py compares the weapons' count with the models' and wants every check met.

#include "game/sound_table.h"

// --- what the weapon calls -----------------------------------------------------------------------------

// SetCallback (frame slot 0x28): the callback and messages per frame.
std::map<const I3D_frame *, std::pair<I3D_CALLBACK *, uint32_t>> g_callbacks;
void __stdcall F_SetCallback(I3D_frame *self, I3D_CALLBACK *callback, uint32_t messages)
{
    g_callbacks[self] = {callback, messages};
}

// The frames' data blocks.
void SetFrameData(I3D_frame *frame, const S_FrameData &data)
{
    if (!frame->m_data)
        frame->m_data = new S_FrameData();
    *frame->m_data = data;
}

// The weapon's item: room for C_WeaponItem's fields (SetLoadedRounds writes its rounds there); its
// amount is those rounds, as a loaded weapon's.
class C_FakeWeaponItem : public C_FakeItem {
public:
    float GetAmount() override { return Rounds(); }
    float &Rounds() { return reinterpret_cast<C_WeaponItem *>(this)->m_rounds; }
    // (Part 3b.) Use spends the rounds; Reload answers `reloads` (each asked logged).
    void Use(float amount) override
    {
        uses.push_back(amount);
        Rounds() -= amount;
    }
    bool Reload(C_Actor *owner, int container, int *ammoUid, bool keepLast) override
    {
        reloadAsks.push_back({owner, container, keepLast});
        *ammoUid = 77;
        return reloads;
    }
    struct S_ReloadAsk {
        C_Actor *owner;
        int container;
        bool keepLast;
    };
    std::vector<float> uses;
    std::vector<S_ReloadAsk> reloadAsks;
    bool reloads = true;
    alignas(16) uint8_t room[sizeof(C_WeaponItem)] = {};
};
static_assert(sizeof(C_FakeWeaponItem) >= sizeof(C_WeaponItem), "the fake item holds a weapon item's fields");
// The free items made (each ask; g_failItemId gives none) of g_freeItemDef, and those destroyed
// (kept for the checks).
std::vector<int> g_freeItemAsks;
std::vector<C_FakeWeaponItem *> g_freeItems;
std::vector<C_Item *> g_destroyedItems;
int g_failItemId = -1000;
C_ItemDef *g_freeItemDef;
C_Item *C_ItemsModule::CreateFreeItem(int typeId)
{
    g_freeItemAsks.push_back(typeId);
    if (typeId == g_failItemId)
        return nullptr;
    auto *item = new C_FakeWeaponItem();
    item->m_typeId = typeId;
    item->m_def = g_freeItemDef;
    item->Rounds() = 30.75f;
    g_freeItems.push_back(item);
    return item;
}
void C_ItemsModule::DestroyItem(C_Item *item)
{
    g_destroyedItems.push_back(item);
}
// The item's weapon component (its muzzle flash, its kind).
class C_FakeWeaponComp : public C_WeaponComp {
public:
    C_FakeWeaponComp(E_ItemComponentKind kind, int effect) : C_WeaponComp(kind)
    {
        m_effect = effect;
        m_attackDelay = 87.9f;                          // (part 3b: the delay between shots)
    }
    void Save(int *, void *) override {}
    void Load(int, const void *) override {}
    float GetAimedDispersion() override { return dispersion; }
    int GetReloadSound() override { return reloadSound; }
    int GetReloadTimeMs() override { return reloadTime; }
    float dispersion = 0.0f;
    int reloadSound = 41, reloadTime = 2300;
};

// --- part 3b: the shots, the camera, the rays, the model's animations ---------------------------------

// The player's BuildBasis (game/player_weapons.cpp, not linked here): a copy.
void SwNormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-8f)
        return;
    if (square >= 1e-8f)
        v *= 1.0f / std::sqrt(square);
    else if (v.x != 0.0f)
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    else if (v.z != 0.0f)
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    else
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
}
void BuildBasis(const S_vector &dir, S_vector &side, S_vector &up)
{
    S_vector across(dir.y * 0.0f - dir.z, dir.z * 0.0f - dir.x * 0.0f, dir.x - dir.y * 0.0f);
    SwNormalizeBy(across, (across.x * across.x + across.y * across.y) + across.z * across.z);
    side = across.Cross(dir);
    SwNormalizeBy(side, (side.x * side.x + side.y * side.y) + side.z * side.z);
    up = side.Cross(dir);
    SwNormalizeBy(up, (up.x * up.x + up.y * up.y) + up.z * up.z);
}

// The shot manager's shots.
struct S_SwShot {
    bool projectile;
    C_Actor *shooter;
    C_Item *item;
    const S_vector *pos;
    S_vector posValue, dir;
    I3D_frame *muzzle, *ejector, *projectile3D;
    bool tracer, flySound, cameraFollow;
    uint32_t flags;
    int shellSound, trail;
};
std::vector<S_SwShot> g_swShots;
void C_ShotManager::FireBulletFrom(C_Human *shooter, C_WeaponItem *weapon, const S_vector &pos, const S_vector &dir,
                                   I3D_frame *muzzle, I3D_frame *ejectDummy, bool tracer, uint32_t flags, int shellSound)
{
    g_swShots.push_back({false, shooter, reinterpret_cast<C_Item *>(weapon), &pos, pos, dir, muzzle, ejectDummy, nullptr,
                         tracer, false, false, flags, shellSound, 0});
}
void C_ShotManager::FireProjectile(C_Actor *shooter, C_Item *weapon, const S_vector &pos, const S_vector &dir,
                                   I3D_frame *projectile, int trailEffect, bool flySound, bool cameraFollow, uint32_t flags)
{
    g_swShots.push_back({true, shooter, weapon, &pos, pos, dir, nullptr, nullptr, projectile, false, flySound,
                         cameraFollow, flags, 0, trailEffect});
}
// The weapon item's shot sounds.
struct S_SwShotSound {
    const void *item;
    C_Actor *shooter;
    I3D_frame *where;
    bool quiet;
};
std::vector<S_SwShotSound> g_swShotSounds;
void C_WeaponItem::PlayShotSound(C_Actor *shooter, I3D_frame *where, bool quiet)
{
    g_swShotSounds.push_back({this, shooter, where, quiet});
}

// The model's animations (I3D_model slot 0x84 SetAnimation).
struct S_SwAnimation {
    I3D_frame *model;
    I3D_animation_set *anim;
    int stage;
    float weight;
    int flags;
};
std::vector<S_SwAnimation> g_swAnimations;
void __stdcall F_SetAnimation(I3D_frame *self, I3D_animation_set *anim, int stage, float weight, int flags)
{
    g_swAnimations.push_back({self, anim, stage, weight, flags});
}

// Slot 0x1C as the turret uses it: the frame's local z set to `dir` (logged in g_dirs too).
std::map<const I3D_frame *, int> g_swDirsSet;
void __stdcall F_SwSetDir(I3D_frame *self, const S_vector &dir, float)
{
    g_dirs[self] = dir;
    ++g_swDirsSet[self];
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len == 0.0f)
        return;
    const S_vector z(dir.x / len, dir.y / len, dir.z / len);
    S_vector x = S_vector(0.0f, 1.0f, 0.0f).Cross(z);
    float xl = std::sqrt(x.x * x.x + x.y * x.y + x.z * x.z);
    if (xl < 1e-6f) {
        x = S_vector(1.0f, 0.0f, 0.0f);
        xl = 1.0f;
    }
    x = S_vector(x.x / xl, x.y / xl, x.z / xl);
    const S_vector y = z.Cross(x);
    S_matrix &m = self->m_localMatrix;
    m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = x.z;
    m.m[1][0] = y.x, m.m[1][1] = y.y, m.m[1][2] = y.z;
    m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
    self->m_rot.Make(m);
    self->m_frameFlags |= FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID | FRMFLAGS_ROT_DIRTY;
    self->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
}

// The camera module (the game's GetCameraModule): its place and direction.
S_vector4 g_swCameraPos = {10.0f, 2.0f, -4.0f, 1.0f}, g_swCameraDir = {0.6f, 0.0f, 0.8f, 0.25f};
int g_swCameraAsks;
alignas(16) uint8_t g_swCameraModule[0x100];
C_CameraModule *C_game::GetCameraModule()
{
    return reinterpret_cast<C_CameraModule *>(g_swCameraModule);
}
bool C_CameraModule::GetCameraPos(int index, S_vector4 *pos)
{
    g_swCameraAsks += index == 0;
    *pos = g_swCameraPos;
    return true;
}
bool C_CameraModule::GetCameraDir(int index, S_vector4 *dir)
{
    g_swCameraAsks += index == 0;
    *dir = g_swCameraDir;
    return true;
}
// The collision world's ray (the third-person aim): a hit at g_swRayHit when g_swRayMeets.
struct S_SwRay {
    S_vector from, dir;
    bool normalAsked;
    uint32_t types, flags;
};
std::vector<S_SwRay> g_swRays;
bool g_swRayMeets;
S_vector g_swRayHit;
S_CollisionVolume g_swRayVolume;
S_CollisionVolume *C_CollisionWorld::RayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                             S_vector *hitNormal, uint32_t excludeTypes, uint32_t excludeFlags) const
{
    g_swRays.push_back({from, dir, hitNormal != nullptr, excludeTypes, excludeFlags});
    if (!g_swRayMeets)
        return nullptr;
    *hitPos = g_swRayHit;
    return &g_swRayVolume;
}

// The weapon's message sender (the car's SeatWeaponSendMessage stands in elsewhere).
struct S_SwSent {
    int kind;
    bool hasPos, hasDir;
    S_vector4 pos, dir;
    int barrel, seat;
    C_Actor *car;
};
std::vector<S_SwSent> g_swSent;
bool __fastcall SwSender(int kind, const S_vector4 *pos, const S_vector4 *dir, int barrel, int seat, C_Actor *car)
{
    S_SwSent sent = {kind, pos != nullptr, dir != nullptr, {}, {}, barrel, seat, car};
    if (pos)
        sent.pos = *pos;
    if (dir)
        sent.dir = *dir;
    g_swSent.push_back(sent);
    return true;
}
// The vehicle's impulse callback (ApplyImpulse's last step): the calls.
int g_swImpulses;
void __fastcall SwNoteImpulse(void *)
{
    ++g_swImpulses;
}

// The model cache's shared models (Get): "GunShells" is g_shellsModel (null: none), each Get one
// reference.
std::vector<std::string> g_modelGets;
I3D_frame *g_shellsModel;
template <>
HRESULT C_ResourceCache<I3D_model>::Get(const char *fileName, I3D_model **out, T_CacheErrorProc, void *)
{
    g_modelGets.push_back(fileName);
    *out = nullptr;
    if (!g_shellsModel || std::strcmp(fileName, "GunShells") != 0)
        return I3DERR_NOFILE;
    ++g_shellsModel->m_refCount;
    *out = reinterpret_cast<I3D_model *>(g_shellsModel);
    return I3D_OK;
}

// The animation cache: a set per name (slot 0 Release counted), g_animMissing not found.
struct S_FakeAnim {
    void **vtable;
    std::string name;
    int refs = 0, releases = 0;
};
void *g_animVtable[SLOTS];
uint32_t __stdcall F_AnimRelease(S_FakeAnim *self)
{
    ++self->releases;
    return static_cast<uint32_t>(--self->refs);
}
std::map<std::string, S_FakeAnim *> g_anims;
std::vector<std::string> g_animGets;
std::string g_animMissing;
template <>
C_ResourceCache<I3D_animation_set>::~C_ResourceCache()
{
}
template <>
HRESULT C_ResourceCache<I3D_animation_set>::OpenInto(I3D_animation_set *, const char *, uint32_t, void *, void *, int)
{
    return I3D_OK;
}
template <>
HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *fileName, I3D_animation_set **out, T_CacheErrorProc,
                                                void *)
{
    g_animGets.push_back(fileName);
    *out = nullptr;
    if (g_animMissing == fileName)
        return I3DERR_NOFILE;
    S_FakeAnim *&anim = g_anims[fileName];
    if (!anim)
        anim = new S_FakeAnim{g_animVtable, fileName};
    ++anim->refs;
    *out = reinterpret_cast<I3D_animation_set *>(anim);
    return I3D_OK;
}
I3D_animation_set *C_AnimationCache::Create()
{
    return nullptr;
}
C_AnimationCache g_animationCache;
S_FakeAnim *Anim(const std::string &name)
{
    auto it = g_anims.find(name);
    return it == g_anims.end() ? nullptr : it->second;
}

// The sound table: chapter 3's groups by id (the asks logged).
S_SoundSample::~S_SoundSample() {}
void S_SoundGroup::Clear()
{
    for (S_SoundSample *sample : samples)
        delete sample;
    samples.clear();
}
std::map<int, S_SoundGroup *> g_manipulationGroups;
std::vector<std::pair<int, int>> g_groupAsks;
S_SoundGroup *C_SoundModule::GetGroup(int chapter, int group) const
{
    g_groupAsks.emplace_back(chapter, group);
    if (chapter != SOUND_CHAPTER_WEAPON_MANIPULATION)
        return nullptr;
    auto it = g_manipulationGroups.find(group);
    return it == g_manipulationGroups.end() ? nullptr : it->second;
}
// Group `id`: one sample "sw<id>.wav" of its own volume and ranges (`file` instead when given; no
// sample when `empty`).
S_SoundGroup *MakeGroup(int id, const char *file = nullptr, bool empty = false)
{
    auto *group = new S_SoundGroup();
    if (!empty) {
        auto *sample = new S_SoundSample();
        sample->fileName = file ? std::string(file).c_str() : ("sw" + std::to_string(id) + ".wav").c_str();
        sample->volume = 0.5f + static_cast<float>(id) / 1000.0f;
        sample->nearRange = static_cast<float>(id) / 10.0f;
        sample->farRange = static_cast<float>(id);
        group->samples.push_back(sample);
    }
    return group;
}

// The quantised floats (as bit count -2 each, as the fake streams hold them) and their sender.
int g_senderPlayer;
std::vector<int> g_senderAsks;
std::vector<std::pair<const S_NetPlayer *, uint32_t>> g_quantized;
S_NetPlayer *C_NetMessage::GetSender(int playerIndex) const
{
    g_senderAsks.push_back(playerIndex);
    return reinterpret_cast<S_NetPlayer *>(&g_senderPlayer);
}
void C_NetMessage::WriteQuantized(const float *values, const S_NetPlayer *player, uint8_t mask)
{
    auto *s = reinterpret_cast<S_FakeStream *>(this);
    for (int i = 0; i < 3; ++i) {
        if (mask & (1u << i)) {
            uint32_t bits;
            std::memcpy(&bits, &values[i], sizeof bits);
            s->bits.emplace_back(bits, -2);
        }
    }
    g_quantized.emplace_back(player, mask);
}
void C_NetMessage::ReadQuantized(float *values, const S_NetPlayer *player, uint32_t mask)
{
    for (int i = 0; i < 3; ++i) {
        if (!(mask & (1u << i)))
            continue;
        if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != -2) {
            g_readMismatch = true;
            return;
        }
        const uint32_t bits = g_readBits[g_readPos++].first;
        std::memcpy(&values[i], &bits, sizeof bits);
    }
    g_quantized.emplace_back(player, mask);
}

// --- the tests ------------------------------------------------------------------------------------------

// A check of SeatWeaponTest (CAR_TRACE=1: the failing lines).
void SwExpect(bool &flag, bool ok, int line)
{
    if (!ok && std::getenv("CAR_TRACE"))
        std::printf("  seat weapon check failed: line %d\n", line);
    flag = flag && ok;
}
#define SW(flag, ...) SwExpect(flag, (__VA_ARGS__), __LINE__)

bool SwNear(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}
bool SwNear(const S_vector &a, const S_vector &b, float eps = 1e-5f)
{
    return SwNear(a.x, b.x, eps) && SwNear(a.y, b.y, eps) && SwNear(a.z, b.z, eps);
}
bool SwNear(const S_quat &a, const S_quat &b, float eps = 1e-5f)
{
    return SwNear(a.v, b.v, eps) && SwNear(a.s, b.s, eps);
}
bool SwSameQuat(const S_quat &a, const S_quat &b)
{
    return a.v == b.v && a.s == b.s;
}
S_vector SwXYZ(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}
S_vector Unit(const S_vector &v)
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return S_vector(v.x / len, v.y / len, v.z / len);
}
// The Hamilton product a b.
S_quat Mul(const S_quat &a, const S_quat &b)
{
    S_quat q;
    q.s = a.s * b.s - (a.v.x * b.v.x + a.v.y * b.v.y + a.v.z * b.v.z);
    q.v.x = a.s * b.v.x + b.s * a.v.x + (a.v.y * b.v.z - a.v.z * b.v.y);
    q.v.y = a.s * b.v.y + b.s * a.v.y + (a.v.z * b.v.x - a.v.x * b.v.z);
    q.v.z = a.s * b.v.z + b.s * a.v.z + (a.v.x * b.v.y - a.v.y * b.v.x);
    return q;
}
S_quat AxisAngle(const S_vector &axis, float angle)
{
    S_quat q;
    q.Make(axis, angle);
    return q;
}
S_quat Normalized(S_quat q)
{
    q.Normalize();
    return q;
}

// A user property's number ("<key><n>", the key in any case; -1 without).
int PropInt(const I3D_frame *f, const char *key)
{
    if (!f)
        return -1;
    std::string props = f->GetUserProps(), k = key;
    for (char &c : props)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char &c : k)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const size_t at = props.find(k);
    return at == std::string::npos ? -1 : std::atoi(props.c_str() + at + k.size());
}
I3D_frame *FindFormatted(I3D_frame *root, const char *format, int a, int b, uint32_t mask)
{
    char name[64];
    std::snprintf(name, sizeof name, format, a, b);
    return Find(root, name, mask);
}
bool IsOnFrame(const I3D_frame *f)
{
    return (f->m_frameFlags & 1u) != 0;
}

// The model's file as the weapon sees it (a directory and a second dot) and the animations' base.
std::string g_swFile, g_swBase;

// What a seat's frames say its weapon is.
struct S_SwExpected {
    I3D_frame *hort = nullptr, *vert = nullptr, *aim = nullptr, *view = nullptr, *barrel0 = nullptr;
    int itemId = -1;
    std::vector<I3D_frame *> barrels, shellDummies, carDummies;   // every BARREL<s>_<k>* from 0
    std::vector<std::string> animations;                           // "<model>_f<s>_<k>" each
    int kept = 0;                                                  // the barrels with a shell dummy
    bool ok = false;
};
S_SwExpected ExpectedWeapon(int seat)
{
    constexpr uint32_t VISUALS = ENUMF_WILDMASK | FRAME_MASK_VISUAL, DUMMIES = ENUMF_WILDMASK | FRAME_MASK_DUMMY;
    S_SwExpected e;
    e.hort = FindFormatted(g_model, "HORT%02d*", seat, 0, DUMMIES);
    e.vert = FindFormatted(g_model, "VERT%02d*", seat, 0, DUMMIES);
    e.aim = FindFormatted(g_model, "AIM%02d*", seat, 0, DUMMIES);
    e.view = FindFormatted(g_model, "VIEW%02d", seat, 0, VISUALS);
    e.barrel0 = FindFormatted(g_model, "BARREL%02d_%02d*", seat, 0, VISUALS);
    e.itemId = PropInt(e.barrel0, "itemid");
    const std::string &base = g_swBase;
    for (int k = 0;; ++k) {
        I3D_frame *barrel = FindFormatted(g_model, "BARREL%02d_%02d*", seat, k, VISUALS);
        if (!barrel)
            break;
        e.barrels.push_back(barrel);
        e.shellDummies.push_back(Find(barrel, "SHDUM*", DUMMIES));
        e.carDummies.push_back(Find(barrel, "CARDUM*", DUMMIES));
        char suffix[32];
        std::snprintf(suffix, sizeof suffix, "_f%02d_%02d", seat, k);
        e.animations.push_back(base + suffix);
        e.kept += e.shellDummies.back() != nullptr;
    }
    e.ok = e.itemId >= 0 && e.itemId != g_failItemId && e.hort && e.vert && e.kept > 0;
    return e;
}

// Weapon w's state after an Init of seat `seat` that `e` describes (its item g_freeItems.back()).
void CheckSetUp(bool &init, bool &props, bool &barrels, C_SeatWeapon *w, C_Car *car, I3D_frame *seatFrame,
                int seat, const S_SwExpected &e, uint32_t missionTime, int flash, bool launcher)
{
    // The seat's frame: its parent when that is the seat's HORT (its first 6 characters).
    char hortName[16];
    std::snprintf(hortName, sizeof hortName, "HORT%02d", seat);
    I3D_frame *parent = seatFrame->m_parent;
    I3D_frame *seatExpected = parent && std::strncmp(parent->GetShortName(), hortName, 6) == 0 ? parent : seatFrame;
    SW(init, w->m_started && !w->m_failed && w->m_seat == seat && w->m_owner == car && w->m_model == g_model &&
                 w->m_seatFrame == seatExpected && !w->m_gunner && w->m_delay == 0 && w->m_barrel == 0 &&
                 w->m_flags == 0 && w->m_unknownE8 == 0 && w->m_reloadLeft == 0 && w->m_reloadTime == 0 &&
                 w->m_refillTime == missionTime && w->m_refillBelow == 0 && w->m_tilt == 0.0f && w->m_turn == 0.0f);
    SW(init, w->m_view == e.view && (!e.view || !IsOnFrame(e.view)));
    SW(init, !g_freeItems.empty() && w->m_item == reinterpret_cast<C_WeaponItem *>(g_freeItems.back()) &&
                 g_freeItemAsks.back() == e.itemId && w->m_flashEffect == flash && w->m_launcher == launcher);
    SW(init, w->m_hort == e.hort && w->m_vert == e.vert && w->m_aim == e.aim &&
                 w->m_shells == reinterpret_cast<I3D_model *>(g_shellsModel) && !g_modelGets.empty() &&
                 g_modelGets.back() == "GunShells" && !IsOnFrame(g_shellsModel));
    SW(init, w->m_projectileFrame == Find(g_model, "projectile", FRAME_MASK_VISUAL));
    // The rates (degrees a second; pi/4 without) and limits (degrees; 1e6 without; down and left
    // negated) from the first barrel.
    const auto rate = [](int v) { return v == -1 ? 0.7853982f : static_cast<float>(v) * 0.017453292f; };
    const auto limit = [](int v, float sign) { return v == -1 ? 1e6f : static_cast<float>(v) * (0.017453292f * sign); };
    SW(props, w->m_tiltRate == rate(PropInt(e.barrel0, "vs")) && w->m_turnRate == rate(PropInt(e.barrel0, "hs")) &&
                  w->m_upLimit == limit(PropInt(e.barrel0, "up"), 1.0f) &&
                  w->m_downLimit == limit(PropInt(e.barrel0, "down"), -1.0f) &&
                  w->m_leftLimit == limit(PropInt(e.barrel0, "left"), -1.0f) &&
                  w->m_rightLimit == limit(PropInt(e.barrel0, "right"), 1.0f));
    // The barrels with a shell dummy, in turn, their animation each; their callbacks and data.
    SW(barrels, w->m_barrels.size() == static_cast<size_t>(e.kept));
    size_t kept = 0;
    for (size_t k = 0; k < e.barrels.size(); ++k) {
        I3D_frame *barrel = e.barrels[k];
        if (!e.shellDummies[k])
            continue;
        if (kept >= w->m_barrels.size())
            break;
        const S_SeatWeaponBarrel &b = w->m_barrels[kept++];
        SW(barrels, b.frame == barrel && b.muzzle == e.shellDummies[k] && b.ejector == e.carDummies[k] &&
                        b.animation == reinterpret_cast<I3D_animation_set *>(Anim(e.animations[k])));
        SW(barrels, g_callbacks.count(barrel) && g_callbacks[barrel].first == &C_SeatWeapon::OnBarrelNote &&
                        g_callbacks[barrel].second == I3DCALLBACK_ANIM_NOTE);
        SW(barrels, barrel->m_data && barrel->m_data->unknown1C == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w)));
    }
}

// The weapon's flags and the controller's indices (seat_weapon.cpp's, human_controller.h's).
constexpr uint32_t SEAT_WEAPON_TRAVERSE_PLUS = 0x01, SEAT_WEAPON_TRAVERSE_MINUS = 0x02, SEAT_WEAPON_ELEVATION_UP = 0x04,
                   SEAT_WEAPON_ELEVATION_DOWN = 0x08, SEAT_WEAPON_TURNING = 0x0F, SEAT_WEAPON_RELOADING = 0x20,
                   SEAT_WEAPON_MOTOR_SOUND = 0x400;
constexpr int SW_AXIS_FIRE = 6, SW_AXIS_RELOAD = 13, SW_BUTTON_FIRE = 6;

// --- part 3b's tests ------------------------------------------------------------------------------------

struct S_Sw3b {
    bool update = true, turn = true, view = true, controls = true, fire = true, shots = true, reload = true,
         aim = true;
};

// A frame's local z as the weapon reads it (the matrix's, or the rotation's when the matrix is stale).
S_vector SwLocalZ(const I3D_frame *f)
{
    S_matrix m = f->m_localMatrix;
    if (!(f->m_frameFlags & FRMFLAGS_LMATRIX_VALID)) {
        S_quat q = f->m_rot;
        q.Normalize();
        m.SetRot3(q);
    }
    return Unit(m(2));
}
double SwHeading(const S_vector &d)
{
    return std::atan2(-static_cast<double>(d.x), static_cast<double>(d.z));
}
double SwElevation(const S_vector &d)
{
    return std::atan2(static_cast<double>(d.y), std::sqrt(static_cast<double>(d.x) * d.x + static_cast<double>(d.z) * d.z));
}
float SwWrap(float a)
{
    return a < 0.0f ? a + 6.2831855f : a > 6.2831855f ? a - 6.2831855f : a;
}
bool SwSameAngle(float a, float b, float eps = 1e-4f)
{
    return std::fabs(std::remainder(static_cast<double>(a) - b, 6.283185307179586)) < eps;
}
// The direction of heading `h` and elevation `e` (SwHeading / SwElevation's inverse).
S_vector SwDir(double h, double e)
{
    return S_vector(static_cast<float>(-std::sin(h) * std::cos(e)), static_cast<float>(std::sin(e)),
                    static_cast<float>(std::cos(h) * std::cos(e)));
}
bool SameLog(const std::vector<std::pair<const I3D_frame *, char>> &log, I3D_sound *first, char a, I3D_sound *second,
             char b)
{
    std::vector<std::pair<const I3D_frame *, char>> want;
    if (first)
        want.push_back({first, a});
    if (second)
        want.push_back({second, b});
    return log == want;
}
// SpreadDirection as the original: the same random draws (game/seat_weapon.cpp's 0x004c4440).
S_vector SwSpread(S_vector dir, float spread)
{
    if (spread == 0.0f)
        return dir;
    S_vector side, up;
    BuildBasis(dir, side, up);
    const float a = (RandFloat() - 0.5f) * 2.0f;
    const float b = (RandFloat() - 0.5f) * 2.0f;
    const S_vector across = Unit(S_vector(side.x * b + up.x * a, side.y * b + up.y * a, side.z * b + up.z * a));
    const float scale = spread * 0.01f * RandFloat();
    return Unit(S_vector(dir.x + across.x * scale, dir.y + across.y * scale, dir.z + across.z * scale));
}

void SeatWeapon3bTest(S_Sw3b &r, C_Car *car, I3D_frame *seatFrame, int seat, const S_SwExpected &e, C_FakeWeaponComp &comp,
                      C_FakeDef &def)
{
    C_Human gunner, local;
    local.localPlayer = true;
    auto *w = new C_SeatWeapon();
    if (!w->Init(car, g_model, seatFrame, seat)) {
        SW(r.update, false);
        delete w;
        return;
    }
    auto *item = reinterpret_cast<C_FakeWeaponItem *>(w->m_item);
    C_Vehicle &v = car->m_vehicle;
    v.SetCallback(VEHICLE_CALLBACK_IMPULSE, reinterpret_cast<void *>(&SwNoteImpulse));
    g_gameRole = GAME_ROLE_LOCAL;
    def.m_shellSound = 33;

    // --- Update / Tick: the countdowns; dead or no gunner; the turning, the role's other half, idling.
    {
        w->m_reloadLeft = 50;
        w->Update(20);
        SW(r.update, w->m_reloadLeft == 30);
        w->Update(40);
        SW(r.update, w->m_reloadLeft == 0);
        w->m_gunner = &gunner;
        gunner.health = 0;
        w->m_flags = SEAT_WEAPON_MOTOR_SOUND | 0x3;
        g_soundLog.clear();
        const int views = gunner.viewDirs, setDirs = g_swDirsSet[e.hort] + g_swDirsSet[e.vert];
        w->Update(10);
        SW(r.update, w->m_flags == 0x3 && SameLog(g_soundLog, w->m_motorSound, 'o', w->m_stopSound, 'O') &&
                         gunner.viewDirs == views && g_swDirsSet[e.hort] + g_swDirsSet[e.vert] == setDirs);
        w->m_flags = SEAT_WEAPON_MOTOR_SOUND;
        w->m_delay = 30;
        g_soundLog.clear();
        w->Tick(10);
        SW(r.update, w->m_flags == 0 && w->m_delay == 30 && SameLog(g_soundLog, w->m_motorSound, 'o', w->m_stopSound, 'O'));
        w->Update(10);                                  // (dead, the sound already stopped: nothing)
        SW(r.update, w->m_flags == 0 && g_soundLog.size() <= 2);
        gunner.health = 800;
        // Single player: Tick counts the delay down (the reload's end), Update does not tick.
        w->m_flags = SEAT_WEAPON_RELOADING;
        w->m_reloadLeft = 100;
        w->Tick(20);
        SW(r.update, w->m_delay == 10 && (w->m_flags & SEAT_WEAPON_RELOADING) && w->m_reloadLeft == 100);
        w->Tick(20);
        SW(r.update, w->m_delay == 0 && !(w->m_flags & SEAT_WEAPON_RELOADING));
        w->m_delay = 40;
        gunner.lookDir = Unit(e.vert->GetMatrix()(2));  // (no turn to make)
        w->Update(5);
        SW(r.update, w->m_delay == 40 && w->m_reloadLeft == 95);
        // A server's Tick updates; a client's Update ticks.
        g_gameRole = GAME_ROLE_SERVER;
        w->Tick(5);
        SW(r.update, w->m_reloadLeft == 90 && w->m_delay == 35);
        w->Update(5);
        SW(r.update, w->m_reloadLeft == 85 && w->m_delay == 35);
        g_gameRole = GAME_ROLE_CLIENT;
        w->Update(5);
        SW(r.update, w->m_reloadLeft == 80 && w->m_delay == 30);
        w->Tick(5);
        SW(r.update, w->m_reloadLeft == 80 && w->m_delay == 25);
        g_gameRole = GAME_ROLE_LOCAL;
        // Idling: TurnToView counts it, then Update again while both controls are released.
        w->m_turnReleased = w->m_tiltReleased = true;
        w->m_idleTime = 7;
        w->Update(10);
        SW(r.update, w->m_idleTime == 27 && (w->m_flags & SEAT_WEAPON_TURNING) == 0);
        w->m_tiltReleased = false;
        w->Update(10);
        SW(r.update, w->m_idleTime == 0);
        // A local gunner's turret: TurnByControls; the model updated again when turning.
        w->m_gunner = &local;
        w->m_turnSpeed = w->m_tiltSpeed = 0.0f;
        int modelUpdates = g_frameUpdates[g_model];
        w->m_flags = 0;
        w->Update(10);
        SW(r.update, local.viewDirs == 1 && g_frameUpdates[g_model] == modelUpdates + 1);
        w->m_flags = SEAT_WEAPON_ELEVATION_UP;
        modelUpdates = g_frameUpdates[g_model];
        w->Update(10);
        SW(r.update, local.viewDirs == 2 && g_frameUpdates[g_model] == modelUpdates + 2);
        // Failed: nothing.
        w->m_failed = true;
        w->m_reloadLeft = 5;
        w->Update(10);
        w->Tick(10);
        SW(r.update, local.viewDirs == 2 && w->m_reloadLeft == 0);
        w->m_failed = false;
        w->m_flags = 0;
    }

    // --- TurnByControls: a step of each speed, the limits, the release, the sound, the view.
    {
        w->m_gunner = &local;
        w->m_rightLimit = w->m_leftLimit = 1e6f;
        w->m_upLimit = 1.0f;
        w->m_downLimit = -1.0f;
        w->m_turnReleased = w->m_tiltReleased = false;
        // A stale local matrix: the direction from the rotation.
        e.hort->m_frameFlags &= ~FRMFLAGS_LMATRIX_VALID;
        const S_vector h0 = SwLocalZ(e.hort);
        const double heading0 = SwHeading(h0);
        w->m_turnSpeed = 0.5f;
        w->m_tiltSpeed = 0.0f;
        const int hortUpdates = g_frameUpdates[e.hort], hortDirs = g_swDirsSet[e.hort];
        g_soundLog.clear();
        w->TurnByControls(100);
        const S_vector h1 = SwLocalZ(e.hort);
        SW(r.turn, SwNear(w->m_turn, static_cast<float>(heading0 - 0.05), 1e-4f) &&
                       SwNear(static_cast<float>(SwHeading(h1)), w->m_turn, 1e-4f) && SwNear(h1.y, h0.y, 1e-4f) &&
                       g_frameUpdates[e.hort] == hortUpdates + 2 && g_swDirsSet[e.hort] == hortDirs + 1);
        SW(r.turn, (w->m_flags & SEAT_WEAPON_MOTOR_SOUND) && SameLog(g_soundLog, w->m_stopSound, 'o', w->m_motorSound, 'O') &&
                       w->m_turnSpeed == 0.5f);
        S_vector4 view = local.viewDir;
        SW(r.turn, SwNear(S_vector(view.x, view.y, view.z), Unit(e.vert->GetMatrix()(2))));
        // The limits.
        const float hNow = static_cast<float>(SwHeading(SwLocalZ(e.hort)));
        w->m_rightLimit = hNow + 0.01f;
        w->m_leftLimit = hNow - 0.02f;
        w->m_turnSpeed = -2.0f;
        w->TurnByControls(100);
        SW(r.turn, w->m_turn == w->m_rightLimit);
        w->m_turnSpeed = 3.0f;
        w->TurnByControls(100);
        SW(r.turn, w->m_turn == w->m_leftLimit);
        w->m_rightLimit = w->m_leftLimit = 1e6f;
        // The release slows the traverse by 0.005 a ms.
        w->m_turnReleased = true;
        w->m_turnSpeed = 0.7f;
        w->TurnByControls(100);
        SW(r.turn, SwNear(w->m_turnSpeed, 0.2f, 1e-5f));
        w->TurnByControls(100);
        SW(r.turn, w->m_turnSpeed == 0.0f);
        w->m_turnSpeed = -0.3f;
        w->TurnByControls(40);
        SW(r.turn, SwNear(w->m_turnSpeed, -0.1f, 1e-5f));
        w->TurnByControls(40);
        SW(r.turn, w->m_turnSpeed == 0.0f);
        w->m_turnReleased = false;
        // The elevation along VERT's own heading (turned off its parent's for the test).
        e.vert->SetWorldDir(SwDir(SwHeading(SwLocalZ(e.vert)) + 0.3, SwElevation(SwLocalZ(e.vert))), 0.0f);
        const S_vector v0 = SwLocalZ(e.vert);
        const double vh = SwHeading(v0), el0 = SwElevation(v0);
        w->m_tiltSpeed = -0.4f;
        const int vertDirs = g_swDirsSet[e.vert], vertUpdates = g_frameUpdates[e.vert];
        w->TurnByControls(100);
        SW(r.turn, g_frameUpdates[e.vert] == vertUpdates + 1 && w->m_tiltSpeed == -0.4f);
        const S_vector v1 = SwLocalZ(e.vert);
        SW(r.turn, SwNear(w->m_tilt, static_cast<float>(el0 + 0.04), 1e-4f) &&
                       SwNear(static_cast<float>(SwElevation(v1)), w->m_tilt, 1e-4f) &&
                       SwNear(static_cast<float>(SwHeading(v1)), static_cast<float>(vh), 1e-4f) &&
                       g_swDirsSet[e.vert] == vertDirs + 1);
        const float eNow = static_cast<float>(SwElevation(v1));
        w->m_upLimit = eNow + 0.01f;
        w->m_tiltSpeed = -2.0f;
        w->TurnByControls(100);
        SW(r.turn, w->m_tilt == w->m_upLimit);
        w->m_downLimit = eNow - 0.03f;
        w->m_tiltSpeed = 3.0f;
        w->TurnByControls(100);
        SW(r.turn, w->m_tilt == w->m_downLimit);
        w->m_upLimit = 1e6f;
        w->m_downLimit = -1e6f;
        w->m_tiltSpeed = -100.0f;
        w->TurnByControls(100);
        SW(r.turn, w->m_tilt == 1.5676548f);
        w->m_tiltSpeed = 100.0f;
        w->TurnByControls(100);
        SW(r.turn, w->m_tilt == -1.5676548f);
        w->m_upLimit = 1.0f;
        w->m_downLimit = -1.0f;
        // The release slows the elevation by 0.004 x its rate a ms.
        w->m_tiltReleased = true;
        w->m_tiltRate = 0.5f;
        w->m_tiltSpeed = 0.5f;
        w->TurnByControls(100);
        SW(r.turn, SwNear(w->m_tiltSpeed, 0.3f, 1e-5f));
        w->m_tiltSpeed = -0.1f;
        w->TurnByControls(100);
        SW(r.turn, w->m_tiltSpeed == 0.0f);
        w->m_tiltReleased = false;
        // Not turning: the sound stops after more than 300 ms idle.
        w->m_turnSpeed = w->m_tiltSpeed = 0.0f;
        w->m_flags = SEAT_WEAPON_MOTOR_SOUND;
        w->m_idleTime = 300;
        g_soundLog.clear();
        const int allDirs = g_swDirsSet[e.hort] + g_swDirsSet[e.vert];
        w->TurnByControls(10);
        SW(r.turn, w->m_flags == SEAT_WEAPON_MOTOR_SOUND && g_soundLog.empty() &&
                       g_swDirsSet[e.hort] + g_swDirsSet[e.vert] == allDirs);
        w->m_idleTime = 301;
        w->TurnByControls(10);
        SW(r.turn, w->m_flags == 0 && SameLog(g_soundLog, w->m_motorSound, 'o', w->m_stopSound, 'O'));
        // Turning with the sound on already: nothing more.
        w->m_flags = SEAT_WEAPON_MOTOR_SOUND;
        g_soundLog.clear();
        w->m_turnSpeed = 0.1f;
        w->TurnByControls(10);
        SW(r.turn, w->m_flags == SEAT_WEAPON_MOTOR_SOUND && g_soundLog.empty());
        w->m_turnSpeed = 0.0f;
    }

    // --- TurnToView: a step towards another gunner's view, the flags, idling.
    {
        w->m_gunner = &gunner;
        w->m_rightLimit = w->m_leftLimit = 1e6f;
        w->m_upLimit = 1.0f;
        w->m_downLimit = -1.0f;
        w->m_turnRate = 0.3f;
        w->m_tiltRate = 0.2f;
        for (int k = 0; k < 6; ++k) {
            const S_vector aim = Unit(e.vert->GetMatrix()(2));
            const double dh = k == 0 ? 0.5 : k == 1 ? -0.4 : k == 2 ? 2.5 : k == 4 ? 0.008 : 0.0;
            const double de = k == 0 ? 0.2 : k == 1 ? -0.15 : k == 4 ? -0.006 : 0.0;
            // (k 5: a view across heading 0 and a step longer than the gap, which is measured the long
            // way round there.)
            w->m_turnRate = k == 5 ? 200.0f : 0.3f;
            const double h = k == 5 ? (SwHeading(aim) >= 0.0 ? -0.2 : 0.2) : SwHeading(aim) + dh;
            gunner.lookDir = SwDir(h, SwElevation(aim) + de);
            // The oracle.
            const S_vector look = gunner.lookDir;
            const float aimH = SwWrap(static_cast<float>(SwHeading(aim))), viewH = SwWrap(static_cast<float>(SwHeading(look)));
            const float aimE = static_cast<float>(SwElevation(aim)), viewE = static_cast<float>(SwElevation(look));
            const float gapH = std::fabs(aimH - viewH), gapE = std::fabs(aimE - viewE);
            const S_vector hl = SwLocalZ(e.hort), vl = SwLocalZ(e.vert);
            float stepH = std::min(50 * w->m_turnRate * 0.001f, gapH);
            if (SwWrap(viewH - aimH) < 3.1415927f)
                stepH = -stepH;
            float stepE = std::min(50 * 0.2f * 0.001f, gapE);
            if (aimE < viewE)
                stepE = -stepE;
            const float nextH = static_cast<float>(SwHeading(hl) - stepH);
            const float nextE = static_cast<float>(SwElevation(vl) - stepE);
            uint32_t flags = 0;
            if (gapH > 0.001f && std::fabs(nextH - static_cast<float>(SwHeading(hl))) > 0.001f)
                flags |= nextH > static_cast<float>(SwHeading(hl)) ? SEAT_WEAPON_TRAVERSE_PLUS : SEAT_WEAPON_TRAVERSE_MINUS;
            if (gapE > 0.001f && std::fabs(nextE - static_cast<float>(SwElevation(vl))) > 0.001f)
                flags |= nextE > static_cast<float>(SwElevation(vl)) ? SEAT_WEAPON_ELEVATION_UP : SEAT_WEAPON_ELEVATION_DOWN;
            const float turn0 = w->m_turn, tilt0 = w->m_tilt;
            w->m_idleTime = 40;
            w->m_flags = 0x3 | SEAT_WEAPON_MOTOR_SOUND;
            const int modelUpdates = g_frameUpdates[g_model];
            w->TurnToView(50);
            SW(r.view, (w->m_flags & SEAT_WEAPON_TURNING) == flags && g_frameUpdates[g_model] == modelUpdates + 1 &&
                           w->m_idleTime == (flags ? 0 : 90));
            if (flags & (SEAT_WEAPON_TRAVERSE_PLUS | SEAT_WEAPON_TRAVERSE_MINUS))
                SW(r.view, SwNear(w->m_turn, nextH, 1e-4f) && SwSameAngle(static_cast<float>(SwHeading(SwLocalZ(e.hort))), nextH));
            else
                SW(r.view, w->m_turn == turn0);
            if (flags & (SEAT_WEAPON_ELEVATION_UP | SEAT_WEAPON_ELEVATION_DOWN)) {
                const S_vector vz = SwLocalZ(e.vert);
                SW(r.view, SwNear(w->m_tilt, nextE, 1e-4f) && SwNear(vz.x, 0.0f, 1e-5f) &&
                               SwNear(static_cast<float>(SwElevation(vz)), nextE, 1e-4f));
            } else {
                SW(r.view, w->m_tilt == tilt0);
            }
        }
        w->m_turnRate = 0.3f;
        // Turning starts the sound; idling past 300 ms stops it.
        gunner.lookDir = SwDir(SwHeading(Unit(e.vert->GetMatrix()(2))) + 0.3, 0.0);
        w->m_flags = 0;
        g_soundLog.clear();
        w->TurnToView(50);
        SW(r.view, (w->m_flags & SEAT_WEAPON_MOTOR_SOUND) && SameLog(g_soundLog, w->m_stopSound, 'o', w->m_motorSound, 'O'));
        gunner.lookDir = Unit(e.vert->GetMatrix()(2));
        w->m_idleTime = 260;
        g_soundLog.clear();
        w->TurnToView(40);
        SW(r.view, w->m_idleTime == 300 && (w->m_flags & SEAT_WEAPON_MOTOR_SOUND) && g_soundLog.empty());
        w->TurnToView(1);
        SW(r.view, w->m_idleTime == 301 && !(w->m_flags & SEAT_WEAPON_MOTOR_SOUND) &&
                       SameLog(g_soundLog, w->m_motorSound, 'o', w->m_stopSound, 'O'));
        // The side limits hold the traverse.
        const S_vector hl = SwLocalZ(e.hort);
        w->m_rightLimit = static_cast<float>(SwHeading(hl)) + 0.002f;
        w->m_leftLimit = static_cast<float>(SwHeading(hl)) - 0.002f;
        for (double dh : {0.5, -0.5}) {
            const S_vector hNow = SwLocalZ(e.hort);
            w->m_rightLimit = static_cast<float>(SwHeading(hNow)) + 0.002f;
            w->m_leftLimit = static_cast<float>(SwHeading(hNow)) - 0.002f;
            gunner.lookDir = SwDir(SwHeading(Unit(e.vert->GetMatrix()(2))) + dh, 0.0);
            const int hortUpdates = g_frameUpdates[e.hort];
            w->TurnToView(50);
            SW(r.view, (w->m_turn == w->m_rightLimit || w->m_turn == w->m_leftLimit) &&
                           g_frameUpdates[e.hort] == hortUpdates + 2);
        }
        w->m_rightLimit = w->m_leftLimit = 1e6f;
        // So do the elevation's.
        for (double de : {0.3, -0.3}) {
            const float eNow = static_cast<float>(SwElevation(SwLocalZ(e.vert)));
            w->m_upLimit = eNow + 0.002f;
            w->m_downLimit = eNow - 0.002f;
            const S_vector aim = Unit(e.vert->GetMatrix()(2));
            gunner.lookDir = SwDir(SwHeading(aim), SwElevation(aim) + de);
            const int vertUpdates = g_frameUpdates[e.vert];
            w->TurnToView(50);
            SW(r.view, (w->m_tilt == w->m_upLimit || w->m_tilt == w->m_downLimit) &&
                           g_frameUpdates[e.vert] == vertUpdates + 2);
        }
        w->m_upLimit = 1.0f;
        w->m_downLimit = -1.0f;
    }

    // --- ApplyControls: the speeds, firing, reloading.
    {
        float axes[64] = {}, buttons[64] = {};
        w->m_gunner = nullptr;
        w->m_turnSpeed = 0.25f;
        axes[0x34] = 0.5f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, w->m_turnSpeed == 0.25f);
        w->m_gunner = &gunner;
        w->m_launcher = false;
        w->m_turnRate = w->m_tiltRate = 10.0f;
        axes[0x34] = 0.1f;
        axes[0x35] = -0.2f;
        item->Rounds() = 5.0f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, SwNear(w->m_turnSpeed, 1.5f, 1e-6f) && SwNear(w->m_tiltSpeed, -3.0f, 1e-6f) &&
                           !w->m_turnReleased && !w->m_tiltReleased);
        w->m_turnRate = 1.0f;
        w->m_tiltRate = 2.0f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, w->m_turnSpeed == 1.0f && w->m_tiltSpeed == -2.0f);
        axes[0x34] = -0.3f;
        axes[0x35] = 0.5f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, w->m_turnSpeed == -1.0f && w->m_tiltSpeed == 2.0f);
        axes[0x34] = axes[0x35] = 0.0f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, w->m_turnSpeed == 0.0f && w->m_tiltSpeed == 0.0f && w->m_turnReleased && w->m_tiltReleased);
        // Each released on its own.
        axes[0x34] = 0.2f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, !w->m_turnReleased && w->m_tiltReleased);
        axes[0x34] = 0.0f;
        axes[0x35] = 0.2f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, w->m_turnReleased && !w->m_tiltReleased);
        axes[0x35] = 0.0f;
        w->ApplyControls(axes, buttons);
        // A launcher's controls add up while held.
        w->m_launcher = true;
        w->m_turnSpeed = 0.2f;
        w->m_tiltSpeed = -0.1f;
        axes[0x34] = 0.1f;
        axes[0x35] = 0.3f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, SwNear(w->m_turnSpeed, 0.3f, 1e-6f) && SwNear(w->m_tiltSpeed, 0.2f, 1e-6f));
        axes[0x34] = axes[0x35] = 0.0f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, SwNear(w->m_turnSpeed, 0.3f, 1e-6f) && w->m_turnReleased && w->m_tiltReleased);
        w->m_turnSpeed = 1.6f;
        w->m_turnRate = 1.5f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, w->m_turnSpeed == 1.5f);
        w->m_launcher = false;
        w->m_turnSpeed = w->m_tiltSpeed = 0.0f;
        // An empty weapon: the trigger reloads it (from the owner's container 0; an enemy keeps his
        // last magazine).
        const auto ready = [&]() {
            w->m_delay = 0;
            w->m_flags = 0;
            g_swShots.clear();
            item->reloadAsks.clear();
        };
        ready();
        item->Rounds() = 0.0f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, item->reloadAsks.empty() && g_swShots.empty());
        buttons[SW_BUTTON_FIRE] = 1.0f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, item->reloadAsks.size() == 1 && item->reloadAsks[0].owner == car &&
                           item->reloadAsks[0].container == 0 && item->reloadAsks[0].keepLast && g_swShots.empty());
        // Loaded: the trigger or the fire axis fires.
        ready();
        item->Rounds() = 5.0f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, g_swShots.size() == 1 && item->reloadAsks.empty());
        buttons[SW_BUTTON_FIRE] = 0.0f;
        axes[SW_AXIS_FIRE] = 1.0f;
        ready();
        w->ApplyControls(axes, buttons);
        SW(r.controls, g_swShots.size() == 1 && item->reloadAsks.empty());
        // The last round: reloaded when every seat is taken.
        std::vector<C_Actor *> occupants;
        for (auto &s : v.m_seats)
            occupants.push_back(s.occupant);
        std::vector<C_Human> crew(v.m_seats.size());
        for (size_t i = 0; i < v.m_seats.size(); ++i)
            v.m_seats[i].occupant = &crew[i];
        ready();
        item->Rounds() = 1.0f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, g_swShots.size() == 1 && item->reloadAsks.size() == 1);
        if (!v.m_seats.empty()) {
            v.m_seats[0].occupant = nullptr;
            ready();
            item->Rounds() = 1.0f;
            w->ApplyControls(axes, buttons);
            SW(r.controls, g_swShots.size() == 1 && item->reloadAsks.empty());
        }
        for (size_t i = 0; i < v.m_seats.size(); ++i)
            v.m_seats[i].occupant = occupants[i];
        axes[SW_AXIS_FIRE] = 0.0f;
        // The reload axis.
        ready();
        item->Rounds() = 3.0f;
        axes[SW_AXIS_RELOAD] = 1.0f;
        w->ApplyControls(axes, buttons);
        SW(r.controls, item->reloadAsks.size() == 1 && g_swShots.empty());
        axes[SW_AXIS_RELOAD] = 0.0f;
    }

    // --- Fire: along the muzzle; the spread; the shot, its animation, sound, message, delay, flash; the
    // barrels in turn; the disguise and the noise; a launcher's projectile and recoil; the third
    // person's aim.
    {
        w->m_gunner = &gunner;
        w->m_launcher = false;
        w->SetMessageSender(&SwSender, car);
        comp.dispersion = 0.0f;
        g_playerTeam.insert(&gunner);
        const auto fire = [&]() {
            w->m_delay = 0;
            g_swShots.clear();
            g_swShotSounds.clear();
            g_swSent.clear();
            g_swAnimations.clear();
            g_particlesMade.clear();
            item->uses.clear();
            if (item->Rounds() < 2.0f)
                item->Rounds() = 10.0f;
            w->Fire();
        };
        item->Rounds() = 10.0f;
        w->m_barrel = 0;
        const int disguises = gunner.disguises, noises = gunner.noises;
        const int modelUpdates = g_frameUpdates[g_model];
        fire();
        I3D_frame *muzzle = w->m_barrels[0].muzzle;
        const S_vector dir = Unit(muzzle->GetMatrix()(2));
        SW(r.fire, g_swShots.size() == 1);
        if (g_swShots.size() == 1) {
            const S_SwShot &shot = g_swShots[0];
            SW(r.fire, !shot.projectile && shot.shooter == &gunner && shot.item == item &&
                           shot.pos == &muzzle->GetMatrix()(3) && shot.posValue == muzzle->GetMatrix()(3) &&
                           SwNear(shot.dir, dir) && shot.muzzle == muzzle && shot.ejector == w->m_barrels[0].ejector &&
                           shot.tracer && shot.flags == 0 && shot.shellSound == 33);
        }
        const bool animated = w->m_barrels[0].animation != nullptr;
        SW(r.fire, animated ? g_swAnimations.size() == 1 && g_swAnimations[0].model == g_model &&
                                  g_swAnimations[0].anim == w->m_barrels[0].animation && g_swAnimations[0].stage == 0 &&
                                  g_swAnimations[0].weight == 1.0f && g_swAnimations[0].flags == 0 &&
                                  g_frameUpdates[g_model] == modelUpdates + 1
                            : g_swAnimations.empty());
        SW(r.fire, g_swShotSounds.size() == 1 && g_swShotSounds[0].item == item && g_swShotSounds[0].shooter == &gunner &&
                       g_swShotSounds[0].where == muzzle && !g_swShotSounds[0].quiet);
        SW(r.fire, g_swSent.size() == 1 && g_swSent[0].kind == SEAT_WEAPON_MSG_SHOT && g_swSent[0].hasPos &&
                       g_swSent[0].hasDir && g_swSent[0].barrel == 0 && g_swSent[0].seat == seat && g_swSent[0].car == car &&
                       S_vector(g_swSent[0].pos.x, g_swSent[0].pos.y, g_swSent[0].pos.z) == muzzle->GetMatrix()(3) &&
                       g_swSent[0].pos.w == muzzle->GetMatrix().m[3][3] &&
                       SwNear(S_vector(g_swSent[0].dir.x, g_swSent[0].dir.y, g_swSent[0].dir.z), dir));
        SW(r.fire, w->m_delay == 87 && item->uses == std::vector<float>{1.0f} && item->Rounds() == 9.0f &&
                       g_particlesMade.size() == 1 && g_particlesMade[0].effect == w->m_flashEffect &&
                       g_particlesMade[0].parent == muzzle && g_particlesMade[0].faceCamera &&
                       !g_particlesMade[0].transient &&
                       std::memcmp(&g_particlesMade[0].matrix, &muzzle->GetMatrix(), sizeof(S_matrix)) == 0);
        SW(r.fire, w->m_barrel == (w->m_barrels.size() > 1 ? 1 : 0) && gunner.disguises == disguises + 1 &&
                       gunner.disguise == -10.0f && gunner.noises == noises + 1);
        // The barrels in turn, back to the first.
        for (size_t k = 1; k < w->m_barrels.size(); ++k) {
            fire();
            SW(r.fire, g_swShots.size() == 1 && g_swShots[0].muzzle == w->m_barrels[k].muzzle && g_swSent.size() == 1 &&
                           g_swSent[0].barrel == static_cast<int>(k) && g_particlesMade.size() == 1 &&
                           g_particlesMade[0].parent == w->m_barrels[k].muzzle);
        }
        SW(r.fire, w->m_barrel == 0);
        // Not ready: the delay, no rounds, no gunner.
        g_swShots.clear();
        w->m_delay = 1;
        w->Fire();
        item->Rounds() = 0.0f;
        w->m_delay = 0;
        w->Fire();
        item->Rounds() = 5.0f;
        w->m_gunner = nullptr;
        w->Fire();
        w->m_gunner = &gunner;
        SW(r.fire, g_swShots.empty());
        // Not of the player's team, a network game: no disguise; a server: no shot sound.
        g_playerTeam.erase(&gunner);
        int before = gunner.disguises;
        fire();
        SW(r.fire, gunner.disguises == before);
        g_playerTeam.insert(&gunner);
        g_gameRole = GAME_ROLE_SERVER;
        before = gunner.disguises;
        fire();
        SW(r.fire, gunner.disguises == before && g_swShotSounds.empty() && g_swShots.size() == 1);
        g_gameRole = GAME_ROLE_LOCAL;
        // No sender: nothing sent.
        w->m_sender = nullptr;
        fire();
        SW(r.fire, g_swSent.empty() && g_swShots.size() == 1);
        w->SetMessageSender(&SwSender, car);
        // The spread: the item's dispersion, the same draws.
        w->m_barrel = 0;
        comp.dispersion = 40.0f;
        std::srand(4321);
        const S_vector spread = SwSpread(dir, 40.0f);
        std::srand(4321);
        fire();
        SW(r.fire, g_swShots.size() == 1 && SwNear(g_swShots[0].dir, spread, 1e-4f) && !SwNear(spread, dir, 1e-3f));
        comp.dispersion = 0.0f;
        // A launcher: the projectile (the camera follows it), the recoil on the car (only a wake-up:
        // at the lever origin, no push).
        w->m_launcher = true;
        w->m_barrel = 0;
        const int impulses = g_swImpulses;
        v.m_unknown22C0 = 0;
        fire();
        SW(r.fire, g_swShots.size() == 1 && g_swShots[0].projectile && g_swShots[0].projectile3D == w->m_projectileFrame &&
                       g_swShots[0].trail == -1 && g_swShots[0].flySound && g_swShots[0].cameraFollow &&
                       g_swShots[0].flags == 0 && g_swShots[0].shooter == &gunner && g_swShotSounds.size() == 1 &&
                       g_swImpulses == impulses + 1 && v.m_unknown22C0 == 1);
        w->m_launcher = false;
        // A local player in third person: at the camera's aim.
        w->m_gunner = &local;
        g_firstPerson = false;
        g_swRays.clear();
        g_swRayMeets = false;
        w->m_barrel = 0;
        fire();
        const S_vector4 &cd = g_swCameraDir, &cp = g_swCameraPos;
        SW(r.fire, g_swRays.size() == 1 && g_swRays[0].from == S_vector(cd.x * 5.0f + cp.x, cp.y + cd.y * 5.0f, cp.z + cd.z * 5.0f) &&
                       g_swRays[0].dir == S_vector(cd.x * 500.0f, cd.y * 500.0f, cd.z * 500.0f) && !g_swRays[0].normalAsked &&
                       g_swRays[0].types == 0x208001 && g_swRays[0].flags == 0xB00000 && g_swShots.size() == 1 &&
                       g_swShots[0].dir == S_vector(cd.x, cd.y, cd.z) && g_swSent.size() == 1 && g_swSent[0].dir.w == cd.w);
        g_swRayMeets = true;
        g_swRayHit = muzzle->GetMatrix()(3) + S_vector(10.0f, 1.0f, -2.0f);
        w->m_barrel = 0;
        fire();
        SW(r.fire, g_swShots.size() == 1 && SwNear(g_swShots[0].dir, Unit(S_vector(10.0f, 1.0f, -2.0f))));
        g_swRayHit = muzzle->GetMatrix()(3) + S_vector(1.0f, 1.0f, 1.0f);
        w->m_barrel = 0;
        fire();
        SW(r.fire, g_swShots.size() == 1 && SwNear(g_swShots[0].dir, dir));
        g_swRayMeets = false;
        g_firstPerson = true;
        g_swRays.clear();
        w->m_barrel = 0;
        fire();
        SW(r.fire, g_swRays.empty() && g_swShots.size() == 1 && SwNear(g_swShots[0].dir, dir));
        // Another gunner in third person: along the muzzle.
        w->m_gunner = &gunner;
        g_firstPerson = false;
        w->m_barrel = 0;
        fire();
        SW(r.fire, g_swRays.empty() && g_swShots.size() == 1 && SwNear(g_swShots[0].dir, dir));
        g_firstPerson = true;
        g_playerTeam.erase(&gunner);
    }

    // --- FireShot (the server firing a client's shot) and PlayShot (a client showing another's).
    {
        const S_vector4 pos = {3.0f, 4.0f, 5.0f, 1.0f}, dir = {0.0f, 0.6f, 0.8f, 0.0f};
        const auto clear = [&]() {
            g_swShots.clear();
            g_swShotSounds.clear();
            g_particlesMade.clear();
            item->uses.clear();
        };
        clear();
        item->Rounds() = 0.0f;
        SW(r.shots, !w->FireShot(&pos, &dir, 0, 1000) && g_swShots.empty());
        item->Rounds() = 5.0f;
        w->m_gunner = nullptr;
        SW(r.shots, !w->FireShot(&pos, &dir, 0, 1000) && g_swShots.empty());
        w->m_gunner = &gunner;
        const int last = static_cast<int>(w->m_barrels.size()) - 1;
        SW(r.shots, !w->FireShot(&pos, &dir, last + 1, 1000) && !w->FireShot(&pos, &dir, -1, 1000) && g_swShots.empty());
        w->m_barrel = 0;
        SW(r.shots, w->FireShot(&pos, &dir, last, 7));
        I3D_frame *muzzle = w->m_barrels[last].muzzle;
        SW(r.shots, g_swShots.size() == 1 && !g_swShots[0].projectile && g_swShots[0].posValue == S_vector(3.0f, 4.0f, 5.0f) &&
                        g_swShots[0].dir == S_vector(0.0f, 0.6f, 0.8f) && g_swShots[0].muzzle == muzzle &&
                        g_swShots[0].ejector == w->m_barrels[last].ejector && g_particlesMade.size() == 1 &&
                        g_particlesMade[0].parent == muzzle && item->uses == std::vector<float>{1.0f} && w->m_barrel == 0 &&
                        g_swShotSounds.size() == 1 && g_swShotSounds[0].where == muzzle);
        w->m_launcher = true;
        clear();
        const int impulses = g_swImpulses;
        SW(r.shots, w->FireShot(&pos, &dir, 0, 1000) && g_swShots.size() == 1 && g_swShots[0].projectile &&
                        !g_swShots[0].cameraFollow && g_swImpulses == impulses + 1);
        // PlayShot: from the weapon's own next barrel along its muzzle, whatever the message's.
        clear();
        w->m_barrel = 0;
        w->PlayShot(last);
        I3D_frame *first = w->m_barrels[0].muzzle;
        SW(r.shots, g_swShots.size() == 1 && g_swShots[0].posValue == first->GetMatrix()(3) &&
                        g_swShots[0].pos != &first->GetMatrix()(3) && SwNear(g_swShots[0].dir, Unit(first->GetMatrix()(2))) &&
                        g_swShots[0].projectile && !g_swShots[0].cameraFollow && g_particlesMade.size() == 1 &&
                        g_particlesMade[0].parent == first && item->uses.size() == 1 && g_swImpulses == impulses + 2 &&
                        w->m_barrel == (w->m_barrels.size() > 1 ? 1 : 0) && g_swShotSounds.size() == 1 &&
                        g_swShotSounds[0].where == first);
        w->m_launcher = false;
        clear();
        w->m_barrel = 0;
        w->PlayShot(last);
        SW(r.shots, g_swShots.size() == 1 && g_swShots[0].muzzle == first && g_swShots[0].ejector == w->m_barrels[0].ejector &&
                        g_swShotSounds.size() == 1 && g_swShotSounds[0].where == first);
        clear();
        w->m_gunner = nullptr;
        w->PlayShot(0);
        SW(r.shots, g_swShots.empty());
        w->m_gunner = &gunner;
        // A server's own shots make no sound.
        g_gameRole = GAME_ROLE_SERVER;
        clear();
        w->m_barrel = 0;
        w->FireShot(&pos, &dir, 0, 1000);
        w->PlayShot(0);
        SW(r.shots, g_swShots.size() == 2 && g_swShotSounds.empty());
        g_gameRole = GAME_ROLE_LOCAL;
    }

    // --- The reloads: the player's (StartReload), the server's (Reload), a client's (ShowReload).
    {
        C_ItemContainer *store = car->GetContainer(0);
        const auto clear = [&]() {
            item->reloadAsks.clear();
            g_soundRequests.clear();
            g_swSent.clear();
            w->m_delay = w->m_reloadLeft = w->m_reloadTime = 0;
            w->m_flags = 0;
        };
        clear();
        w->m_flags = SEAT_WEAPON_RELOADING;
        w->StartReload(store);
        w->Reload(store);
        w->ShowReload(store);
        SW(r.reload, item->reloadAsks.empty() && w->m_delay == 0);
        clear();
        item->reloads = false;
        w->StartReload(store);
        w->Reload(store);
        SW(r.reload, item->reloadAsks.size() == 2 && w->m_flags == 0 && w->m_delay == 0 && g_soundRequests.empty() &&
                         g_swSent.empty());
        item->reloads = true;
        gunner.role = HUMAN_ROLE_PLAYER;
        clear();
        w->StartReload(nullptr);
        SW(r.reload, item->reloadAsks.size() == 1 && item->reloadAsks[0].owner == car &&
                         item->reloadAsks[0].container == 0 && !item->reloadAsks[0].keepLast &&
                         w->m_flags == SEAT_WEAPON_RELOADING && w->m_delay == 2300 && w->m_reloadLeft == 2300 &&
                         w->m_reloadTime == 2300);
        SW(r.reload, g_soundRequests.size() == 1 && g_soundRequests[0].flags == SOUND_SILENT_TO_AI &&
                         g_soundRequests[0].attachTo == w->m_barrels[0].frame && g_soundRequests[0].chapter == 3 &&
                         g_soundRequests[0].group == 41 && g_soundRequests[0].pos == S_vector(0.0f, 0.0f, 0.0f) &&
                         !g_soundRequests[0].source);
        SW(r.reload, g_swSent.size() == 1 && g_swSent[0].kind == SEAT_WEAPON_MSG_RELOAD && !g_swSent[0].hasPos &&
                         !g_swSent[0].hasDir && g_swSent[0].barrel == 0 && g_swSent[0].seat == seat && g_swSent[0].car == car);
        gunner.role = HUMAN_ROLE_ENEMY;
        g_gameRole = GAME_ROLE_SERVER;
        clear();
        w->StartReload(store);
        SW(r.reload, item->reloadAsks.size() == 1 && item->reloadAsks[0].keepLast && g_soundRequests.empty() &&
                         g_swSent.size() == 1);
        clear();
        w->Reload(store);
        SW(r.reload, item->reloadAsks.size() == 1 && item->reloadAsks[0].keepLast && w->m_flags == SEAT_WEAPON_RELOADING &&
                         w->m_delay == 2300 && w->m_reloadLeft == 0 && w->m_reloadTime == 0 && g_soundRequests.empty() &&
                         g_swSent.empty());
        // (Fix) no gunner: not an enemy's.
        clear();
        w->m_gunner = nullptr;
        w->Reload(store);
        w->m_flags = 0;
        w->ShowReload(store);
        SW(r.reload, item->reloadAsks.size() == 2 && !item->reloadAsks[0].keepLast && !item->reloadAsks[1].keepLast);
        w->m_gunner = &gunner;
        g_gameRole = GAME_ROLE_CLIENT;
        clear();
        item->reloads = false;
        w->ShowReload(store);
        SW(r.reload, item->reloadAsks.size() == 1 && item->reloadAsks[0].keepLast && w->m_flags == SEAT_WEAPON_RELOADING &&
                         w->m_delay == 2300 && w->m_reloadLeft == 2300 && w->m_reloadTime == 2300 &&
                         g_soundRequests.size() == 1 && g_soundRequests[0].group == 41 && g_swSent.empty());
        item->reloads = true;
        g_gameRole = GAME_ROLE_LOCAL;
        clear();
    }

    // --- CanAim: within the limits (strictly), from the barrel's place.
    {
        const S_vector at = e.vert->GetMatrix()(3);
        const S_vector aim = Unit(e.vert->GetMatrix()(2));
        const double h = SwHeading(aim), el = SwElevation(aim);
        const auto target = [&](double dh, double de) { return at + SwDir(h + dh, el + de) * 20.0f; };
        w->m_rightLimit = 0.5f;
        w->m_leftLimit = -0.5f;
        w->m_upLimit = 0.3f;
        w->m_downLimit = -0.3f;
        w->m_turn = 0.0f;
        w->m_tilt = 0.0f;
        S_vector t = target(0.0, 0.0);
        SW(r.aim, w->CanAim(&t.x) && w->CanTurnTo(t) && w->CanTiltTo(t));
        // The traverse: the angle signed by VERT's x axis, from the turn.
        const S_matrix &m = e.vert->GetMatrix();
        const S_vector side = Unit(S_vector(m.m[0][0], 0.0f, m.m[0][2]));
        for (double dh : {0.3, -0.3, 0.7, -0.7}) {
            t = target(dh, 0.0);
            const S_vector to = Unit(S_vector(t.x - at.x, 0.0f, t.z - at.z));
            const float sign = side.x * to.x + side.z * to.z < 0.0f ? -1.0f : 1.0f;
            const float angle = sign * static_cast<float>(std::fabs(dh));
            for (float turn : {0.0f, 0.35f, -0.35f}) {
                w->m_turn = turn;
                const float total = angle + turn;
                if (std::fabs(std::fabs(total) - 0.5f) > 1e-3f)
                    SW(r.aim, w->CanTurnTo(t) == (total < 0.5f && total > -0.5f));
            }
        }
        w->m_turn = 0.0f;
        w->m_rightLimit = 1e6f;
        t = target(2.5, 0.0);
        SW(r.aim, w->CanTurnTo(t));
        t = target(-2.5, 0.0);
        SW(r.aim, w->CanTurnTo(t));
        w->m_rightLimit = 0.5f;
        // The elevation, less the tilt.
        for (double de : {0.2, -0.2, 0.4, -0.4}) {
            t = target(0.0, de);
            for (float tilt : {0.0f, 0.15f, -0.15f}) {
                w->m_tilt = tilt;
                const float total = static_cast<float>(de) + tilt;
                if (std::fabs(std::fabs(total) - 0.3f) > 1e-3f)
                    SW(r.aim, w->CanTiltTo(t) == (total < 0.3f && total > -0.3f) &&
                                  w->CanAim(&t.x) == (total < 0.3f && total > -0.3f));
            }
        }
        w->m_tilt = 0.0f;
        t = target(0.0, 0.0);
        w->m_failed = true;
        SW(r.aim, !w->CanAim(&t.x));
        w->m_failed = false;
        t = target(0.7, 0.0);
        SW(r.aim, !w->CanAim(&t.x) == !w->CanTurnTo(t));
    }

    w->Stop();
    delete w;
}

void SeatWeaponTest(const char *path)
{
    if (!LoadModel(path))
        return;
    // (The shells the last model threw were its frames, freed by LoadModel: off the sector.)
    g_sector->m_firstChild = nullptr;
    std::printf("== %s\n", g_modelFile.c_str());
    g_swBase = g_modelFile.substr(0, g_modelFile.rfind('.')) + ".v2";
    g_swFile = "MODELS.HD2\\" + g_swBase + g_modelFile.substr(g_modelFile.rfind('.'));
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(g_model) + 0x134) = g_swFile.c_str();
    g_frameVtable[0x28 / 4] = reinterpret_cast<void *>(&F_SetCallback);
    g_frameVtable[0x4C / 4] = reinterpret_cast<void *>(&F_Duplicate);
    g_frameVtable[0x84 / 4] = reinterpret_cast<void *>(&F_SetAnimation);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_SwSetDir);
    FillTraps(g_animVtable, std::make_integer_sequence<int, SLOTS>());
    g_animVtable[0] = reinterpret_cast<void *>(&F_AnimRelease);
    g_countUpdates = true;
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    const int n = static_cast<int>(v.m_seats.size());
    car->SetNetId(55);
    g_gameRole = GAME_ROLE_LOCAL;
    GetGame()->m_scene = g_carScene;
    g_callbacks.clear();
    g_freeItemAsks.clear();
    g_freeItems.clear();
    g_destroyedItems.clear();
    g_failItemId = -1000;
    g_modelGets.clear();
    g_animGets.clear();
    g_animMissing.clear();
    g_groupAsks.clear();
    g_physicsMade.clear();
    g_physicsPushes.clear();
    g_duplicates.clear();
    g_netFindNone = false;
    // The shells model: "GunShells" with its "SHELL" visual.
    const size_t shellsFrom = g_frames.size();
    g_shellsModel = NewFrame(FRAME_MODEL, "GunShells", nullptr);
    I3D_frame *shellSource = NewFrame(FRAME_VISUAL, "GunShells.SHELL", g_shellsModel);
    FinishFrames(shellsFrom, g_shellsModel);
    // The item: a firearm's component (its flash effect), its text.
    C_FakeDef def(61);
    def.m_textId = 4321;
    C_FakeWeaponComp firearm(ICK_FIREARM, 0x6A), launcher(ICK_PROJECTILE_WEAPON, 0x2B);
    def.m_component = &firearm;
    g_freeItemDef = &def;
    // Chapter 3's groups: one for every id the model's HORT frames name.
    for (auto &g : g_manipulationGroups)
        delete g.second;
    g_manipulationGroups.clear();
    for (int s = 0; s < 10; ++s) {
        I3D_frame *hort = FindFormatted(g_model, "HORT%02d*", s, 0, ENUMF_WILDMASK | FRAME_MASK_DUMMY);
        for (const char *key : {"soundm", "sounds"}) {
            const int id = PropInt(hort, key);
            if (id >= 0 && !g_manipulationGroups.count(id))
                g_manipulationGroups[id] = MakeGroup(id);
        }
    }
    C_Human local, other;
    local.localPlayer = true;
    other.localPlayer = false;

    bool ctor = true, init = true, props = true, barrels = true, rest = true, sounds = true, again = true;
    bool accessors = true, gunner = true, shell = true, save = true, load = true, stop = true, restart = true;
    bool release = true, failed = true, messages = true;
    S_Sw3b r3b;
    int weapons = 0, failures = 0;
    bool firstWeapon = true;
    for (int seat = 0; seat < n; ++seat) {
        I3D_frame *seatFrame = v.m_seats[seat].frame;
        S_SwExpected e = ExpectedWeapon(seat);
        if (e.ok && firstWeapon) {
            // The first weapon of the model gets two barrels more after its last: one without a shell
            // dummy (left out) and one with (kept), an AIM dummy, a "projectile" visual (no shipped car
            // has them), and its first barrel a data block of its own.
            const size_t from = g_frames.size();
            const int k = static_cast<int>(e.barrels.size());
            const std::string prefix = "car.BARREL" + std::string(seat < 10 ? "0" : "") + std::to_string(seat) + "_";
            const auto two = [](int x) { return (x < 10 ? "0" : "") + std::to_string(x); };
            NewFrame(FRAME_VISUAL, prefix + two(k), e.vert);
            I3D_frame *last = NewFrame(FRAME_VISUAL, prefix + two(k + 1), e.vert);
            NewFrame(FRAME_DUMMY, "car.SHDUMx", last);
            NewFrame(FRAME_DUMMY, "car.AIM" + two(seat), e.vert);
            NewFrame(FRAME_VISUAL, "car.projectile", e.vert);
            FinishFrames(from, g_model);
            S_FrameData data = {};
            data.actor = car;
            data.physicsObject = nullptr;
            SetFrameData(e.barrel0, data);
            e = ExpectedWeapon(seat);
        }
        // The turret turned before Init: HORT's rotation as it is, VERT's from its matrix.
        S_quat hortRot0, vertRot0;
        if (e.hort && e.vert) {
            e.hort->m_rot = Normalized(AxisAngle(S_vector(0.0f, 1.0f, 0.0f), 0.4f + 0.05f * static_cast<float>(seat)));
            e.hort->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
            S_matrix m = e.vert->m_localMatrix;
            const float c = std::cos(0.25f), s = std::sin(0.25f);
            m.m[0][0] = 1.0f, m.m[0][1] = 0.0f, m.m[0][2] = 0.0f;
            m.m[1][0] = 0.0f, m.m[1][1] = c, m.m[1][2] = s;
            m.m[2][0] = 0.0f, m.m[2][1] = -s, m.m[2][2] = c;
            e.vert->m_localMatrix = m;
            e.vert->m_frameFlags &= ~FRMFLAGS_ROT_DIRTY;
            hortRot0 = e.hort->m_rot;
            vertRot0.Make(m);
        }
        if (e.view)
            e.view->m_frameFlags |= 1u;
        g_shellsModel->m_frameFlags |= 1u;

        auto *w = new C_SeatWeapon();
        // The constructor's defaults.
        bool zeros = true;
        for (uint8_t b : w->m_unknown60)
            zeros = zeros && b == 0;
        SW(ctor, zeros && w->m_barrels.empty() && w->m_tiltRate == 0.3926991f && w->m_turnRate == 0.3926991f &&
                     !w->m_shells && !w->m_vert && !w->m_hort && !w->m_aim && !w->m_started && !w->m_sender &&
                     !w->m_senderContext && !w->m_stopSound && !w->m_motorSound && w->m_turnSpeed == 0.0f &&
                     w->m_tiltSpeed == 0.0f && !w->m_tiltReleased && !w->m_turnReleased && w->m_idleTime == 0 &&
                     !w->m_launcher && !w->m_item && w->m_rightLimit == -1.0f && w->m_leftLimit == -1.0f &&
                     w->m_downLimit == -1.0f && w->m_upLimit == -1.0f && w->m_seat == -1);
        // Init over junk.
        w->m_delay = 7, w->m_barrel = 3, w->m_flags = 0xFFFF, w->m_unknownE8 = 5, w->m_reloadLeft = 9;
        w->m_reloadTime = 8, w->m_refillBelow = 4, w->m_gunner = &other, w->m_tilt = 1.5f, w->m_turn = -2.0f;
        w->m_failed = true;
        const uint32_t missionTime = 40000u + static_cast<uint32_t>(seat);
        GetGame()->m_missionTime = missionTime;
        const size_t asks0 = g_freeItemAsks.size(), anims0 = g_animGets.size(), groups0 = g_groupAsks.size();
        const int hortUpdates0 = e.hort ? g_frameUpdates[e.hort] : 0, vertUpdates0 = e.vert ? g_frameUpdates[e.vert] : 0;
        const int viewUpdates0 = e.view ? g_frameUpdates[e.view] : 0;
        const bool ok = w->Init(car, g_model, seatFrame, seat);
        SW(init, ok == e.ok);
        if (ok != e.ok) {
            // (Nothing more to compare with.)
            if (ok)
                w->Stop();
            delete w;
            continue;
        }
        // The animations asked for: every barrel's, in turn (none without the item).
        std::vector<std::string> asked(g_animGets.begin() + static_cast<long>(anims0), g_animGets.end());
        if (e.itemId >= 0)
            SW(barrels, asked == e.animations);
        else
            SW(barrels, asked.empty());
        // The barrels left out keep no reference to their animation (part 3a's fix).
        for (size_t k = 0; k < e.barrels.size(); ++k)
            if (!e.shellDummies[k] && Anim(e.animations[k]))
                SW(barrels, Anim(e.animations[k])->refs == 0);
        if (!ok) {
            ++failures;
            // Failed: the item made only from an "itemid"; the turret's frames looked for only then.
            SW(failed, w->m_failed && w->m_started && w->m_seat == seat);
            if (e.itemId < 0) {
                SW(failed, g_freeItemAsks.size() == asks0 && !w->m_item && !w->m_hort && !w->m_vert && !w->m_shells);
            } else {
                SW(failed, g_freeItemAsks.size() == asks0 + 1 && w->m_item && w->m_hort == e.hort &&
                               w->m_vert == e.vert && w->m_shells == reinterpret_cast<I3D_model *>(g_shellsModel));
            }
            // Deleted as the car's StartMission does: what it holds released (part 3a's fix).
            C_WeaponItem *item = w->m_item;
            const int shellRefs = g_shellsModel->m_refCount;
            std::vector<S_FakeAnim *> held;
            for (const S_SeatWeaponBarrel &b : w->m_barrels)
                held.push_back(reinterpret_cast<S_FakeAnim *>(b.animation));
            const bool hadShells = w->m_shells != nullptr;
            const size_t destroyed0 = g_destroyedItems.size();
            delete w;
            SW(release, g_destroyedItems.size() == destroyed0 + (item ? 1 : 0) &&
                            (!item || g_destroyedItems.back() == reinterpret_cast<C_Item *>(item)) &&
                            g_shellsModel->m_refCount == shellRefs - (hadShells ? 1 : 0));
            for (S_FakeAnim *a : held)
                SW(release, !a || a->refs == 0);
            continue;
        }
        ++weapons;
        CheckSetUp(init, props, barrels, w, car, seatFrame, seat, e, missionTime, 0x6A, false);
        SW(init, !e.view || g_frameUpdates[e.view] > viewUpdates0);
        if (firstWeapon) {
            // The first barrel's own data block kept, the weapon put in; the others' made.
            SW(barrels, e.barrel0->m_data->actor == car && e.barrel0->m_data->physicsObject == nullptr);
            for (const S_SeatWeaponBarrel &b : w->m_barrels)
                if (b.frame != e.barrel0)
                    SW(barrels, b.frame->m_data->physicsObject == g_noPhysicsObject && !b.frame->m_data->actor &&
                                    !b.frame->m_data->script && !b.frame->m_data->versionMaster);
            SW(barrels, e.barrels.size() >= 3 && !e.shellDummies[e.barrels.size() - 2] &&
                            e.shellDummies.back() && w->m_barrels.back().frame == e.barrels.back());
        }
        // The turret's rest: VERT's rotation made from its matrix, HORT's as it was; both updated.
        {
            S_vector4 axis;
            float angle;
            vertRot0.Inverse(reinterpret_cast<S_vector &>(axis), angle);
            SW(rest, w->m_vertRestAxis.x == axis.x && w->m_vertRestAxis.y == axis.y && w->m_vertRestAxis.z == axis.z &&
                         w->m_vertRestAngle == angle && SwSameQuat(e.vert->m_rot, vertRot0) &&
                         (e.vert->m_frameFlags & FRMFLAGS_ROT_DIRTY));
            hortRot0.Inverse(reinterpret_cast<S_vector &>(axis), angle);
            SW(rest, w->m_hortRestAxis.x == axis.x && w->m_hortRestAxis.y == axis.y && w->m_hortRestAxis.z == axis.z &&
                         w->m_hortRestAngle == angle && SwSameQuat(e.hort->m_rot, hortRot0));
            SW(rest, SwNear(AxisAngle(SwXYZ(w->m_hortRestAxis), w->m_hortRestAngle), hortRot0) &&
                         g_frameUpdates[e.hort] > hortUpdates0 && g_frameUpdates[e.vert] > vertUpdates0);
        }
        // The sounds: HORT's "soundm" (looping) and "sounds" from chapter 3, linked to it, off.
        const int motorId = PropInt(e.hort, "soundm"), stopId = PropInt(e.hort, "sounds");
        const auto soundOk = [&](I3D_sound *sound, int id, bool loop) {
            if (id < 0)
                return !sound;
            const S_SoundSample *sample = g_manipulationGroups[id]->samples[0];
            if (!sound)
                return false;
            const S_FakeSound &f = g_sounds[sound];
            return f.file == "sw" + std::to_string(id) + ".wav" && f.type == I3DSOUND_POINT &&
                   f.cone[0] == 1.5707964f && f.cone[1] == 1.5707964f && f.loop == loop &&
                   f.volume == sample->volume && f.range[0] == sample->nearRange && f.range[1] == sample->farRange &&
                   f.range[2] == 0.5f && f.range[3] == 0.5f && sound->m_parent == e.hort && !IsOnFrame(sound) &&
                   (sound->m_frameFlags & FRMFLAGS_CHANGED) && sound->m_localMatrix(3) == S_vector(0.0f, 0.0f, 0.0f);
        };
        SW(sounds, soundOk(w->m_motorSound, motorId, true) && soundOk(w->m_stopSound, stopId, false));
        for (size_t i = groups0; i < g_groupAsks.size(); ++i)
            SW(sounds, g_groupAsks[i].first == SOUND_CHAPTER_WEAPON_MANIPULATION);

        // Init again while set up: refused, nothing changed.
        {
            const size_t asks = g_freeItemAsks.size();
            C_WeaponItem *item = w->m_item;
            SW(again, !w->Init(car, g_model, seatFrame, seat) && g_freeItemAsks.size() == asks && w->m_item == item &&
                          w->m_started && !w->m_failed);
            C_SeatWeapon fresh;
            SW(again, !fresh.Init(car, nullptr, seatFrame, seat) && !fresh.Init(car, g_model, nullptr, seat) &&
                          !fresh.m_started && !fresh.m_failed && fresh.m_seat == -1 && g_freeItemAsks.size() == asks);
            // (ShowView does nothing before Init.)
            if (e.view) {
                fresh.m_view = e.view;
                fresh.ShowView(true);
                SW(again, !IsOnFrame(e.view));
                fresh.m_view = nullptr;
            }
        }

        // The accessors.
        {
            SW(accessors, w->GetItemDef() == &def && w->GetItemText() == 4321 && w->UnknownAC() == 0.0f);
            SW(accessors, w->GetOrigin() == e.vert->GetMatrix()(3) && SwNear(w->GetAim(), Unit(e.vert->GetMatrix()(2))));
            // GetAim's normalisation from a world matrix of the test's.
            const S_matrix saved = e.vert->m_worldMatrix;
            e.vert->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
            const auto aimOf = [&](float x, float y, float z) {
                e.vert->m_worldMatrix.m[2][0] = x, e.vert->m_worldMatrix.m[2][1] = y, e.vert->m_worldMatrix.m[2][2] = z;
                return w->GetAim();
            };
            SW(accessors, aimOf(0.0f, 3.0f, 4.0f) == S_vector(0.0f, 0.6f, 0.8f) &&
                              aimOf(0.0f, 1.0f, 0.0f) == S_vector(0.0f, 1.0f, 0.0f) &&
                              aimOf(0.0f, 0.0f, 0.0f) == S_vector(0.0f, 1.0f, 0.0f) &&
                              aimOf(0.0f, -1e-5f, 0.0f) == S_vector(0.0f, -1.0f, 0.0f) &&
                              aimOf(-2e-5f, 1e-5f, 0.0f) == S_vector(-1.0f, 1e-5f, 0.0f) &&
                              aimOf(0.0f, 1e-5f, 2e-5f) == S_vector(0.0f, 1e-5f, 1.0f) &&
                              aimOf(0.0f, 1e-5f, -2e-5f) == S_vector(0.0f, 1e-5f, -1.0f) &&
                              aimOf(3e-5f, 0.0f, 0.0f) == S_vector(1.0f, 0.0f, 0.0f) &&
                              aimOf(2e-5f, 0.0f, -1e-5f) == S_vector(1.0f, 0.0f, -1e-5f) &&
                              aimOf(0.0f, 1.0f, 1e-5f) == S_vector(0.0f, 1.0f, 1e-5f) &&
                              SwNear(aimOf(0.0f, 0.6f, 0.824f), Unit(S_vector(0.0f, 0.6f, 0.824f))) &&
                              !SwNear(aimOf(0.0f, 0.6f, 0.824f), S_vector(0.0f, 0.6f, 0.824f)));
            e.vert->m_worldMatrix.m[3][0] = 7.0f, e.vert->m_worldMatrix.m[3][1] = -8.0f, e.vert->m_worldMatrix.m[3][2] = 9.0f;
            SW(accessors, w->GetOrigin() == S_vector(7.0f, -8.0f, 9.0f));
            e.vert->m_worldMatrix = saved;
            e.vert->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            // Failed: none of them.
            w->m_failed = true;
            SW(accessors, !w->GetItemDef() && w->GetItemText() == -1 && w->GetOrigin() == S_vector(0.0f, 0.0f, 0.0f) &&
                              w->GetAim() == S_vector(0.0f, 0.0f, 0.0f));
            w->m_failed = false;
            // The rounds (the item's amount, truncated; set as loaded rounds), empty.
            auto *item = reinterpret_cast<C_FakeWeaponItem *>(w->m_item);
            SW(accessors, w->GetRounds() == 30 && !w->IsEmpty());
            item->Rounds() = -2.5f;
            SW(accessors, w->GetRounds() == -2 && !w->IsEmpty());
            item->Rounds() = 0.0f;
            SW(accessors, w->GetRounds() == 0 && w->IsEmpty());
            item->Rounds() = 0.5f;
            SW(accessors, w->GetRounds() == 0 && !w->IsEmpty());
            w->SetRounds(17);
            SW(accessors, item->Rounds() == 17.0f && w->GetRounds() == 17);
            // The reload's progress.
            int left = -5, total = -6, text = -7;
            SW(accessors, !w->UnknownC8(&left, &total, &text) && left == -5 && total == -6 && text == -7);
            w->m_reloadLeft = 1500, w->m_reloadTime = 3000;
            SW(accessors, w->UnknownC8(&left, &total, &text) && left == 1500 && total == 3000 && text == 0x1B9F);
            w->m_reloadLeft = 0, w->m_reloadTime = 0;
            // The limits, the refill, the sender.
            float up, down, l, r;
            w->GetLimits(&up, &down, &l, &r);
            SW(accessors, up == w->m_upLimit && down == w->m_downLimit && l == w->m_leftLimit && r == w->m_rightLimit);
            w->SetRefillTime(77u);
            w->SetRefillBelow(12);
            w->SetMessageSender(&SeatWeaponSendMessage, car);
            SW(accessors, w->GetRefillTime() == 77u && w->m_refillTime == 77u && w->GetRefillBelow() == 12 &&
                              w->m_refillBelow == 12 && w->m_sender == &SeatWeaponSendMessage && w->m_senderContext == car);
        }

        // The gunner: the first-person parts shown to a local player in first person only; the turning
        // sound stopped.
        {
            const auto viewOn = [&]() { return e.view && IsOnFrame(e.view); };
            g_firstPerson = true;
            w->SetGunner(&local);
            SW(gunner, w->m_gunner == &local && viewOn() == (e.view != nullptr));
            g_firstPerson = false;
            w->SetGunner(&local);
            SW(gunner, w->m_gunner == &local && !viewOn());
            g_firstPerson = true;
            w->SetGunner(&other);
            SW(gunner, w->m_gunner == &other && !viewOn());
            w->SetGunner(&local);
            w->SetGunner(nullptr);
            SW(gunner, !w->m_gunner && !viewOn());
            g_soundLog.clear();
            w->m_flags = 0x21;
            w->SetGunner(&other);
            SW(gunner, w->m_flags == 0x21 && g_soundLog.empty());
            w->m_flags = 0x421;
            if (w->m_motorSound)
                F_SoundSetOn(w->m_motorSound, true);
            g_soundLog.clear();
            w->SetGunner(&other);
            std::vector<std::pair<const I3D_frame *, char>> log;
            if (w->m_motorSound)
                log.push_back({w->m_motorSound, 'o'});
            if (w->m_stopSound)
                log.push_back({w->m_stopSound, 'O'});
            SW(gunner, w->m_flags == 0x21 && g_soundLog == log && (!w->m_motorSound || !IsOnFrame(w->m_motorSound)));
            if (w->m_stopSound)
                F_SetOn(w->m_stopSound, false);
            w->m_flags = 0;
            w->SetGunner(nullptr);
        }

        // A shell: the first barrel's "S" note.
        {
            I3D_frame *barrel = w->m_barrels[0].frame;
            I3D_CALLBACK *note = g_callbacks.count(barrel) ? g_callbacks[barrel].first : nullptr;
            SW(shell, note != nullptr);
            if (note) {
                std::srand(1000 + static_cast<unsigned>(seat));
                float r[4];
                for (float &x : r)
                    x = RandFloat();
                std::srand(1000 + static_cast<unsigned>(seat));
                // (The barrel's world matrix stretched and skewed: the directions are normalised.)
                const S_matrix worldSaved = barrel->m_worldMatrix;
                const uint32_t flagsSaved = barrel->m_frameFlags;
                S_matrix stretched = barrel->GetMatrix();
                for (int c = 0; c < 3; ++c) {
                    stretched.m[1][c] *= 1.7f;
                    stretched.m[2][c] *= 0.6f;
                }
                stretched.m[2][0] += 0.2f;
                stretched.m[1][2] -= 0.3f;
                barrel->m_worldMatrix = stretched;
                barrel->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
                const S_matrix m = barrel->GetMatrix();
                const S_vector dir = Unit(m(2));
                S_vector up = m(1);
                up.x *= r[0] * 0.5f + 1.0f;
                up.y *= r[1] + 1.0f;
                up.z *= r[2] * 0.5f + 1.0f;
                up = Unit(up) * 3.0f;
                const S_vector at = m(3) + up * r[3];
                const size_t frames0 = g_frames.size(), made0 = g_physicsMade.size(), pushes0 = g_physicsPushes.size();
                const size_t sectors0 = g_sectorFrames.size();
                SW(shell, note(barrel, I3DCALLBACK_ANIM_NOTE, reinterpret_cast<uintptr_t>("Shell"), 0) == 0);
                SW(shell, g_frames.size() == frames0 + 1 && g_physicsMade.size() == made0 + 1 &&
                              g_physicsPushes.size() == pushes0 + 1 && g_sectorFrames.size() == sectors0 + 1);
                if (g_frames.size() == frames0 + 1 && g_physicsMade.size() == made0 + 1 &&
                    g_physicsPushes.size() == pushes0 + 1 && g_sectorFrames.size() == sectors0 + 1) {
                    I3D_frame *copy = g_frames.back()->frame;
                    const S_SectorUpdate &sector = g_sectorFrames.back();
                    SW(shell, g_duplicates[copy] == shellSource && copy->m_parent == g_sector && sector.frame == copy &&
                                  sector.pos == sector.at);
                    SW(shell, SwNear(copy->m_localMatrix(3), at) && SwNear(g_dirs[copy], dir) && IsOnFrame(copy) &&
                                  g_frameUpdates[copy] == 1 && copy->m_refCount == 0);
                    const S_PhysicsMade &made = g_physicsMade.back();
                    SW(shell, made.frame == copy && made.type == PHYS_SIMPLE && !made.callback && made.context == w &&
                                  made.material == 0xC && !made.unknown14 && !made.collideWithBoxes && made.startActive &&
                                  made.shootThrough && made.unknown18 == 0 && !made.freeSwing && !made.tree &&
                                  !made.breakable && made.angle == 1.5707964f && made.radius == -1.0f);
                    const S_PhysicsPush &push = g_physicsPushes.back();
                    SW(shell, push.object == made.handle && SwNear(push.impulse, up) &&
                                  push.point == S_vector(0.0f, 0.0f, 0.0f) && push.factor == 0.05f);
                }
                // Other notes, a frame without the weapon, no shells model: nothing.
                const size_t frames1 = g_frames.size();
                note(barrel, I3DCALLBACK_ANIM_NOTE, reinterpret_cast<uintptr_t>("xS"), 0);
                note(barrel, I3DCALLBACK_ANIM_NOTE, 0, 0);
                note(shellSource, I3DCALLBACK_ANIM_NOTE, reinterpret_cast<uintptr_t>("S"), 0);
                note(nullptr, I3DCALLBACK_ANIM_NOTE, reinterpret_cast<uintptr_t>("S"), 0);
                I3D_model *shells = w->m_shells;
                w->m_shells = nullptr;
                note(barrel, I3DCALLBACK_ANIM_NOTE, reinterpret_cast<uintptr_t>("S"), 0);
                w->m_shells = shells;
                SW(shell, g_frames.size() == frames1 && g_physicsMade.size() == made0 + 1);
                barrel->m_worldMatrix = worldSaved;
                barrel->m_frameFlags = flagsSaved;
            }
        }

        // The saved game: chunk 2000 the state, 0x7D1 the rounds; loaded into another weapon of the seat.
        {
            w->m_tilt = 0.31f, w->m_turn = -0.52f, w->m_flags = 0x25, w->m_barrel = 1, w->m_delay = 120;
            w->m_reloadLeft = 800, w->m_reloadTime = 2500;
            reinterpret_cast<C_FakeWeaponItem *>(w->m_item)->Rounds() = 41.5f;
            C_chunk out;
            out.Open("sw.sav", true);
            out.Begin(0x2711);
            out.Write(seat);
            SW(save, w->SaveGame(out));
            out.Ascend();
            out.Begin(2000);                            // (a weapon's state after the seat's chunk)
            out.Write(9.5f);
            out.Ascend();
            out.Close();
            const std::vector<uint8_t> &bytes = g_files["sw.sav"];
            const auto u16 = [&](size_t at) { uint16_t x = 0; if (at + 2 <= bytes.size()) std::memcpy(&x, &bytes[at], 2); return x; };
            const auto u32 = [&](size_t at) { uint32_t x = 0; if (at + 4 <= bytes.size()) std::memcpy(&x, &bytes[at], 4); return x; };
            const auto f32 = [&](size_t at) { float x = 0.0f; if (at + 4 <= bytes.size()) std::memcpy(&x, &bytes[at], 4); return x; };
            SW(save, bytes.size() == 64 && u16(0) == 0x2711 && u32(2) == 54 && u32(6) == static_cast<uint32_t>(seat) &&
                         u16(10) == 2000 && u32(12) == 34 && f32(16) == 0.31f && f32(20) == -0.52f && u32(24) == 0x25 &&
                         u32(28) == 1 && u32(32) == 120 && u32(36) == 800 && u32(40) == 2500 && u16(44) == 0x7D1 &&
                         u32(46) == 10 && f32(50) == 41.5f);

            e.hort->SetRot(AxisAngle(Unit(S_vector(0.3f, 1.0f, 0.2f)), 0.7f));
            e.vert->SetRot(AxisAngle(Unit(S_vector(1.0f, 0.4f, -0.2f)), -0.45f));
            auto *w2 = new C_SeatWeapon();
            w2->Init(car, g_model, seatFrame, seat);
            const S_quat hortAt = e.hort->m_rot, vertAt = e.vert->m_rot;
            const int hortUpdates = g_frameUpdates[e.hort], vertUpdates = g_frameUpdates[e.vert];
            const int aimUpdates = e.aim ? g_frameUpdates[e.aim] : 0, modelUpdates = g_frameUpdates[g_model];
            C_chunk in;
            in.Open("sw.sav", false);
            uint16_t id = 0;
            int savedSeat = -1;
            in.Descend(id);
            in.Read(savedSeat);
            SW(load, w2->LoadGame(in));
            in.Ascend();
            uint16_t next = 0;
            SW(load, in.Descend(next) && next == 2000);
            in.Close();
            SW(load, w2->m_tilt == 0.31f && w2->m_turn == -0.52f && w2->m_flags == 0x25 && w2->m_barrel == 1 &&
                         w2->m_delay == 120 && w2->m_reloadLeft == 800 && w2->m_reloadTime == 2500 &&
                         reinterpret_cast<C_FakeWeaponItem *>(w2->m_item)->Rounds() == 41.5f);
            // The turret turned by them from where it was: HORT about -y, VERT about x; AIM as VERT.
            SW(load, SwNear(e.hort->m_rot, Normalized(Mul(hortAt, AxisAngle(S_vector(0.0f, -1.0f, 0.0f), -0.52f)))) &&
                         SwNear(e.vert->m_rot, Normalized(Mul(vertAt, AxisAngle(S_vector(1.0f, 0.0f, 0.0f), 0.31f)))) &&
                         (!e.aim || SwNear(e.aim->m_rot, e.vert->m_rot)) && g_frameUpdates[e.hort] == hortUpdates + 1 &&
                         g_frameUpdates[e.vert] == vertUpdates + 1 && (!e.aim || g_frameUpdates[e.aim] == aimUpdates + 1) &&
                         g_frameUpdates[g_model] == modelUpdates + 1);
            // An unknown chunk passed over, a short rounds chunk (the rounds kept: part 3a's fix), a short
            // state chunk (only the tilt read).
            C_chunk odd;
            odd.Open("sw2.sav", true);
            odd.Begin(0x2711);
            odd.Write(seat);
            odd.Begin(1234);
            odd.Write(5);
            odd.Ascend();
            odd.Begin(0x7D1);
            odd.Ascend();
            odd.Begin(2000);
            odd.Write(0.77f);
            odd.Ascend();
            odd.Ascend();
            odd.Close();
            w2->m_turn = 9.0f, w2->m_flags = 0x77, w2->m_delay = 3;
            reinterpret_cast<C_FakeWeaponItem *>(w2->m_item)->Rounds() = 3.25f;
            in.Open("sw2.sav", false);
            in.Descend(id);
            in.Read(savedSeat);
            SW(load, w2->LoadGame(in) && in.RemainingSize() == 0);
            in.Ascend();
            in.Close();
            SW(load, w2->m_tilt == 0.77f && w2->m_turn == 9.0f && w2->m_flags == 0x77 && w2->m_delay == 3 &&
                         reinterpret_cast<C_FakeWeaponItem *>(w2->m_item)->Rounds() == 3.25f);
            w2->Stop();
            delete w2;
            // (The turret back where it was for w.)
            e.hort->SetRot(hortAt);
            e.vert->SetRot(vertAt);
        }

        // Stop: the turret back at rest, everything released; a second Stop does nothing.
        {
            e.hort->SetRot(AxisAngle(S_vector(0.0f, 1.0f, 0.0f), 1.0f));
            e.vert->SetRot(AxisAngle(S_vector(1.0f, 0.0f, 0.0f), -0.6f));
            const S_quat hortRest = Normalized(AxisAngle(SwXYZ(w->m_hortRestAxis), w->m_hortRestAngle));
            const S_quat vertRest = Normalized(AxisAngle(SwXYZ(w->m_vertRestAxis), w->m_vertRestAngle));
            const int hortUpdates = g_frameUpdates[e.hort], vertUpdates = g_frameUpdates[e.vert];
            const int modelUpdates = g_frameUpdates[g_model];
            C_WeaponItem *item = w->m_item;
            std::vector<S_FakeAnim *> held;
            for (const S_SeatWeaponBarrel &b : w->m_barrels)
                held.push_back(reinterpret_cast<S_FakeAnim *>(b.animation));
            const int shellRefs = g_shellsModel->m_refCount;
            I3D_sound *motor = w->m_motorSound, *stopSound = w->m_stopSound;
            const size_t destroyed0 = g_destroyedItems.size();
            e.hort->m_frameFlags |= 0x134u;
            e.vert->m_frameFlags |= 0x134u;
            w->Stop();
            const uint32_t set = FRMFLAGS_CHANGED | FRMFLAGS_ROT_DIRTY;
            SW(stop, !w->m_started && SwSameQuat(e.hort->m_rot, hortRest) && SwSameQuat(e.vert->m_rot, vertRest) &&
                         (e.hort->m_frameFlags & set) == set && (e.vert->m_frameFlags & set) == set &&
                         !(e.hort->m_frameFlags & 0x134u) && !(e.vert->m_frameFlags & 0x134u) &&
                         g_frameUpdates[e.hort] == hortUpdates + 1 && g_frameUpdates[e.vert] == vertUpdates + 1 &&
                         g_frameUpdates[g_model] == modelUpdates + 1);
            SW(stop, g_destroyedItems.size() == destroyed0 + 1 && g_destroyedItems.back() == reinterpret_cast<C_Item *>(item) &&
                         !w->m_item && w->m_barrels.empty() && g_shellsModel->m_refCount == shellRefs - 1 && !w->m_shells &&
                         !w->m_hort && !w->m_vert && !w->m_aim && !w->m_motorSound && !w->m_stopSound &&
                         (!motor || motor->m_refCount == 0) && (!stopSound || stopSound->m_refCount == 0));
            for (S_FakeAnim *a : held)
                SW(stop, a && a->refs == 0);
            w->Stop();
            SW(stop, g_destroyedItems.size() == destroyed0 + 1 && g_frameUpdates[g_model] == modelUpdates + 1 &&
                         g_shellsModel->m_refCount == shellRefs - 1);
            delete w;
            SW(stop, g_destroyedItems.size() == destroyed0 + 1 && g_shellsModel->m_refCount == shellRefs - 1);
        }

        // Restart: Stop, Init as before, the gunner kept (a local player in first person: the view on).
        {
            auto *w3 = new C_SeatWeapon();
            w3->Init(car, g_model, seatFrame, seat);
            g_firstPerson = true;
            w3->SetGunner(&local);
            C_WeaponItem *item = w3->m_item;
            const size_t asks = g_freeItemAsks.size(), destroyed0 = g_destroyedItems.size();
            I3D_frame *seatOf = w3->m_seatFrame;
            w3->Restart();
            SW(restart, g_freeItemAsks.size() == asks + 1 && g_destroyedItems.size() == destroyed0 + 1 &&
                            g_destroyedItems.back() == reinterpret_cast<C_Item *>(item) && w3->m_item != item &&
                            w3->m_started && !w3->m_failed && w3->m_gunner == &local && w3->m_seatFrame == seatOf &&
                            w3->m_hort == e.hort && w3->m_barrels.size() == static_cast<size_t>(e.kept) &&
                            (!e.view || IsOnFrame(e.view)));
            w3->Stop();
            delete w3;
        }

        // The item's component: none (no flash, part 3a's fix), a launcher's; no item.
        {
            auto *w4 = new C_SeatWeapon();
            def.m_component = nullptr;
            SW(failed, w4->Init(car, g_model, seatFrame, seat) && w4->m_flashEffect == 0 && !w4->m_launcher);
            w4->Stop();
            def.m_component = &launcher;
            const size_t gets = g_modelGets.size();
            SW(failed, w4->Init(car, g_model, seatFrame, seat));
            CheckSetUp(init, props, barrels, w4, car, seatFrame, seat, e, missionTime, 0x2B, true);
            SW(failed, g_modelGets.size() == gets + 1);
            w4->Stop();
            def.m_component = &firearm;
            delete w4;
            auto *w5 = new C_SeatWeapon();
            g_failItemId = e.itemId;
            const size_t asks = g_freeItemAsks.size(), anims = g_animGets.size(), gets5 = g_modelGets.size();
            SW(failed, !w5->Init(car, g_model, seatFrame, seat) && w5->m_failed && !w5->m_item && !w5->m_hort &&
                           g_freeItemAsks.size() == asks + 1 && g_animGets.size() == anims && g_modelGets.size() == gets5);
            g_failItemId = -1000;
            delete w5;
        }

        if (firstWeapon) {
            // The first barrel's properties: only the item (the defaults), and in capitals.
            const char *props0 = e.barrel0->m_userProps;
            const std::string bare = "itemid" + std::to_string(e.itemId);
            const std::string capitals = "ITEMID" + std::to_string(e.itemId) + ",UP5,DOWN6,LEFT7,RIGHT8,HS9,VS10";
            for (const std::string *text : {&bare, &capitals}) {
                e.barrel0->m_userProps = text->c_str();
                auto *w8 = new C_SeatWeapon();
                SW(props, w8->Init(car, g_model, seatFrame, seat));
                CheckSetUp(init, props, barrels, w8, car, seatFrame, seat, e, missionTime, 0x6A, false);
                SW(props, text == &bare ? w8->m_tiltRate == 0.7853982f && w8->m_upLimit == 1e6f && w8->m_rightLimit == 1e6f
                                        : w8->m_tiltRate == 10.0f * 0.017453292f && w8->m_leftLimit == 7.0f * -0.017453292f);
                w8->Stop();
                delete w8;
            }
            e.barrel0->m_userProps = props0;
            // VERT not found (its type changed): the item, barrels and shells got, then failed; Stop
            // leaves the turret as it is; deleted without Stop, all of it released.
            const E_I3D_FRAME_TYPE vertType = e.vert->m_type;
            e.vert->m_type = FRAME_VISUAL;
            e.hort->SetRot(AxisAngle(Unit(S_vector(0.2f, 1.0f, 0.1f)), 0.9f));
            const S_quat hortBefore = e.hort->m_rot;
            auto *w9 = new C_SeatWeapon();
            const size_t destroyed0 = g_destroyedItems.size();
            SW(failed, !w9->Init(car, g_model, seatFrame, seat) && w9->m_failed && w9->m_item && w9->m_hort == e.hort &&
                           !w9->m_vert && w9->m_barrels.size() == static_cast<size_t>(e.kept) && w9->m_shells);
            w9->Stop();
            SW(failed, SwSameQuat(e.hort->m_rot, hortBefore) && !w9->m_item && g_destroyedItems.size() == destroyed0 + 1);
            delete w9;
            auto *w10 = new C_SeatWeapon();
            const int shellRefs = g_shellsModel->m_refCount;
            SW(release, !w10->Init(car, g_model, seatFrame, seat) && w10->m_shells && !w10->m_barrels.empty());
            C_WeaponItem *item = w10->m_item;
            std::vector<S_FakeAnim *> held;
            for (const S_SeatWeaponBarrel &b : w10->m_barrels)
                held.push_back(reinterpret_cast<S_FakeAnim *>(b.animation));
            delete w10;
            SW(release, g_destroyedItems.size() == destroyed0 + 2 &&
                            g_destroyedItems.back() == reinterpret_cast<C_Item *>(item) && g_shellsModel->m_refCount == shellRefs);
            for (S_FakeAnim *a : held)
                SW(release, a && a->refs == 0);
            e.vert->m_type = vertType;
            // No barrel kept (their shell dummies not dummies): failed too.
            for (I3D_frame *d : e.shellDummies)
                if (d)
                    d->m_type = FRAME_VISUAL;
            auto *w11 = new C_SeatWeapon();
            SW(failed, !w11->Init(car, g_model, seatFrame, seat) && w11->m_failed && w11->m_hort == e.hort &&
                           w11->m_vert == e.vert && w11->m_barrels.empty());
            delete w11;
            for (I3D_frame *d : e.shellDummies)
                if (d)
                    d->m_type = FRAME_DUMMY;
        }

        // Part 3b: the turret, the controls, firing, reloading, aiming.
        SeatWeapon3bTest(r3b, car, seatFrame, seat, e, firearm, def);

        // The sounds' other cases: the turning sound's group missing (then no stopping sound either), one
        // without samples, a sample not found (its frame released).
        if (motorId >= 0) {
            S_SoundGroup *motorGroup = g_manipulationGroups[motorId];
            g_manipulationGroups.erase(motorId);
            auto *w6 = new C_SeatWeapon();
            SW(sounds, w6->Init(car, g_model, seatFrame, seat) && !w6->m_motorSound && !w6->m_stopSound);
            w6->Stop();
            g_manipulationGroups[motorId] = MakeGroup(motorId, nullptr, true);
            SW(sounds, w6->Init(car, g_model, seatFrame, seat) && !w6->m_motorSound &&
                           soundOk(w6->m_stopSound, stopId == motorId ? -1 : stopId, false));
            w6->Stop();
            delete g_manipulationGroups[motorId];
            g_manipulationGroups[motorId] = MakeGroup(motorId, "missing.wav");
            std::set<const I3D_frame *> before;
            for (const auto &f : g_sounds)
                before.insert(f.first);
            SW(sounds, w6->Init(car, g_model, seatFrame, seat) && !w6->m_motorSound);
            int unopened = 0;
            for (const auto &f : g_sounds) {
                if (!before.count(f.first) && f.second.file.empty()) {
                    ++unopened;
                    SW(sounds, f.first->m_refCount == 0 && f.second.opens == 1);
                }
            }
            SW(sounds, unopened == (stopId == motorId ? 2 : 1));
            w6->Stop();
            delete w6;
            delete g_manipulationGroups[motorId];
            g_manipulationGroups[motorId] = motorGroup;
        }
        if (stopId >= 0 && stopId != motorId) {
            S_SoundGroup *stopGroup = g_manipulationGroups[stopId];
            g_manipulationGroups.erase(stopId);
            auto *w7 = new C_SeatWeapon();
            SW(sounds, w7->Init(car, g_model, seatFrame, seat) && !w7->m_stopSound &&
                           soundOk(w7->m_motorSound, motorId, true));
            w7->Stop();
            // (A group without samples: none either.)
            g_manipulationGroups[stopId] = MakeGroup(stopId, nullptr, true);
            SW(sounds, w7->Init(car, g_model, seatFrame, seat) && !w7->m_stopSound &&
                           soundOk(w7->m_motorSound, motorId, true));
            w7->Stop();
            delete w7;
            delete g_manipulationGroups[stopId];
            g_manipulationGroups[stopId] = stopGroup;
        }
        firstWeapon = false;
    }

    // The network messages.
    {
        static int replication;
        g_netReplication = reinterpret_cast<C_NetReplication *>(&replication);
        g_queued.clear();
        g_heldMessages.clear();
        g_netFinds.clear();
        const S_vector4 pos = {1.0f, 2.0f, 3.0f, 1.0f}, dir = {0.0f, 0.6f, 0.8f, 0.0f};
        g_gameRole = GAME_ROLE_CLIENT;
        SW(messages, SeatWeaponSendMessage(SEAT_WEAPON_MSG_SHOT, &pos, &dir, 2, 3, car));
        SW(messages, g_netFinds.size() == 1 && g_netFinds[0] == std::make_pair(55, false) && g_queued.size() == 1 &&
                         g_queued[0].type == SEAT_WEAPON_MSG_SHOT && g_queued[0].seat == 3 && g_queued[0].sender == 6 &&
                         g_queued[0].reliable && g_queued[0].fromClient);
        if (g_heldMessages.size() == 1) {
            const auto *shot = dynamic_cast<const C_SeatWeaponShotMessage *>(g_heldMessages[0]);
            SW(messages, shot && shot->barrel == 2 && shot->pos.x == 1.0f && shot->pos.y == 2.0f && shot->pos.z == 3.0f &&
                             shot->pos.w == 1.0f && shot->dir.y == 0.6f && shot->dir.z == 0.8f && shot->dir.w == 0.0f);
        }
        g_gameRole = GAME_ROLE_SERVER;
        SW(messages, SeatWeaponSendMessage(SEAT_WEAPON_MSG_SHOT, &pos, &dir, 1, 4, car) && g_queued.size() == 2 &&
                         !g_queued[1].fromClient && g_queued[1].seat == 4);
        SW(messages, SeatWeaponSendMessage(SEAT_WEAPON_MSG_RELOAD, nullptr, nullptr, 0, 5, car) && g_queued.size() == 3 &&
                         g_queued[2].type == SEAT_WEAPON_MSG_RELOAD && g_queued[2].seat == 5 && !g_queued[2].fromClient &&
                         g_queued[2].reliable && dynamic_cast<const C_SeatWeaponReloadMessage *>(g_heldMessages[2]));
        g_gameRole = GAME_ROLE_CLIENT;
        SW(messages, SeatWeaponSendMessage(SEAT_WEAPON_MSG_RELOAD, nullptr, nullptr, 0, 6, car) && g_queued.size() == 4 &&
                         g_queued[3].fromClient);
        // Other kinds: nothing sent; no net object: nothing (part 3a's fix).
        for (int kind : std::initializer_list<int>{SEAT_WEAPON_MSG_ROUNDS, SEAT_WEAPON_MSG_AMMO, -1, 7})
            SW(messages, SeatWeaponSendMessage(kind, &pos, &dir, 0, 0, car));
        g_netFindNone = true;
        SW(messages, SeatWeaponSendMessage(SEAT_WEAPON_MSG_SHOT, &pos, &dir, 0, 0, car));
        g_netFindNone = false;
        SW(messages, g_queued.size() == 4 && g_netFinds.size() == 5);
        // The factory: the number of types (4, in 3 bits) written; the types read.
        S_FakeStream st{0, false, {}, false};
        SW(messages, !MakeSeatWeaponMessage(car, reinterpret_cast<C_BitStream *>(&st), true) &&
                         st.bits == std::vector<std::pair<uint32_t, int>>{{4u, 3}});
        for (uint32_t k = 0; k < 8; ++k) {
            g_readBits = {{k, 3}};
            g_readPos = 0;
            g_readMismatch = false;
            C_NetObjectMessage *m = MakeSeatWeaponMessage(car, reinterpret_cast<C_BitStream *>(&st), false);
            bool right = !g_readMismatch && g_readPos == 1;
            if (k == 0)
                right = right && dynamic_cast<C_SeatWeaponShotMessage *>(m);
            else if (k == 1)
                right = right && dynamic_cast<C_SeatWeaponReloadMessage *>(m);
            else if (k == 2)
                right = right && dynamic_cast<C_SeatWeaponRoundsMessage *>(m);
            else if (k == 3)
                right = right && dynamic_cast<C_SeatWeaponAmmoMessage *>(m);
            else
                right = right && !m;
            SW(messages, right && (!m || m->GetType() == static_cast<int>(k)));
            delete m;
        }
        // The messages' data: a shot (a client's with its direction and position), a reload, the rounds,
        // the ammo.
        const auto bitsOf = [](float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; };
        C_SeatWeaponShotMessage shot;
        shot.seat = 5, shot.barrel = 3, shot.dir = {0.25f, -0.5f, 2.0f, 0.0f}, shot.pos = {10.0f, 20.0f, 30.0f, 1.0f};
        S_FakeStream sc{0, false, {}, false}, ss{0, false, {}, false};
        g_quantized.clear();
        g_senderAsks.clear();
        shot.Write(reinterpret_cast<C_BitStream *>(&sc));
        const std::vector<std::pair<uint32_t, int>> shotBits = {
            {0u, 3}, {5u, 3}, {3u, 2}, {bitsOf(0.25f), 32}, {bitsOf(-0.5f), 32}, {bitsOf(2.0f), 32},
            {bitsOf(10.0f), -2}, {bitsOf(20.0f), -2}, {bitsOf(30.0f), -2}};
        SW(messages, sc.bits == shotBits && g_quantized.size() == 1 &&
                         g_quantized[0].first == reinterpret_cast<S_NetPlayer *>(&g_senderPlayer) &&
                         g_quantized[0].second == 7 && g_senderAsks == std::vector<int>{-1});
        g_gameRole = GAME_ROLE_SERVER;
        shot.Write(reinterpret_cast<C_BitStream *>(&ss));
        SW(messages, ss.bits == std::vector<std::pair<uint32_t, int>>(shotBits.begin(), shotBits.begin() + 3));
        C_SeatWeaponShotMessage readShot;
        g_readBits.assign(shotBits.begin() + 1, shotBits.end());
        g_readPos = 0;
        g_readMismatch = false;
        readShot.Read(reinterpret_cast<C_BitStream *>(&ss));
        SW(messages, !g_readMismatch && g_readPos == 8 && readShot.seat == 5 && readShot.barrel == 3 &&
                         readShot.dir.x == 0.25f && readShot.dir.y == -0.5f && readShot.dir.z == 2.0f &&
                         readShot.pos.x == 10.0f && readShot.pos.y == 20.0f && readShot.pos.z == 30.0f);
        g_gameRole = GAME_ROLE_CLIENT;
        C_SeatWeaponShotMessage clientShot;
        g_readPos = 0;
        clientShot.Read(reinterpret_cast<C_BitStream *>(&ss));
        SW(messages, g_readPos == 2 && clientShot.seat == 5 && clientShot.barrel == 3 && clientShot.dir.x == 0.0f &&
                         clientShot.pos.x == 0.0f);
        C_SeatWeaponReloadMessage reload;
        reload.seat = 6;
        S_FakeStream sr{0, false, {}, false};
        reload.Write(reinterpret_cast<C_BitStream *>(&sr));
        SW(messages, sr.bits == std::vector<std::pair<uint32_t, int>>{{1u, 3}, {6u, 3}});
        g_readBits = {{6u, 3}};
        g_readPos = 0;
        C_SeatWeaponReloadMessage readReload;
        readReload.Read(reinterpret_cast<C_BitStream *>(&sr));
        SW(messages, g_readPos == 1 && readReload.seat == 6);
        C_SeatWeaponRoundsMessage rounds;
        rounds.seat = 4, rounds.rounds = 700;
        S_FakeStream so{0, false, {}, false};
        rounds.Write(reinterpret_cast<C_BitStream *>(&so));
        SW(messages, so.bits == std::vector<std::pair<uint32_t, int>>{{2u, 3}, {4u, 3}, {700u, 10}});
        g_readBits = {{4u, 3}, {700u, 10}};
        g_readPos = 0;
        C_SeatWeaponRoundsMessage readRounds;
        readRounds.Read(reinterpret_cast<C_BitStream *>(&so));
        SW(messages, g_readPos == 2 && readRounds.seat == 4 && readRounds.rounds == 700);
        C_SeatWeaponAmmoMessage ammo;
        ammo.uid = 1234, ammo.typeId = 61;
        S_FakeStream sa{0, false, {}, false};
        ammo.Write(reinterpret_cast<C_BitStream *>(&sa));
        SW(messages, sa.bits == std::vector<std::pair<uint32_t, int>>{{3u, 3}, {1234u, 16}, {61u, 8}});
        g_readBits = {{0xFFFEu, 16}, {61u, 8}};
        g_readPos = 0;
        C_SeatWeaponAmmoMessage readAmmo;
        readAmmo.Read(reinterpret_cast<C_BitStream *>(&sa));
        SW(messages, g_readPos == 2 && readAmmo.uid == -2 && readAmmo.typeId == 61);
        g_readBits = {{1234u, 16}, {200u, 8}};
        g_readPos = 0;
        readAmmo.Read(reinterpret_cast<C_BitStream *>(&sa));
        SW(messages, readAmmo.uid == 1234 && readAmmo.typeId == 200);
        g_gameRole = GAME_ROLE_LOCAL;
        for (C_NetObjectMessage *m : g_heldMessages)
            delete m;
        g_heldMessages.clear();
    }
    g_countUpdates = false;

    std::printf("seat weapon: weapons %d failed %d ctor %d init %d props %d barrels %d rest %d sounds %d again %d "
                "accessors %d gunner %d shell %d save %d load %d stop %d restart %d release %d failed %d messages %d "
                "update %d turn %d view %d controls %d fire %d shots %d reload %d aim %d\n",
                weapons, failures, ctor, init, props, barrels, rest, sounds, again, accessors, gunner, shell, save, load,
                stop, restart, release, failed, messages, r3b.update, r3b.turn, r3b.view, r3b.controls, r3b.fire,
                r3b.shots, r3b.reload, r3b.aim);
}
#undef SW
