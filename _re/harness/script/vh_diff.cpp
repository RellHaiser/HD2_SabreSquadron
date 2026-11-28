// Vehicle physics part 2 on: the vehicles (game/vehicle*.cpp, the original's C_Vehicle 0x004e2900..0x0051c180) against
// the original executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the vehicles'
// calls go through vh_calls.s / vh_calls.inc (gen_cmd_calls.py vh). The LS3DF math is LS3DF.dll's on both sides.
//
// Each case builds the same world on each side (cmd_diff's) and a shipped car model's rig under a model frame of the
// test's (export_cars.py's dumps: the suites' inputs\cars4ds, or HD2_VH_CARS: every frame's type, parent, name, local
// matrix, scale, user properties, a dummy's box, a visual's vertices and their box), makes the vehicle on each side
// (the original's by its constructor 0x004e2900 in a block of 0x22D0, the port's by its own) and runs the same
// sequence: Setup, then operations. After each, every field of the vehicle (the original's at its offsets, the port's
// members: vh_fields.inc, gen_vfields.py), its wheels and body collision boxes, the frames and every call made into the
// game and the engine are compared.
// MinGW; build_vh.bat; runs vh_diff.exe ("vh_diff: N checks, 0 failures"). Env: HD2_VH_CASES (default 1,500;
// HD2_VH_QUICK: 300), HD2_VH_SEED, HD2_VH_SHOW (1: the port's lines; 2: both), HD2_VH_ONLY (the case shown),
// HD2_VH_TRACE (a case's operations as they start), HD2_VH_CARS (the dumps' folder).
#define HARNESS_NAME "vh_diff"
#define CMD_CALLS_INC "vh_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <fstream>
#include <sstream>

#define private public
#define protected public
#include "game/car_effects.h"
#include "game/car_tables.h"
#include "game/collision.h"
#include "game/vehicle.h"
#include "game/vehicle_params.h"
#undef protected
#undef private

// ---- the port's side of what the vehicles call and the test provides ----
uint32_t g_carEffectCount;                              // (car_effects.cpp's, not linked here)
// LS3DF.dll's math (the original's calls go there too; a struct answer's address after `this`).
template <class T> T Ls3dfFn(const char *name) { return reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df(name))); }
bool __stdcall S_matrix::Inverse(const S_matrix &src)
{
    static const auto f = Ls3dfFn<bool(__stdcall *)(S_matrix *, const S_matrix &)>("?Inverse@S_matrix@@QAG_NABU1@@Z");
    return f(this, src);
}
S_vector &__stdcall S_vector::operator*=(const S_matrix &m)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_vector *, const S_matrix &)>("??XS_vector@@QAGAAU0@ABUS_matrix@@@Z");
    f(this, m);
    return *this;
}
S_matrix __stdcall S_matrix::operator*(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_matrix *, S_matrix *, const S_matrix &)>("??DS_matrix@@QBG?AU0@ABU0@@Z");
    S_matrix r;
    f(this, &r, m);
    return r;
}
void __stdcall S_quat::Make(const S_vector &axis, float angle)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_quat *, const S_vector &, float)>("?Make@S_quat@@QAGXABUS_vector@@M@Z");
    f(this, axis, angle);
}
S_matrix __stdcall S_quat::RotationMatrix() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_matrix *)>("?RotationMatrix@S_quat@@QBG?AUS_matrix@@XZ");
    S_matrix m;
    f(this, &m);
    return m;
}
S_vector __stdcall S_matrix::GetScale() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_matrix *, float *)>("?GetScale@S_matrix@@QBG?AUS_vector@@XZ");
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}
void __stdcall S_matrix::SetDir3(const S_vector &dir, const S_vector &up)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_matrix *, const S_vector &, const S_vector &)>(
        "?SetDir3@S_matrix@@QAGXABUS_vector@@0@Z");
    f(this, dir, up);
}
float __stdcall S_vector::AngleTo(const S_vector &v) const
{
    static const auto f = Ls3dfFn<float(__stdcall *)(const S_vector *, const S_vector &)>("?AngleTo@S_vector@@QBGMABU1@@Z");
    return f(this, v);
}
// The port's allocations zeroed, as the original's (its operator new and _aligned_malloc zeroing ones here).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void *operator new[](size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }
void *operator new(size_t n, std::align_val_t a)
{
    void *p = _aligned_malloc(n ? n : 1, size_t(a));
    std::memset(p, 0, n ? n : 1);
    return p;
}
void operator delete(void *p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { _aligned_free(p); }
void *operator new[](size_t n, std::align_val_t a)
{
    void *p = _aligned_malloc(n ? n : 1, size_t(a));
    std::memset(p, 0, n ? n : 1);
    return p;
}

namespace H {

int __cdecl O_Atexit(void *) { return 0; }
void *__cdecl O_AlignedMalloc(size_t size, size_t align)
{
    void *p = _aligned_malloc(size ? size : 1, align);
    std::memset(p, 0, size ? size : 1);
    return p;
}
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
uint32_t Ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
template <class T> T &Mut(void *p, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
// (Denormals shown as "tiny": the original's x87 rounds them twice, the harness's SSE once.)
std::string Fl(float f) { return f != 0.0f && std::fabs(f) < 1e-37f ? std::string("tiny") : FloatText(f); }
std::string VN(const void *p, int n)
{
    std::string s = "(";
    for (int k = 0; k < n; ++k)
        s += (k ? " " : "") + Fl(static_cast<const float *>(p)[k]);
    return s + ")";
}
std::string V3(const void *p) { return VN(p, 3); }
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}
std::string PortString(const std::string &s) { return "\"" + s + "\""; }
// (The original's strings are char *: null when empty.)
std::string OrigCStr(uint32_t p) { return p ? "\"" + std::string(reinterpret_cast<const char *>(p)) + "\"" : std::string("\"\""); }
// An MSVC 6 vector's element count (first +4, last +8).
size_t OrigCount(const uint8_t *o, size_t elemSize)
{
    const uint32_t first = At<uint32_t>(o, 4), last = At<uint32_t>(o, 8);
    return first && last > first ? (last - first) / elemSize : 0;
}

// (The parameter block and the sound layers, which only ApplyParams changes: shown after the setup, ApplyParams and
// the last operation.)
bool g_showConstant = true;
int g_fullEvery = 4;                                    // (every so many operations all the fields: HD2_VH_FULL_EVERY)
#include "vh_fields.inc"

// ---- the original's functions ----
constexpr uint32_t O_VH_CTOR = 0x004e2900, O_VH_DTOR = 0x004e7f10, O_VH_SETUP = 0x004e2e30, O_VH_RELEASE = 0x004e81a0,
                   O_VH_APPLY_PARAMS = 0x004ed270, O_VH_START = 0x004ea780, O_CAR_SCENE = 0x0089c1d0, O_VH_STOP = 0x004ec5c0,
                   O_VH_STEP = 0x00509aa0, O_VH_UPDATE = 0x0050b180, O_VH_SET_GEAR = 0x004f2030, O_VH_SET_THROTTLE = 0x004f2100,
                   O_VH_SET_BRAKE = 0x004f2270, O_VH_SET_4F23B0 = 0x004f23b0, O_VH_SET_STEERING = 0x004f2420,
                   O_VH_SET_ENGINE = 0x004f2530, O_VH_SET_4F2630 = 0x004f2630, O_VH_SET_FUEL = 0x004f2670,
                   O_VH_SET_HANDBRAKE = 0x004f26a0, O_VH_ENABLE_SOUNDS = 0x004f2710, O_VH_APPLY_IMPULSE = 0x004f2790,
                   O_VH_ADD_FORCE = 0x004f2a30, O_VH_SETTLE = 0x004f67b0, O_VH_RESET = 0x004e95b0, O_VH_RESTART = 0x004e9db0,
                   O_VH_SET_SEAT_LOAD = 0x004f3950, O_VH_STATE_SIZE = 0x004f7300, O_VH_SAVE_STATE = 0x004f7650,
                   O_VH_LOAD_STATE = 0x004f8510, O_VH_CAR_HIT = 0x004f2b60, O_VH_VOLUMES_IN_WORLD = 0x004f5840,
                   O_VH_SPECIAL_VOLUMES = 0x004f5620, O_VH_CAMERA_OFFSET = 0x004f3630, O_VH_READ_MODEL_STATE = 0x004f5d10, O_VH_WHEEL_BURST = 0x0051a880, O_VH_DOOR_SOUND = 0x0051b660, O_VH_BEAM_SCALE = 0x004f4e40, O_CAR_EMITTERS = 0x0089bd50, O_VH_CONTACT_RESPONSE = 0x004f32a0, O_VH_ENGINE_TORQUE = 0x004f3840, O_VH_SOLVE = 0x0050de60, O_VH_GET_ENGINE_RPM = 0x004f4c50, O_VH_GET_SIZE = 0x004f4c80;
constexpr size_t VEHICLE_BYTES = 0x22D0;

// ---- the car models ----
struct S_VhFrame {
    int type = 0, parent = 0;                           // (parent: 0 the model, else 1-based)
    std::string name, props;
    float local[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float scale[3] = {1, 1, 1};
    float box[8] = {};                                  // (a dummy's)
    bool hasBox = false;
    std::vector<float> vertices;                        // (a visual's first LOD: x y z)
    std::vector<std::vector<float>> lods;               // (its further LODs')
};
struct S_VhModel {
    std::string file;
    std::vector<S_VhFrame> frames;
};
std::vector<S_VhModel> g_vhModels;
// The rig's frames' data (their dump's records) by address, per side.
std::map<uint32_t, const S_VhFrame *> g_frameData[2];

void LoadVhModels(const std::string &dir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    std::vector<std::string> names;
    do
        names.push_back(fd.cFileName);
    while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        std::ifstream in(dir + "\\" + n);
        S_VhModel m;
        std::string line;
        int pending = 0;                                // (vertex lines still to read)
        std::vector<float> *into = nullptr;             // (the LOD they go to)
        while (std::getline(in, line)) {
            if (pending > 0) {
                std::istringstream s(line);
                float x, y, z;
                s >> x >> y >> z;
                into->push_back(x);
                into->push_back(y);
                into->push_back(z);
                --pending;
                continue;
            }
            if (line.compare(0, 6, "model ") == 0) {
                std::istringstream s(line.substr(6));
                s >> m.file;
                continue;
            }
            if (line.size() < 2 || line[1] != ' ')
                continue;
            std::istringstream s(line.substr(2));
            if (line[0] == 'f') {
                S_VhFrame f;
                s >> f.type >> f.parent;
                s.get();
                std::getline(s, f.name);
                m.frames.push_back(f);
            } else if (m.frames.empty()) {
                continue;
            } else if (line[0] == 'L') {
                for (float &v : m.frames.back().local)
                    s >> v;
            } else if (line[0] == 'S') {
                for (float &v : m.frames.back().scale)
                    s >> v;
            } else if (line[0] == 'B') {
                for (float &v : m.frames.back().box)
                    s >> v;
                m.frames.back().hasBox = true;
            } else if (line[0] == 'V') {
                s >> pending;
                into = &m.frames.back().vertices;
            } else if (line[0] == 'W') {
                int lod;
                s >> lod >> pending;
                m.frames.back().lods.emplace_back();
                into = &m.frames.back().lods.back();
            } else if (line[0] == 'P') {
                std::string hex;
                s >> hex;
                std::string props;
                for (size_t i = 0; hex != "-" && i + 1 < hex.size(); i += 2)
                    props += char(std::stoi(hex.substr(i, 2), nullptr, 16));
                m.frames.back().props = props;
            }
        }
        if (!m.frames.empty())
            g_vhModels.push_back(m);
    }
}

// ---- the cars' parameter blocks (the shipped missions' blobs: the suites' inputs\carbins, or HD2_VH_PARAMS) ----
struct S_VhParams {
    std::string file;
    S_VehicleParams params;
    int axles = 0;                                      // (the leading axles with a value at +0x18C)
};
std::vector<S_VhParams> g_vhParams;
void LoadVhParams(const std::string &dir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.bin").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    std::vector<std::string> names;
    do
        names.push_back(fd.cFileName);
    while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        std::ifstream in(dir + "\\" + n, std::ios::binary);
        std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (blob.size() < VEHICLE_PARAMS_BLOB_SIZE)
            continue;
        S_VhParams p;
        p.file = n;
        std::memset(&p.params, 0, sizeof p.params);
        ReadVehicleParams(blob.data(), p.params, false);
        while (p.axles < VEHICLE_AXLES && p.params.axleUnknown18C[p.axles] > 0.009f)
            ++p.axles;
        g_vhParams.push_back(p);
    }
}

// ---- the car tables (both sides': the tyre types from tables\tyres.bin, the surfaces the test's) ----
constexpr uint32_t O_TYRE_TYPES = 0x0089c174, O_CAR_SURFACES = 0x0089c160, O_SURFACE_OF_MATERIAL = 0x0089bd60,
                   O_LOAD_TYRES = 0x004d6900;
const int SURFACE_MATERIALS[] = {1, 2, 5, 0x23, 0x30, 0x40, 0x55, 0x5F};
void SetUpCarTables()
{
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        const bool ok = side ? OriginalAt<bool(__cdecl *)()>(O_LOAD_TYRES)() : LoadTyres();
        const size_t types = side ? OrigCount(OriginalAt<const uint8_t *>(O_TYRE_TYPES), 0x28) : g_tyreTypes.size();
        std::printf("vh_diff: %s tyre types %d (%d)\n", SIDE_NAMES[side], int(types), int(ok));
    }
    // The car effects' definitions (eight, the first an empty one as in tables\effects_car.def): the same bytes on
    // both sides.
    {
        static S_CarEffectDef defs[8];
        for (int k = 0; k < 8; ++k) {
            S_CarEffectDef &d = defs[k];
            std::memset(&d, 0, sizeof d);
            if (k == 0)
                continue;
            std::snprintf(d.texture, sizeof d.texture, "fx%d.bmp", k);
            std::snprintf(d.alphaTexture, sizeof d.alphaTexture, "fx%da.bmp", k);
            for (int c = 0; c < 4; ++c) {
                d.color1[c] = 0.5f + 0.05f * float(k + c);
                d.color2[c] = 0.25f + 0.1f * float(c);
                d.rect[c] = c < 2 ? 0.0f : 1.0f;
            }
            d.unknown70 = k;
            d.unknown74 = 2 * k;
            d.unknown80 = 0.5f * float(k);
            d.unknown8C = 3.5f;
            d.flags = 0x101;
            d.unknownA0 = 0.1f * float(k);
            d.unknownAC = 1.5f;
            d.velocity = S_vector(0.0f, 0.5f * float(k), 0.25f);
            d.velocity2 = S_vector(0.1f, 0.2f, 0.3f);
            d.unknownC8 = 2.0f;
        }
        g_carEffects = defs;
        g_carEffectCount = 8;
        uint8_t *copy = static_cast<uint8_t *>(_aligned_malloc(sizeof defs, 16));
        std::memcpy(copy, defs, sizeof defs);
        *OriginalAt<uint8_t **>(0x0097b9ac) = copy;
        *OriginalAt<uint32_t *>(0x0097b9b0) = 8;
    }
    // The surfaces: a default entry, then one per material of SURFACE_MATERIALS (the rest the default's).
    std::vector<S_CarSurface> surfaces(1 + sizeof SURFACE_MATERIALS / sizeof *SURFACE_MATERIALS);
    for (size_t i = 0; i < surfaces.size(); ++i) {
        S_CarSurface &s = surfaces[i];
        std::memset(&s, 0, sizeof s);
        const float k = float(i);
        s.material = i ? SURFACE_MATERIALS[i - 1] : 0;
        s.unknown04 = 0.02f + 0.01f * k;
        s.unknown08 = 0.05f + 0.02f * k;
        s.unknown0C = 0.1f + 0.05f * k;
        s.unknown10 = 0.2f + 0.05f * k;
        s.unknown14 = 0.02f * float(i % 5);
        s.friction = i ? 0.2f + 0.08f * k : 0.35f;
        s.bounce = 1.0f + 0.1f * float(i % 4);
        s.dustEffect = int(i % 3);
        s.skidEffect = int(i % 4);
        s.contactEffect = i % 2 ? -int(i) : int(i);
        s.tyreSqueal = uint8_t(i % 2);
        s.skidMarks = uint8_t(i % 3 != 0);
    }
    g_carSurfaces = surfaces;
    uint8_t *data = static_cast<uint8_t *>(_aligned_malloc(surfaces.size() * sizeof(S_CarSurface), 16));
    std::memcpy(data, surfaces.data(), surfaces.size() * sizeof(S_CarSurface));
    *OriginalAt<uint8_t **>(O_CAR_SURFACES + 4) = data;
    *OriginalAt<uint8_t **>(O_CAR_SURFACES + 8) = data + surfaces.size() * sizeof(S_CarSurface);
    *OriginalAt<uint8_t **>(O_CAR_SURFACES + 12) = data + surfaces.size() * sizeof(S_CarSurface);
    for (int m = 0; m < 256; ++m) {
        int entry = 0;
        for (size_t i = 0; i < sizeof SURFACE_MATERIALS / sizeof *SURFACE_MATERIALS; ++i)
            if (SURFACE_MATERIALS[i] == m)
                entry = int(i + 1);
        g_carSurfaceOfMaterial[m] = entry;
        OriginalAt<int *>(O_SURFACE_OF_MATERIAL)[m] = entry;
    }
}

// ---- the case ----
enum { OP_APPLY_PARAMS, OP_START, OP_STOP, OP_STEP, OP_UPDATE, OP_TICK, OP_GEAR, OP_THROTTLE, OP_BRAKE, OP_STEER,
       OP_HANDBRAKE, OP_ENGINE, OP_FUEL, OP_SET4F23B0, OP_SET4F2630, OP_SOUNDS, OP_IMPULSE, OP_FORCE, OP_SETTLE, OP_RESET,
       OP_RESTART, OP_SEAT_LOAD, OP_GETTERS, OP_SOLVE, OP_OPTIONS, OP_FIELDS, OP_DRIVER, OP_CAR_HIT, OP_VOLUMES, OP_STATE, OP_DOOR, OP_LIGHTS, OP_SOUNDS2, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"ApplyParams", "Start", "Stop", "Step", "Update", "Tick", "SetGear", "SetThrottle",
                                     "SetBrake", "SetSteering", "SetHandbrake", "SetEngine", "SetFuel", "Set4f23b0",
                                     "Set4f2630", "EnableSounds", "ApplyImpulse", "AddForce", "Settle", "Reset", "Restart",
                                     "SetSeatLoad", "Getters", "Solve", "Options", "Fields", "Driver", "CarHit", "Volumes", "State", "Door", "Lights", "Sounds"};
// The ground: a tilted plane (its material, or stripes of materials along x), water above it now and then, a wall
// across x.
constexpr int GROUND_STRIPES = 4;
struct S_VhGround {
    float y = 0.0f, tiltX = 0.0f, tiltZ = 0.0f;
    int material = 1;
    bool stripes = false;
    int stripeMaterials[GROUND_STRIPES] = {1, 2, 0x30, 0x40};
    float stripeWidth = 2.0f;
    bool water = false;
    float waterY = 0.5f;
    int waterMaterial = 0x23;
    bool wall = false;
    float wallX = 10.0f;
    int wallMaterial = 0x55;
    bool platform = false;                              // (a moving actor's: a dynamic volume over minX..maxX)
    float platformY = 1.0f, platformMinX = -2.0f, platformMaxX = 2.0f;
    int platformMaterial = 2, platformActor = 0;
};
struct S_VhOp {
    int kind = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_VhCase {
    int model = 0;
    int params = -1;                                    // (g_vhParams' index)
    int skidMarks = 2;                                  // (+0x678 skid marks per wheel: the game's 50 now and then)
    uint32_t f74 = 0;                                   // (+0xF74: bit 0 no skid marks)
    S_VhGround ground;
    bool windows = false;                               // (a window added under each door visual)
    int emitters = 0;                                   // (the car effects' pool: so many emitters)
    int groups = -1;                                    // (>= 0: a third of the deform visuals in groups, by this salt)
    bool destroy = false;                               // (the end: the destructor, not Release)
    bool skidFails = false;                             // (the skid marks' models fail to load)
    bool bumpSound = false;                             // (both groups given a bump sound)
    float pos[3] = {};
    float rot[4] = {1, 0, 0, 0};
    std::vector<S_VhOp> ops;
};

uint8_t *g_vehicle[2];
DWORD g_vhClock[2];                                     // (timeGetTime: 60 ms on per call, from 1000 at each case's start)
uint8_t *g_modelFrame[2];
std::vector<uint8_t *> g_modelFrames[2];                // (the rig's frames, the dump's order)
std::deque<std::string> g_vhTexts[2];                   // (a deque: the texts stay where they are)
const char *Text(const std::string &s)
{
    g_vhTexts[g_side].push_back(s);
    return g_vhTexts[g_side].back().c_str();
}
C_Vehicle *Vehicle() { return reinterpret_cast<C_Vehicle *>(g_vehicle[g_side]); }

void SetLocal(uint8_t *frame, const float *m16, const float *scale)
{
    I3D_frame *f = AsFrame(frame);
    std::memcpy(&f->m_localMatrix, m16, 64);
    f->m_scale = S_vector(scale[0], scale[1], scale[2]);
    f->m_frameFlags = (f->m_frameFlags & ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_DIRTY | FRMFLAGS_BOUND_VALID)) | FRMFLAGS_CHANGED |
                      FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
}
void SetVisualBox(uint8_t *visual, const float *mn, const float *mx)
{
    I3D_bound_volume &b = *reinterpret_cast<I3D_bound_volume *>(visual + 0x1A0);
    b.bbox.min = S_vector(mn[0], mn[1], mn[2]);
    b.bbox.max = S_vector(mx[0], mx[1], mx[2]);
    b.bbox.minW = 0.0f;
    b.bbox.maxW = 0.0f;
    b.center = S_vector((mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f);
    b.radius = 1.0f;
    Mut<uint32_t>(visual, 0x1D0) |= VISFLAGS_BOUND_VALID;
}

void LinkLast(I3D_frame *f, I3D_frame *parent);
std::string Upper(std::string s)
{
    for (char &ch : s)
        ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

// ---- the meshes ----
// I3D_mesh: +0x44 the level count, +0x48 the references, +0x50 the levels (MESH_LEVELS). I3D_mesh_level: +0x54 the
// vertex count, +0x58 the face groups (1), +0x5C the face group, +0x60 the vertices (0x20 bytes each, the position
// first). I3D_face_group: +0x04 the material, +0x08 the faces. I3D_material: +0x04 the references, +0x08 the flags,
// +0x0C the texture.
constexpr int MESH_LEVELS = 8;
uint8_t *g_modelMaterial[2];
int g_meshesMade[2];
uint8_t *NewMaterial(const std::string &name)
{
    uint8_t *m = Block(0x40);
    *reinterpret_cast<void ***>(m) = EngineVtable("I3D_material");
    Mut<int>(m, 4) = 1;
    Name(m, name);
    return m;
}
uint8_t *NewLevel(const float *positions, int count, uint8_t *material)
{
    uint8_t *l = Block(0x80);
    *reinterpret_cast<void ***>(l) = EngineVtable("I3D_mesh_level");
    uint8_t *data = Block(size_t(count > 0 ? count : 1) * 0x20);
    for (int v = 0; v < count && positions; ++v)
        std::memcpy(data + 0x20 * v, positions + 3 * v, 12);
    Mut<int>(l, 0x54) = count;
    Mut<int>(l, 0x58) = 1;
    uint8_t *group = Block(0x20);
    *reinterpret_cast<void ***>(group) = EngineVtable("I3D_face_group");
    Mut<uint8_t *>(group, 4) = material;
    Mut<int>(group, 8) = count / 3;
    Mut<uint8_t *>(l, 0x5C) = group;
    Mut<uint8_t *>(l, 0x60) = data;
    return l;
}
uint8_t *NewMesh(const std::string &name)
{
    uint8_t *m = Block(0x80);
    *reinterpret_cast<void ***>(m) = EngineVtable("I3D_mesh");
    Mut<int>(m, 0x48) = 1;
    Name(m, name);
    return m;
}
uint8_t *&MeshLevel(uint8_t *mesh, int i) { return Mut<uint8_t *>(mesh, 0x50 + 4 * uint32_t(i)); }
void CopyMesh(uint8_t *to, const uint8_t *from)
{
    const uint32_t levels = At<uint32_t>(from, 0x44);
    for (uint32_t i = 0; i < levels && i < uint32_t(MESH_LEVELS); ++i) {
        const uint8_t *src = MeshLevel(const_cast<uint8_t *>(from), int(i));
        const int count = At<int>(src, 0x54);
        const uint8_t *group = At<uint8_t *>(src, 0x5C);
        uint8_t *l = NewLevel(nullptr, count, At<uint8_t *>(group, 4));
        std::memcpy(Mut<uint8_t *>(l, 0x60), At<uint8_t *>(src, 0x60), size_t(count) * 0x20);
        MeshLevel(to, int(i)) = l;
    }
    Mut<uint32_t>(to, 0x44) = levels;
}

void BuildVhModel(const S_VhCase &c)
{
    const S_VhModel &m = g_vhModels[size_t(c.model)];
    uint8_t *model = NewFrame(FRAME_MODEL, "car");
    AsFrame(model)->m_name = W().frames.back().name.c_str();
    Mut<const char *>(model, 0x134) = Text(m.file);
    S_quat q;
    q.s = c.rot[0];
    q.v = S_vector(c.rot[1], c.rot[2], c.rot[3]);
    S_matrix world;
    world.Identity();
    world.SetRot3(q);
    world.m[3][0] = c.pos[0];
    world.m[3][1] = c.pos[1];
    world.m[3][2] = c.pos[2];
    static const float one[3] = {1, 1, 1};
    SetLocal(model, &world.m[0][0], one);
    AsFrame(model)->m_rot = q;
    g_modelFrame[g_side] = model;
    std::vector<uint8_t *> &made = g_modelFrames[g_side];
    made.clear();
    g_frameData[g_side].clear();
    std::map<std::string, int> slots;                   // (the deform groups' next slots)
    g_modelMaterial[g_side] = NewMaterial("model material");
    // (Its texture: the one a glass part's crack swaps out.)
    uint8_t *texture = Block(0x40);
    *reinterpret_cast<void ***>(texture) = EngineVtable("I3D_texture");
    Mut<int>(texture, 4) = 1;
    Name(texture, "model texture");
    Mut<uint8_t *>(g_modelMaterial[g_side], 0xC) = texture;
    g_meshesMade[g_side] = 0;
    for (const S_VhFrame &f : m.frames) {
        uint8_t *parent = f.parent > 0 && size_t(f.parent) <= made.size() ? made[size_t(f.parent - 1)] : model;
        // (Named "<model>.<name>", as a loaded model's frames; linked after their siblings, as the engine adds them.)
        uint8_t *x = NewFrame(f.type == 1 ? FRAME_VISUAL : FRAME_DUMMY, "car." + f.name);
        S_FakeFrame &ff = W().frames.back();
        if (f.type == 1)
            ff.visualType = I3DVISUAL_OBJECT;
        I3D_frame *fx = AsFrame(x);
        fx->m_name = ff.name.c_str();
        fx->m_nameOffset = 4;
        fx->m_ownerModel = reinterpret_cast<I3D_frame *>(model);
        fx->m_frameFlags |= 1;
        fx->m_parent = AsFrame(parent);
        I3D_frame **link = &AsFrame(parent)->m_firstChild;
        while (*link)
            link = &(*link)->m_nextSibling;
        *link = fx;
        // (No shipped car groups its deform visuals: with the case's groups a third of them "[+DEFORM <type> <group><slot>]",
        // groups 1..2, each group's slots in order from 0 (the original crashes on a group with an empty slot).)
        std::string props = f.props;
        if (c.groups >= 0 && props.compare(0, 8, "[DEFORM ") == 0 && props.find(']') != std::string::npos &&
            (made.size() + size_t(c.groups)) % 3 == 0) {
            const size_t close = props.find(']');
            const std::string type = props.substr(1, close - 1);
            const int group = int(made.size() % 2) + 1;
            int &next = slots[type + F("%d", group)];
            if (next <= 9) {
                props = "[+" + type + F(" %d", group * 10 + next) + props.substr(close);
                ++next;
            }
        }
        if (!props.empty())
            fx->m_userProps = Text(props);
        SetLocal(x, f.local, f.scale);
        g_frameData[g_side][Ptr(x)] = &f;
        if (f.hasBox) {
            I3D_bbox &box = *reinterpret_cast<I3D_bbox *>(x + 0x120);
            box.min = S_vector(f.box[0], f.box[1], f.box[2]);
            box.max = S_vector(f.box[4], f.box[5], f.box[6]);
        }
        // A visual's mesh: its LODs' vertices (none: no mesh).
        Mut<uint8_t *>(x, 0x1E0) = nullptr;
        if (f.type == 1 && !f.vertices.empty()) {
            uint8_t *mesh = NewMesh("mesh " + f.name);
            MeshLevel(mesh, 0) = NewLevel(f.vertices.data(), int(f.vertices.size() / 3), g_modelMaterial[g_side]);
            int levels = 1;
            for (const std::vector<float> &lod : f.lods)
                if (levels < MESH_LEVELS)
                    MeshLevel(mesh, levels++) = NewLevel(lod.data(), int(lod.size() / 3), g_modelMaterial[g_side]);
            Mut<uint32_t>(mesh, 0x44) = uint32_t(levels);
            Mut<uint8_t *>(x, 0x1E0) = mesh;
        }
        if (f.type == 1 && !f.vertices.empty()) {
            float mn[3] = {f.vertices[0], f.vertices[1], f.vertices[2]}, mx[3] = {mn[0], mn[1], mn[2]};
            for (size_t k = 0; k + 2 < f.vertices.size(); k += 3)
                for (int a = 0; a < 3; ++a) {
                    mn[a] = std::min(mn[a], f.vertices[k + size_t(a)]);
                    mx[a] = std::max(mx[a], f.vertices[k + size_t(a)]);
                }
            SetVisualBox(x, mn, mx);
        }
        made.push_back(x);
    }
    // (No shipped car has a window in a door: the case's model gets one per door visual now and then, a small quad 0.3
    // above the door's origin.)
    if (c.windows) {
        const size_t frames = made.size();
        for (size_t i = 0; i < frames; ++i) {
            const S_VhFrame &f = m.frames[i];
            if (f.type != 1 || f.name.size() < 4 || Upper(f.name.substr(0, 4)) != "DOOR")
                continue;
            uint8_t *w = NewFrame(FRAME_VISUAL, "car.WINDOW" + f.name.substr(4));
            S_FakeFrame &ff = W().frames.back();
            ff.visualType = I3DVISUAL_OBJECT;
            I3D_frame *fw = AsFrame(w);
            fw->m_name = ff.name.c_str();
            fw->m_nameOffset = 4;
            fw->m_ownerModel = reinterpret_cast<I3D_frame *>(model);
            fw->m_frameFlags |= 1;
            LinkLast(fw, AsFrame(made[i]));
            static const float local[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.05f, 0.3f, 0.1f, 1};
            static const float one[3] = {1, 1, 1};
            SetLocal(w, local, one);
            static const float quad[] = {-0.3f, -0.2f, 0.0f, 0.3f, -0.2f, 0.0f, 0.3f, 0.2f, 0.0f, -0.3f, 0.2f, 0.0f};
            uint8_t *mesh = NewMesh("mesh window");
            MeshLevel(mesh, 0) = NewLevel(quad, 4, g_modelMaterial[g_side]);
            Mut<uint32_t>(mesh, 0x44) = 1;
            Mut<uint8_t *>(w, 0x1E0) = mesh;
            const float mn[3] = {-0.3f, -0.2f, 0.0f}, mx[3] = {0.3f, 0.2f, 0.0f};
            SetVisualBox(w, mn, mx);
            made.push_back(w);
        }
    }
}

// ---- the answers ----
// A frame's name against a mask (as the vehicle harness's stub engine: '*' wildcards with ENUMF_WILDMASK, else the
// exact short name; both case-insensitive).
bool Match(const char *mask, const char *name)
{
    if (!*mask)
        return !*name;
    if (*mask == '*')
        return Match(mask + 1, name) || (*name && Match(mask, name + 1));
    return *name && std::toupper(static_cast<unsigned char>(*mask)) == std::toupper(static_cast<unsigned char>(*name)) &&
           Match(mask + 1, name + 1);
}
bool Matches(I3D_frame *f, uint32_t typeMask, const char *mask)
{
    if (!f->m_type || !(typeMask & (1u << (f->m_type - 1))))
        return false;
    if (!mask)
        return true;
    const char *shortName = f->GetShortName();
    if (typeMask & ENUMF_WILDMASK)
        return Match(mask, shortName);
    size_t i = 0;
    for (; mask[i] && shortName[i]; ++i)
        if (std::toupper(static_cast<unsigned char>(mask[i])) != std::toupper(static_cast<unsigned char>(shortName[i])))
            return false;
    return !mask[i] && !shortName[i];
}
// As LS3DF's EnumFramesProc: a callback's 2 ends the enumeration, 1 skips the frame's children.
using T_Enum = uint32_t(__stdcall *)(I3D_frame *, void *);
bool Enumerate(I3D_frame *f, T_Enum callback, void *context, uint32_t typeMask, const char *mask)
{
    for (I3D_frame *c = f->m_firstChild; c;) {
        I3D_frame *next = c->m_nextSibling;
        uint32_t result = 0;
        if (Matches(c, typeMask, mask))
            result = callback(c, context);
        if (result == 2)
            return false;
        if (result != 1 && !Enumerate(c, callback, context, typeMask, mask))
            return false;
        c = next;
    }
    return true;
}
I3D_frame *FindFrame(I3D_frame *f, const char *mask, uint32_t typeMask)
{
    for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling) {
        if (Matches(c, typeMask, mask))
            return c;
        if (I3D_frame *found = FindFrame(c, mask, typeMask))
            return found;
    }
    return nullptr;
}
// The driver's hierarchy box: the frame's subtree in its own space (as the vehicle harness's stub engine: the visuals'
// vertices and the dummies' boxes through the matrices down to them).
void ExpandBox(I3D_bbox &box, I3D_frame *f, const S_matrix &toRoot)
{
    auto expand = [&box](const S_vector &v) {
        box.min.x = std::min(box.min.x, v.x);
        box.min.y = std::min(box.min.y, v.y);
        box.min.z = std::min(box.min.z, v.z);
        box.max.x = std::max(box.max.x, v.x);
        box.max.y = std::max(box.max.y, v.y);
        box.max.z = std::max(box.max.z, v.z);
    };
    auto through = [](const S_vector &v, const S_matrix &m) {
        return S_vector(v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0],
                        v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1],
                        v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2]);
    };
    const auto it = g_frameData[g_side].find(Ptr(f));
    if (it != g_frameData[g_side].end()) {
        const std::vector<float> &v = it->second->vertices;
        for (size_t i = 0; i + 2 < v.size(); i += 3)
            expand(through(S_vector(v[i], v[i + 1], v[i + 2]), toRoot));
    }
    if (f->m_type == FRAME_DUMMY) {
        const I3D_bbox &b = *reinterpret_cast<const I3D_bbox *>(reinterpret_cast<const uint8_t *>(f) + 0x120);
        expand(through(b.min, toRoot));
        expand(through(b.max, toRoot));
    }
    for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling) {
        S_matrix m;
        const S_matrix &l = c->m_localMatrix;
        for (int r = 0; r < 4; ++r)
            for (int col = 0; col < 4; ++col) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += l.m[r][k] * toRoot.m[k][col];
                m.m[r][col] = sum;
            }
        ExpandBox(box, c, m);
    }
}
void Unlink(I3D_frame *f)
{
    if (I3D_frame *p = f->m_parent) {
        for (I3D_frame **link = &p->m_firstChild; *link; link = &(*link)->m_nextSibling)
            if (*link == f) {
                *link = f->m_nextSibling;
                break;
            }
    }
    f->m_parent = nullptr;
    f->m_nextSibling = nullptr;
}
void LinkLast(I3D_frame *f, I3D_frame *parent)
{
    f->m_parent = parent;
    I3D_frame **link = &parent->m_firstChild;
    while (*link)
        link = &(*link)->m_nextSibling;
    *link = f;
}
void StaleTree(I3D_frame *f)
{
    f->m_frameFlags &= ~uint32_t(FRMFLAGS_WMATRIX_VALID);
    for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
        StaleTree(c);
}
// ---- the ground (S_VhGround) ----
S_VhGround g_ground;
enum { VOLUME_WATER = GROUND_STRIPES, VOLUME_WALL, VOLUME_PLATFORM, VOLUMES };
// The ground's stripes (0 alone without them), water, wall, the moving actor's platform (a dynamic volume).
uint8_t *g_groundVolume[2][VOLUMES];
void BuildVhGround(const S_VhGround &g)
{
    static const char *const names[] = {"ground0", "ground1", "ground2", "ground3", "water", "wall", "platform"};
    for (int i = 0; i < VOLUMES; ++i) {
        g_groundVolume[g_side][i] = Block(0x60);
        Name(g_groundVolume[g_side][i], names[i]);
        S_CollisionVolume *v = reinterpret_cast<S_CollisionVolume *>(g_groundVolume[g_side][i]);
        const int material = i < GROUND_STRIPES ? (g.stripes ? g.stripeMaterials[i] : g.material)
                             : i == VOLUME_WATER ? g.waterMaterial : i == VOLUME_WALL ? g.wallMaterial : g.platformMaterial;
        v->flags = (uint32_t(material) << 24) | (i == VOLUME_WATER ? COLLISION_FLAG_WATER : 0u) |
                   (i == VOLUME_PLATFORM ? COLLISION_FLAG_DYNAMIC : 0u);
        if (i == VOLUME_PLATFORM && !W().actors.empty())
            v->actor = reinterpret_cast<C_Actor *>(W().actors[size_t(g.platformActor) % W().actors.size()].bytes);
    }
}
struct S_VhPlane {
    S_vector n;
    float d;
    int volume;
    bool bounded = false;                               // (only over minX..maxX: the platform)
    float minX = 0.0f, maxX = 0.0f;
};
bool Within(const S_VhPlane &p, const S_vector &at) { return !p.bounded || (at.x >= p.minX && at.x <= p.maxX); }
std::vector<S_VhPlane> GroundPlanes(uint32_t excludeFlags)
{
    const S_VhGround &g = g_ground;
    std::vector<S_VhPlane> planes;
    S_vector n(-g.tiltX, 1.0f, -g.tiltZ);
    const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    n = S_vector(n.x / len, n.y / len, n.z / len);
    planes.push_back({n, -(n.y * g.y), 0});
    if (g.water && !(excludeFlags & COLLISION_FLAG_WATER))
        planes.push_back({S_vector(0.0f, 1.0f, 0.0f), -g.waterY, VOLUME_WATER});
    if (g.wall)
        planes.push_back({S_vector(-1.0f, 0.0f, 0.0f), g.wallX, VOLUME_WALL});
    if (g.platform)
        planes.push_back({S_vector(0.0f, 1.0f, 0.0f), -g.platformY, VOLUME_PLATFORM, true, g.platformMinX, g.platformMaxX});
    return planes;
}
float GroundHeight(float x, float z)
{
    const S_VhPlane p = GroundPlanes(0)[0];
    return -(p.d + p.n.x * x + p.n.z * z) / p.n.y;
}
uint32_t HitVolume(const S_VhPlane &p, const S_vector &at)
{
    int volume = p.volume;
    if (volume == 0 && g_ground.stripes) {
        const int k = int(std::floor(at.x / g_ground.stripeWidth));
        volume = ((k % GROUND_STRIPES) + GROUND_STRIPES) % GROUND_STRIPES;
    }
    return Ptr(g_groundVolume[g_side][volume]);
}
float PlaneDistance(const S_VhPlane &p, const S_vector &v) { return p.n.x * v.x + p.n.y * v.y + p.n.z * v.z + p.d; }
// The ray from `from` along `dir` (its length the reach): the nearest plane it meets from the front.
uint32_t VhRayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal, uint32_t excludeFlags)
{
    float best = 2.0f;
    uint32_t hit = 0;
    for (const S_VhPlane &p : GroundPlanes(excludeFlags)) {
        const float denominator = p.n.x * dir.x + p.n.y * dir.y + p.n.z * dir.z;
        if (!(denominator < 0.0f))
            continue;
        const float t = -PlaneDistance(p, from) / denominator;
        if (!(t >= 0.0f) || !(t <= 1.0f) || !(t < best))
            continue;
        const S_vector at(from.x + dir.x * t, from.y + dir.y * t, from.z + dir.z * t);
        if (!Within(p, at))
            continue;
        best = t;
        hit = HitVolume(p, at);
        if (hitPos)
            *hitPos = at;
        if (hitNormal)
            *hitNormal = p.n;
    }
    return hit;
}
// The sphere (radius `r`) moving from `from` along `dir`: the first plane it touches (already touching: at the start).
uint32_t VhSphereTest(const S_vector &from, const S_vector &dir, float r, S_vector *hitPos, S_vector *hitNormal, uint32_t excludeFlags)
{
    float best = 2.0f;
    uint32_t hit = 0;
    for (const S_VhPlane &p : GroundPlanes(excludeFlags)) {
        const float start = PlaneDistance(p, from);
        const float denominator = p.n.x * dir.x + p.n.y * dir.y + p.n.z * dir.z;
        float t;
        if (start < r && start > -r)
            t = 0.0f;
        else if (denominator < 0.0f && start >= r)
            t = (r - start) / denominator;
        else
            continue;
        if (!(t >= 0.0f) || !(t <= 1.0f) || !(t < best))
            continue;
        const S_vector centre(from.x + dir.x * t, from.y + dir.y * t, from.z + dir.z * t);
        const S_vector at(centre.x - p.n.x * r, centre.y - p.n.y * r, centre.z - p.n.z * r);
        if (!Within(p, at))
            continue;
        best = t;
        hit = HitVolume(p, at);
        if (hitPos)
            *hitPos = at;
        if (hitNormal)
            *hitNormal = p.n;
    }
    return hit;
}
// The box at placement `to`: a corner within 0.3 of a plane (or behind it).
bool VhBoxSweep(const I3D_bbox &box, const S_matrix &to, uint32_t excludeFlags)
{
    for (const S_VhPlane &p : GroundPlanes(excludeFlags))
        for (int corner = 0; corner < 8; ++corner) {
            const S_vector c(corner & 1 ? box.max.x : box.min.x, corner & 2 ? box.max.y : box.min.y, corner & 4 ? box.max.z : box.min.z);
            const S_vector w(c.x * to.m[0][0] + c.y * to.m[1][0] + c.z * to.m[2][0] + to.m[3][0],
                             c.x * to.m[0][1] + c.y * to.m[1][1] + c.z * to.m[2][1] + to.m[3][1],
                             c.x * to.m[0][2] + c.y * to.m[1][2] + c.z * to.m[2][2] + to.m[3][2]);
            if (PlaneDistance(p, w) < 0.3f && Within(p, w))
                return true;
        }
    return false;
}

// A frame the engine makes (CreateFrame, the skid marks' models' children): outside the world's list, at the origin.
uint8_t *LooseFrame(int type, const std::string &name)
{
    uint8_t *b = Block(0x600);
    *reinterpret_cast<void ***>(b) = EngineVtable(FrameClass(type));
    I3D_frame *f = AsFrame(b);
    f->m_name = Text(name);
    f->m_type = E_I3D_FRAME_TYPE(type);
    f->m_refCount = 1;
    f->m_localMatrix.Identity();
    f->m_rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
    f->m_scale = S_vector(1.0f, 1.0f, 1.0f);
    f->m_frameFlags = 1u | FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
    Name(b, "frame " + name);
    return b;
}
int g_framesMade[2];
bool g_skidFails;                                       // (the case's: the skid marks' second model not found)
void SetUpVhAnswers()
{
    // A particle visual's render state: the colours (0x1D, 0x1E) passed by their address, the rest by value.
    g_extraQuiet.insert("I3D_particle_visual::SetRenderState");
    g_answers["I3D_particle_visual::SetRenderState"] = [](const std::vector<uint32_t> &a) {
        const bool colour = a[1] == 0x1D || a[1] == 0x1E;
        g_log[g_side].push_back(F("I3D_particle_visual::SetRenderState(%s, %u, ", NameOf(a[0]).c_str(), a[1]) +
                                (colour && a[2] ? VN(reinterpret_cast<const void *>(a[2]), 3) : F("%u", a[2])) + ")");
        return 0u;
    };
    // (The colours' conversions: the three values passed through, the same on both sides. The colour's alpha is not
    // shown above: the original leaves it unset, the port's Fix gives the effect's.)
    g_answers["RgbToHsv"] = [](const std::vector<uint32_t> &a) {
        std::memcpy(reinterpret_cast<void *>(a[1]), reinterpret_cast<const void *>(a[0]), 12);
        return 0u;
    };
    g_answers["HsvToRgb"] = g_answers["RgbToHsv"];
    // The ground's queries (their hits follow from the arguments the calls' lines show, the same on both sides).
    g_answers["CollisionRayTest"] = [](const std::vector<uint32_t> &a) {
        return VhRayTest(*reinterpret_cast<const S_vector *>(a[0]), *reinterpret_cast<const S_vector *>(a[1]),
                         reinterpret_cast<S_vector *>(a[2]), reinterpret_cast<S_vector *>(a[3]), a[5]);
    };
    g_answers["CollisionSphereTest"] = [](const std::vector<uint32_t> &a) {
        float r;
        std::memcpy(&r, &a[2], 4);
        return VhSphereTest(*reinterpret_cast<const S_vector *>(a[0]), *reinterpret_cast<const S_vector *>(a[1]), r,
                            reinterpret_cast<S_vector *>(a[3]), reinterpret_cast<S_vector *>(a[4]), a[6]);
    };
    g_answers["C_CollisionWorld::TestBoxSweep"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(VhBoxSweep(*reinterpret_cast<const I3D_bbox *>(a[1]), *reinterpret_cast<const S_matrix *>(a[3]), a[8]));
    };
    // (A visual of the type asked for: a projector's or a particle visual's class, else an object visual.)
    g_answers["I3D_driver::CreateVisual"] = [](const std::vector<uint32_t> &a) {
        uint8_t *v = LooseFrame(FRAME_VISUAL, F("visual%d", ++g_framesMade[g_side]));
        const char *cls = a[1] == I3DVISUAL_PROJECTOR ? "I3D_projector_visual" : a[1] == I3DVISUAL_EMITTER ? "I3D_particle_visual"
                                                                                                         : "I3D_object_visual";
        *reinterpret_cast<void ***>(v) = EngineVtable(cls);
        return Ptr(v);
    };
    g_answers["I3D_driver::CreateFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(LooseFrame(int(a[1]), F("new%d", ++g_framesMade[g_side])));
    };
    // The model cache: the skid marks' models get a visual child with a quad's mesh (any other: not found).
    g_answers["C_ResourceCache<I3D_model>::OpenInto"] = [](const std::vector<uint32_t> &a) {
        const std::string file = a[2] ? Lower(reinterpret_cast<const char *>(a[2])) : std::string();
        if ((file != "shlaif01.i3d" && file != "shlaif02.i3d") || (g_skidFails && file == "shlaif02.i3d"))
            return 1u;
        uint8_t *model = reinterpret_cast<uint8_t *>(a[1]);
        uint8_t *child = LooseFrame(FRAME_VISUAL, file.substr(0, 8) + F(" %d", g_framesMade[g_side]));
        static const float quad[] = {-0.1f, 0.0f, -0.1f, 0.1f, 0.0f, -0.1f, 0.1f, 0.0f, 0.1f, -0.1f, 0.0f, 0.1f};
        uint8_t *mesh = NewMesh("mesh " + file);
        MeshLevel(mesh, 0) = NewLevel(quad, 4, g_modelMaterial[g_side]);
        Mut<uint32_t>(mesh, 0x44) = 1;
        Mut<uint8_t *>(child, 0x1E0) = mesh;
        LinkLast(AsFrame(child), AsFrame(model));
        return 0u;
    };
    g_answers["I3D_frame::LinkTo"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        Unlink(f);
        if (a[1])
            LinkLast(f, reinterpret_cast<I3D_frame *>(a[1]));
        StaleTree(f);
        return 0u;
    };
    g_answers["I3D_model::RemoveModelFrame"] = [](const std::vector<uint32_t> &) { return 0u; };
    // The meshes (see NewMesh).
    g_answers["I3D_mesh::GetLevel"] = [](const std::vector<uint32_t> &a) {
        uint8_t *m = reinterpret_cast<uint8_t *>(a[0]);
        const int i = int(a[1]);
        return i >= 0 && uint32_t(i) < At<uint32_t>(m, 0x44) && i < MESH_LEVELS ? Ptr(MeshLevel(m, i)) : 0u;
    };
    g_answers["I3D_mesh::SetLevel"] = [](const std::vector<uint32_t> &a) {
        uint8_t *m = reinterpret_cast<uint8_t *>(a[0]);
        const int i = int(a[2]);
        if (i < 0 || i >= MESH_LEVELS)
            return 0u;
        if (!a[1]) {
            // (Null drops the level and the ones after it.)
            if (uint32_t(i) < At<uint32_t>(m, 0x44))
                Mut<uint32_t>(m, 0x44) = uint32_t(i);
            return 0u;
        }
        MeshLevel(m, i) = reinterpret_cast<uint8_t *>(a[1]);
        Mut<uint32_t>(m, 0x44) = std::max(At<uint32_t>(m, 0x44), uint32_t(i + 1));
        return 0u;
    };
    g_answers["I3D_mesh::Duplicate"] = [](const std::vector<uint32_t> &a) {
        if (!a[1])
            return 1u;
        CopyMesh(reinterpret_cast<uint8_t *>(a[0]), reinterpret_cast<const uint8_t *>(a[1]));
        return 0u;
    };
    g_answers["I3D_mesh::Release"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(--Mut<int>(reinterpret_cast<uint8_t *>(a[0]), 0x48));
    };
    g_answers["I3D_mesh_level::GetVertices"] = [](const std::vector<uint32_t> &a) {
        return Ptr(At<uint8_t *>(reinterpret_cast<uint8_t *>(a[0]), 0x60));
    };
    g_answers["I3D_mesh_level::GetFaceGroup"] = [](const std::vector<uint32_t> &a) {
        return a[1] == 0 ? Ptr(At<uint8_t *>(reinterpret_cast<uint8_t *>(a[0]), 0x5C)) : 0u;
    };
    g_answers["I3D_driver::CreateMesh"] = [](const std::vector<uint32_t> &) {
        return Ptr(NewMesh(F("made mesh %d", ++g_meshesMade[g_side])));
    };
    g_answers["I3D_object_visual::DuplicateMesh"] = [](const std::vector<uint32_t> &a) {
        uint8_t *v = reinterpret_cast<uint8_t *>(a[0]);
        if (const uint8_t *from = At<uint8_t *>(v, 0x1E0)) {
            uint8_t *m = NewMesh(F("made mesh %d", ++g_meshesMade[g_side]));
            CopyMesh(m, from);
            Mut<uint8_t *>(v, 0x1E0) = m;
        }
        return 0u;
    };
    g_answers["I3D_object_visual::SetMesh"] = [](const std::vector<uint32_t> &a) {
        uint8_t *mesh = reinterpret_cast<uint8_t *>(a[1]);
        if (mesh)
            ++Mut<int>(mesh, 0x48);
        Mut<uint8_t *>(reinterpret_cast<uint8_t *>(a[0]), 0x1E0) = mesh;
        return 0u;
    };
    g_answers["I3D_face_group::SetMaterial"] = [](const std::vector<uint32_t> &a) {
        uint8_t *material = reinterpret_cast<uint8_t *>(a[1]);
        if (material)
            ++Mut<int>(material, 4);
        Mut<uint8_t *>(reinterpret_cast<uint8_t *>(a[0]), 4) = material;
        return 0u;
    };
    g_answers["I3D_material::Release"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(--Mut<int>(reinterpret_cast<uint8_t *>(a[0]), 4));
    };
    // IGraph: LoadTexture makes a texture named after its files (0: done).
    g_answers["GetIGraph"] = [](const std::vector<uint32_t> &) {
        static uint8_t *igraph[2];
        if (!igraph[g_side]) {
            igraph[g_side] = static_cast<uint8_t *>(_aligned_malloc(0x40, 16));
            std::memset(igraph[g_side], 0, 0x40);
            *reinterpret_cast<void ***>(igraph[g_side]) = EngineVtable("IGraph");
        }
        Name(igraph[g_side], "igraph");
        return Ptr(igraph[g_side]);
    };
    g_answers["IGraph::LoadTexture"] = [](const std::vector<uint32_t> &a) {
        uint8_t *t = Block(0x40);
        *reinterpret_cast<void ***>(t) = EngineVtable("I3D_texture");
        Mut<int>(t, 4) = 1;
        Name(t, "texture " + ArgText('s', a[1]) + " " + ArgText('s', a[2]));
        if (a[4])
            *reinterpret_cast<uint8_t **>(a[4]) = t;
        return 0u;
    };
    g_answers["I3D_texture::Release"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(--Mut<int>(reinterpret_cast<uint8_t *>(a[0]), 4));
    };
    g_answers["I3D_driver::CreateMaterial"] = [](const std::vector<uint32_t> &) {
        return Ptr(NewMaterial(F("made material %d", ++g_meshesMade[g_side])));
    };
    // (A copy takes the flags and the texture.)
    g_answers["I3D_material::CopyFrom"] = [](const std::vector<uint32_t> &a) {
        uint8_t *m = reinterpret_cast<uint8_t *>(a[0]);
        if (const uint8_t *from = reinterpret_cast<const uint8_t *>(a[1])) {
            Mut<uint32_t>(m, 8) = At<uint32_t>(from, 8);
            Mut<uint32_t>(m, 0xC) = At<uint32_t>(from, 0xC);
        }
        return 0u;
    };
    g_answers["I3D_material::SetTexture"] = [](const std::vector<uint32_t> &a) {
        Mut<uint32_t>(reinterpret_cast<uint8_t *>(a[0]), 0xC) = a[1];
        return 0u;
    };
    g_answers["I3D_driver::GetHierarchyBBox"] = [](const std::vector<uint32_t> &a) {
        I3D_bbox &out = *reinterpret_cast<I3D_bbox *>(a[1]);
        out = I3D_bbox::Empty();
        S_matrix identity;
        identity.Identity();
        ExpandBox(out, reinterpret_cast<I3D_frame *>(a[2]), identity);
        return a[1];
    };
    // (Logged without the callback and its context: each side's own.)
    g_extraQuiet.insert("I3D_frame::EnumFrames");
    g_answers["I3D_frame::EnumFrames"] = [](const std::vector<uint32_t> &a) {
        g_log[g_side].push_back(F("I3D_frame::EnumFrames(%s, types %08x, mask %s)", NameOf(a[0]).c_str(), a[3], ArgText('s', a[4]).c_str()));
        Enumerate(reinterpret_cast<I3D_frame *>(a[0]), reinterpret_cast<T_Enum>(a[1]), reinterpret_cast<void *>(a[2]), a[3],
                  reinterpret_cast<const char *>(a[4]));
        return 0u;
    };
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(FindFrame(reinterpret_cast<I3D_frame *>(a[0]), reinterpret_cast<const char *>(a[1]), a[2]));
    };
}

// ---- the vehicle's callbacks (as C_Car::SetFrame sets them after Setup: all but the impulse's, the seat's and the
// collision's), logged on each side; a wheel's or a body point's ground passed on but every fifth taken away ----
#define VH_FC __attribute__((fastcall))
int g_callbackCalls[2];
std::string VP(const void *p, int n = 3) { return p ? VN(p, n) : std::string("null"); }
void VH_FC CbActorImpulse(void *context, S_CollisionVolume *hit, const S_vector4 *point, const S_vector4 *impulse,
                          const S_vector *normal, float mass)
{
    g_log[g_side].push_back("callback ActorImpulse " + NameOf(Ptr(context)) + " " + NameOf(Ptr(hit)) + " " + VP(point) + " " +
                            VP(impulse) + " " + VP(normal) + " " + Fl(mass));
}
void VH_FC CbActorTouch(void *context, S_CollisionVolume *hit, const S_vector4 *point, const S_vector4 *velocity, const S_vector *normal)
{
    g_log[g_side].push_back("callback ActorTouch " + NameOf(Ptr(hit)) + " " + VP(point) + " " + VP(velocity) + " " + VP(normal));
}
// (A moving actor's side as C_Car's: every third contact refused; the rest a heavy body moving slowly, half of them
// pushing.)
bool VH_FC CbActorResponse(void *vehicle, S_CollisionVolume *hit, const S_vector4 *point, const S_vector *normal, S_vector4 *arm,
                           S_vector4 *velocity, float *mass, S_vector4 *turn, float *scale, bool *push)
{
    g_log[g_side].push_back("callback ActorResponse " + NameOf(Ptr(hit)) + " " + VP(point) + " " + VP(normal));
    const int n = ++g_callbackCalls[g_side];
    if (n % 3 == 0)
        return false;
    *arm = {point->x * 0.1f, 0.5f, point->z * -0.1f, 0.0f};
    *velocity = {0.5f, 0.0f, -0.25f, 0.0f};
    *mass = n % 2 ? 1500.0f : 80.0f;
    *turn = {0.0001f, 0.0002f, -0.0001f, 0.0f};
    *scale = n % 4 ? -1.2f : 0.5f;
    *push = n % 2 != 0;
    return true;
}
bool VH_FC CbActorFirm(void *context, S_CollisionVolume *hit, bool *deform)
{
    g_log[g_side].push_back("callback ActorFirm " + NameOf(Ptr(hit)));
    const int n = ++g_callbackCalls[g_side];
    if (n % 5 == 0)
        *deform = false;
    return n % 4 != 0;
}
void VH_FC CbImpact(void *context, const S_vector *impulse, const S_vector *largest, const S_vector4 *point, float radius,
                    const S_vector *velocity, C_Actor *actor, const S_CollisionVolume *volume, int unknown)
{
    g_log[g_side].push_back("callback Impact " + VP(impulse) + " " + VP(largest) + " " + VP(point) + " " + Fl(radius) + " " +
                            VP(velocity) + " " + NameOf(Ptr(actor)) + " " + NameOf(Ptr(volume)) + F(" %d", unknown));
}
bool VH_FC CbPartOff(void *context, int index, const S_vector *velocity, const S_vector *push)
{
    g_log[g_side].push_back(F("callback PartOff %d ", index) + VP(velocity) + " " + VP(push));
    return true;
}
bool VH_FC CbWheelOff(void *context, int index, const S_vector *velocity, const S_vector *push)
{
    g_log[g_side].push_back(F("callback WheelOff %d ", index) + VP(velocity) + " " + VP(push));
    return true;
}
void VH_FC CbGlass(void *context, int part, const S_vector *velocity, const char *sound)
{
    g_log[g_side].push_back(F("callback GlassBroken %d ", part) + VP(velocity) + " " + ArgText('s', Ptr(sound)));
}
S_CollisionVolume *VH_FC CbBodyGround(void *context, S_CollisionVolume *hit, S_vector *hitPos, S_vector *normal)
{
    g_log[g_side].push_back("callback BodyGround " + NameOf(Ptr(hit)) + " " + VP(hitPos) + " " + VP(normal));
    return ++g_callbackCalls[g_side] % 5 ? hit : nullptr;
}
S_CollisionVolume *VH_FC CbWheelGround(void *context, S_CollisionVolume *ground, int wheel)
{
    g_log[g_side].push_back("callback WheelGround " + NameOf(Ptr(ground)) + F(" %d", wheel));
    return ++g_callbackCalls[g_side] % 5 ? ground : nullptr;
}
void VH_FC CbPartHit(void *context, I3D_frame *frame, int type, const S_vector4 *point, const S_vector *impulse, bool dented)
{
    g_log[g_side].push_back("callback PartHit " + NameOf(Ptr(frame)) + F(" %d ", type) + VP(point) + " " + VP(impulse) + F(" %d", int(dented)));
}
void VH_FC CbRepaired(void *context) { g_log[g_side].push_back("callback Repaired"); }
// (As C_Car's: the effect set up on the emitter, every third refused; another emitter's slot reused now and then.)
int EmitterIndex(const void *emitter)
{
    const uint8_t *base = g_side ? *OriginalAt<uint8_t **>(O_CAR_EMITTERS) : reinterpret_cast<const uint8_t *>(g_carEmitters.data());
    return base ? int((static_cast<const uint8_t *>(emitter) - base) / (g_side ? 0x30 : int(sizeof(S_CarEmitter)))) : -1;
}
bool VH_FC CbSetupEmitter(struct S_CarEmitter *emitter, uint32_t effect)
{
    g_log[g_side].push_back(F("callback SetupEmitter %d %x", EmitterIndex(emitter), effect));
    // (Not an effect past the definitions, as C_Car's.)
    if (++g_callbackCalls[g_side] % 3 == 0 || effect >= g_carEffectCount)
        return false;
    emitter->effect = int(effect);
    emitter->velocity = {0.0f, 1.0f, 0.5f, 0.0f};
    emitter->velocity2 = {0.25f, 0.5f, 0.0f, 0.0f};
    return true;
}
int VH_FC CbReuseEmitter(int slot, uint32_t effect)
{
    g_log[g_side].push_back(F("callback ReuseEmitter %d %x", slot, effect));
    return ++g_callbackCalls[g_side] % 4 == 0 ? slot : -1;
}
// The pool on each side (the original's at O_CAR_EMITTERS: data, capacity, count, growth): the case's emitters, their
// particle visuals, every fourth taken already, effects 0..4.
void BuildEmitters(int count)
{
    std::vector<S_CarEmitter> pool(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        S_CarEmitter &e = pool[size_t(i)];
        std::memset(&e, 0, sizeof e);
        uint8_t *visual = LooseFrame(FRAME_VISUAL, F("Car_particle %d", i));
        *reinterpret_cast<void ***>(visual) = EngineVtable("I3D_particle_visual");
        e.visual = reinterpret_cast<I3D_particle_visual *>(visual);
        e.effect = i % 8;
        e.active = i % 4 == 3;
        e.velocity = {float(i), 1.0f, 0.0f, 0.0f};
    }
    if (g_side) {
        uint8_t *data = count ? Block(size_t(count) * 0x30) : nullptr;
        for (int i = 0; i < count; ++i)
            std::memcpy(data + 0x30 * i, &pool[size_t(i)], 0x2A);   // (the record's 0x2A bytes; the original's stride 0x30)
        *OriginalAt<uint8_t **>(O_CAR_EMITTERS) = data;
        *OriginalAt<int *>(O_CAR_EMITTERS + 4) = count;
        *OriginalAt<int *>(O_CAR_EMITTERS + 8) = count;
        *OriginalAt<int *>(O_CAR_EMITTERS + 12) = 2;
    } else {
        g_carEmitters = pool;
    }
}
std::string EmitterState()
{
    std::string s = "emitters";
    const int count = g_side ? *OriginalAt<int *>(O_CAR_EMITTERS + 8) : int(g_carEmitters.size());
    const uint8_t *base = g_side ? *OriginalAt<uint8_t **>(O_CAR_EMITTERS) : reinterpret_cast<const uint8_t *>(g_carEmitters.data());
    for (int i = 0; i < count; ++i) {
        const uint8_t *e = base + (g_side ? 0x30 : sizeof(S_CarEmitter)) * size_t(i);
        s += F(" %d:%d%d", At<int>(e, 0x24), int(At<uint8_t>(e, 0x28)), int(At<uint8_t>(e, 0x29))) + "/" + V3(e) + V3(e + 0x10);
    }
    return s;
}
void SetVhCallbacks(uint8_t *v)
{
    void *const callbacks[VEHICLE_CALLBACKS] = {
        reinterpret_cast<void *>(&CbActorImpulse), reinterpret_cast<void *>(&CbActorTouch), reinterpret_cast<void *>(&CbActorResponse),
        reinterpret_cast<void *>(&CbActorFirm), reinterpret_cast<void *>(&CbImpact), reinterpret_cast<void *>(&CbPartOff),
        reinterpret_cast<void *>(&CbWheelOff), reinterpret_cast<void *>(&CbGlass), nullptr, nullptr,
        reinterpret_cast<void *>(&CbBodyGround), reinterpret_cast<void *>(&CbWheelGround), reinterpret_cast<void *>(&CbPartHit),
        reinterpret_cast<void *>(&CbRepaired), nullptr, reinterpret_cast<void *>(&CbSetupEmitter),
        reinterpret_cast<void *>(&CbReuseEmitter)};
    for (int i = 0; i < VEHICLE_CALLBACKS; ++i) {
        Mut<void *>(v, g_side ? 0x680 + 4 * uint32_t(i) : uint32_t(offsetof(C_Vehicle, m_callbacks) + 4 * size_t(i))) = callbacks[i];
        Name(callbacks[i], F("callback %d", i));
    }
}

// ---- the compared state ----
std::vector<std::string> VhState()
{
    std::vector<std::string> out;
    const uint8_t *o = g_side ? g_vehicle[1] : nullptr;
    const C_Vehicle *p = g_side ? nullptr : Vehicle();
    Fields_C_Vehicle(o, p, "v", out);
    const int count = g_side ? At<int>(o, 0x7C0) : p->m_wheelCount;
    const uint32_t wheels = g_side ? At<uint32_t>(o, 0xF64) : Ptr(p->m_wheels);
    for (int k = 0; k < count && wheels && k < 32; ++k) {
        const uint32_t w = reinterpret_cast<const uint32_t *>(wheels)[k];
        if (!w) {
            out.push_back(F("wheel%d null", k));
            continue;
        }
        Fields_S_VehicleWheel(g_side ? reinterpret_cast<const uint8_t *>(w) : nullptr,
                              g_side ? nullptr : reinterpret_cast<const S_VehicleWheel *>(w), F("wheel%d", k), out);
    }
    // The body collision boxes (a tree: each box's points, children, then the next).
    std::vector<std::pair<uint32_t, std::string>> todo = {{g_side ? At<uint32_t>(o, 0x794) : Ptr(p->m_bodyCollision), "box"}};
    int shown = 0;
    while (!todo.empty() && shown < 200) {
        uint32_t b = todo.back().first;
        const std::string prefix = todo.back().second;
        todo.pop_back();
        for (int n = 0; b && shown < 200; ++n, ++shown) {
            const std::string bp = prefix + F("%d", n);
            const uint8_t *bo = g_side ? reinterpret_cast<const uint8_t *>(b) : nullptr;
            const S_BodyCollisionBox *bb = g_side ? nullptr : reinterpret_cast<const S_BodyCollisionBox *>(b);
            Fields_S_BodyCollisionBox(bo, bb, bp, out);
            uint32_t pt = g_side ? At<uint32_t>(bo, 0x60) : Ptr(bb->points);
            for (int k = 0; pt && k < 64; ++k) {
                const uint8_t *po = g_side ? reinterpret_cast<const uint8_t *>(pt) : nullptr;
                const S_BodyCollisionPoint *pp = g_side ? nullptr : reinterpret_cast<const S_BodyCollisionPoint *>(pt);
                Fields_S_BodyCollisionPoint(po, pp, bp + F(".pt%d", k), out);
                pt = g_side ? At<uint32_t>(po, 0x48) : Ptr(pp->next);
            }
            const uint32_t children = g_side ? At<uint32_t>(bo, 0x64) : Ptr(bb->children);
            if (children)
                todo.push_back({children, bp + "."});
            b = g_side ? At<uint32_t>(bo, 0x74) : Ptr(bb->next);
        }
    }
    return out;
}
// The state after most operations: the model's place, the motion, the drive, each wheel's place and contact.
std::vector<std::string> VhCompactState()
{
    std::vector<std::string> out;
    const uint8_t *o = g_side ? g_vehicle[1] : nullptr;
    const C_Vehicle *p = g_side ? nullptr : Vehicle();
    const I3D_frame *model = AsFrame(g_modelFrame[g_side]);
    out.push_back("model " + VN(&model->m_localMatrix, 16));
#define VH_FIELD(off, member) (g_side ? static_cast<const void *>(o + (off)) : static_cast<const void *>(&p->member))
    out.push_back("velocity " + V3(VH_FIELD(0x410, m_velocity)) + " spin " + V3(VH_FIELD(0x210, m_unknown210)) + " pos " +
                  V3(VH_FIELD(0x160, m_modelPos)) + " com " + V3(VH_FIELD(0x1B0, m_unknown1B0)));
    out.push_back("revs " + Fl(At<float>(VH_FIELD(0x864, m_engineRevs), 0)) + F(" gear %d/%d", At<int>(VH_FIELD(0x874, m_gear), 0),
                                                                                At<int>(VH_FIELD(0x878, m_requestedGear), 0)) +
                  " speed " + Fl(At<float>(VH_FIELD(0x8C8, m_speed), 0)) + " throttle " + Fl(At<float>(VH_FIELD(0x6FC, m_throttle), 0)) +
                  " brake " + Fl(At<float>(VH_FIELD(0x8E4, m_brake), 0)) + " steer " + Fl(At<float>(VH_FIELD(0x948, m_steering), 0)) +
                  "/" + Fl(At<float>(VH_FIELD(0x94C, m_unknown94C), 0)) + F(" substeps %d rest %d/%d", int(At<uint16_t>(VH_FIELD(0x2280, m_substeps), 0)),
                                                                           At<int>(VH_FIELD(0x500, m_unknown500), 0), At<int>(VH_FIELD(0x604, m_unknown604), 0)));
#undef VH_FIELD
    out.push_back(EmitterState());
    const int count = g_side ? At<int>(o, 0x7C0) : p->m_wheelCount;
    const uint32_t wheels = g_side ? At<uint32_t>(o, 0xF64) : Ptr(p->m_wheels);
    for (int k = 0; k < count && wheels && k < 32; ++k) {
        const uint8_t *w = reinterpret_cast<const uint8_t *>(reinterpret_cast<const uint32_t *>(wheels)[k]);
        if (!w)
            continue;
        const S_VehicleWheel *pw = reinterpret_cast<const S_VehicleWheel *>(w);
#define WH_FIELD(off, member) (g_side ? static_cast<const void *>(w + (off)) : static_cast<const void *>(&pw->member))
        out.push_back(F("wheel%d ", k) + V3(WH_FIELD(0x40, worldPos)) + " contact " + V3(WH_FIELD(0x50, unknown050)) + " normal " +
                      V3(WH_FIELD(0x80, unknown080)) + " ext " + Fl(At<float>(WH_FIELD(0x16C, extension), 0)) + " load " +
                      Fl(At<float>(WH_FIELD(0x168, unknown168), 0)) + F(" flags %x ground ", At<uint32_t>(WH_FIELD(0x1FC, flags), 0)) +
                      NameOf(At<uint32_t>(WH_FIELD(0x200, ground), 0)));
#undef WH_FIELD
    }
    return out;
}
std::vector<std::string> VhFrameState()
{
    std::vector<std::string> out;
    for (uint8_t *b : g_modelFrames[g_side]) {
        const I3D_frame *f = AsFrame(b);
        out.push_back(F("frame %s flags %08x parent %s local ", f->m_name, f->m_frameFlags, NameOf(Ptr(f->m_parent)).c_str()) +
                      VN(&f->m_localMatrix, 16) + " scale " + V3(&f->m_scale));
    }
    return out;
}

// ---- the cases ----
int g_vhCases, g_opCounts[OP_KINDS];
float RF(int lo, int hi, float scale) { return float(I(lo, hi)) * scale; }
S_VhCase RandomVhCase()
{
    S_VhCase c;
    c.model = I(0, int(g_vhModels.size()) - 1);
    // The ground: its height, a tilt now and then, materials (stripes now and then), water, a wall ahead or to the side.
    S_VhGround &g = c.ground;
    g.y = RF(-50, 50, 0.1f);
    if (P(0.4)) {
        g.tiltX = RF(-30, 30, 0.01f);
        g.tiltZ = RF(-30, 30, 0.01f);
    }
    static const int materials[] = {1, 2, 5, 0x23, 0x30, 0x40, 0x55, 0x5F, 10, 0x61};
    g.material = materials[I(0, 9)];
    g.stripes = P(0.3);
    for (int &m : g.stripeMaterials)
        m = materials[I(0, 9)];
    g.stripeWidth = RF(5, 40, 0.1f);
    g.water = P(0.15);
    g.waterY = g.y + RF(-5, 15, 0.1f);
    g.waterMaterial = P(0.7) ? 0x23 : materials[I(0, 9)];
    g.wall = P(0.2);
    g.wallX = RF(20, 150, 0.1f);
    g.wallMaterial = materials[I(0, 9)];
    g.platform = P(0.25);
    g.platformMaterial = materials[I(0, 9)];
    g.platformActor = I(0, 1000);
    // The car: on the ground mostly (a little above or below it), dropped from up to 5 m now and then.
    c.pos[0] = RF(-100, 100, 0.1f);
    c.pos[2] = RF(-100, 100, 0.1f);
    g_ground = g;
    c.pos[1] = GroundHeight(c.pos[0], c.pos[2]) + (P(0.8) ? RF(-10, 30, 0.01f) : RF(30, 500, 0.01f));
    // (The platform under the car or beside it: a little above the ground, the car on it or driving onto it.)
    g.platformY = GroundHeight(c.pos[0], c.pos[2]) + RF(0, 40, 0.01f);
    g.platformMinX = c.pos[0] + RF(-60, 10, 0.1f);
    g.platformMaxX = g.platformMinX + RF(5, 80, 0.1f);
    if (g.platform && P(0.5))
        c.pos[1] = g.platformY + RF(-5, 30, 0.01f);
    if (P(0.7)) {
        const float a = RF(-314, 314, 0.01f);
        c.rot[0] = std::cos(a * 0.5f);
        c.rot[2] = std::sin(a * 0.5f);
    }
    // A parameter block: mostly one for as many axles as the model's wheels make.
    if (!g_vhParams.empty()) {
        int axles = 0;
        for (const S_VhFrame &f : g_vhModels[size_t(c.model)].frames)
            axles += (f.name.size() >= 2 && std::toupper(static_cast<unsigned char>(f.name[0])) == 'W' &&
                      std::toupper(static_cast<unsigned char>(f.name[1])) == 'H');
        axles /= 2;
        std::vector<int> matching;
        for (size_t i = 0; i < g_vhParams.size(); ++i)
            if (g_vhParams[i].axles == axles)
                matching.push_back(int(i));
        c.params = !matching.empty() && P(0.85) ? matching[size_t(I(0, int(matching.size()) - 1))] : I(0, int(g_vhParams.size()) - 1);
    }
    c.skidMarks = P(0.05) ? 50 : I(0, 3);
    c.windows = P(0.3);
    c.emitters = P(0.2) ? 0 : I(1, 12);
    c.groups = P(0.3) ? I(0, 2) : -1;
    c.destroy = P(0.5);
    c.skidFails = P(0.1);
    c.f74 = P(0.1) ? 1u : 0u;
    c.bumpSound = P(0.5);
    if (P(0.95))
        c.ops.push_back({OP_APPLY_PARAMS, P(0.5) ? 1 : 0, P(0.4) ? 0 : 1, I(0, 100000), I(0, 100000)});
    if (P(0.9))
        c.ops.push_back({OP_START, 0, 0, 0, 0});
    // Then driving: the engine started mostly, the controls set now and then, the ticks the most.
    if (P(0.8))
        c.ops.push_back({OP_ENGINE, 1, I(0, 1), 0, 0});
    if (P(0.75))
        c.ops.push_back({OP_DRIVER, 1, 0, 0, 0});
    // (A quarter of the cases: blocks with a word spoiled applied again, with and without the fix.)
    if (P(0.25))
        for (int k = 0; k < 2; ++k)
            c.ops.push_back({OP_APPLY_PARAMS, k, 0, I(0, 100000), I(0, 100000)});
    // (A sixth of the cases driven in the drive's mode 1, the two throttle channels, from the start.)
    if (P(0.17))
        c.ops.push_back({OP_FIELDS, 0, 1, 0, 0});
    static const int opScale = std::getenv("HD2_VH_OPS") ? std::atoi(std::getenv("HD2_VH_OPS")) : 100;   // (percent)
    const int count = (P(0.1) ? I(60, 150) : I(10, 50)) * opScale / 100;
    // (Now and then left alone for a while: the car comes to rest.)
    if (P(0.15)) {
        const int quiet = I(20, 40) * opScale / 100;
        c.ops.push_back({OP_THROTTLE, 0, 1, 0, 0});
        for (int k = 0; k < quiet; ++k)
            c.ops.push_back({OP_TICK, 0, 0, 0, 0});
        // (Asleep, then kept awake now and then.)
        if (P(0.4))
            c.ops.push_back({OP_FIELDS, 3, 1, 0, 0});
    }
    // (A tenth of the cases driven hard through the gears for a while, lifted and pressed again: the gearbox's
    // shifts; their steps alone now and then.)
    if (P(0.1)) {
        c.ops.push_back({OP_GEAR, 3, 0, 0, 0});
        c.ops.push_back({OP_THROTTLE, 102, 1, 0, 0});
        const int hard = I(30, 70) * opScale / 100;
        for (int k = 0; k < hard; ++k)
            c.ops.push_back({P(0.3) ? OP_STEP : OP_TICK, 0, 0, 0, 0});
        // (A light brake, under 0.3, keeps the gearbox off its coasting branch: the down-shifts and the kickdown,
        // held through the throttle pressed again half the time.)
        c.ops.push_back({OP_THROTTLE, I(0, 40), 1, 0, 0});
        const bool light = P(0.7);
        if (light)
            c.ops.push_back({OP_BRAKE, I(2, 29), 1, 0, 0});
        for (int k = light ? I(5, 40) : I(3, 15); k > 0; --k)
            c.ops.push_back({P(0.4) ? OP_STEP : OP_TICK, 0, 0, 0, 0});
        if (!light || P(0.5))
            c.ops.push_back({OP_BRAKE, 0, 1, 0, 0});
        c.ops.push_back({OP_THROTTLE, 102, 1, 0, 0});
        for (int k = I(10, 30); k > 0; --k)
            c.ops.push_back({P(0.3) ? OP_STEP : OP_TICK, 0, 0, 0, 0});
    }
    // (Now and then frozen for a while, then let go.)
    if (P(0.05)) {
        c.ops.push_back({OP_FIELDS, 4, 0, 0, 0});
        for (int k = I(2, 8); k > 0; --k)
            c.ops.push_back({OP_TICK, 0, 0, 0, 0});
        c.ops.push_back({OP_FIELDS, 4, 1, 0, 0});
    }
    for (int k = 0; k < count; ++k) {
        S_VhOp op;
        const int r = I(0, 999);
        op.kind = r < 300 ? OP_TICK : r < 380 ? OP_STEP : r < 420 ? OP_UPDATE : r < 470 ? OP_GEAR : r < 560 ? OP_THROTTLE
                : r < 610 ? OP_BRAKE : r < 690 ? OP_STEER : r < 720 ? OP_HANDBRAKE : r < 750 ? OP_ENGINE : r < 760 ? OP_FUEL
                : r < 775 ? OP_SET4F23B0 : r < 790 ? OP_SET4F2630 : r < 800 ? OP_SOUNDS : r < 840 ? OP_IMPULSE : r < 870 ? OP_FORCE
                : r < 885 ? OP_SETTLE : r < 905 ? OP_RESET : r < 925 ? OP_RESTART : r < 945 ? OP_SEAT_LOAD : r < 975 ? OP_GETTERS
                : r < 985 ? OP_STOP : r < 995 ? OP_START : OP_APPLY_PARAMS;
        // (The car's options now and then, its fields more rarely.)
        if (op.kind == OP_TICK && P(0.12))
            op.kind = P(0.7) ? OP_OPTIONS : OP_FIELDS;
        else if (op.kind == OP_TICK && P(0.1))
            op.kind = I(0, 3) == 0 ? OP_DRIVER : I(0, 1) ? OP_CAR_HIT : OP_VOLUMES;
        else if (op.kind == OP_TICK && P(0.06))
            op.kind = OP_STATE;
        else if (op.kind == OP_TICK && P(0.08))
            op.kind = OP_DOOR;
        else if (op.kind == OP_TICK && P(0.1))
            op.kind = OP_LIGHTS;
        else if (op.kind == OP_TICK && P(0.06))
            op.kind = OP_SOUNDS2;
        if (std::getenv("HD2_VH_SOLVE") && op.kind == OP_TICK && P(0.5))
            op.kind = OP_SOLVE;
        op.a = I(0, 100000);
        op.b = I(0, 100000);
        op.c = I(0, 100000);
        op.d = I(0, 100000);
        if (op.kind == OP_APPLY_PARAMS)
            op.a = P(0.3) ? 1 : 0;
        c.ops.push_back(op);
    }
    return c;
}
// A float from an op's number: a fraction of the range lo..hi in steps of 0.01 of it, its ends now and then.
float OpF(int n, float lo, float hi)
{
    const int k = n % 103;
    return k == 101 ? lo : k == 102 ? hi : lo + (hi - lo) * float(k) * 0.01f;
}
S_vector OpV(int n, float range)
{
    return S_vector(OpF(n, -range, range), OpF(n / 103, -range, range), OpF(n / 10609 + 7 * n, -range, range));
}

using T_VhVoid = void(TC *)(void *);
bool g_started[2];
std::string RunVhOp(const S_VhCase &c, const S_VhOp &op, int index)
{
    const bool o = g_side != 0;
    uint8_t *v = g_vehicle[g_side];
    C_Vehicle *p = Vehicle();
    switch (op.kind) {
    case OP_APPLY_PARAMS: {
        if (c.params < 0)
            return "ApplyParams (none)";
        S_VehicleParams params = g_vhParams[size_t(c.params)].params;
        // (Half the cases a bump sound in both groups: the shipped blocks have none.)
        const bool bump = c.bumpSound;
        if (bump) {
            std::strcpy(params.sounds1_9E4[6], "BUMP.WAV");
            std::strcpy(params.sounds2_E80[6], "BUMP.WAV");
        }
        // (Now and then one word of the block spoiled: below 0, just over 0, 0 or one past a likely range: the checks'
        // and the fix's ways.)
        std::string spoiled;
        if (op.b % 3 == 0) {
            const uint32_t word = uint32_t(op.c) % (0x6B8 / 4);   // (the checked fields: up to +0x6B4)
            uint8_t *at = reinterpret_cast<uint8_t *>(&params) + 4 * word;
            float f;
            std::memcpy(&f, at, 4);
            // (No huge value: the checks are lower bounds and ranges, and a huge accepted one runs the car into NaNs.)
            static const float bad[] = {-5.0f, 0.005f, 0.0f, 101.0f};
            const float value = bad[op.d % 4];
            // (A tyre id only -1, which the check refuses: another id unknown to tables\tyres.bin is the original's
            // weakness, see the blocks left out.)
            if (4 * word >= 0x2DC && 4 * word < 0x30C) {
                const int32_t none = -1;
                std::memcpy(at, &none, 4);
            } else if (f == f && std::fabs(f) < 1e7f && (f != 0.0f || op.d % 2))
                std::memcpy(at, &value, 4);
            else {
                const int32_t ivalue = op.d % 2 ? -1 : 100;
                std::memcpy(at, &ivalue, 4);
            }
            spoiled = F(" spoiled +0x%x", 4 * word);
        }
        const bool r = o ? OriginalAt<bool(TC *)(void *, S_VehicleParams *, int)>(O_VH_APPLY_PARAMS)(v, &params, op.a)
                         : p->ApplyParams(params, uint8_t(op.a));
        // (A spoiled block, refused part way or taken (a 5 g car), leaves values the car runs on into NaNs with: the
        // clean one applied again.)
        if (!spoiled.empty()) {
            S_VehicleParams clean = g_vhParams[size_t(c.params)].params;
            if (bump) {
                std::strcpy(clean.sounds1_9E4[6], "BUMP.WAV");
                std::strcpy(clean.sounds2_E80[6], "BUMP.WAV");
            }
            // (With the fix: a clean block can still be refused after a spoiled one, its values taken against the
            // spoiled ones.)
            const bool again = o ? OriginalAt<bool(TC *)(void *, S_VehicleParams *, int)>(O_VH_APPLY_PARAMS)(v, &clean, 1)
                                 : p->ApplyParams(clean, 1);
            spoiled += F(" clean again -> %d", int(again));
        }
        return F("ApplyParams %s flags %d -> %d", g_vhParams[size_t(c.params)].file.c_str(), op.a, int(r)) + spoiled;
    }
    case OP_START:
        // (Not twice without a Stop: the original then reads special collision records past the end; the port's Fix
        // stops there.)
        if (g_started[g_side])
            return "Start (started)";
        g_started[g_side] = true;
        if (o) {
            OriginalAt<T_VhVoid>(O_VH_START)(v);
            // (The skid marks' "fresh" flags the original leaves unset: the port's 0.)
            for (uint32_t w = At<uint32_t>(v, 0xF60); w; w = At<uint32_t>(reinterpret_cast<void *>(w), 0x278)) {
                uint8_t *wb = reinterpret_cast<uint8_t *>(w);
                for (uint32_t m = At<uint32_t>(wb, 0x208); m && m < At<uint32_t>(wb, 0x20C); m += 12)
                    Mut<uint8_t>(reinterpret_cast<void *>(m), 9) = 0;
            }
        } else {
            p->Start();
        }
        return "Start";
    case OP_STOP:
        g_started[g_side] = false;
        if (o)
            OriginalAt<T_VhVoid>(O_VH_STOP)(v);
        else
            p->Stop();
        return "Stop";
    case OP_STEP:
    case OP_UPDATE:
    case OP_TICK: {
        // (Mostly the game's 83 ms; now and then shorter or longer.)
        static const uint32_t times[] = {83, 83, 83, 83, 83, 40, 10, 1, 120, 250, 0, 33};
        const uint32_t ms = times[op.a % 12];
        std::string s = F("%s %u", OP_KIND_NAMES[op.kind], ms);
        if (op.kind != OP_UPDATE) {
            if (o)
                OriginalAt<void(TC *)(void *, uint32_t)>(O_VH_STEP)(v, ms);
            else
                p->Step(ms);
        }
        if (op.kind != OP_STEP) {
            const int r = o ? OriginalAt<int(TC *)(void *, uint32_t)>(O_VH_UPDATE)(v, ms) : p->Update(ms);
            s += F(" -> %d", r);
        }
        return s;
    }
    case OP_GEAR: {
        const int gear = op.a % 14 - 2;
        const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_VH_SET_GEAR)(v, gear) : p->SetGear(gear);
        return F("SetGear %d -> %d", gear, int(r));
    }
    case OP_THROTTLE: {
        const float value = OpF(op.a, op.b % 5 ? 0.0f : -0.5f, op.b % 7 ? 1.0f : 1.5f);
        const int first = op.c % 2;
        const bool r = o ? OriginalAt<bool(TC *)(void *, float, int)>(O_VH_SET_THROTTLE)(v, value, first) : p->SetThrottle(value, first != 0);
        return "SetThrottle " + Fl(value) + F(" %d -> %d", first, int(r));
    }
    case OP_BRAKE: {
        const float value = OpF(op.a, op.b % 5 ? 0.0f : -0.5f, op.b % 7 ? 1.0f : 1.5f);
        const bool r = o ? OriginalAt<bool(TC *)(void *, float)>(O_VH_SET_BRAKE)(v, value) : p->SetBrake(value);
        return "SetBrake " + Fl(value) + F(" -> %d", int(r));
    }
    case OP_STEER: {
        const float value = OpF(op.a, op.b % 5 ? -1.0f : -1.5f, op.b % 7 ? 1.0f : 1.5f);
        const bool r = o ? OriginalAt<bool(TC *)(void *, float)>(O_VH_SET_STEERING)(v, value) : p->SetSteering(value);
        return "SetSteering " + Fl(value) + F(" -> %d", int(r));
    }
    case OP_HANDBRAKE: {
        const int on = op.a % 2, second = op.b % 2;
        const bool r = o ? OriginalAt<bool(TC *)(void *, int, int)>(O_VH_SET_HANDBRAKE)(v, on, second) : p->SetHandbrake(on != 0, second != 0);
        return F("SetHandbrake %d %d -> %d", on, second, int(r));
    }
    case OP_ENGINE: {
        const int on = op.a % 3 != 0, instant = op.b % 2;
        const bool r = o ? OriginalAt<bool(TC *)(void *, int, int)>(O_VH_SET_ENGINE)(v, on, instant) : p->SetEngine(on != 0, instant != 0);
        return F("SetEngine %d %d -> %d", on, instant, int(r));
    }
    case OP_FUEL: {
        // (Now and then just over the start's 0.02 l.)
        const float litres = op.b % 6 == 0 ? 0.025f : OpF(op.a, -10.0f, 300.0f);
        const bool r = o ? OriginalAt<bool(TC *)(void *, float)>(O_VH_SET_FUEL)(v, litres) : p->SetFuel(litres);
        return "SetFuel " + Fl(litres) + F(" -> %d", int(r));
    }
    case OP_SET4F23B0:
    case OP_SET4F2630: {
        const float value = OpF(op.a, -0.5f, 1.5f);
        const bool first = op.kind == OP_SET4F23B0;
        const bool r = o ? OriginalAt<bool(TC *)(void *, float)>(first ? O_VH_SET_4F23B0 : O_VH_SET_4F2630)(v, value)
                         : first ? p->SetUnknown4f23b0(value) : p->SetUnknown4f2630(value);
        return F("%s ", OP_KIND_NAMES[op.kind]) + Fl(value) + F(" -> %d", int(r));
    }
    case OP_SOUNDS: {
        const int on = op.a % 2;
        if (o)
            OriginalAt<void(TC *)(void *, int)>(O_VH_ENABLE_SOUNDS)(v, on);
        else
            p->EnableSounds(on != 0);
        return F("EnableSounds %d", on);
    }
    case OP_IMPULSE:
    case OP_FORCE: {
        // (An impulse at a point near the car: its size from small to a crash's; the factors mostly 1.)
        static const float scales[] = {10.0f, 500.0f, 5000.0f, 50000.0f};
        const S_vector amount = OpV(op.a, scales[op.d % 4]);
        const S_matrix &m = AsFrame(g_modelFrame[g_side])->m_localMatrix;
        const S_vector local = OpV(op.b, 3.0f);
        const S_vector point(local.x * m.m[0][0] + local.y * m.m[1][0] + local.z * m.m[2][0] + m.m[3][0],
                             local.x * m.m[0][1] + local.y * m.m[1][1] + local.z * m.m[2][1] + m.m[3][1],
                             local.x * m.m[0][2] + local.y * m.m[1][2] + local.z * m.m[2][2] + m.m[3][2]);
        const S_vector factors = op.c % 4 ? S_vector(1.0f, 1.0f, 1.0f) : OpV(op.c, 2.0f);
        const int flags = (op.c / 4) % 8;
        const bool impulse = op.kind == OP_IMPULSE;
        using T_Push = void(TC *)(void *, const S_vector *, const S_vector *, int, const S_vector *);
        if (o)
            OriginalAt<T_Push>(impulse ? O_VH_APPLY_IMPULSE : O_VH_ADD_FORCE)(v, &amount, &point, flags, &factors);
        else if (impulse)
            p->ApplyImpulse(amount, point, uint8_t(flags), factors);
        else
            p->AddForce(amount, point, uint8_t(flags), factors);
        return F("%s ", OP_KIND_NAMES[op.kind]) + V3(&amount) + " at " + V3(&point) + F(" flags %d ", flags) + V3(&factors);
    }
    case OP_SETTLE: {
        const uint32_t maxMs = uint32_t(op.a % 3000);
        const float speed = OpF(op.b, 0.0f, 2.0f);
        const int minSteps = op.c % 6;
        const bool r = o ? OriginalAt<bool(TC *)(void *, uint32_t, float, int)>(O_VH_SETTLE)(v, maxMs, speed, minSteps)
                         : p->Settle(maxMs, speed, minSteps);
        return F("Settle %u ", maxMs) + Fl(speed) + F(" %d -> %d", minSteps, int(r));
    }
    case OP_RESET: {
        const int flags = op.a % 8;
        if (o)
            OriginalAt<void(TC *)(void *, int)>(O_VH_RESET)(v, flags);
        else
            p->Reset(uint8_t(flags));
        return F("Reset %d", flags);
    }
    case OP_RESTART: {
        const float speed = OpF(op.a, 0.0f, 30.0f);
        const int repair = op.b % 2;
        if (o)
            OriginalAt<void(TC *)(void *, float, int)>(O_VH_RESTART)(v, speed, repair);
        else
            p->Restart(speed, repair != 0);
        return "Restart " + Fl(speed) + F(" %d", repair);
    }
    case OP_SEAT_LOAD: {
        // (Not a negative seat: the original checks only the end and writes before the seats; the port's Fix returns.)
        const int seat = op.a % 8;
        const float mass = OpF(op.b, 0.0f, 200.0f);
        if (o)
            OriginalAt<void(TC *)(void *, int, float)>(O_VH_SET_SEAT_LOAD)(v, seat, mass);
        else
            p->SetSeatLoad(seat, mass);
        return F("SetSeatLoad %d ", seat) + Fl(mass);
    }
    case OP_GETTERS: {
        const float rpm = o ? OriginalAt<float(TC *)(void *)>(O_VH_GET_ENGINE_RPM)(v) : p->GetEngineRpm();
        const int unscaled = op.a % 2;
        const S_vector4 *size = o ? OriginalAt<const S_vector4 *(TC *)(void *, int)>(O_VH_GET_SIZE)(v, unscaled) : &p->GetSize(unscaled != 0);
        S_vector offset(9.0f, 9.0f, 9.0f);
        if (o)
            OriginalAt<void(TC *)(void *, S_vector *)>(O_VH_CAMERA_OFFSET)(v, &offset);
        else
            p->GetCameraOffset(&offset);
        // (GetEngineTorque: rpm in and past the engine's range, every gear and two past them.)
        const float rpm2 = OpF(op.b, -100.0f, 8000.0f);
        const int gear = op.c % 14 - 2;
        const int braking = op.d % 2;
        float torque = 7.0f;
        const bool r = o ? OriginalAt<bool(TC *)(void *, float, float *, int, int)>(O_VH_ENGINE_TORQUE)(v, rpm2, &torque, gear, braking)
                         : p->GetEngineTorque(rpm2, &torque, gear, braking != 0);
        // (ReadModelState now and then, as the car's place is set from outside.)
        if (op.d % 4 == 0) {
            if (o)
                OriginalAt<T_VhVoid>(O_VH_READ_MODEL_STATE)(v);
            else
                p->ReadModelState();
        }
        // (GetContactResponse, as another car's callback asks it: a point near the car, a normal. No w is shown: the
        // original leaves the arm's and the turn's, and the velocity's is the vehicle's own leftover.)
        const S_matrix &m = AsFrame(g_modelFrame[g_side])->m_localMatrix;
        const S_vector4 point = {m.m[3][0] + OpF(op.a / 7, -2.0f, 2.0f), m.m[3][1] + 0.5f, m.m[3][2] + OpF(op.b / 7, -3.0f, 3.0f), 0.0f};
        const S_vector normal = OpV(op.c / 7, 1.0f);
        S_vector4 arm = {}, velocity = {}, turn = {};
        float mass = 0.0f;
        using T_Response = void(TC *)(void *, const S_vector4 *, const S_vector *, S_vector4 *, S_vector4 *, float *, S_vector4 *);
        if (o)
            OriginalAt<T_Response>(O_VH_CONTACT_RESPONSE)(v, &point, &normal, &arm, &velocity, &mass, &turn);
        else
            p->GetContactResponse(point, normal, &arm, &velocity, &mass, &turn);
        return "GetEngineRpm " + Fl(rpm) + F(" GetSize %d ", unscaled) + V3(size) + " GetCameraOffset " + V3(&offset) +
               " GetContactResponse " + V3(&arm) + " " + V3(&velocity) + " " + Fl(mass) + " " + V3(&turn) +
               " GetEngineTorque " + Fl(rpm2) + F(" %d %d -> %d ", gear, braking, int(r)) + Fl(torque);
    }
    case OP_SOLVE: {
        // (One solver step alone: a substep's time, no drag, the drive's torques.)
        const float dt = OpF(op.a, 0.001f, 0.028f);
        const float torque = OpF(op.b, -500.0f, 2000.0f);
        const bool r = o ? OriginalAt<bool(TC *)(void *, float, float, float, float, float, float)>(O_VH_SOLVE)(v, dt, 0.0f, 0.0f, 0.0f,
                                                                                                         torque, torque)
                         : p->Solve(dt, 0.0f, 0.0f, 0.0f, torque, torque);
        return "Solve " + Fl(dt) + " " + Fl(torque) + F(" -> %d", int(r));
    }
    case OP_OPTIONS: {
        // As C_Car::UpdateDetail: the kept bits, then by the camera's distance (squared) and whether the car was drawn;
        // a wreck all, a server its own.
        static const float distances[] = {100.0f, 450.0f, 700.0f, 950.0f, 5000.0f, 30000.0f};
        const float d2 = distances[op.a % 6];
        const bool shown = op.b % 2 != 0, wrecked = op.c % 10 == 0, server = op.c % 10 == 1;
        uint32_t options = (o ? At<uint32_t>(v, 0xF6C) : p->m_unknownF6C) & 0x7FFC2026u;
        if (wrecked) {
            options |= 0x800313D1u;
        } else if (server) {
            options |= 0x8000CC10u;
        } else {
            if (d2 < 900.0f)
                options |= VEHICLE_SIM_SKID_MARKS | 0x1;
            if (shown || d2 < 900.0f)
                options |= VEHICLE_SIM_EFFECTS;
            if (shown || d2 < 600.0f)
                options |= VEHICLE_SIM_LAMP_SPOT_LIGHTS | VEHICLE_SIM_LAMP_PROJECTORS;
            if (d2 < 22500.0f)
                options |= VEHICLE_SIM_LAMPS;
            if (shown || d2 < 400.0f)
                options |= VEHICLE_SIM_IMPACT_DAMAGE | VEHICLE_SIM_DEFORM;
            options |= shown && d2 < 600.0f ? VEHICLE_SIM_RAY_EVERY_WHEEL : VEHICLE_SIM_WHEEL_PLANES;
            options |= VEHICLE_SIM_BODY_COLLISION;
            if (d2 < 900.0f)
                options |= VEHICLE_SIM_COLLIDE_EACH_SUBSTEP;
            options |= 0x80000000u;
        }
        if (op.d % 5 == 0)
            options |= VEHICLE_SIM_GROUND_FIRST_SUBSTEP;
        Mut<uint32_t>(v, o ? 0xF6C : uint32_t(offsetof(C_Vehicle, m_unknownF6C))) = options;
        return F("Options %x", options);
    }
    case OP_FIELDS: {
        // A field the other game code (or a loaded state) sets: the drive's mode, the steering target, the solver's
        // longest substep, the physics' state bits, the frozen count, the body's own friction.
        const int which = op.a % 15;
        switch (which) {
        case 0:
            Mut<int>(v, o ? 0x550 : uint32_t(offsetof(C_Vehicle, m_unknown550))) = op.b % 3;
            return F("Fields mode %d", op.b % 3);
        case 1: {
            const S_matrix &m = AsFrame(g_modelFrame[g_side])->m_localMatrix;
            const S_vector target = S_vector(m.m[3][0], m.m[3][1], m.m[3][2]) + OpV(op.b, 30.0f);
            Mut<S_vector>(v, o ? 0x040 : uint32_t(offsetof(C_Vehicle, m_unknown040))) = target;
            Mut<uint8_t>(v, o ? 0x2287 : uint32_t(offsetof(C_Vehicle, m_unknown2287))) = uint8_t(op.c % 3 != 0);
            return "Fields target " + V3(&target) + F(" %d", int(op.c % 3 != 0));
        }
        case 2: {
            static const float steps[] = {0.028f, 0.01f, 0.05f, 0.1f};
            Mut<float>(v, o ? 0xF54 : uint32_t(offsetof(C_Vehicle, m_unknownF54))) = steps[op.b % 4];
            return "Fields max step " + Fl(steps[op.b % 4]);
        }
        case 3: {
            uint32_t &state = Mut<uint32_t>(v, o ? 0x600 : uint32_t(offsetof(C_Vehicle, m_unknown600)));
            state ^= op.b % 2 ? VEHICLE_PHYSICS_KEEP_AWAKE : VEHICLE_PHYSICS_RESTING;
            return F("Fields state %x", state);
        }
        case 4:
            Mut<int>(v, o ? 0x554 : uint32_t(offsetof(C_Vehicle, m_unknown554))) = op.b % 3 == 0;
            return F("Fields frozen %d", int(op.b % 3 == 0));
        case 5:
            Mut<uint8_t>(v, o ? 0x228C : uint32_t(offsetof(C_Vehicle, m_unknown228C))) = uint8_t(op.b % 2);
            return F("Fields own friction %d", op.b % 2);
        case 6:
            Mut<uint8_t>(v, o ? 0x228D : uint32_t(offsetof(C_Vehicle, m_unknown228D))) = uint8_t(op.b % 2);
            return F("Fields own bumps %d", op.b % 2);
        // (Flags only a loaded state sets in this executable: the engine held off, the steering at once, the clutch's
        // limit at once.)
        case 7:
            Mut<uint8_t>(v, o ? 0x22C1 : uint32_t(offsetof(C_Vehicle, m_unknown22C1))) = uint8_t(op.b % 2);
            return F("Fields engine held %d", op.b % 2);
        case 8:
            Mut<uint8_t>(v, o ? 0x2299 : uint32_t(offsetof(C_Vehicle, m_unknown2299))) = uint8_t(op.b % 2);
            return F("Fields steering at once %d", op.b % 2);
        case 9:
            Mut<uint8_t>(v, o ? 0x229C : uint32_t(offsetof(C_Vehicle, m_unknown229C))) = uint8_t(op.b % 2);
            return F("Fields clutch limit at once %d", op.b % 2);
        // (A wheel knocked off (as the car's 0x004d4eb0 does), the engine's and gearbox's wear part way (as impacts
        // leave it), the engine's +0x22A0 left on, the stability brake (a block byte the shipped blocks leave 0), a
        // steering target close by.)
        case 10: {
            const int count = o ? At<int>(v, 0x7C0) : p->m_wheelCount;
            if (count <= 0)
                return "Fields detach (no wheels)";
            const int i = op.b % count;
            uint8_t *w = o ? At<uint8_t *>(At<uint8_t *>(v, 0xF64), 4 * uint32_t(i)) : reinterpret_cast<uint8_t *>(p->m_wheels[i]);
            if (!w)
                return F("Fields detach %d (missing)", i);
            Mut<uint32_t>(w, o ? 0x1FC : uint32_t(offsetof(S_VehicleWheel, flags))) |= WHEEL_DETACHED;
            return F("Fields detach %d", i);
        }
        case 11: {
            static const float wear[] = {0.5f, 0.25f, 0.9f, 0.05f};
            const float e = wear[op.b % 4], g = wear[op.c % 4];
            Mut<float>(v, o ? 0x610 : uint32_t(offsetof(C_Vehicle, m_unknown610))) = e;
            Mut<float>(v, o ? 0x63C : uint32_t(offsetof(C_Vehicle, m_unknown63C))) = g;
            return "Fields wear " + Fl(e) + " " + Fl(g);
        }
        case 12:
            Mut<uint8_t>(v, o ? 0x22A0 : uint32_t(offsetof(C_Vehicle, m_unknown22A0))) = uint8_t(op.b % 2);
            return F("Fields +22A0 %d", op.b % 2);
        case 13:
            Mut<uint8_t>(v, o ? 0x2290 : uint32_t(offsetof(C_Vehicle, m_unknown2290))) = uint8_t(op.b % 2);
            return F("Fields stability brake %d", op.b % 2);
        default: {
            const S_matrix &m = AsFrame(g_modelFrame[g_side])->m_localMatrix;
            const S_vector target = S_vector(m.m[3][0], m.m[3][1], m.m[3][2]) + OpV(op.b, 3.0f);
            Mut<S_vector>(v, o ? 0x040 : uint32_t(offsetof(C_Vehicle, m_unknown040))) = target;
            Mut<uint8_t>(v, o ? 0x2287 : uint32_t(offsetof(C_Vehicle, m_unknown2287))) = 1;
            return "Fields target near " + V3(&target);
        }
        }
    }
    case OP_DRIVER: {
        // (As C_Car::SetDriver: a driver in the seat or none.)
        const int driven = op.a % 4 != 0;
        Mut<uint8_t>(v, o ? 0x2291 : uint32_t(offsetof(C_Vehicle, m_driven))) = uint8_t(driven);
        return F("Driver %d", driven);
    }
    case OP_CAR_HIT: {
        // (As C_Car's hit by another car: the other a world actor, the scale 1.2 (0.6 now and then), the push and a
        // point near the car.)
        uint8_t *other = W().actors.empty() ? nullptr : W().actors[size_t(op.a) % W().actors.size()].bytes;
        static const float scales[] = {10.0f, 300.0f, 3000.0f, 30000.0f};
        const S_vector push = OpV(op.b, scales[op.d % 4]);
        const S_matrix &m = AsFrame(g_modelFrame[g_side])->m_localMatrix;
        const S_vector local = OpV(op.c, 2.5f);
        const S_vector4 point = {local.x * m.m[0][0] + local.y * m.m[1][0] + local.z * m.m[2][0] + m.m[3][0],
                                 local.x * m.m[0][1] + local.y * m.m[1][1] + local.z * m.m[2][1] + m.m[3][1],
                                 local.x * m.m[0][2] + local.y * m.m[1][2] + local.z * m.m[2][2] + m.m[3][2], 0.0f};
        const S_vector normal = OpV(op.d, 1.0f);
        const float scale = op.a % 5 ? 1.2f : 0.6f;
        using T_Hit = void(TC *)(void *, void *, float, const S_vector4 *, const S_vector *, const S_vector *, int);
        if (o)
            OriginalAt<T_Hit>(O_VH_CAR_HIT)(v, other, scale, &point, &push, &normal, 0);
        else
            p->ApplyCarHit(reinterpret_cast<C_Actor *>(other), scale, point, push, normal, 0);
        return "CarHit " + NameOf(Ptr(other)) + " " + Fl(scale) + " " + V3(&point) + " " + V3(&push) + " " + V3(&normal);
    }
    case OP_VOLUMES: {
        // (As C_Car's mission state: the special volumes, the volumes in the world.)
        if (op.a % 2) {
            const int on = op.b % 2;
            const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_VH_SPECIAL_VOLUMES)(v, on) : p->SetSpecialVolumes(on != 0);
            return F("SetSpecialVolumes %d -> %d", on, int(r));
        }
        const int main = op.b % 2, second = op.c % 2;
        if (o)
            OriginalAt<void(TC *)(void *, int, int)>(O_VH_VOLUMES_IN_WORLD)(v, main, second);
        else
            p->SetVolumesInWorld(main != 0, second != 0);
        return F("SetVolumesInWorld %d %d", main, second);
    }
    case OP_STATE: {
        // (As C_Car's SaveGame / LoadGame: the state's size, the state into zeroed bytes, then loaded back: now and then
        // with the model turned upside down in it, or another version.)
        const uint32_t size = o ? OriginalAt<uint32_t(TC *)(void *)>(O_VH_STATE_SIZE)(v) : p->StateSize();
        std::vector<uint8_t> state(size < 0x100000 ? size : 0, 0);
        bool saved = false, loaded = false;
        if (!state.empty())
            saved = o ? OriginalAt<bool(TC *)(void *, void *)>(O_VH_SAVE_STATE)(v, state.data()) : p->SaveState(state.data());
        std::string s = F("State size %u saved %d ", size, int(saved)) + Hex(std::string(state.begin(), state.end()));
        if (op.a % 5 != 0 && state.size() >= 0x24) {
            if (op.b % 4 == 0) {
                // (Upside down: the turn 180 degrees about z.)
                const float flipped[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                std::memcpy(state.data() + 0x14, flipped, sizeof flipped);
            } else if (op.b % 9 == 1) {
                state[0] = 9;                           // (another version)
            }
            loaded = o ? OriginalAt<bool(TC *)(void *, const void *)>(O_VH_LOAD_STATE)(v, state.data()) : p->LoadState(state.data());
            s += F(" loaded %d", int(loaded));
        }
        return s;
    }
    case OP_DOOR: {
        // (As a loaded state sets them: a door swinging open or shut, its window winding down or up.)
        const int count = o ? At<int>(v, 0xFB8 + 8) : int(p->m_doors.size());
        if (count <= 0)
            return "Door (none)";
        const int k = op.a % count;
        uint8_t *od = o ? At<uint8_t *>(v, 0xFB8) + 0x80 * k : nullptr;
        S_VehicleDoor *pd = o ? nullptr : &p->m_doors[size_t(k)];
        const float open = o ? At<float>(od, 0x2C) : pd->unknown2C;
        const float target = op.b % 3 == 0 ? 0.0f : op.b % 3 == 1 ? open : open * 0.5f;
        // (A window only on a door that has one: the original's UpdateWindows reads through a missing one.)
        const bool hasWindow = (o ? At<uint32_t>(od, 0x50) : Ptr(pd->window)) != 0;
        const bool window = hasWindow && op.c % 2 != 0, down = op.d % 2 != 0;
        if (o) {
            Mut<float>(od, 0x28) = target;
            Mut<uint8_t>(od, 0x70) |= DOOR_MOVING;
            if (window) {
                Mut<uint8_t>(od, 0x66) = 1;
                Mut<uint8_t>(od, 0x67) = uint8_t(down);
            }
        } else {
            pd->targetAngle = target;
            pd->flags |= DOOR_MOVING;
            if (window) {
                pd->windowMoving = true;
                pd->windowDown = down;
            }
        }
        return F("Door %d ", k) + Fl(target) + F(" window %d down %d", int(window), int(down));
    }
    case OP_LIGHTS: {
        // (As the car's light switches: the switches of every kind, their beams, the automatic ones; now and then
        // SetBeamScale, as the car's mission start: every projector 12 x 12 x 40, or one kind's.)
        if (op.a % 5 == 0) {
            static const uint32_t kinds[] = {0xFFFFFFFFu, VEHICLE_LIGHT_HEADLIGHT_BEAMS, VEHICLE_LIGHT_REAR_BEAMS, VEHICLE_LIGHT_BRAKE_BEAMS};
            const uint32_t beams = kinds[op.b % 4];
            const S_vector scale = op.c % 2 ? S_vector(12.0f, 12.0f, 40.0f) : OpV(op.c, 20.0f);
            if (o)
                OriginalAt<void(TC *)(void *, uint32_t, const S_vector *)>(O_VH_BEAM_SCALE)(v, beams, &scale);
            else
                p->SetBeamScale(beams, scale);
            return F("SetBeamScale %x ", beams) + V3(&scale);
        }
        static const uint32_t bits[] = {VEHICLE_LIGHT_INDICATOR_LEFT, VEHICLE_LIGHT_INDICATOR_RIGHT, VEHICLE_LIGHT_AUTO_INDICATORS,
                                        VEHICLE_LIGHT_FLASHING, VEHICLE_LIGHT_BRAKE, VEHICLE_LIGHT_AUTO_BRAKE, VEHICLE_LIGHT_REVERSE,
                                        VEHICLE_LIGHT_AUTO_REVERSE, VEHICLE_LIGHT_HEADLIGHTS, VEHICLE_LIGHT_INDICATOR_BEAMS,
                                        VEHICLE_LIGHT_FLASH_BEAMS, VEHICLE_LIGHT_REAR_BEAMS, VEHICLE_LIGHT_HEADLIGHT_BEAMS,
                                        VEHICLE_LIGHT_BRAKE_BEAMS, VEHICLE_LIGHT_REVERSE_BEAMS};
        uint32_t lights = Mut<uint32_t>(v, o ? 0x5B0 : uint32_t(offsetof(C_Vehicle, m_unknown5B0)));
        for (int k = 0; k < 15; ++k)
            if ((uint32_t(op.b) >> k) & 1u)
                lights ^= bits[k];
        Mut<uint32_t>(v, o ? 0x5B0 : uint32_t(offsetof(C_Vehicle, m_unknown5B0))) = lights;
        return F("Lights %x", lights);
    }
    case OP_SOUNDS2: {
        // (The sounds the car's code plays: a wheel's burst, a seat door's; or the sound group wanted, which
        // ApplyParams sets from the block's group 1 sounds.)
        const int which = op.a % 3;
        if (which == 0) {
            const int count = o ? At<int>(v, 0x7C0) : p->m_wheelCount;
            if (count <= 0)
                return "PlayWheelBurst (no wheels)";
            const int wheel = op.b % count;
            if (o)
                OriginalAt<void(TC *)(void *, int)>(O_VH_WHEEL_BURST)(v, wheel);
            else
                p->PlayWheelBurst(wheel);
            return F("PlayWheelBurst %d", wheel);
        }
        if (which == 1) {
            // (A seat with a door: the original reads through a missing one.)
            const int seats = o ? At<int>(v, 0xFA8 + 8) : int(p->m_seats.size());
            if (seats <= 0)
                return "PlayDoorSound (no seats)";
            const int seat = op.b % seats;
            const uint32_t door = o ? At<uint32_t>(At<uint8_t *>(v, 0xFA8) + 0x40 * seat, 0x28) : Ptr(p->m_seats[size_t(seat)].door);
            if (!door)
                return F("PlayDoorSound %d (no door)", seat);
            const int opening = op.c % 2;
            if (o)
                OriginalAt<void(TC *)(void *, int, int)>(O_VH_DOOR_SOUND)(v, seat, opening);
            else
                p->PlayDoorSound(seat, opening != 0);
            return F("PlayDoorSound %d %d", seat, opening);
        }
        Mut<uint8_t>(v, o ? 0x2297 : uint32_t(offsetof(C_Vehicle, m_unknown2297))) = uint8_t(op.b % 2);
        return F("Sound group %d", op.b % 2);
    }
    default:
        return "?";
    }
}

volatile DWORD g_caseStart;
HANDLE g_mainThread;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("vh_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            // (Where: the main thread's EIP and stack words.)
            if (g_mainThread && SuspendThread(g_mainThread) != DWORD(-1)) {
                CONTEXT ctx;
                ctx.ContextFlags = CONTEXT_FULL;
                if (GetThreadContext(g_mainThread, &ctx)) {
                    std::printf("  eip %08lx esp %08lx ebp %08lx\n", ctx.Eip, ctx.Esp, ctx.Ebp);
                    const uint32_t *sp = reinterpret_cast<const uint32_t *>(ctx.Esp);
                    for (int k = 0; k < 400; ++k)
                        if ((sp[k] >= 0x00400000 && sp[k] < 0x00800000) || (sp[k] >= 0x20001000 && sp[k] < 0x20400000))
                            std::printf("  stack +%02x %08x\n", k * 4, sp[k]);
                }
            }
            const std::vector<std::string> &log = g_log[g_side];
            for (size_t k = log.size() > 12 ? log.size() - 12 : 0; k < log.size(); ++k)
                std::printf("  last call %s\n", log[k].substr(0, 300).c_str());
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_vhShow;
double g_opTime[OP_KINDS];                              // (HD2_VH_PROF: the operations' times)
void RunVhCase(const S_VhCase &c)
{
    g_caseStart = GetTickCount();
    ++g_vhCases;
    g_holdFrees = true;
    g_current = F("case %d (%s)", g_vhCases, g_vhModels[size_t(c.model)].file.c_str());
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    // (HD2_VH_SKIP with HD2_VH_ONLY: the cases before it drawn but not run.)
    static const int skipTo = std::getenv("HD2_VH_SKIP") && std::getenv("HD2_VH_ONLY") ? std::atoi(std::getenv("HD2_VH_ONLY")) : 0;
    if (g_vhCases < skipTo)
        return;
    g_ground = c.ground;
    g_skidFails = c.skidFails;
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_vhTexts[side].clear();
        BuildWorld(side, worldSeed);
        BuildVhModel(c);
        BuildVhGround(c.ground);
        BuildEmitters(c.emitters);
        // (The vehicles' scene: the world's.)
        if (side)
            *OriginalAt<uint8_t **>(O_CAR_SCENE) = W().scene;
        else
            g_carScene = reinterpret_cast<I3D_scene *>(W().scene);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        uint8_t *v = Block(VEHICLE_BYTES + 16);
        if (side) {
            OriginalAt<T_VhVoid>(O_VH_CTOR)(v);
            // (The effect slots the original leaves unset until Start: the port's -1.)
            Mut<int>(v, 0x638) = -1;
            for (int k = 0; k < 3; ++k)
                Mut<int>(v, 0x7A8 + 4 * k) = -1;
            // (Nor its body volume's descriptor: the port's defaults.)
            Mut<uint32_t>(v, 0x2278) = 0x40;
            Mut<uint8_t>(v, 0x227C) = 1;
        } else
            new (v) C_Vehicle();
        g_vehicle[side] = v;
        g_framesMade[side] = 0;
        g_started[side] = false;
        g_vhClock[side] = 1000;
        // (The case's skid marks per wheel and +0xF74.)
        Mut<int>(v, side ? 0x678 : uint32_t(offsetof(C_Vehicle, m_unknown678))) = c.skidMarks;
        Mut<uint32_t>(v, side ? 0xF74 : uint32_t(offsetof(C_Vehicle, m_unknownF74))) = c.f74;
        // (The callbacks' context: the car, as its constructor sets it.)
        uint8_t *car = Block(0x40);
        Name(car, "car");
        Mut<uint8_t *>(v, side ? 0xF78 : uint32_t(offsetof(C_Vehicle, m_callbackContext))) = car;
        g_callbackCalls[side] = 0;
        Name(v, "vehicle");
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : VhState())
            out.push_back(s);
        uint8_t *model = g_modelFrame[side];
        const bool ok = side ? OriginalAt<bool(TC *)(void *, void *)>(O_VH_SETUP)(v, model) : Vehicle()->Setup(AsFrame(model));
        out.push_back(F("Setup -> %d", int(ok)));
        if (ok)
            SetVhCallbacks(v);
        // (The wheels' effect slots the original leaves unset until Start: the port's -1.)
        if (side)
            for (uint32_t w = At<uint32_t>(v, 0xF60); w; w = At<uint32_t>(reinterpret_cast<void *>(w), 0x278))
                Mut<int>(reinterpret_cast<void *>(w), 0x270) = -1;
        for (const std::string &s : g_log[side])
            out.push_back("  call " + s);
        g_log[side].clear();
        if (side && std::getenv("HD2_VH_RAW")) {
            static const uint32_t offs[] = {0x56C, 0x57C, 0x590, 0x5A0, 0x654, 0x664, 0x6C4, 0x6D4, 0x744, 0x798,
                                            0xFA8, 0xFB8, 0xFC8, 0x2228, 0x2238, 0x2248, 0x2258, 0x2268};
            for (uint32_t off : offs)
                std::printf("raw %x: %08x %08x %08x %08x\n", off, At<uint32_t>(v, off), At<uint32_t>(v, off + 4),
                            At<uint32_t>(v, off + 8), At<uint32_t>(v, off + 12));
        }
        for (const std::string &s : VhState())
            out.push_back("  " + s);
        for (const std::string &s : VhFrameState())
            out.push_back("  " + s);
        static const int trace = std::getenv("HD2_VH_TRACE") ? std::atoi(std::getenv("HD2_VH_TRACE")) : -1;
        for (size_t k = 0; k < c.ops.size(); ++k) {
            ++g_opCounts[c.ops[k].kind];
            if (trace == g_vhCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(k), OP_KIND_NAMES[c.ops[k].kind],
                            c.ops[k].a, c.ops[k].b, c.ops[k].c, c.ops[k].d);
            LARGE_INTEGER t0, t1;
            QueryPerformanceCounter(&t0);
            out.push_back(RunVhOp(c, c.ops[k], int(k)));
            QueryPerformanceCounter(&t1);
            g_opTime[c.ops[k].kind] += double(t1.QuadPart - t0.QuadPart);
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            // (Every field after the setup's operations, every fourth and the last; the rest the compact state.)
            static const bool everyOp = std::getenv("HD2_VH_FULL") != nullptr;
            const bool full = everyOp || c.ops[k].kind == OP_APPLY_PARAMS || c.ops[k].kind == OP_START || c.ops[k].kind == OP_SOLVE || c.ops[k].kind == OP_VOLUMES || k % g_fullEvery == g_fullEvery - 1 || k + 1 == c.ops.size();
            g_showConstant = c.ops[k].kind == OP_APPLY_PARAMS || k + 1 == c.ops.size();
            for (const std::string &s : full ? VhState() : VhCompactState())
                out.push_back("  " + s);
            g_showConstant = true;
        }
        // The end: released, or destroyed (Release then) half the time.
        if (c.destroy) {
            if (side)
                OriginalAt<T_VhVoid>(O_VH_DTOR)(v);
            else
                Vehicle()->~C_Vehicle();
        } else if (side) {
            OriginalAt<T_VhVoid>(O_VH_RELEASE)(v);
        } else {
            Vehicle()->Release();
        }
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_vehicle[side] = nullptr;
    }
    static const int only = std::getenv("HD2_VH_ONLY") ? std::atoi(std::getenv("HD2_VH_ONLY")) : 0;
    if (g_vhShow && (!only || only == g_vhCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_vhShow == 2)
            for (const std::string &s : r.orig)
                std::printf("  orig: %s\n", s.c_str());
    }
    Compare(g_current, r);
    ReleaseHeldFrees();
    if (!g_overrun.empty()) {
        ++failures;
        std::printf("FAIL %s: the original wrote past a block\n", g_overrun.c_str());
        g_overrun.clear();
    }
    FreeWorld(0);
    FreeWorld(1);
}

DWORD __stdcall O_TimeGetTime() { return VhTimeGetTime(); }

void SetUpVh()
{
    SetUpCmd();
    SetUpVhAnswers();
    {
        void **slot = OriginalAt<void **>(0x0080f3f8);   // (the original's timeGetTime import)
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        *slot = reinterpret_cast<void *>(&O_TimeGetTime);
        VirtualProtect(slot, 4, old, &old);
    }
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e562e, reinterpret_cast<const void *>(&O_Atexit)},
        {0x007e53c2, reinterpret_cast<const void *>(&O_AlignedMalloc)},
        {0x007e559c, reinterpret_cast<const void *>(&O_AlignedFree)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    // The original CRT's float conversions (_cfltcvt_init: its sscanf of a wheel's "[<cm>]" width needs them).
    OriginalAt<void(__cdecl *)()>(0x007e599c)();
}

}  // namespace H

DWORD VhTimeGetTime()
{
    using namespace H;
    const DWORD now = g_vhClock[g_side];
    g_vhClock[g_side] += 60;
    g_log[g_side].push_back(F("timeGetTime -> %lu", now));
    return now;
}

int main()
{
    using namespace H;
    std::puts("vh_diff: main");
    std::fflush(stdout);
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("vh_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_VH_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_VH_FULL_EVERY"))
        g_fullEvery = std::max(1, std::atoi(s));
    if (const char *s = std::getenv("HD2_VH_SHOW"))
        g_vhShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_VH_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_VH_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpVh();
    {
        const char *cars = std::getenv("HD2_VH_CARS");
        const char *temp = std::getenv("TEMP");
        LoadVhModels(cars ? cars : std::string(temp ? temp : ".") + "\\hd2_suites\\inputs\\cars4ds");
    }
    if (g_vhModels.empty()) {
        std::printf("vh_diff: no car models (HD2_VH_CARS, or the suites' inputs\\cars4ds)\n");
        return 1;
    }
    {
        const char *params = std::getenv("HD2_VH_PARAMS");
        const char *temp = std::getenv("TEMP");
        LoadVhParams(params ? params : std::string(temp ? temp : ".") + "\\hd2_suites\\inputs\\carbins");
    }
    SetUpCarTables();
    // (Not a block with a tyre id tables\tyres.bin lacks: the original's solver then reads before the tyre types; the
    // port's Fix takes the wheel's own values.)
    {
        const size_t before = g_vhParams.size();
        g_vhParams.erase(std::remove_if(g_vhParams.begin(), g_vhParams.end(), [](const S_VhParams &p) {
                             for (int a = 0; a < VEHICLE_AXLES; ++a) {
                                 bool known = false;
                                 for (const S_TyreType &type : g_tyreTypes)
                                     known = known || type.id == p.params.axleTyreType[a];
                                 if (!known)
                                     return true;
                             }
                             return false;
                         }),
                         g_vhParams.end());
        std::printf("vh_diff: %d parameter blocks (%d with unknown tyres left out)\n", int(g_vhParams.size()),
                    int(before - g_vhParams.size()));
    }
    SetMission("AFRICA1");
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunVhCase(RandomVhCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("vh_diff: %d cases (%s), %d car models, %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_vhCases,
                ops.c_str(), int(g_vhModels.size()), g_calls[0], g_overruns, checks, failures);
    if (std::getenv("HD2_VH_PROF")) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        for (int i = 0; i < OP_KINDS; ++i)
            std::printf("time %s %.2f s\n", OP_KIND_NAMES[i], g_opTime[i] / double(f.QuadPart));
    }
    return failures != 0;
}
