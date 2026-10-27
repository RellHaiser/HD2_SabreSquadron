// LS3D (Insanity 3D lineage) engine interfaces exported by LS3DF.DLL.
//
// These objects live inside the engine DLL. The game calls their virtual methods
// (COM-style, __stdcall, `this` on the stack) and reads a few data members through
// SDK inline accessors, so both the vtable slot order and the member offsets below
// must match the DLL exactly.
//
// Slots whose purpose has not been identified yet are declared as placeholders
// (`SlotXX`, XX = byte offset in the vtable) to keep later slots at the right index.
#pragma once

#include <cstddef>
#include <cstdint>
#include <windows.h>

#include "ls3d/math.h"

#define I3DMETHOD(type, name) virtual type __stdcall name
#define I3D_SLOT(off) virtual void __stdcall Slot##off() = 0
// Four consecutive placeholder slots.
#define I3D_SLOTS_4(a, b, c, d) I3D_SLOT(a); I3D_SLOT(b); I3D_SLOT(c); I3D_SLOT(d)

class I3D_frame;
class I3D_scene;
class I3D_camera;
class I3D_driver;
class I3D_sector;
struct I3D_particle_vertex;

using PI3D_frame = I3D_frame *;
using PI3D_scene = I3D_scene *;
using PI3D_driver = I3D_driver *;

// Frame flag bits (I3D_frame::m_frameFlags, +0xE0).
enum : uint32_t {
    FRMFLAGS_ROT_VALID = 0x00000004,       // local matrix rotation part is up to date
    FRMFLAGS_ROT_DIRTY = 0x00000008,       // set by SetRot; matrices rebuilt on next update
    FRMFLAGS_LMATRIX_VALID = 0x00000010,   // local matrix is up to date
    FRMFLAGS_WMATRIX_VALID = 0x00000020,   // world matrix is up to date
    FRMFLAGS_POS_LOCKED = 0x00000080,      // SetPos is ignored
    FRMFLAGS_BOUND_VALID = 0x00000100,     // m_boundRadius is up to date
    FRMFLAGS_SETPOS_CLEAR = 0x00000120,    // bits SetPos clears
    FRMFLAGS_SETROT_CLEAR = 0x00000134,    // bits SetRot clears
    FRMFLAGS_HUMAN_MODEL = 0x00400000,     // set on human models by C_HumanMovement (meaning not identified)
    FRMFLAGS_CHANGED = 0x40000000,
};

// Frame types (I3D_frame::GetType()).
enum E_I3D_FRAME_TYPE : uint16_t {
    FRAME_NULL = 0,
    FRAME_VISUAL = 1,
    FRAME_LIGHT = 2,
    FRAME_CAMERA = 3,
    FRAME_SOUND = 4,
    FRAME_SECTOR = 5,
    FRAME_DUMMY = 6,              // carries a bounding box
    FRAME_TARGET = 7,
    FRAME_USER = 8,
    FRAME_MODEL = 9,              // animated model
    FRAME_JOINT = 10,
    FRAME_VOLUME = 11,            // collision volume of a model (C_Team collects a member's; inferred)
    FRAME_SCENE = 13,             // the scene itself (I3D_driver::CreateFrame; C_game)
    FRAME_SHADOW = 15,            // projected shadow ("._shad." frames, see the shadow cache)
};

// Results of the engine's file loading (I3D_model::Open() etc.).
constexpr HRESULT I3D_OK = 0;
constexpr HRESULT I3DERR_NOFILE = static_cast<HRESULT>(0x80000001);          // file not found
constexpr HRESULT I3DERR_OUTOFMEM = static_cast<HRESULT>(0x80000002);
constexpr HRESULT I3DERR_GENERIC = static_cast<HRESULT>(0x80000005);
constexpr HRESULT I3DERR_CANCELED = static_cast<HRESULT>(0x8000000A);        // stopped by the load callback
constexpr HRESULT I3DERR_BADFILEVERSION = static_cast<HRESULT>(0x80000013);  // stale cached conversion
constexpr HRESULT I3DERR_BADFILETIME = static_cast<HRESULT>(0x80000014);

// Progress / error callback of the engine's loaders.
using I3D_LOAD_CB_PROC = bool __stdcall(uint32_t message, uint32_t param1, uint32_t param2, void *context);

// Type mask accepted by the frame search functions: every regular frame type.
constexpr uint32_t FRAME_MASK_ALL = 0xCFFFF;
// The 16 basic type bits (what most game code passes).
constexpr uint32_t FRAME_MASK_ANY = 0xFFFF;
// Single types in such masks (bit type-1).
constexpr uint32_t FRAME_MASK_VISUAL = 1u << (FRAME_VISUAL - 1);
constexpr uint32_t FRAME_MASK_DUMMY = 1u << (FRAME_DUMMY - 1);
constexpr uint32_t FRAME_MASK_LIGHT = 1u << (FRAME_LIGHT - 1);
constexpr uint32_t FRAME_MASK_VOLUME = 1u << (FRAME_VOLUME - 1);
// I3D_frame::EnumFrames() type mask of sectors.
constexpr uint32_t ENUMF_SECTOR = 0x10;
// EnumFrames() / FindChildFrame() flag: the name mask holds '*' wildcards (inferred: the
// vehicle's pattern searches pass it, its exact-name searches do not).
constexpr uint32_t ENUMF_WILDMASK = 0x10000;

// Game data attached to an engine frame (I3D_frame::GetData()).
class C_Actor;
class C_Script;
struct S_FrameData {              // 0x20 bytes, see SetFrameData() (0x006e9ea0)
    C_Actor *actor;               // actor owning the frame, if any
    C_Script *script;             // script running on the frame, if any
    uint32_t unknown08;
    uint32_t unknown0C;
    struct S_PhysicsHandle *physicsObject;   // physics module object (physics.h), g_noPhysicsObject when none
    class C_VersionMaster *versionMaster;  // destructible object owning the frame (versions.h)
    class C_VersionKeeper *versionKeeper;  // destructible part this frame is
    uint32_t unknown1C;
};

// Engine bounding box: min and max corners, each padded to 16 bytes.
struct I3D_bbox {
    S_vector min;
    float minW;
    S_vector max;
    float maxW;

    // Inverted box (min > max) that the first Expand() replaces.
    static I3D_bbox Empty()
    {
        return {S_vector(1e16f, 1e16f, 1e16f), 0.0f, S_vector(-1e16f, -1e16f, -1e16f), 0.0f};
    }
    bool IsValid() const { return min.x <= max.x; }
    void Expand(const S_vector &p)
    {
        if (p.x < min.x) min.x = p.x;
        if (p.x > max.x) max.x = p.x;
        if (p.y < min.y) min.y = p.y;
        if (p.y > max.y) max.y = p.y;
        if (p.z < min.z) min.z = p.z;
        if (p.z > max.z) max.z = p.z;
    }
};

// Bounding box plus the bounding sphere around it.
struct I3D_bound_volume {
    I3D_bbox bbox;
    S_vector center;
    float radius;
};

// I3D_frame::EnumFrames() callback; returns 0 to continue.
using I3D_ENUMPROC = uint32_t __stdcall(I3D_frame *frame, void *context);
// I3D_frame::SetCallback() callback: the frame, the message (I3DCALLBACK_*) and its parameters.
using I3D_CALLBACK = int __stdcall(I3D_frame *frame, uint32_t message, uintptr_t param1, uintptr_t param2);
enum : uint32_t {
    // A note key of an animation of the frame's model: param1 the note text, param2 the time
    // (inferred from C_HumanAnimMixer's use).
    I3DCALLBACK_ANIM_NOTE = 0x1000,
};

#pragma pack(push, 4)
class I3D_frame {
public:
    // Drops one reference; the frame is destroyed when the last one goes.
    I3DMETHOD(uint32_t, Release)() = 0;                       // 0x00
    // FourCC of the visual type for visual frames (e.g. 'LMAP', 'OBJ_'), 0 otherwise.
    I3DMETHOD(uint32_t, GetVisualType)() = 0;                 // 0x04
    I3DMETHOD(void, SetPos)(const S_vector &pos) = 0;         // 0x08
    I3DMETHOD(void, SetDir)(const S_vector &dir, float roll) = 0;    // 0x0C
    // Places the frame at `pos` (world space) with its z axis `dir` and y axis `up` (LS3DF
    // 0x1001cca0, its SetWorldPosDir3; the rigid bodies' frames).
    I3DMETHOD(void, SetWorldPosDir3)(const S_vector &pos, const S_vector &dir, const S_vector &up) = 0;   // 0x10
    // Places the frame at `pos` facing `dir` (used for dropped items; meaning inferred).
    I3DMETHOD(void, SetPosDir)(const S_vector &pos, const S_vector &dir, float roll) = 0;   // 0x14
    I3D_SLOT(18);                                             // (SetPosRot below)
    I3DMETHOD(void, SetWorldDir)(const S_vector &dir, float roll) = 0;   // 0x1C
    // Advances the frame's own animation by `time` ms (models).
    I3DMETHOD(void, Tick)(int time) = 0;                      // 0x20
    // Propagates pending position/rotation changes into the matrices and children.
    I3DMETHOD(void, Update)() = 0;                            // 0x24
    // Calls `callback` with the messages in `messages` (null: none).
    I3DMETHOD(void, SetCallback)(I3D_CALLBACK *callback, uint32_t messages) = 0;   // 0x28
    I3D_SLOT(2C);                                             // LS3DF: SetUserProps
    I3DMETHOD(void, SetOn)(bool on) = 0;                      // 0x30
    I3DMETHOD(void, SetName)(const char *name) = 0;           // 0x34
    // Re-parents the frame (flags: I3DLINK_*).
    I3DMETHOD(void, LinkTo)(I3D_frame *parent, uint32_t flags) = 0;   // 0x38
    I3DMETHOD(int, NumChildren)() = 0;                        // 0x3C
    I3DMETHOD(I3D_frame *, GetChild)(int index) = 0;          // 0x40
    // Calls `callback(frame, context)` for every descendant matching `typeMask` and the name
    // `mask` (null: any; the short name, see ENUMF_WILDMASK).
    I3DMETHOD(void, EnumFrames)(I3D_ENUMPROC *callback, void *context, uint32_t typeMask,
                                const char *mask) = 0;        // 0x44
    I3DMETHOD(I3D_frame *, FindChildFrame)(const char *name, uint32_t typeMask) = 0;   // 0x48
    // Copies the contents (visual, children) of another frame of the same type.
    I3DMETHOD(void, Duplicate)(I3D_frame *source) = 0;       // 0x4C
    // Called with 1.0 on a weapon's "flash" visual when it fires (meaning not identified).
    I3DMETHOD(void, SetHierarchyLightLevel)(float value) = 0;           // 0x50
    // Opacity of the object visuals of the hierarchy (LS3DF 0x1001ebf0; 0x50 is their light level).
    I3DMETHOD(void, SetHierarchyAlpha)(float alpha) = 0;      // 0x54
    // Recomputes the bound of the frame and its children (GetBound()).
    I3DMETHOD(void, UpdateBound)() = 0;                       // 0x58

    // --- SDK inline accessors (operate on DLL-owned data) ------------------
    void AddRef() { ++m_refCount; }
    int GetRefCount() const { return m_refCount; }
    E_I3D_FRAME_TYPE GetType() const { return m_type; }
    S_FrameData *GetData() const { return m_data; }
    void SetData(S_FrameData *data) { m_data = data; }

    const S_matrix &GetMatrix() const
    {
        if (!(m_frameFlags & FRMFLAGS_WMATRIX_VALID))
            const_cast<I3D_frame *>(this)->UpdateWMatrixProc();
        return m_worldMatrix;
    }
    const S_vector &GetWorldPos() const { return GetMatrix()(3); }
    // Normalized world z axis.
    S_vector GetWorldDir() const { return GetMatrix()(2).Normalized(); }
    const S_vector &GetPos() const { return m_localMatrix(3); }
    // Inline position setter (the virtual SetPos also notifies the scene).
    void SetPosInline(const S_vector &pos)
    {
        if (m_frameFlags & FRMFLAGS_POS_LOCKED)
            return;
        m_localMatrix(3) = pos;
        m_frameFlags = (m_frameFlags & ~FRMFLAGS_SETPOS_CLEAR) | FRMFLAGS_CHANGED;
    }
    const S_quat &GetRot()
    {
        if (!(m_frameFlags & FRMFLAGS_ROT_DIRTY)) {
            m_frameFlags |= FRMFLAGS_ROT_DIRTY;
            m_rot.Make(m_localMatrix);
        }
        return m_rot;
    }
    void SetRot(const S_quat &rot)
    {
        m_rot = rot;
        m_rot.Normalize();
        m_frameFlags = (m_frameFlags & ~FRMFLAGS_SETROT_CLEAR) | FRMFLAGS_CHANGED | FRMFLAGS_ROT_DIRTY;
    }
    // Slot 0x18: places the frame at `pos` turned by `rot` (the car's damage parts put their model
    // on the car with the car's world ones; meaning inferred). Called by its offset: the slot stays
    // unnamed above, as the decompiles show the subclasses' slot 0x74 (I3D_model::Open,
    // I3D_sound::Open) called through a frame's vtable as its "[1].Slot18".
    void SetPosRot(const S_vector &pos, const S_quat &rot)
    {
        using T_SetPosRot = void(__stdcall *)(I3D_frame *self, const S_vector &pos, const S_quat &rot);
        reinterpret_cast<T_SetPosRot>((*reinterpret_cast<void *const *const *>(this))[0x18 / 4])(this, pos, rot);
    }
    const S_vector &GetScale() const { return m_scale; }
    // World scale (the local one for root frames).
    S_vector GetWorldScale() const { return m_parent ? GetMatrix().GetScale() : m_scale; }
    // Inline scale setter; the rotation is captured first so it survives.
    void SetScale(const S_vector &scale)
    {
        m_scale = scale;
        if (!(m_frameFlags & FRMFLAGS_ROT_DIRTY))
            m_rot.Make(m_localMatrix);
        m_frameFlags = (m_frameFlags & ~FRMFLAGS_SETROT_CLEAR) | FRMFLAGS_CHANGED | FRMFLAGS_ROT_DIRTY;
    }
    // GetLocalDir before its normalisation: the local z axis, `*normalize` false when it was just
    // rebuilt from the rotation (then unit). For callers that sum the square in another order.
    S_vector GetLocalDirUnnormalized(bool *normalize)
    {
        *normalize = (m_frameFlags & FRMFLAGS_LMATRIX_VALID) != 0;
        if (!*normalize) {
            m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
            m_rot.Normalize();
            m_localMatrix.SetRot3(m_rot);
        }
        return m_localMatrix(2);
    }
    // Normalized local z axis (rebuilds the rotation part of the local matrix if needed).
    S_vector GetLocalDir()
    {
        if (!(m_frameFlags & FRMFLAGS_LMATRIX_VALID)) {
            m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
            m_rot.Normalize();
            m_localMatrix.SetRot3(m_rot);
            return m_localMatrix(2);
        }
        return m_localMatrix(2).Normalized();
    }
    const char *GetName() const { return m_name ? m_name : ""; }
    // Name without the model prefix ("model.frame" -> "frame").
    const char *GetShortName() const { return m_name ? m_name + m_nameOffset : ""; }
    I3D_frame *GetParent() const { return m_parent; }
    I3D_CALLBACK *GetCallback() const { return m_callback; }
    // Children as a linked list (the engine's own iteration; NumChildren/GetChild are virtual).
    I3D_frame *GetFirstChild() const { return m_firstChild; }
    I3D_frame *GetNextSibling() const { return m_nextSibling; }
    // Free-form properties set in the modelling tool (e.g. "version=50 sound=3").
    const char *GetUserProps() const { return m_userProps ? m_userProps : ""; }
    // Root model frame this frame was loaded with, null for scene frames.
    I3D_frame *GetOwnerModel() const { return m_ownerModel; }
    bool IsOn() const { return (m_frameFlags & 1) != 0; }
    uint32_t GetFrameFlags() const { return m_frameFlags; }
    void OrFrameFlags(uint32_t bits) { m_frameFlags |= bits; }
    // Visuals and models only (+0x11C): number of the scene frame that last drew it
    // (equal to I3D_scene::GetRenderStamp() when it was visible in the last frame).
    int GetRenderStamp() const { return Field<int>(0x11C); }
    float GetBoundRadius() const { return m_bound.radius; }
    // Bound of the frame and its children in local space (SDK inline).
    const I3D_bound_volume &GetBound()
    {
        if (!(m_frameFlags & FRMFLAGS_BOUND_VALID))
            UpdateBound();
        return m_bound;
    }
    bool IsBoundValid() const { return (m_frameFlags & FRMFLAGS_BOUND_VALID) != 0; }
    float GetPosW() const { return m_localMatrix.m[3][3]; }
    // Sets the world matrix directly and marks it valid (the frame's local transform is
    // left as it is; used for unparented frames that are positioned in world space).
    void SetWorldMatrixInline(const S_matrix &m)
    {
        m_worldMatrix = m;
        m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
    }
    // Gives the local matrix the rotation looking along `dir` with `up` (its scale lost; SDK
    // inline, the vehicle's wheels at Start).
    void SetLocalDir3(const S_vector &dir, const S_vector &up)
    {
        m_localMatrix.SetDir3(dir, up);
        m_frameFlags = (m_frameFlags & ~0x12Cu) | FRMFLAGS_CHANGED | FRMFLAGS_LMATRIX_VALID;
    }
    // Replaces the local matrix (rotation and scale are re-derived on demand).
    void SetLocalMatrix(const S_matrix &m)
    {
        m_localMatrix = m;
        m_scale = m_localMatrix.GetScale();
        m_frameFlags = (m_frameFlags & ~0x28u) | FRMFLAGS_CHANGED | FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
    }

    // Local matrix, rebuilt from rotation and scale when needed (SDK inline 0x0041d370).
    const S_matrix &GetLocalMatrix()
    {
        if (!(m_frameFlags & FRMFLAGS_LMATRIX_VALID)) {
            m_frameFlags |= FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
            m_localMatrix.SetRot3(m_rot);
        } else {
            if (m_frameFlags & FRMFLAGS_ROT_VALID)
                return m_localMatrix;
            if (!(m_frameFlags & FRMFLAGS_ROT_DIRTY))
                m_rot.Make(m_localMatrix);
            m_frameFlags |= FRMFLAGS_ROT_VALID | FRMFLAGS_ROT_DIRTY;
        }
        for (int c = 0; c < 3; ++c) {
            m_localMatrix.m[0][c] *= m_scale.x;
            m_localMatrix.m[1][c] *= m_scale.y;
            m_localMatrix.m[2][c] *= m_scale.z;
        }
        return m_localMatrix;
    }

protected:
    // Members of derived engine frames that the SDK reads inline.
    template <class T> T &Field(size_t offset) { return *reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(this) + offset); }
    template <class T> const T &Field(size_t offset) const
    {
        return *reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(this) + offset);
    }

private:
    LS3D_API void UpdateWMatrixProc();                        // LS3DF.DLL (private, __thiscall)

    uint8_t m_unknown04[0x0C];
    I3D_bound_volume m_bound;                                 // +0x10 valid with FRMFLAGS_BOUND_VALID
    S_matrix m_worldMatrix;                                   // +0x40
    S_matrix m_localMatrix;                                   // +0x80 (row 3 = m_pos)
    S_quat m_rot;                                             // +0xC0
    S_vector m_scale;                                         // +0xD0
    float m_scaleW;                                           // +0xDC copied along with m_scale
    uint32_t m_frameFlags;                                    // +0xE0
    int16_t m_refCount;                                       // +0xE4
    int16_t m_nameOffset;                                     // +0xE6 start of the short name in m_name
    const char *m_name;                                       // +0xE8
    S_FrameData *m_data;                                      // +0xEC
    const char *m_userProps;                                  // +0xF0 user property text of the model file
    I3D_CALLBACK *m_callback;                                 // +0xF4 SetCallback()
    E_I3D_FRAME_TYPE m_type;                                  // +0xF8
    uint8_t m_unknownFA[6];
    I3D_frame *m_ownerModel;                                  // +0x100
    uint8_t m_unknown104[4];
    I3D_frame *m_parent;                                      // +0x108
    I3D_frame *m_firstChild;                                  // +0x10C
    I3D_frame *m_nextSibling;                                 // +0x110

public:
    // Member offsets are fixed by the engine DLL.
    static constexpr bool CheckLayout();
};
#pragma pack(pop)

constexpr bool I3D_frame::CheckLayout()
{
    static_assert(offsetof(I3D_frame, m_bound) == 0x10, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_worldMatrix) == 0x40, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_localMatrix) == 0x80, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_rot) == 0xC0, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_scale) == 0xD0, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_frameFlags) == 0xE0, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_refCount) == 0xE4, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_name) == 0xE8, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_data) == 0xEC, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_userProps) == 0xF0, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_type) == 0xF8, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_ownerModel) == 0x100, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_parent) == 0x108, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_firstChild) == 0x10C, "I3D_frame layout");
    static_assert(offsetof(I3D_frame, m_nextSibling) == 0x110, "I3D_frame layout");
    return true;
}

// I3D_frame::LinkTo() flags.
enum : uint32_t {
    I3DLINK_KEEP_WORLD = 1,       // keep the world transform
};

// Visual types (I3D_frame::GetVisualType()).
enum : uint32_t {
    I3DVISUAL_LMAP = 0x50414D4C,  // 'LMAP' lightmapped object
    I3DVISUAL_OBJECT = 0x5F4A424F,// 'OBJ_'
    I3DVISUAL_SINGLEMESH = 0x4D474E53,  // 'SNGM' skinned mesh
    I3DVISUAL_SINGLEMORPH = 0x524D4D53, // 'SMMR' skinned morphed mesh
    I3DVISUAL_PROJECTOR = 0x4A4F5250,   // 'PROJ' projected texture (decal)
    I3DVISUAL_EMITTER = 0x52544D45,     // 'EMTR' particle quads
    I3DVISUAL_MIRROR = 0x5252494D,      // 'MIRR'
    I3DVISUAL_BILLBOARD = 0x44524242,   // 'BBRD' (bullet traces)
    I3DVISUAL_SHD = 0x00444853,         // 'SHD' (not identified; no decals on it)
};

// Visual flag bits (I3D_visual::VisualFlags(), +0x1D0); others are not identified (the
// debris code clears 0x2000).
enum : uint32_t {
    VISFLAGS_WORLD_SPACE = 0x00000040,     // geometry is in world space (no inverse transform)
    VISFLAGS_1000 = 0x00001000,            // set on the parts of the HUD's member panels (not identified)
    VISFLAGS_BOUND_VALID = 0x00020000,     // GetLocalBound() is up to date
    VISFLAGS_WBOUND_VALID = 0x00040000,    // GetWorldBound() is up to date
    VISFLAGS_INVMATRIX_VALID = 0x00100000, // cached inverse world matrix is up to date
};

// Methods common to all visual types (vtable slots 0x50..0x90).
class I3D_visual : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);                              // LS3DF: Init, Uninit, MatrixChanged, Slot6C
    I3D_SLOT(70);                                             // LS3DF: Load
    // Sets the bounds used for culling (particle visuals, whose quads the game
    // rebuilds every frame).
    I3DMETHOD(void, SetBoundVolume)(const I3D_bound_volume &bound) = 0;   // 0x74
    I3D_SLOT(78);                                             // LS3DF: SetBBox
    // Called with 1 on the visuals whose geometry the level collision uses (meaning not
    // identified).
    I3DMETHOD(void, SetWorldSpace)(int value) = 0;            // 0x7C
    // 0 on the visuals of a carried body, 2 when it is put down (meaning not identified).
    I3DMETHOD(void, SetModeA)(int value) = 0;            // 0x80
    I3D_SLOT(84);                                             // LS3DF: GetModeA
    I3D_SLOT(88);                                             // LS3DF: SetModeB
    I3D_SLOT(8C);                                             // LS3DF: GetModeB
    // Recomputes the bound of the visual's own geometry (GetLocalBound()).
    I3DMETHOD(void, UpdateLocalBound)() = 0;                  // 0x90

    // Bound of the visual's own geometry (SDK inline).
    const I3D_bound_volume &GetLocalBound()
    {
        if (!(VisualFlags() & VISFLAGS_BOUND_VALID))
            UpdateLocalBound();
        return Field<I3D_bound_volume>(0x1A0);
    }
    // Box of the visual in world space (SDK inline; the interface's menus test the cursor against
    // it).
    const I3D_bbox &GetWorldBound()
    {
        if (!(VisualFlags() & VISFLAGS_WBOUND_VALID))
            UpdateWBoundProc();
        return Field<I3D_bbox>(0x170);
    }
    // Id of the texture-shift entry (C_TShiftModule) of this visual, 0 if none.
    uint32_t GetShiftId() const { return Field<uint32_t>(0x1E0); }
    uint32_t &VisualFlags() { return Field<uint32_t>(0x1D0); }
    // Geometry of mesh-based visuals (object, lightmap, billboard visuals; +0x1E0).
    class I3D_mesh *GetVisualMesh() const { return Field<class I3D_mesh *>(0x1E0); }

    // Inverse of the world matrix (cached at +0x130), identity for world-space visuals.
    const S_matrix &GetInvMatrix()
    {
        if (VisualFlags() & VISFLAGS_WORLD_SPACE)
            return IdentityMatrix();
        if (!(VisualFlags() & VISFLAGS_INVMATRIX_VALID)) {
            VisualFlags() |= VISFLAGS_INVMATRIX_VALID;
            Field<S_matrix>(0x130).Inverse(GetMatrix());
        }
        return Field<S_matrix>(0x130);
    }

private:
    LS3D_API void __stdcall UpdateWBoundProc();               // LS3DF.DLL (private; the visual on the stack)
};

// Lens flare visual ("lensf" frames of models). Field meanings not identified.
class I3D_lensflare_visual : public I3D_visual {
public:
    void SetUnknownParams(float p1E0, float p1E4, float p240, float p244)
    {
        Field<float>(0x1E0) = p1E0;
        Field<float>(0x1E4) = p1E4;
        Field<float>(0x240) = p240;
        Field<float>(0x244) = p244;
    }
    // The signal flare grows and fades its "flare" child through these three fields
    // (+0x1E0, +0x1E4, +0x244; read back from +0x1E4). Meaning inferred: the flare's size.
    float GetFlareScale() const { return Field<float>(0x1E4); }
    void SetFlareScale(float scale)
    {
        Field<float>(0x1E0) = scale;
        Field<float>(0x1E4) = scale;
        Field<float>(0x244) = scale;
    }
};

// Object visual (I3DVISUAL_OBJECT, 'OBJ_').
class I3D_object_visual : public I3D_visual {
public:
    I3D_SLOT(94);                                             // LS3DF: Prepare
    I3D_SLOT(98);                                             // LS3DF: Draw
    // Gives the visual another mesh (the vehicle passes 0; meaning of the flag not identified).
    I3DMETHOD(void, SetMesh)(class I3D_mesh *mesh, int unknown) = 0;   // 0x9C
    // Gives the visual its own copy of its mesh (before the HUD changes a face group's material;
    // inferred).
    I3DMETHOD(void, DuplicateMesh)() = 0;                     // 0xA0
    // Light level of the visual (C_HumanEquipment gives item models the body's).
    I3DMETHOD(void, SetLightLevel)(float level) = 0;          // 0xA4
    // Opacity of the visual (1 on thrown grenades; the compass' radar fades its blips with it;
    // inferred).
    I3DMETHOD(void, SetAlpha)(float alpha) = 0;               // 0xA8
    // Called per detail level after the mesh's vertices were rewritten (the vehicle's repaired
    // parts; meaning not identified).
    I3DMETHOD(void, RefreshLevel)(int level) = 0;                // 0xAC

    float GetAlpha() const { return Field<float>(0x1EC); }
};

// Skinned mesh visual (the "a" body visual of human models).
class I3D_singlemesh_visual : public I3D_visual {
public:
    // Joint frames the mesh is skinned to (+0x214; the hit zones use indices 1..21).
    I3D_frame *const *GetJoints() const { return Field<I3D_frame **>(0x214); }
};

// A texture of a material (its file name; the HUD keeps references).
class I3D_texture {
public:
    I3DMETHOD(uint32_t, Release)() = 0;                       // 0x00
    // (The reference count at +0x14 raised inline.)
    void AddRef() { ++*reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(this) + 0x14); }
    const char *GetFileName() const
    {
        const char *name = *reinterpret_cast<const char *const *>(reinterpret_cast<const uint8_t *>(this) + 0x18);
        return name ? name : "";
    }
};

constexpr uint32_t I3DMATERIAL_TEXTURE_PENDING_BIT = 0x04000000;

// Material of a mesh face group. Slots 0x04 - 0x18 set the D3DMATERIAL8 the DLL keeps at +0x14
// (LS3DF 0x1002b140 - 0x1002b1d0); +0x08 holds flags, of which the traces set 0x08000000.
class I3D_material {
public:
    I3DMETHOD(uint32_t, Release)() = 0;                       // 0x00
    // (The member panels pass their photo's colour: (1, 1, 1) or the human's +0x1E0.)
    I3DMETHOD(void, SetAmbient)(const S_vector &color) = 0;   // 0x04
    I3DMETHOD(void, SetDiffuse)(const S_vector &color) = 0;   // 0x08
    I3DMETHOD(void, SetEmissive)(const S_vector &color) = 0;  // 0x0C
    I3DMETHOD(void, SetSpecular)(const S_vector &color) = 0;  // 0x10
    I3DMETHOD(void, SetPower)(float power) = 0;               // 0x14
    I3D_SLOT(18);                                             // LS3DF: SetOpacity
    // (The first-person view gives the hands the uniform's sleeve texture.)
    I3DMETHOD(void, SetTexture)(I3D_texture *texture) = 0;    // 0x1C
    // Copies another material (the vehicle's glass parts start from their face group's; inferred).
    I3DMETHOD(void, CopyFrom)(I3D_material *source) = 0;      // 0x20
    I3D_SLOT(24);                                             // LS3DF: SetTwoSided
    I3D_SLOT(28);                                             // LS3DF: SetEnvironment
    I3D_SLOT(2C);                                             // LS3DF: SetEnvTexture
    I3D_SLOTS_4(30, 34, 38, 3C);                              // LS3DF: SetBits8, SetBits12, SetBits0, SetAlpha
    I3D_SLOT(40);
    // Called before the texture (+0x0C) is read while flag I3DMATERIAL_TEXTURE_PENDING_BIT is set
    // (meaning inferred).
    I3DMETHOD(void, ResolveTexture)() = 0;                    // 0x44

    uint32_t &Flags() { return *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(this) + 0x08); }
    I3D_texture *GetTexture()
    {
        if (Flags() & I3DMATERIAL_TEXTURE_PENDING_BIT)
            ResolveTexture();
        return *reinterpret_cast<I3D_texture **>(reinterpret_cast<uint8_t *>(this) + 0x0C);
    }
};
constexpr uint32_t I3DMATERIAL_TRACE_FLAG = 0x08000000;     // set on bullet traces (not identified)

// Face group of a mesh: +0x04 its material.
class I3D_face_group {
public:
    // Replaces the face group's material (the vehicle's glass parts; inferred).
    I3DMETHOD(void, SetMaterial)(I3D_material *material) = 0; // 0x00
    I3D_SLOTS_4(04, 08, 0C, 10);                              // LS3DF: SetFaces, AllocFaces, Lock, Unlock
    I3D_SLOT(14);                                             // LS3DF: Duplicate
    // Gives the face group its own copy of its material (the HUD's icons and bars then change
    // theirs alone; inferred).
    I3DMETHOD(void, DuplicateMaterial)() = 0;                 // 0x18

    I3D_material *material;                                   // +0x04
};

// Geometry of a billboard visual (reached through its +0x1E0). Only the calls the
// bullet traces make are known: the first level's first face group.
class I3D_mesh_level {
public:
    I3D_SLOT(00);                                             // LS3DF: Release
    // Vertex array of the level (0x20-byte vertices, position first); the level collision
    // passes 1 (meaning not identified).
    I3DMETHOD(void *, GetVertices)(int unknown) = 0;           // 0x04
    // Ends a GetVertices(0) (inferred: the vehicle's track strips call it after reading).
    I3DMETHOD(void, UnlockVertices)() = 0;                    // 0x08
    I3D_SLOT(0C);                                             // LS3DF: Duplicate
    I3D_SLOT(10);                                             // LS3DF: NumFaces
    I3DMETHOD(I3D_face_group *, GetFaceGroup)(int index) = 0;  // 0x14
    I3D_SLOTS_4(18, 1C, 20, 24);                              // LS3DF: GetStats, AddFaceGroup, RemoveFaceGroup, SetVertices
    I3D_SLOT(28);                                             // LS3DF: AllocVertices
    // Called after the level's vertex positions were rewritten (the vehicle's repaired parts;
    // meaning not identified).
    I3DMETHOD(void, ComputeBounds)() = 0;                         // 0x2C

    int NumVertices() const { return *reinterpret_cast<const int *>(reinterpret_cast<const uint8_t *>(this) + 0x54); }
    int NumFaceGroups() const { return *reinterpret_cast<const int *>(reinterpret_cast<const uint8_t *>(this) + 0x58); }
    // The face groups' array (+0x5C; the HUD's icons read the first one's material inline).
    I3D_face_group *FaceGroups() const
    {
        return *reinterpret_cast<I3D_face_group *const *>(reinterpret_cast<const uint8_t *>(this) + 0x5C);
    }
};
class I3D_mesh {
public:
    I3DMETHOD(uint32_t, Release)() = 0;                       // 0x00
    I3DMETHOD(I3D_mesh_level *, GetLevel)(int index) = 0;      // 0x04
    // (The vehicle's kept originals call it with null for each level past the second: inferred
    // dropping it.)
    I3DMETHOD(void, SetLevel)(I3D_mesh_level *level, int index) = 0;   // 0x08
    // Copies another mesh into this new one (0 on success; the vehicle's damage parts get their
    // own copy, inferred).
    I3DMETHOD(HRESULT, Duplicate)(I3D_mesh *source) = 0;      // 0x0C
    I3D_SLOT(10);                                             // LS3DF: GetStats
    // Called after the levels' vertices were rewritten (the vehicle's repaired parts; meaning not
    // identified).
    I3DMETHOD(void, ComputeBounds)() = 0;                         // 0x14
    // Called on the inventory's icons once they have their own mesh (meaning not identified).
    I3DMETHOD(void, DuplicateMaterials)() = 0;                         // 0x18

    // Number of detail levels (+0x44).
    uint32_t NumLevels() const { return *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(this) + 0x44); }
};

// Billboard visual (I3DVISUAL_BILLBOARD): a quad stretched along its z axis (bullet traces).
class I3D_billboard_visual : public I3D_visual {
public:
    I3D_SLOT(94);                                             // LS3DF: Prepare
    I3D_SLOT(98);                                             // LS3DF: Draw
    I3D_SLOT(9C);                                             // LS3DF: SetMesh
    I3D_SLOT(A0);                                             // LS3DF: DuplicateMesh
    // Called with 1.0 on the traces of client bullets only (meaning not identified).
    I3DMETHOD(void, SetLightLevel)(float value) = 0;           // 0xA4

    I3D_mesh *GetMesh() const { return Field<I3D_mesh *>(0x1E0); }
};

// Projected texture (I3DVISUAL_PROJECTOR): bullet holes, blood and scorch marks. Slot
// meanings inferred from the shot manager, which always calls SetMode(3), SetUnknownB4(1)
// and SetOrthogonal(1).
class I3D_projector_visual : public I3D_visual {
public:
    I3D_SLOT(94);                                             // LS3DF: Prepare
    I3D_SLOT(98);                                             // LS3DF: Draw
    I3DMETHOD(void, SetTexture)(class IImage *image) = 0;     // 0x9C
    I3D_SLOT(A0);                                             // LS3DF: SetMaterial
    I3DMETHOD(void, SetOrthogonal)(int value) = 0;             // 0xA4
    // Opacity of the projected texture (0..1; footprints fade with it).
    I3DMETHOD(void, SetOpacity)(float opacity) = 0;           // 0xA8
    I3D_SLOT(AC);                                             // LS3DF: SetOpacity
    I3DMETHOD(void, SetColor)(const S_vector &color) = 0;     // 0xB0
    I3DMETHOD(void, SetUnknownB4)(int value) = 0;             // 0xB4
    I3DMETHOD(void, SetMode)(int mode) = 0;                   // 0xB8
    // Projects the texture onto the geometry under it again (footprints call it with 0, 0).
    I3DMETHOD(void, Reproject)(int unknown1, int unknown2) = 0;   // 0xBC

    // Set to 22 on footprints (meaning not identified).
    void SetUnknown1E4(float value) { Field<float>(0x1E4) = value; }
    float GetOpacity() const { return Field<float>(0x1E8); }
    // Full-light color of the decal (the shot manager scales it by the light at the decal).
    const S_vector &GetBaseColor() const { return Field<S_vector>(0x1F0); }
};

// Lightmapped visual (I3DVISUAL_LMAP). Slot meanings are inferred from the
// versions module, which carries the lighting over to a replacement visual.
class I3D_lmap_visual : public I3D_visual {
public:
    I3D_SLOT(94);                                             // LS3DF: Prepare
    I3D_SLOT(98);                                             // LS3DF: Draw
    I3D_SLOT(9C);                                             // LS3DF: SetMesh
    I3D_SLOTS_4(A0, A4, A8, AC);                              // LS3DF: DuplicateMesh, SetLightLevel, SetAlpha, RefreshLevel
    I3D_SLOT(B0);                                             // LS3DF: RefreshLevels
    // Recomputes the lighting of the visual in `scene`.
    I3DMETHOD(void, Relight)(I3D_scene *scene, const S_vector &up, uint32_t param, int quality,
                             int unknown1, int unknown2) = 0;     // 0xB4
    I3D_SLOT(B8);                                             // LS3DF: FreeLightmaps
    I3D_SLOT(BC);                                             // LS3DF: LoadLightmaps
    I3D_SLOT(C0);                                             // LS3DF: SaveLightmaps
    I3DMETHOD(void, LoadLightmap)(uint32_t lightmapId) = 0;  // 0xC4
    I3DMETHOD(void, SelectLightmap)(int index) = 0;          // 0xC8

    // Lightmap records (0x38 bytes each) and the selected one.
    int GetLightmapIndex() const { return Field<int>(0x210); }
    uint32_t &LightmapId(int index) { return Field<uint32_t>(0x218 + index * 0x38); }
    uint32_t &LightmapParam1(int index) { return Field<uint32_t>(0x21C + index * 0x38); }
    uint32_t &LightmapParam2(int index) { return Field<uint32_t>(0x220 + index * 0x38); }
};

// A texture key of an I3D_particle_visual as the car effects pass it to AddKey (0x34 bytes; the
// visual keeps 0x3C-byte copies at +0x1F4, the rectangle first).
struct I3D_emitter_key {
    float rect[4];                                            // +0x00 the texture rectangle
    int unknown10, unknown14, unknown18;                      // +0x10 (1, 1, 1)
    int unknown1C;                                            // +0x1C (0)
    int unknown20;                                            // +0x20 (2000)
    void *unknown24[3];                                       // +0x24 (an empty vector: begin, end, capacity)
    int unknown30;                                            // +0x30 (8)
};

// Particle visual (I3DVISUAL_EMITTER): a dynamic list of textured quads that the
// particles module refills every frame. The car effects (game/car_effects.cpp) let it emit its
// own particles instead: set up by properties (SetRenderState), run by Tick.
class I3D_particle_visual : public I3D_visual {
public:
    I3D_SLOT(94);                                             // LS3DF: Prepare
    I3D_SLOT(98);                                             // LS3DF: Draw
    I3DMETHOD(void, SetTexture)(class IImage *image) = 0;     // 0x9C
    // A property (the value's bits: an int, a float or a pointer): 0x13 the blend mode; the car
    // effects' 0..0x1A, 0x1D / 0x1E (colours, float[3] pointers), 0x22 (S_CarEffectDef).
    I3DMETHOD(void, SetRenderState)(int state, uint32_t value) = 0;   // 0xA0
    I3D_SLOT(A4);                                             // LS3DF: GetParam
    // (Called once on a new car emitter after its first Update; meaning not identified.)
    I3DMETHOD(void, SetDefaults)() = 0;                         // 0xA8
    // Starts / stops emitting new particles (the live ones run on).
    I3DMETHOD(void, SetEmitting)(bool on) = 0;                // 0xAC
    I3DMETHOD(void, SetSrcBlend)(int blend) = 0;              // 0xB0
    I3D_SLOT(B4);                                             // LS3DF: GetSrcBlend
    I3DMETHOD(void, SetDstBlend)(int blend) = 0;              // 0xB8
    I3D_SLOT(BC);                                             // LS3DF: GetDestBlend
    // The texture keys: one marked for use (inferred), one added (index -1: at the end), one
    // removed.
    I3DMETHOD(void, SelectKey)(int index) = 0;                // 0xC0
    I3DMETHOD(void, AddKey)(const I3D_emitter_key &key, int index) = 0;   // 0xC4
    I3DMETHOD(void, RemoveKey)(int index) = 0;                // 0xC8
    I3D_SLOT(CC);                                             // LS3DF: ClearParticles
    I3D_SLOT(D0);                                             // LS3DF: UpdateParticleBound
    // Resizes the quad list to `count` quads (4 vertices each) and returns it for
    // filling; `depth` sorts the visual among the transparent ones.
    I3DMETHOD(I3D_particle_vertex *, LockQuads)(int count, float depth) = 0;   // 0xD4

    // Stamp of the last rendered frame (I3D_scene::GetRenderStamp()), -1 before the first.
    int &RenderStamp() { return Field<int>(0x11C); }
    // Byte set to 1 by the particles module right after creating the visual (meaning
    // not identified).
    uint8_t &Unknown1E1() { return Field<uint8_t>(0x1E1); }
    // The file name of the texture SetTexture gave it ("" when unnamed).
    const char *GetTextureFileName() const
    {
        const char *name = *reinterpret_cast<const char *const *>(Field<const uint8_t *>(0x1F0) + 0x18);
        return name ? name : "";
    }
    // The texture keys (0x3C bytes each, the rectangle first).
    int NumKeys() const { return Field<int>(0x1FC); }
    const float *GetKeyRect(int index) const
    {
        return reinterpret_cast<const float *>(Field<const uint8_t *>(0x1F4) + index * 0x3C);
    }
    // The particles alive (its own emission).
    int NumParticles() const { return Field<int>(0x220); }
};

// Vertex of I3D_particle_visual quads (in the visual's local space).
struct I3D_particle_vertex {
    S_vector pos;
    uint32_t diffuse;             // ARGB
    float u, v;
};

class I3D_projector : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);                              // LS3DF: Init, Uninit, MatrixChanged, Slot6C
    I3D_SLOTS_4(70, 74, 78, 7C);                              // LS3DF: Load, SetBoundVolume, SetBBox, SetWorldSpace
    I3D_SLOTS_4(80, 84, 88, 8C);                              // LS3DF: SetModeA, GetModeA, SetModeB, GetModeB
    I3D_SLOTS_4(90, 94, 98, 9C);                              // LS3DF: UpdateLocalBound, Prepare, Draw, SetTexture
    I3D_SLOTS_4(A0, A4, A8, AC);                              // LS3DF: SetMaterial, SetOrthogonal, SetIntensity, SetOpacity
    I3D_SLOT(B0);                                             // LS3DF: SetColor
    I3D_SLOT(B4);                                             // LS3DF: SetMode
    I3D_SLOT(B8);                                             // LS3DF: SetBuffered
    // Re-projects onto the geometry around it (after the target changed).
    I3DMETHOD(void, Reproject)(int unknown1, int unknown2) = 0;   // 0xBC
};

// Joint of a model's skeleton (FRAME_JOINT).
class I3D_joint : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);
    I3D_SLOTS_4(70, 74, 78, 7C);
    I3D_SLOTS_4(80, 84, 88, 8C);
    I3D_SLOTS_4(90, 94, 98, 9C);
    I3D_SLOT(A0);
    I3D_SLOT(A4);
    // Called with 0 on the pelvis of every posed human while the skeleton debug switch
    // (0x0098af98) is on (not identified). Quirk kept: LS3DF.DLL's joint vtable (0x1009b9d8) ends
    // at slot 0x70, so this reads the next vtable's slot 0x30 (0x1001cfe0, I3D_frame::SetOn).
    I3DMETHOD(void, UnknownA8)(int value) = 0;                // 0xA8
};

// Dummy frame: an invisible box.
class I3D_dummy : public I3D_frame {
public:
    const I3D_bbox &GetBBox() const { return Field<I3D_bbox>(0x120); }
};


// Keyframe animation that can be put on a model stage (refcounted).
class I3D_animation_set {
public:
    I3DMETHOD(uint32_t, Release)() = 0;                       // 0x00
    I3DMETHOD(HRESULT, Open)(const char *fileName, uint32_t flags) = 0;   // 0x04
    I3DMETHOD(int, GetEndTime)() = 0;                         // 0x08 ms
    I3D_SLOT(0C);
    I3D_SLOT(10);
    // The keys of the joints by name (C_HumanWeaponPose reads them; -1: no track).
    I3DMETHOD(int, FindTrack)(const char *name) = 0;          // 0x14
    I3DMETHOD(int, NumPosKeys)(int track) = 0;                // 0x18
    I3DMETHOD(void, GetPosKey)(int track, int key, int *time, S_vector *pos) = 0;   // 0x1C
    I3DMETHOD(int, NumRotKeys)(int track) = 0;                // 0x20
    I3DMETHOD(void, GetRotKey)(int track, int key, int *time, S_quat *rot) = 0;    // 0x24
    // Play mode, set before the set is put on a stage.
    void SetPlayMode(int mode) { m_playMode = mode; }        // 0x0041d4c0
    void AddRef() { ++m_refCount; }
    int GetRefCount() const { return m_refCount; }
    int GetPlayMode() const { return m_playMode; }
    // The file it was loaded from (with the path; may be null).
    const char *GetFileName() const { return m_fileName; }

private:
    int m_refCount;                                           // +0x04
    int m_playMode;                                           // +0x08
    const char *m_fileName;                                   // +0x0C
};

enum E_I3D_ANIM_MODE {
    I3DANIM_PLAY_ONCE = 1,
    I3DANIM_LOOP = 2,
};

// A joint override: turns one joint of a model away from its animated pose (I3D_driver::
// CreateJointOverride makes one for a joint name). Only the meleeTracker's head look uses these, so
// the names are inferred from that use.
class I3D_joint_override {
public:
    I3D_SLOT(00);
    I3D_SLOT(04);
    // Stops overriding: the joint follows the animation again.
    I3DMETHOD(void, Clear)() = 0;                             // 0x08
    I3D_SLOTS_4(0C, 10, 14, 18);
    // Points the joint along `dir` (world space).
    I3DMETHOD(void, SetDir)(const S_vector &dir, float roll) = 0;   // 0x1C
};

// A set of joint overrides put on a model (I3D_model::SetJointOverrides; refcounted).
class I3D_joint_override_set {
public:
    I3DMETHOD(uint32_t, Release)() = 0;                       // 0x00
    I3DMETHOD(void, Add)(I3D_joint_override *item) = 0;       // 0x04
    I3DMETHOD(void, Remove)(I3D_joint_override *item) = 0;    // 0x08
    int NumItems() const { return *reinterpret_cast<const int *>(reinterpret_cast<const uint8_t *>(this) + 0x10); }
    I3D_joint_override *GetItem(int index) const
    {
        return (*reinterpret_cast<I3D_joint_override *const *const *>(reinterpret_cast<const uint8_t *>(this) + 0x08))[index];
    }
};

// An object of a model told about a stage (I3D_model::GetTracks; not identified: slot 0
// takes the stage, +0xEC holds the frame C_HumanAnimMixer::NotifyFrames looks for: the
// animation player passes joints of the skeleton).
class I3D_model_part {
public:
    I3DMETHOD(void, OnStage)(int stage) = 0;                  // 0x00
    I3D_frame *GetFrame() const
    {
        return *reinterpret_cast<I3D_frame *const *>(reinterpret_cast<const uint8_t *>(this) + 0xEC);
    }
};

// Animated model (FRAME_MODEL). Animation stages blend on top of each other.
class I3D_model : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);                              // LS3DF: Init, Uninit, MatrixChanged, Slot6C
    I3D_SLOT(70);                                             // LS3DF: Load
    // Loads the model file (hierarchy, meshes, animations) into this frame.
    I3DMETHOD(HRESULT, Open)(const char *fileName, uint32_t flags, I3D_LOAD_CB_PROC *callback,
                             void *context) = 0;              // 0x74
    I3D_SLOT(78);                                             // LS3DF: Close
    // Make a frame part of / no longer part of the model's own frame list.
    I3DMETHOD(void, AddModelFrame)(I3D_frame *frame) = 0;    // 0x7C
    I3DMETHOD(void, RemoveModelFrame)(I3D_frame *frame) = 0; // 0x80
    I3DMETHOD(void, SetAnimation)(I3D_animation_set *anim, int stage, float weight, int flags) = 0;  // 0x84
    I3DMETHOD(I3D_animation_set *, GetAnimation)(int stage) = 0;  // 0x88
    I3DMETHOD(void, StopAnimation)(int stage) = 0;            // 0x8C
    // (The first-person view calls it after SetAnimTime(stage, 0) when a shot's animation
    // starts over; meaning not identified.)
    I3DMETHOD(void, StartAnimation)(int stage) = 0;                // 0x90
    // Blend weight of a stage (C_HumanEquipment::InitHolsters saves and restores a stage
    // through 0x94..0xAC; meanings inferred from that use).
    I3DMETHOD(void, SetAnimWeight)(int stage, float weight) = 0;   // 0x94
    I3DMETHOD(float, GetAnimWeight)(int stage) = 0;           // 0x98
    I3DMETHOD(void, SetAnimTime)(int stage, int time) = 0;    // 0x9C ms
    I3DMETHOD(int, GetAnimTime)(int stage) = 0;               // 0xA0 ms
    I3D_SLOT(A4);                                             // LS3DF: SetAnimMode
    I3DMETHOD(int, GetAnimFlags)(int stage) = 0;              // 0xA8 SetAnimation()'s flags
    I3DMETHOD(bool, IsAnimActive)(int stage) = 0;             // 0xAC
    // Advances the time of each of the 8 stages by times[stage] ms and puts the frames into
    // the pose (C_HumanAnimMixer every update; C_HumanEquipment: stage 7 by 100 ms).
    I3DMETHOD(void, AdvanceStages)(const int *times) = 0;     // 0xB0
    // Puts a set of joint overrides on the model (the meleeTracker's head look; inferred).
    I3DMETHOD(void, SetJointOverrides)(I3D_joint_override_set *set) = 0;   // 0xB4
    I3D_SLOT(B8);                                             // LS3DF: SetPoseByName
    I3D_SLOT(BC);                                             // LS3DF: SetPose
    I3D_SLOT(C0);                                             // LS3DF: SetPoseSetWeight
    // (C_HumanAnimMixer::Init passes 0.)
    I3DMETHOD(void, SetPoseWeight)(int value) = 0;             // 0xC4
    I3D_SLOT(C8);                                             // LS3DF: GetPoseWeight
    I3D_SLOT(CC);                                             // LS3DF: ClearPoses
    // Objects of the model that C_HumanAnimMixer::NotifyFrames tells about a stage: their
    // number and array (not identified; I3D_model_part).
    I3DMETHOD(int, NumTracks)() = 0;                       // 0xD0
    I3DMETHOD(class I3D_model_part **, GetTracks)() = 0;   // 0xD4
    // Copies the mesh, skeleton and animations of another model.
    I3DMETHOD(void, DuplicateModel)(I3D_model *source) = 0;   // 0xD8

    const char *GetFileName() const
    {
        const char *name = Field<const char *>(0x134);
        return name ? name : "";
    }
    // The model's own frames (AddModelFrame; +0x120 array, +0x128 count).
    int NumModelFrames() const { return Field<int>(0x128); }
    I3D_frame *GetModelFrame(int index) const { return Field<I3D_frame **>(0x120)[index]; }
};

// Target frame: other frames aim at it (FRM_SetTarget).
class I3D_target : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);                              // LS3DF: Init, Uninit, MatrixChanged, Slot6C
    I3D_SLOT(70);                                             // LS3DF: Load
    I3DMETHOD(void, AddFrame)(I3D_frame *frame) = 0;          // 0x74
    I3DMETHOD(void, RemoveFrame)(I3D_frame *frame) = 0;       // 0x78
};

class I3D_sound : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);                              // LS3DF: Init, Uninit, MatrixChanged, Slot6C
    I3D_SLOT(70);                                             // LS3DF: Load
    I3DMETHOD(HRESULT, Open)(const char *fileName, uint32_t flags) = 0;   // 0x74
    I3DMETHOD(bool, IsPlaying)(int unknown) = 0;              // 0x78
    I3DMETHOD(void, SetSoundType)(int type) = 0;              // 0x7C I3DSOUND_*
    // Full volume up to `nearRange`, silent beyond `farRange` (m).
    I3DMETHOD(void, SetRange)(float nearRange, float farRange, float unknown1, float unknown2) = 0;   // 0x80
    // Inner / outer cone angles (radians) of a directional sound (meaning inferred).
    I3DMETHOD(void, SetCone)(float inner, float outer) = 0;   // 0x84
    // (C_Vehicle::LoadSound passes 0 after SetCone; meaning not identified.)
    I3DMETHOD(void, SetOutVolume)(int value) = 0;                // 0x88
    I3DMETHOD(void, SetVolume)(float volume) = 0;             // 0x8C 1.0 = 100 %
    I3DMETHOD(void, SetLoop)(bool loop) = 0;                  // 0x90
    // Starts playing at `position`.
    I3DMETHOD(void, Play)(int position) = 0;                  // 0x94
    // Playback rate (1 = as recorded; C_Vehicle's engine loops: the pitch by the revs).
    I3DMETHOD(void, SetFrequency)(float ratio) = 0;           // 0x98
    I3D_SLOT(9C);                                             // LS3DF: GetRange
    I3D_SLOT(A0);                                             // LS3DF: GetCone
    // Play position (ms, inferred: C_Vehicle::SwitchEngine compares GetLength() minus it with 300).
    I3DMETHOD(int, GetPosition)() = 0;                        // 0xA4
    // Length of the sound (ms; inferred from the breath sounds of the morphs).
    I3DMETHOD(int, GetLength)() = 0;                          // 0xA8
    // Starts / stops the sound's processing (stop: immediately when `immediate`).
    I3DMETHOD(void, SetActive)(bool on, bool immediate) = 0;  // 0xAC
    // Velocity used for the Doppler effect (m/s).
    I3DMETHOD(void, SetVelocity)(const S_vector &velocity) = 0;   // 0xB0

    S_vector4 GetVelocityInline() const { return Field<S_vector4>(0x1A0); }
    float GetVolumeInline() const { return Field<float>(0x16C); }
    // File of the sound source (inline 0x0051c0d0), "" without a source.
    const char *GetFileName() const
    {
        const uint8_t *source = Field<const uint8_t *>(0x140);
        // The source keeps its name as a C_str (pointer to { refCount, length, chars }).
        const uint8_t *name = source ? *reinterpret_cast<const uint8_t *const *>(source + 0xC) : nullptr;
        return name ? reinterpret_cast<const char *>(name + 8) : "";
    }
    // Flags the sound was opened with.
    uint32_t GetOpenFlags() const { return Field<uint32_t>(0x178); }
};

// I3D_sound::SetSoundType() values.
enum E_I3D_SOUND_TYPE {
    I3DSOUND_POINT = 1,           // positional
    I3DSOUND_AMBIENT = 3,         // heard everywhere at the same volume
};

// Opening between two sectors (doors, windows ...).
class I3D_portal {
public:
    I3D_SLOT(00);
    // Open (a closed portal, e.g. a shut door, dampens sound).
    I3DMETHOD(bool, IsOn)() = 0;                              // 0x04

    float GetRadius() const { return Field<float>(0x190); }
    I3D_sector *GetOwner() const { return Field<I3D_sector *>(0x194); }
    I3D_sector *GetConnected() const { return Field<I3D_sector *>(0x198); }

private:
    template <class T> const T &Field(size_t offset) const
    {
        return *reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(this) + offset);
    }
};

// The morph channels of a skinned morph visual (I3DVISUAL_SINGLEMORPH): an interface embedded at
// +0x120 of the visual (I3D_GetMorph()). Each channel blends up to its targets' weights.
class I3D_morph {
public:
    I3DMETHOD(int, NumTargets)() = 0;                         // 0x00
    I3D_SLOT(04);                                             // LS3DF: NumMorphChannels
    I3DMETHOD(void, SetWeight)(int channel, int target, float weight) = 0;   // 0x08
};
inline I3D_morph *I3D_GetMorph(I3D_frame *visual)
{
    return reinterpret_cast<I3D_morph *>(reinterpret_cast<uint8_t *>(visual) + 0x120);
}

// Light frame (FRAME_LIGHT). Slot meanings inferred from the muzzle flash lights (type 1,
// mode 0x11, white, power fading from 1, range fading from 4 m).
class I3D_light : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);                              // LS3DF: Init, Uninit, MatrixChanged, Slot6C
    I3D_SLOT(70);                                             // LS3DF: Load
    I3DMETHOD(void, SetLightType)(int type) = 0;              // 0x74
    // (Not identified: the vehicle's lamps pass 0.769, 0.675, 0.541, a warm white.)
    I3DMETHOD(void, SetColorRGB)(float a, float b, float c) = 0;   // 0x78
    I3DMETHOD(void, SetColor)(const S_vector &color) = 0;     // 0x7C
    I3DMETHOD(void, SetColor2)(const S_vector &color) = 0;    // 0x80 (second color, not identified)
    // (Not identified: the interface's light gets 1, 1, 1.)
    I3DMETHOD(void, SetAmbientColor)(const S_vector &value) = 0; // 0x84
    I3DMETHOD(void, SetPower)(float power) = 0;               // 0x88
    I3DMETHOD(void, SetSpecularPower)(float value) = 0;           // 0x8C
    I3DMETHOD(void, SetAmbientPower)(float value) = 0;           // 0x90 (the interface's light: 1)
    I3DMETHOD(void, SetMode)(uint32_t mode) = 0;              // 0x94
    I3DMETHOD(void, SetRange)(float range) = 0;               // 0x98
    I3DMETHOD(void, SetAttenuation)(float a, float b, float c) = 0;   // 0x9C
    I3D_SLOT(A0);                                             // LS3DF: SetFogRange
    // Spot cone (the vehicle's lamps: 0.52 and 1.22, radians; inferred).
    I3DMETHOD(void, SetCone)(float inner, float outer) = 0;   // 0xA4

    int GetLightType() const { return Field<int>(0x164); }       // SetLightType
    uint32_t GetMode() const { return Field<uint32_t>(0x160); }  // SetMode
    float GetRange() const { return Field<float>(0x174); }       // SetRange (far)
    // The color as it shines (the color times the power, inferred; +0x1A0).
    const S_vector &GetEffectiveColor() const { return Field<S_vector>(0x1A0); }
    // Spot lights: cosines of the outer and inner cone and the falloff between them.
    float GetConeOuterCos() const { return Field<float>(0x1D0); }
    float GetConeInnerCos() const { return Field<float>(0x1D4); }
    float GetConeFalloff() const { return Field<float>(0x1D8); }
    // Where the light is and where it points, as the light computes them (+0x224 / +0x230).
    const S_vector &GetLightPos() const { return Field<S_vector>(0x224); }
    const S_vector &GetLightDir() const { return Field<S_vector>(0x230); }
    // Attenuation: constant, linear, quadratic (+0x244).
    float GetAttenuation(int index) const { return (&Field<float>(0x244))[index]; }
    const S_vector &GetColor() const { return Field<S_vector>(0x130); }   // SetColor
    float GetPower() const { return Field<float>(0x168); }      // SetPower
    // Sectors the light was added to (I3D_sector::AddLight()).
    int NumSectors() const { return Field<int>(0x128); }
    I3D_sector *GetSector(int i) const { return Field<I3D_sector **>(0x120)[i]; }
    I3D_sector *const *GetSectors() const { return Field<I3D_sector **>(0x120); }
};
enum : int {
    I3DLIGHT_POINT = 1,                                     // (inferred)
    I3DLIGHT_SPOT = 2,                                      // (C_game::GetLightIntensity)
    I3DLIGHT_DIRECTIONAL = 3,
    I3DLIGHT_AMBIENT = 4,
};
constexpr uint32_t I3DLIGHTMODE_WEAPON_FLASH = 0x11;        // mode of muzzle lights (bits not identified)

// Space of the scene graph connected to its neighbours through portals.
// User data (I3D_frame::GetData()) of sectors is owned by the sound module.
class I3D_sector : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);                              // LS3DF: Init, Uninit, MatrixChanged, Slot6C
    I3D_SLOT(70);                                             // LS3DF: Load
    // Makes a light shine in this sector.
    I3DMETHOD(void, AddLight)(I3D_light *light) = 0;          // 0x74
    I3DMETHOD(void, RemoveLight)(I3D_light *light) = 0;       // 0x78
    // Registers a sound with the sector and takes it back (meaning inferred from their uses).
    I3DMETHOD(void, AddSound)(class I3D_sound *sound) = 0;    // 0x7C
    I3DMETHOD(void, RemoveSound)(class I3D_sound *sound) = 0; // 0x80

    // Portals of this sector (leading to GetConnected()).
    int NumPortals() const { return Field<int>(0x254); }
    I3D_portal *GetPortal(int i) const { return Field<I3D_portal **>(0x24C)[i]; }
    // Portals of other sectors leading into this one (from GetOwner()).
    int NumIncomingPortals() const { return Field<int>(0x264); }
    I3D_portal *GetIncomingPortal(int i) const { return Field<I3D_portal **>(0x25C)[i]; }
    void *GetUserData() const { return Field<void *>(0xEC); }
    void SetUserData(void *data) { Field<void *>(0xEC) = data; }
    // Lights shining in the sector (AddLight).
    int NumLights() const { return Field<int>(0x1AC); }
    I3D_light *GetLight(int i) const { return Field<I3D_light **>(0x1A4)[i]; }
};

// Projected shadow of a model (FRAME_SHADOW), loaded from its own file.
class I3D_shadow : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);                              // LS3DF: Init, Uninit, MatrixChanged, Slot6C
    I3D_SLOT(70);                                             // LS3DF: Load
    I3DMETHOD(HRESULT, Open)(const char *fileName, uint32_t flags) = 0;   // 0x74

    // Set to 20 on the shadows of human models (C_HumanVisual::RebuildShadow; meaning not
    // identified).
    void SetUnknown1FC(float value) { Field<float>(0x1FC) = value; }
};

class I3D_camera : public I3D_frame {
public:
    I3D_SLOT(5C);
    I3D_SLOTS_4(60, 64, 68, 6C);                              // LS3DF: Init, Uninit, MatrixChanged, Slot6C
    I3D_SLOT(70);                                             // LS3DF: Load
    I3DMETHOD(void, SetFOV)(float radians) = 0;               // 0x74
    I3D_SLOT(78);                                             // LS3DF: SetNearSectorFOV
    I3DMETHOD(void, SetRange)(float nearRange, float farRange) = 0;   // 0x7C
    I3D_SLOT(80);                                             // LS3DF: SetNearSectorRange
    // The interface's camera: 2 and on (meanings inferred: an orthogonal projection and its
    // scale).
    I3DMETHOD(void, SetOrthoScale)(float scale) = 0;          // 0x84
    I3DMETHOD(void, SetOrthogonal)(bool on) = 0;              // 0x88
    I3DMETHOD(void, SetAspectRatio)(float aspect) = 0;        // 0x8C (field +0x138)

    float GetFOV() const { return Field<float>(0x120); }
    void GetRange(float &nearRange, float &farRange) const
    {
        nearRange = Field<float>(0x124);
        farRange = Field<float>(0x128);
    }
    float GetAspectRatio() const { return Field<float>(0x138); }
    // World -> view space transform.
    const S_matrix &GetViewMatrix() const { return Field<S_matrix>(0x150); }
};

class I3D_scene {
public:
    // (A frame of type FRAME_SCENE: C_game releases it through slot 0.)
    I3DMETHOD(uint32_t, Release)() = 0;                       // 0x00
    I3D_SLOT(04);                                             // LS3DF: GetVisualType
    I3D_SLOT(08);                                             // LS3DF: SetWorldPos
    I3D_SLOT(0C);                                             // LS3DF: SetWorldDir
    I3D_SLOT(10);                                             // LS3DF: SetWorldPosDir3
    I3D_SLOT(14);                                             // LS3DF: SetWorldPosDir
    I3D_SLOT(18);                                             // LS3DF: SetWorldPosRot
    I3D_SLOT(1C);                                             // LS3DF: SetDir
    I3DMETHOD(void, Tick)(int timeDelta) = 0;                 // 0x20
    I3D_SLOT(24);                                             // LS3DF: Update
    I3D_SLOT(28);                                             // LS3DF: SetCallback
    I3D_SLOT(2C);                                             // LS3DF: SetUserProps
    I3D_SLOT(30);                                             // LS3DF: SetOn
    I3D_SLOT(34);                                             // LS3DF: SetName
    I3D_SLOT(38);                                             // LS3DF: LinkTo
    I3D_SLOT(3C);                                             // LS3DF: NumChildren
    I3D_SLOT(40);                                             // LS3DF: GetChild
    // Calls `callback(frame, context)` for every scene frame matching `typeMask` and `mask`.
    I3DMETHOD(void, EnumFrames)(I3D_ENUMPROC *callback, void *context, uint32_t typeMask,
                                const char *mask) = 0;        // 0x44
    I3D_SLOT(48);                                             // LS3DF: FindChildFrame
    I3D_SLOT(4C);                                             // LS3DF: Duplicate
    I3D_SLOT(50);                                             // LS3DF: SetHierarchyLightLevel
    I3D_SLOT(54);                                             // LS3DF: SetHierarchyAlpha
    I3D_SLOT(58);                                             // LS3DF: UpdateBound
    I3D_SLOT(5C);
    I3D_SLOT(60);                                             // LS3DF: Init
    I3D_SLOT(64);                                             // LS3DF: Uninit
    I3D_SLOT(68);                                             // LS3DF: MatrixChanged
    I3D_SLOT(6C);                                             // LS3DF: Slot6C
    I3D_SLOT(70);                                             // LS3DF: Load
    // A function the scene calls while it renders (C_game: GameRenderCallback, flags 6).
    I3DMETHOD(void, SetRenderCallback)(void *callback, uint32_t flags) = 0;   // 0x74
    I3DMETHOD(void, Render)(uint32_t flags) = 0;              // 0x78
    // Finds a frame by name; typeMask selects frame types (0xCFFFF = all regular types).
    I3DMETHOD(PI3D_frame, FindFrame)(const char *name, uint32_t typeMask) = 0;   // 0x7C
    // Adds a frame (and its children) to the scene; the scene holds a reference.
    I3DMETHOD(void, AddFrame)(PI3D_frame frame) = 0;          // 0x80
    // Removes a frame added with AddFrame(); nonzero if it was in the scene.
    I3DMETHOD(int, RemoveFrame)(PI3D_frame frame) = 0;        // 0x84
    I3D_SLOT(88);                                             // LS3DF: SetLightsColor
    // Brightness of the light falling on `pos` (in `sector`, null: found from `pos`); the
    // flags are 1/0 for decals and 0/0 for blood splats (meanings not identified).
    I3DMETHOD(float, GetLightIntensity)(const S_vector &pos, I3D_sector *sector, int flags1,
                                        int flags2) = 0;      // 0x8C
    I3DMETHOD(void, SetActiveCamera)(I3D_camera *camera) = 0; // 0x90
    // The ray from the active camera through the screen pixel (x, y): its start and unit
    // direction (the command map's picking).
    I3DMETHOD(void, UnmapScreenPoint)(int x, int y, S_vector &origin, S_vector &dir) = 0;   // 0x94
    // World points to the screen (pixels; the interface's outline of the actor picked).
    I3DMETHOD(void, TransformPoints)(const S_vector4 *points, S_vector4 *out, int count) = 0;   // 0x98
    I3D_SLOT(9C);                                             // LS3DF: IsFrameVisible
    I3D_SLOT(A0);                                             // LS3DF: SetAnimation
    I3D_SLOT(A4);                                             // LS3DF: GetAnimation
    I3D_SLOT(A8);                                             // LS3DF: StopAnimation
    I3D_SLOT(AC);                                             // LS3DF: SetAnimWeight
    I3D_SLOT(B0);                                             // LS3DF: GetAnimWeight
    I3D_SLOT(B4);                                             // LS3DF: SetAnimTime
    I3D_SLOT(B8);                                             // LS3DF: GetAnimTime
    I3D_SLOT(BC);                                             // LS3DF: IsAnimActive
    I3D_SLOT(C0);                                             // LS3DF: NumTracks
    I3D_SLOT(C4);                                             // LS3DF: GetTracks
    I3D_SLOT(C8);                                             // LS3DF: AddTickFrame
    I3D_SLOT(CC);                                             // LS3DF: RemoveTickFrame
    I3D_SLOT(D0);                                             // LS3DF: CreateSector
    I3D_SLOT(D4);                                             // LS3DF: LocateSectorIn
    I3D_SLOT(D8);                                             // LS3DF: LocateSectorDepth
    I3D_SLOT(DC);                                             // LS3DF: LocateSector
    // Sector containing `pos`, null if none.
    I3DMETHOD(I3D_sector *, GetSector)(const S_vector &pos) = 0;  // 0xE0
    I3D_SLOT(E4);                                             // LS3DF: SetBackdropRange
    I3D_SLOT(E8);                                             // LS3DF: UpdateFrameSectorIdle
    // Re-evaluates which sector `frame` belongs to after it was moved to `pos`.
    I3DMETHOD(void, UpdateFrameSector)(PI3D_frame frame, const S_vector &pos) = 0;   // 0xEC
    I3D_SLOT(F0);                                             // LS3DF: UpdateSectors
    I3D_SLOT(F4);                                             // LS3DF: Open
    // Frees the scene's contents (C_game::FreeMission).
    I3DMETHOD(void, Close)() = 0;                             // 0xF8
    // The frame a ray from `from` along `dir` hits first, null: none (the interface's visual
    // under its cursor: flags 0x4040000, the rest 0; the arguments' meanings inferred).
    I3DMETHOD(I3D_frame *, PickFrame)(const S_vector4 &from, const S_vector &dir, uint32_t flags,
                                      int unknown1, int unknown2, int unknown3, int unknown4,
                                      int unknown5, int unknown6) = 0;   // 0xFC
    I3D_SLOT(100);                                            // LS3DF: TestRay
    // Ray from `from` along `dir` (its length is the reach) against the geometry of
    // `frame` and its children; returns the frame hit (null: none) and fills `info`.
    // `flags`: I3DCOL_* (meanings inferred from the callers).
    I3DMETHOD(I3D_frame *, IntersectFrame)(const S_vector &from, const S_vector &dir, I3D_frame *frame,
                                           struct I3D_collision_info *info, uint32_t flags,
                                           int unknown1, int unknown2, int unknown3) = 0;   // 0x104
    I3D_SLOT(108);                                            // LS3DF: MoveSphere
    I3D_SLOT(10C);                                            // LS3DF: MoveSphereFrame
    I3D_SLOT(110);                                            // LS3DF: MoveSphereVisual
    I3D_SLOT(114);                                            // LS3DF: SweepSpheres
    I3D_SLOT(118);                                            // LS3DF: SetWorldSpace
    // Weather simulation parameter (E_WeatherParam); floats are passed as their bits.
    I3DMETHOD(void, SetWeather)(int param, uint32_t value) = 0;   // 0x11C

    I3D_SLOTS_4(120, 124, 128, 12C);                          // LS3DF: GetWeather, ResetWeather, AddDrawnDummy, RemoveDrawnDummy
    I3D_SLOT(130);                                            // LS3DF: NumDrawnDummies
    I3D_SLOT(134);                                            // LS3DF: GetDrawnDummies
    // Registers a human model with two of its frames ("q", "z") / forgets it (C_HumanMovement
    // Init / Done; meaning not identified).
    I3DMETHOD(void, AddHumanModel)(I3D_frame *model, I3D_frame *frameQ, I3D_frame *frameZ) = 0;   // 0x138
    I3DMETHOD(void, RemoveHumanModel)(I3D_frame *model) = 0;  // 0x13C
    I3D_SLOT(140);                                            // LS3DF: ClearTrackedModels
    // Called with a frame just put into the world (meaning not identified).
    I3DMETHOD(void, MarkTrackedModel)(I3D_frame *frame) = 0;        // 0x144
    // Screen rectangle the scene renders into (pixels).
    I3DMETHOD(void, SetViewport)(int left, int top, int right, int bottom) = 0;   // 0x148

    // Number of the frame being rendered; visuals drawn in it carry the same stamp.
    int GetRenderStamp() const { return Field<int>(0x11C); }
    // The viewport set with SetViewport (left, top, right, bottom; +0x244).
    const int *GetViewportRect() const { return &Field<int>(0x244); }
    I3D_camera *GetActiveCamera() const { return Field<I3D_camera *>(0x15C); }
    // The outdoor sector everything else is inside of.
    I3D_sector *GetPrimarySector() const { return Field<I3D_sector *>(0x1F8); }
    // Frame flying shots, shells and the hit zone model are linked to (+0x200).
    I3D_frame *GetDynamicRoot() const { return Field<I3D_frame *>(0x200); }
    // Frame of the first-person weapon view (+0x204; the player's own shells go there).
    I3D_frame *GetFirstPersonRoot() const { return Field<I3D_frame *>(0x204); }
    // Listener of the 3D sound (moves with the camera).
    class I3D_sound_listener *GetSoundListener() const { return Field<class I3D_sound_listener *>(0x28C); }

private:
    template <class T> const T &Field(size_t offset) const
    {
        return *reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(this) + offset);
    }
};

// Result of I3D_scene::IntersectFrame() (only the members the game reads are known).
struct alignas(16) I3D_collision_info {                     // 0x30 bytes
    float distance;               // +0x00 from `from` along the normalized `dir`
    uint8_t unknown04[0x0C];
    S_vector normal;              // +0x10 surface normal at the hit
    float normalW;
    uint8_t unknown20[0x10];
};

// I3D_scene::IntersectFrame() flags (meanings inferred from the callers).
enum : uint32_t {
    I3DCOL_VISUAL_GEOMETRY = 0x00040000,   // shots test the rendered geometry
    I3DCOL_HELMET = 0x04000000,            // set when testing a helmet model
    I3DCOL_HIT_ZONE = 0x08000000,          // set when testing the hit-zone boxes
};

class I3D_sound_listener {
public:
    I3D_SLOT(00);
    I3D_SLOT(04);
    I3D_SLOT(08);
    I3D_SLOT(0C);
    // Velocity used for the Doppler effect (m/s).
    I3DMETHOD(void, SetVelocity)(const S_vector &velocity) = 0;   // 0x10
};

// I3D_scene::SetWeather() parameters.
enum E_WeatherParam {
    WEATHER_ON = 0,
    WEATHER_COLOR_IN = 3,
    WEATHER_COLOR_OUT = 4,
    WEATHER_SPEED = 5,           // float m/s
    WEATHER_LENGTH = 6,          // float m
    WEATHER_WIDTH = 7,           // float m
    WEATHER_DISTANCE = 8,        // float m
    WEATHER_HEIGHT = 9,          // float m
    WEATHER_COUNT = 10,
    WEATHER_DIR_X = 11,          // float
    WEATHER_DIR_Y = 12,
    WEATHER_DIR_Z = 13,
    WEATHER_MODE = 14,           // 0 rain, 1 snow
    WEATHER_DEBUG = 15,
};

class I3D_driver {
public:
    I3DMETHOD(HRESULT, Init)(uint32_t flags) = 0;                         // 0x00
    I3DMETHOD(void, Close)() = 0;                                         // 0x04
    I3D_SLOT(08);                                             // LS3DF: FreeUnusedResources
    I3DMETHOD(void, Tick)(int timeDelta) = 0;                             // 0x0C
    I3D_SLOT(10);                                             // LS3DF: GetTime
    I3D_SLOT(14);                                             // LS3DF: GetStats
    I3DMETHOD(void, SetState)(int state, uint32_t value) = 0;             // 0x18
    I3DMETHOD(uint32_t, GetState)(int state) = 0;                         // 0x1C
    I3D_SLOT(20);                                             // LS3DF: Render
    I3D_SLOT(24);                                             // LS3DF: DrawPoint
    I3D_SLOT(28);                                             // LS3DF: DrawPoint2D
    I3D_SLOT(2C);                                             // LS3DF: DrawLine
    I3D_SLOT(30);                                             // LS3DF: DrawLines
    I3D_SLOT(34);                                             // LS3DF: DrawTriangles
    I3D_SLOT(38);                                             // LS3DF: DrawCircle2D
    I3D_SLOT(3C);                                             // LS3DF: DrawText3D
    I3D_SLOT(40);                                             // LS3DF: DrawText2D
    I3D_SLOT(44);                                             // LS3DF: DrawSprite
    I3D_SLOT(48);                                             // LS3DF: DrawBox
    I3D_SLOT(4C);                                             // LS3DF: DrawSphere
    // Creates an empty visual of the given type (I3DVISUAL_*), e.g. to copy another one into.
    I3DMETHOD(PI3D_frame, CreateVisual)(uint32_t visualType) = 0;          // 0x50
    I3DMETHOD(PI3D_frame, CreateFrame)(int type) = 0;                     // 0x54
    // A new material (the vehicle's glass parts copy theirs into it; inferred).
    I3DMETHOD(class I3D_material *, CreateMaterial)() = 0;                // 0x58
    I3D_SLOT(5C);                                             // LS3DF: CreateAnimMaterial
    I3D_SLOT(60);                                             // LS3DF: AllocFaces
    I3D_SLOT(64);                                             // LS3DF: CreateFaceGroup
    I3D_SLOT(68);                                             // LS3DF: CreateMeshLevel
    // A new, empty mesh (the vehicle's damage parts copy their visual's into it; inferred).
    I3DMETHOD(class I3D_mesh *, CreateMesh)() = 0;                        // 0x6C
    I3DMETHOD(I3D_animation_set *, CreateAnimationSet)() = 0;              // 0x70
    // A joint override for the joint named `jointName` / an empty set of them (the meleeTracker's
    // head look; inferred from that use).
    I3DMETHOD(I3D_joint_override *, CreateJointOverride)(const char *jointName) = 0;   // 0x74
    I3DMETHOD(I3D_joint_override_set *, CreateJointOverrideSet)() = 0;     // 0x78
    I3D_SLOT(7C);                                             // LS3DF: GetFrameTypeName
    I3D_SLOT(80);                                             // LS3DF: GetVisualTypeName
    I3D_SLOT(84);                                             // LS3DF: EnumVisualTypes
    I3DMETHOD(PI3D_frame, FindFrame)(PI3D_scene scene, const char *name) = 0;// 0x88
    I3D_SLOT(8C);                                             // LS3DF: RebalanceNameTree
    I3D_SLOTS_4(90, 94, 98, 9C);                              // LS3DF: EnumNamedFrames, FrameReleased, RemoveShadowCaster, RemoveTargetFrame
    I3D_SLOT(A0);                                             // LS3DF: SetHierarchyLightLevel
    I3D_SLOT(A4);                                             // LS3DF: SetHierarchyAlpha
    // The bound of a frame and its hierarchy in the frame's local space (into `out`; the
    // squad AI measures the distance to its box).
    I3DMETHOD(const I3D_bound_volume *, GetHierarchyBound)(I3D_bound_volume &out, I3D_frame *frame) = 0;   // 0xA8
    // Bounding box of a frame and its whole hierarchy in the frame's local space.
    I3DMETHOD(const I3D_bbox *, GetHierarchyBBox)(I3D_bbox &out, I3D_frame *frame) = 0;   // 0xAC
};

extern "C" LS3D_API I3D_driver *I3DGetDriver();
