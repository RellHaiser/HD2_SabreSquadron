// Physics harness, constructors (included by physics_test.cpp after motion_test.inc): every object type's
// constructor against the original's, on the same frame. In the loaded copy of the original, the collision world's
// AddVolume / MoveVolume / RemoveVolume and the fences' AddBreakable are redirected to loggers (its allocator was never
// started), I3DGetDriver, GetGame and GetGEffects to the harness's fakes. Each constructor runs on its own: the port's
// object new, the original's in g_origObject (its layout); the frame is put back between them.
namespace {

std::vector<S_OrigCall> g_origCalls;
struct S_OrigBreakable {
    const void *frame, *object, *volume;
};
std::vector<S_OrigBreakable> g_origBreakables;
void __attribute__((thiscall)) OrigAddVolume(void *, const void *v) { g_origCalls.push_back({'A', v}); }
void __attribute__((thiscall)) OrigMoveVolume(void *, const void *v) { g_origCalls.push_back({'M', v}); }
void __attribute__((thiscall)) OrigRemoveVolume(void *, const void *v) { g_origCalls.push_back({'R', v}); }
void __attribute__((thiscall)) OrigAddBreakable(void *, const S_OrigBreakable *b) { g_origBreakables.push_back(*b); }
// The original's game: its scene at +0x18.
alignas(16) uint8_t g_origGame[0x100];
void *OrigGetGame()
{
    *reinterpret_cast<void **>(g_origGame + 0x18) = GetGame()->m_scene;
    return g_origGame;
}
void *OrigGetGEffects() { return nullptr; }

bool g_origPatched;
void PatchOriginal()
{
    if (g_origPatched || !OriginalExeLoaded())
        return;
    g_origPatched = true;
    RedirectOriginal(0x006ef7b0, reinterpret_cast<const void *>(OrigAddVolume));
    RedirectOriginal(0x006efbc0, reinterpret_cast<const void *>(OrigMoveVolume));
    RedirectOriginal(0x006ef9b0, reinterpret_cast<const void *>(OrigRemoveVolume));
    RedirectOriginal(0x00479540, reinterpret_cast<const void *>(OrigAddBreakable));
    RedirectOriginal(0x007e529c, reinterpret_cast<const void *>(I3DGetDriver));     // (the import's thunk)
    RedirectOriginal(0x006987d0, reinterpret_cast<const void *>(OrigGetGame));
    RedirectOriginal(0x00486510, reinterpret_cast<const void *>(OrigGetGEffects));
}

static_assert(offsetof(S_PhysicsObjectDesc, door) == 0x1C && offsetof(S_PhysicsObjectDesc, radius) == 0x28, "the original's desc");
static_assert(offsetof(S_DoorParams, angle) == 0x04 && offsetof(S_DoorParams, breakModel) == 0x08, "the original's door params");

typedef void *(__attribute__((thiscall)) * T_Ctor2)(void *, I3D_frame *, S_PhysicsHandle *);
typedef void *(__attribute__((thiscall)) * T_Ctor3f)(void *, I3D_frame *, float, S_PhysicsHandle *);
typedef void *(__attribute__((thiscall)) * T_CtorDoor)(void *, const S_PhysicsObjectDesc *, S_PhysicsHandle *);
typedef void *(__attribute__((thiscall)) * T_CtorSwing)(void *, I3D_frame *, S_PhysicsHandle *, I3D_frame *);
void OrigConstruct(int type, const S_PhysicsObjectDesc &d, S_PhysicsHandle *handle)
{
    void *self = g_origObject;
    switch (type) {
    case PHYS_SIMPLE: OriginalAt<T_Ctor2>(0x0040e320)(self, d.frame, handle); break;
    case PHYS_FALLING: OriginalAt<T_Ctor2>(0x0040bd70)(self, d.frame, handle); break;
    case PHYS_BOX: OriginalAt<T_Ctor2>(0x0040daa0)(self, d.frame, handle); break;
    case PHYS_CAN: OriginalAt<T_Ctor3f>(0x0040e810)(self, d.frame, d.radius, handle); break;
    case PHYS_SPHERE: OriginalAt<T_Ctor3f>(0x0040f6a0)(self, d.frame, d.radius, handle); break;
    case PHYS_DOOR: OriginalAt<T_CtorDoor>(0x00407390)(self, &d, handle); break;
    default: OriginalAt<T_CtorSwing>(0x00405ea0)(self, d.frame, handle, d.frame->GetParent()); break;
    }
}
C_PhysObject *PortConstruct(int type, const S_PhysicsObjectDesc &d, S_PhysicsHandle *handle)
{
    switch (type) {
    case PHYS_SIMPLE: return new C_PhysSimpleObject(d.frame, handle);
    case PHYS_FALLING: return new C_PhysFallingObject(d.frame, handle);
    case PHYS_BOX: return new C_PhysBox(d.frame, handle);
    case PHYS_CAN: return new C_PhysCan(d.frame, d.radius, handle);
    case PHYS_SPHERE: return new C_PhysSphere(d.frame, d.radius, handle);
    case PHYS_DOOR: return new C_PhysDoor(d, handle);
    default: return new C_PhysSwingingObject(d.frame, handle, d.frame->GetParent());
    }
}

// Fields compared exactly (pointers, flags, integers), by their place in the original; the others are floats.
bool ExactField(int type, uint32_t orig)
{
    if (orig >= 0x140 && orig <= 0x164)
        return true;
    if (type == PHYS_FALLING && (orig == 0x170 || orig == 0x171))
        return true;
    if (type == PHYS_DOOR && ((orig >= 0x200 && orig <= 0x205) || orig == 0x214 || orig == 0x218 || orig == 0x22C))
        return true;
    if (type == PHYS_SWINGING && (orig == 0x220 || orig == 0x290 || orig == 0x294))
        return true;
    return false;
}
bool NearFloats(const uint8_t *a, const uint8_t *b, size_t size)
{
    for (size_t i = 0; i + 4 <= size; i += 4) {
        float x, y;
        std::memcpy(&x, a + i, 4);
        std::memcpy(&y, b + i, 4);
        if (std::memcmp(&x, &y, 4) != 0 && !(std::fabs(x - y) <= 1e-5f * std::max(1.0f, std::fabs(x))))
            return false;
    }
    return true;
}
// A volume of the port's object (`port`) against the original's (in g_origObject at `orig`): its matrices its own.
std::string VolumeDiffers(const C_PhysObject *object, const S_CollisionVolume &p, uint32_t orig, uint32_t matrixAt, uint32_t invAt)
{
    S_CollisionVolume o;
    std::memcpy(static_cast<void *>(&o), g_origObject + orig, sizeof o);
    if (p.flags != o.flags || p.frame != o.frame || p.type != o.type || p.actor != o.actor)
        return "volume kind";
    if (!NearFloats(reinterpret_cast<const uint8_t *>(&p.bbox), reinterpret_cast<const uint8_t *>(&o.bbox), sizeof p.bbox))
        return "volume box";
    // (Its fourth word, centerW, is written by some (the original's 16-byte copies) but read by nothing.)
    if (!NearFloats(reinterpret_cast<const uint8_t *>(&p.center), reinterpret_cast<const uint8_t *>(&o.center), 12))
        return "volume center";
    if (!NearFloats(reinterpret_cast<const uint8_t *>(&p.radius), reinterpret_cast<const uint8_t *>(&o.radius), 8))
        return "volume radius";
    const auto *base = reinterpret_cast<const uint8_t *>(object);
    const bool portMatrices = p.matrix == nullptr ? true : reinterpret_cast<const uint8_t *>(p.matrix) - base == static_cast<ptrdiff_t>(
                                  matrixAt == 0x90 ? offsetof(C_PhysObject, m_matrix) : offsetof(C_PhysSwingingObject, m_matrix2));
    const bool origMatrices = (p.matrix == nullptr) == (o.matrix == nullptr) &&
                              (!o.matrix || reinterpret_cast<const uint8_t *>(o.matrix) == g_origObject + matrixAt) &&
                              (p.invMatrix == nullptr) == (o.invMatrix == nullptr) &&
                              (!o.invMatrix || reinterpret_cast<const uint8_t *>(o.invMatrix) == g_origObject + invAt);
    if (!portMatrices || !origMatrices)
        return "volume matrices";
    return "";
}

struct S_CtorStats {
    long made = 0, agree = 0;
    std::map<std::string, long> differ;
    std::set<uint32_t> unwritten;       // fields the original's constructor leaves as they were (by its offsets)
};
std::map<std::string, S_CtorStats> g_ctors;

// One constructor compared. The port's object is returned (not in the module: its volumes in the world).
C_PhysObject *DiffCtor(const char *what, int type, const S_PhysicsObjectDesc &d)
{
    S_PhysicsHandle handle{0, false};
    static alignas(16) uint8_t frameBefore[0x400], framePort[0x400];
    std::memcpy(frameBefore, d.frame, sizeof frameBefore);
    const size_t breakables = g_breakables.size();
    C_PhysObject *p = PortConstruct(type, d, &handle);
    S_CtorStats &st = g_ctors[what];
    ++st.made;
    if (!OriginalExeLoaded())
        return p;
    std::memcpy(framePort, d.frame, sizeof framePort);
    // The original's twice: on a buffer of 0xCD, then of zeros; the bytes that differ it never wrote.
    static alignas(16) uint8_t probe[sizeof g_origObject];
    std::memcpy(static_cast<void *>(d.frame), frameBefore, sizeof frameBefore);
    std::memset(g_origObject, 0xCD, sizeof g_origObject);
    OrigConstruct(type, d, &handle);
    std::memcpy(probe, g_origObject, sizeof probe);
    std::memcpy(static_cast<void *>(d.frame), frameBefore, sizeof frameBefore);
    std::memset(g_origObject, 0, sizeof g_origObject);
    g_origCalls.clear();
    g_origBreakables.clear();
    OrigConstruct(type, d, &handle);
    // Those bytes take the port's values (the port's defaults are not compared): noted by field.
    {
        std::vector<S_Field> fields = Fields(type);
        for (const S_Field &f : fields)
            for (size_t i = 0; i < f.size; ++i)
                if (probe[f.orig + i] != g_origObject[f.orig + i]) {
                    g_origObject[f.orig + i] = reinterpret_cast<const uint8_t *>(p)[f.port + i];
                    g_ctors[what].unwritten.insert(f.orig);
                }
    }
    alignas(16) static uint8_t frameOrig[0x400];
    std::memcpy(frameOrig, d.frame, sizeof frameOrig);
    std::memcpy(static_cast<void *>(d.frame), framePort, sizeof framePort);

    std::string differs;
    if (*reinterpret_cast<uint32_t *>(g_origObject) != OrigCode(type).vtable)
        differs = "vtable";
    const auto *portBytes = reinterpret_cast<const uint8_t *>(p);
    for (const S_Field &f : Fields(type)) {
        if (!differs.empty())
            break;
        if (f.orig == 0xD0 || (type == PHYS_SWINGING && f.orig == 0x230))
            continue;                                                   // (the volumes: below)
        if (f.orig == 0x50 || f.orig == 0x90 || (type == PHYS_SWINGING && (f.orig == 0x1A0 || f.orig == 0x1E0))) {
            // Matrices: only where a volume uses them (the others are not set by every constructor).
            if (!p->m_volume.matrix && !(type == PHYS_SWINGING))
                continue;
        }
        const bool same = ExactField(type, f.orig) ? std::memcmp(portBytes + f.port, g_origObject + f.orig, f.size) == 0
                                                   : NearFloats(portBytes + f.port, g_origObject + f.orig, f.size);
        if (!same) {
            char name[48];
            std::snprintf(name, sizeof name, "field +0x%X", f.orig);
            differs = name;
            if (f.size % 4 == 0 && !ExactField(type, f.orig)) {
                static int shownField;
                for (size_t i = 0; shownField < 6 && i < f.size; i += 4) {
                    float x, y;
                    std::memcpy(&x, portBytes + f.port + i, 4);
                    std::memcpy(&y, g_origObject + f.orig + i, 4);
                    std::printf("    %s +0x%X word %u: port %.9g original %.9g\n", what, f.orig, static_cast<unsigned>(i / 4), x, y);
                }
                ++shownField;
            }
        }
    }
    if (differs.empty())
        differs = VolumeDiffers(p, p->m_volume, 0xD0, 0x90, 0x50);
    if (differs.empty() && type == PHYS_SWINGING)
        differs = VolumeDiffers(p, static_cast<C_PhysSwingingObject *>(p)->m_volume2, 0x230, 0x1A0, 0x1E0);
    // The world: the volumes the original added and moved, as the port's are.
    if (differs.empty()) {
        std::vector<S_OrigCall> expected;
        if (InWorld(p->m_volume)) {
            expected.push_back({'A', g_origObject + 0xD0});
            expected.push_back({'M', g_origObject + 0xD0});
        }
        if (type == PHYS_SWINGING && InWorld(static_cast<C_PhysSwingingObject *>(p)->m_volume2)) {
            expected.push_back({'A', g_origObject + 0x230});
            expected.push_back({'M', g_origObject + 0x230});
        }
        bool same = expected.size() == g_origCalls.size();
        for (size_t i = 0; same && i < expected.size(); ++i)
            same = expected[i].what == g_origCalls[i].what && expected[i].volume == g_origCalls[i].volume;
        if (!same)
            differs = "volumes in the world";
    }
    if (differs.empty()) {
        const size_t added = g_breakables.size() - breakables;
        bool same = added == g_origBreakables.size();
        if (same && added) {
            const S_BreakableObject &b = g_breakables.back();
            const S_OrigBreakable &o = g_origBreakables.back();
            same = reinterpret_cast<const void *>(b.frame) == o.frame && o.object == g_origObject && o.volume == g_origObject + 0xD0;
        }
        if (!same)
            differs = "breakables";
    }
    if (differs.empty()) {
        const I3D_frame &a = *reinterpret_cast<const I3D_frame *>(framePort), &b = *reinterpret_cast<const I3D_frame *>(frameOrig);
        if (a.m_refCount != b.m_refCount || a.m_frameFlags != b.m_frameFlags || a.m_parent != b.m_parent)
            differs = "frame";
        else if (!NearFloats(reinterpret_cast<const uint8_t *>(&a.m_localMatrix), reinterpret_cast<const uint8_t *>(&b.m_localMatrix), 64) ||
                 !NearFloats(reinterpret_cast<const uint8_t *>(&a.m_rot), reinterpret_cast<const uint8_t *>(&b.m_rot), 16))
            differs = "frame place";
    }
    CHECK(differs.empty());
    if (differs.empty()) {
        ++st.agree;
    } else {
        ++st.differ[differs];
        static int shown;
        if (shown++ < 12)
            std::printf("  %s constructor differs: %s\n", what, differs.c_str());
    }
    return p;
}

void TestConstructors()
{
    Fresh();
    PatchOriginal();
    static const char *names[] = {"simple", "sphere", "can", "box", "falling", "door", "swinging"};
    // Parents: the primary sector, or a turned, moved, scaled frame (then no scene: nothing relinks).
    I3D_frame *turned = NewFrame(FRAME_DUMMY, "turned", S_vector(3, 0.5f, -2), YawQuat(0.9f), Box(S_vector(1, 1, 1)), "", g_sector);
    for (int i = 0; i < 1400 / QUICK; ++i) {
        const int type = i % 7;                                          // PHYS_SIMPLE .. PHYS_SWINGING
        const bool withScene = (i / 7) % 2 == 0;
        GetGame()->m_scene = withScene ? reinterpret_cast<I3D_scene *>(g_sceneBytes) : nullptr;
        I3D_frame *parent = withScene ? g_sector : turned;
        // Boxes of every proportion (equal sides too), off center.
        S_vector half(U(0.05f, 1.2f), U(0.05f, 1.2f), U(0.05f, 1.2f));
        switch ((i / 7) % 5) {
        case 1: half.y = half.x; break;
        case 2: half.z = half.y; break;
        case 3: half.x = half.z; break;
        case 4: half.y = half.z = half.x; break;
        default: break;
        }
        const S_vector offset(U(-0.3f, 0.3f), U(-0.3f, 0.3f), U(-0.3f, 0.3f));
        I3D_bbox box;
        box.min = offset - half;
        box.max = offset + half;
        S_quat rot;
        rot.Make(RandomDir(), U(0, 3));
        I3D_frame *frame = NewFrame(FRAME_VISUAL, "c", S_vector(U(4, 60), U(1.5f, 6), U(4, 60)), rot, box, "", parent);
        if (i % 3 == 0) {
            frame->SetScale(S_vector(U(0.5f, 2), U(0.5f, 2), U(0.5f, 2)));
            frame->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
            UpdateTree(frame);
        }
        S_PhysicsObjectDesc d = Desc(frame, type, true);
        d.radius = i % 2 ? -1.0f : U(0.05f, 0.6f);
        d.shootThrough = i % 5 < 2;
        d.door.freeSwing = i % 4 == 1;
        d.door.tree = i % 6 == 2;
        d.door.breakable = i % 3 == 1;
        if (d.door.breakable)
            d.door.breakModel = "plotdira";
        d.door.angle = U(0.2f, 2.0f);
        C_PhysObject *p = DiffCtor(names[type], type, d);
        delete p;
    }
    GetGame()->m_scene = nullptr;
    for (auto &[what, st] : g_ctors) {
        std::printf("  %s constructor: %ld, agree %ld", what.c_str(), st.made, st.agree);
        if (!st.unwritten.empty()) {
            std::printf(" (not written by the original:");
            for (uint32_t at : st.unwritten)
                std::printf(" +0x%X", at);
            std::printf(")");
        }
        for (auto &[field, n] : st.differ)
            std::printf(", %s differs %ld", field.c_str(), n);
        std::printf("\n");
    }
    Fresh();
}
}  // namespace
