// Physics objects — source files 0x00405c60..0x00411590.
//
// Simple rigid bodies for scene props: doors, gates and fences that swing open
// when kicked (and rattle when walked into), fences that break into a hole and a
// loose piece, trees that are knocked over, objects that swing on their parent
// when hit, cans, falling debris and small bouncing objects. They move by ray and
// sphere tests against the collision world; there is no real solver.
//
// C_PhysicsModule (g_physicsModule, "Physics") owns them. Game code refers to an
// object by a handle (S_PhysicsHandle, also stored in the frame's
// S_FrameData::physicsObject): its index in the module's active or resting list.
// Moving objects are stepped by the server's fixed tick (doors, debris and
// swinging objects by every client frame instead) and move to the resting list
// when they stop; hitting or pushing them wakes them up.
//
// Scene frames whose user properties contain "phobj" become objects when a mission
// starts: "can" = can, "cross" = swinging object, otherwise a door. For doors,
// "free"/"axis" tip the object over around a horizontal axis instead of turning
// it around its own x axis, "angle<deg>" sets how far it opens, "tree" swaps the
// model for its damaged "d<model>" version when knocked over and "break<model>"
// makes it breakable (replacement model, default "plotdira": "plot" = the fence
// with a hole, "dira" = the piece that falls out). "sightstop" objects block shots.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/collision.h"
#include "game/module.h"
#include "ls3d/i3d.h"

class C_BitStream;

// Handle of a physics object: index into the module's active (`active`) or resting
// list, -1 once the object is gone.
struct S_PhysicsHandle {
    int index = -1;                                         // +0x00
    bool active = false;                                    // +0x04
};
using T_PhysicsObject = S_PhysicsHandle *;

// Handle of "no object" (S_FrameData::physicsObject of frames without one).
extern S_PhysicsHandle g_noPhysicsHandle;                   // 0x00867a98
extern T_PhysicsObject const g_noPhysicsObject;             // 0x00832cdc (= &g_noPhysicsHandle)

// Object types (S_PhysicsObjectDesc::type, C_PhysObject::m_type).
enum E_PhysicsObjectType {
    PHYS_SIMPLE = 0,                // C_PhysSimpleObject
    PHYS_SPHERE = 1,                // C_PhysSphere (not created by the game)
    PHYS_CAN = 2,                   // C_PhysCan
    PHYS_BOX = 3,                   // C_PhysBox (not created by the game)
    PHYS_FALLING = 4,               // C_PhysFallingObject (debris)
    PHYS_DOOR = 5,                  // C_PhysDoor
    PHYS_SWINGING = 6,              // C_PhysSwingingObject
    PHYS_BASE = 7,
};

// C_PhysObject::Hit() kinds.
enum E_PhysicsHitKind {
    PHYS_HIT_PUSH = 0,              // walked into (doors rattle)
    PHYS_HIT_KICK = 1,              // kicked / shot / driven into (doors open)
    PHYS_HIT_BREAK = 3,             // breakable fences break
};

// Called with what happened (E_PhysicsCallbackKind), the object's handle and its context:
// when it is made, when it bounces, when it is pushed awake, when it comes to rest (a
// result of 1 then deletes the object) and when it is deleted. (No game code sets one.)
enum E_PhysicsCallbackKind {
    PHYS_CALLBACK_CREATED = 0,      // 0x0041008c (the handle in EDX)
    PHYS_CALLBACK_BOUNCED = 1,      // 0x0040b5bb
    PHYS_CALLBACK_PUSHED = 2,       // 0x0041051c
    PHYS_CALLBACK_DELETED = 3,      // 0x0040b816
    PHYS_CALLBACK_RESTING = 5,      // 0x00410968
};
struct S_PhysicsHandle;
using T_PhysicsCallback = int(__fastcall *)(int kind, S_PhysicsHandle *handle, void *context);

// Door part of S_PhysicsObjectDesc.
struct S_DoorParams {                                       // 0x0C bytes
    S_DoorParams() = default;                               // 0x004085c0
    bool freeSwing = false;                                 // +0x00 "free" / "axis"
    bool tree = false;                                      // +0x01 "tree"
    bool breakable = false;                                 // +0x02 "break"
    float angle = 1.5707964f;                               // +0x04 opening angle ("angle<deg>": deg * pi / 360)
    C_str breakModel;                                       // +0x08 replacement model when broken
};

// Parameters of C_PhysicsModule::CreateObject().
struct S_PhysicsObjectDesc {                                // 0x2C bytes
    I3D_frame *frame = nullptr;                             // +0x00
    int type = PHYS_BASE;                                   // +0x04 E_PhysicsObjectType
    T_PhysicsCallback callback = nullptr;                   // +0x08
    void *callbackContext = nullptr;                        // +0x0C
    int materialId = 0;                                     // +0x10
    bool unknown14 = false;                                 // +0x14
    bool collideWithBoxes = false;                          // +0x15 box volumes stop the object
    bool startActive = false;                               // +0x16 starts moving
    bool shootThrough = false;                              // +0x17 bullets pass (doors)
    int unknown18 = 0;                                      // +0x18
    S_DoorParams door;                                      // +0x1C
    float radius = 0.0f;                                    // +0x28 cans and balls; < 0 = from the model
};

// Collision box of a fence's broken part ("VOLB0<n>" frames of the break model).
struct alignas(16) S_BreakVolume {                          // 0xE0 bytes
    S_matrix invMatrix;                                     // +0x00
    S_matrix matrix;                                        // +0x40 frame's world matrix without scale
    S_CollisionVolume volume;                               // +0x80
};

// Base rigid body (vtable 0x0080f598), allocated 16-byte aligned.
class alignas(16) C_PhysObject {
public:
    C_PhysObject(I3D_frame *frame, S_PhysicsHandle *handle);   // 0x0040b5e0
    virtual ~C_PhysObject();                                // 0x0040b7e0
    C_PhysObject(const C_PhysObject &) = delete;
    C_PhysObject &operator=(const C_PhysObject &) = delete;

    // Moves the object by `dt` seconds; false once it has come to rest.
    virtual bool Tick(float dt) { return true; }            // 0x0040b890 (DS 0x00427720)
    // Adds a push of `impulse` (direction and strength) at the world position
    // `point`; `factor` scales the effect.
    virtual void ApplyImpulse(const S_vector &impulse, const S_vector &point, float factor);   // 0x0040b030
    virtual void ApplyGravity(float dt);                    // 0x0040ad40
    // Turns the object by its angular velocity (which dies down over a second).
    virtual void Rotate(float dt);                          // 0x0040ade0
    // E_PhysicsHitKind; false if the object ignores it.
    virtual bool Hit(int kind, const S_vector *dir, void *extra) { return false; }   // 0x0040b790 (DS 0x004275c0)

    // Writes the position and rotation into the frame and moves the collision volume.
    void SyncFrame();                                       // 0x0040ab80
    void SetVolumeActive(bool on);                          // 0x0040ad80
    void SetFreezeVolume(bool on) { m_freezeVolume = on; }  // 0x0040ad60
    void SetCallback(T_PhysicsCallback callback) { m_callback = callback; }   // 0x0040b8a0
    void SetMaterialId(int id) { m_materialId = id; }      // 0x0040b8b0
    void SetFlags(bool unknown144, bool collideWithBoxes)   // 0x0040b8c0
    {
        m_unknown144 = unknown144;
        m_collideWithBoxes = collideWithBoxes;
    }
    void SetCallbackContext(void *context) { m_callbackContext = context; }
    int CallCallback(int kind) const { return m_callback ? m_callback(kind, m_handle, m_callbackContext) : 0; }

    I3D_frame *GetFrame() const { return m_frame; }
    S_PhysicsHandle *GetHandle() const { return m_handle; }
    int GetType() const { return m_type; }
    bool IsResting() const { return m_resting; }

protected:
    // Reflects `velocity` off a surface with `normal` (keeping its speed).
    void Bounce(S_vector &velocity, const S_vector &normal);   // 0x0040b360
    // Fills `volume` as a box around `bound` (local) at the frame's current
    // transform (`matrix` / `invMatrix` receive the transform it points to).
    void InitBoxVolume(S_CollisionVolume &volume, S_matrix &matrix, S_matrix &invMatrix,
                       const I3D_bound_volume &bound, uint32_t flags, uint32_t type);
    // InitBoxVolume() and add it to the collision world.
    void AddBoxVolume(S_CollisionVolume &volume, S_matrix &matrix, S_matrix &invMatrix,
                      const I3D_bound_volume &bound, uint32_t flags, uint32_t type);

    S_vector m_velocity = {0.0f, -0.1f, 0.0f};              // +0x010 units per second
    float m_velocityW = 0.0f;
    S_vector m_angularVelocity = {0, 0, 0};                 // +0x020 radians per second around x, y, z
    float m_angularVelocityW = 0.0f;
    S_vector m_pos = {0, 0, 0};                             // +0x030 world
    float m_posW = 0.0f;
    S_quat m_rot = {1.0f, 0.0f, 0.0f, 0.0f};                // +0x040 world (identity)
    S_matrix m_invMatrix;                                   // +0x050 inverse of m_matrix
    S_matrix m_matrix;                                      // +0x090 frame's world matrix (for m_volume)
    S_CollisionVolume m_volume;                             // +0x0D0
    float m_mass = 1.0f;                                    // +0x130
    I3D_frame *m_frame = nullptr;                           // +0x140 holds a reference
    bool m_unknown144 = false;                              // +0x144
    bool m_collideWithBoxes = false;                        // +0x145
    int m_materialId = -1;                                  // +0x148
    T_PhysicsCallback m_callback = nullptr;                 // +0x14C
    S_PhysicsHandle *m_handle = nullptr;                    // +0x150
    void *m_callbackContext = nullptr;                      // +0x154
    bool m_resting = false;                                 // +0x158 stopped (a rest result deletes it)
    int m_type = PHYS_BASE;                                 // +0x15C E_PhysicsObjectType
    bool m_volumeActive = false;                            // +0x160 m_volume is in the collision world
    bool m_freezeVolume = false;                            // +0x161 SyncFrame() leaves m_volume alone
    int m_state = 0;                                        // +0x164 (boxes: E_BoxState)
};

// Type 0: point object that bounces off the collision world (0x170 bytes).
class C_PhysSimpleObject : public C_PhysObject {            // vtable 0x0080f5f0
public:
    C_PhysSimpleObject(I3D_frame *frame, S_PhysicsHandle *handle);   // 0x0040e320
    bool Tick(float dt) override;                           // 0x0040e160
};

// Type 1: ball (0x180 bytes; not created by the game).
class C_PhysSphere : public C_PhysObject {                  // vtable 0x0080f624
public:
    C_PhysSphere(I3D_frame *frame, float radius, S_PhysicsHandle *handle);   // 0x0040f6a0 (DS 0x0042c620)
    bool Tick(float dt) override;                           // 0x0040edd0
    // Seventh virtual slot (only in this class).
    virtual bool Unknown18() { return true; }               // 0x0040fa10 (DS 0x0042ca70)

private:
    float m_radius = 0.0f;                                  // +0x170
};

// Type 2: can (0x180 bytes).
class C_PhysCan : public C_PhysObject {                     // vtable 0x0080f608
public:
    C_PhysCan(I3D_frame *frame, float radius, S_PhysicsHandle *handle);   // 0x0040e810 (DS 0x0042b3a0)
    bool Tick(float dt) override;                           // 0x0040e3b0
    // Kicks the can (fixed strength) and spins it.
    void ApplyImpulse(const S_vector &impulse, const S_vector &point, float factor) override;   // 0x0040eb10
    bool Hit(int kind, const S_vector *dir, void *extra) override { return true; }   // 0x0040eb00 (DS 0x0042b760)

private:
    float m_radius = 0.0f;                                  // +0x170
};

// C_PhysBox::m_state.
enum E_BoxState {
    BOX_FLYING = 0,
    BOX_TIPPING = 1,                // falling over its contact edge
    BOX_ROLLING = 2,
};

// Type 3: tumbling box (0x1D0 bytes; not created by the game).
class C_PhysBox : public C_PhysObject {                     // vtable 0x0080f5d8
public:
    C_PhysBox(I3D_frame *frame, S_PhysicsHandle *handle);   // 0x0040daa0 (DS 0x0042a270)
    bool Tick(float dt) override;                           // 0x0040bee0
    void ApplyGravity(float dt) override;                   // 0x0040bea0 (DS 0x00427f30)
    void Rotate(float dt) override;                         // 0x0040bec0 (DS 0x00427f60)

private:
    bool TickFlying(float dt);                              // 0x0040bf20
    void Tip(float dt);                                     // 0x0040c670
    bool TickTipping(float dt);                             // 0x0040ce00
    void Roll(float dt);                                    // 0x0040d510
    bool TickRolling(float dt);                             // 0x0040d6a0

    // Ends of the box's longest axis and of its second axis (local, through the
    // center).
    S_vector m_axis1Start = {0, 0, 0};                      // +0x170
    float m_axis1StartW = 0.0f;
    S_vector m_axis1End = {0, 0, 0};                        // +0x180
    float m_axis1EndW = 0.0f;
    S_vector m_axis2Start = {0, 0, 0};                      // +0x190
    float m_axis2StartW = 0.0f;
    S_vector m_axis2End = {0, 0, 0};                        // +0x1A0
    float m_axis2EndW = 0.0f;
    S_vector m_contact = {0, 0, 0};                         // +0x1B0 world point it rests on
    float m_contactW = 0.0f;
    S_vector m_rollAxis = {0, 0, 0};                        // +0x1C0
    float m_rollAxisW = 0.0f;
};

// Type 4: falling debris (0x180 bytes).
class C_PhysFallingObject : public C_PhysObject {           // vtable 0x0080f5b0; deleting dtor and dtor: 0x0040be40 (DS 0x00427e90), 0x0040be60 (DS 0x00427ec0)
public:
    C_PhysFallingObject(I3D_frame *frame, S_PhysicsHandle *handle);   // 0x0040bd70
    bool Tick(float dt) override;                           // 0x0040b910

private:
    bool m_sliding = false;                                 // +0x170 small piece: slides on the ground
    bool m_groundChecked = false;                           // +0x171
    float m_groundY = 0.0f;                                 // +0x174
};

// Type 5: door, gate, fence or tree (0x240 bytes).
class C_PhysDoor : public C_PhysObject {                    // vtable 0x0080f578
public:
    C_PhysDoor(const S_PhysicsObjectDesc &desc, S_PhysicsHandle *handle);   // 0x00407390 (DS 0x00421fc0)
    ~C_PhysDoor() override;                                 // 0x004079f0

    bool Tick(float dt) override;                           // 0x004070c0
    void ApplyImpulse(const S_vector &, const S_vector &, float) override {}   // 0x004094d0
    bool Hit(int kind, const S_vector *dir, void *extra) override;   // 0x00408780 (DS 0x00423860)

    // Hides the piece that fell out of a broken fence (wire cutters).
    void HideBrokenPart();                                  // 0x00408750

    // Opening by a kick (not a push rattle); the clients get its direction.
    bool IsOpening() const { return m_moving && !m_rattling; }
    const S_vector &GetHitDir() const { return m_hitDir; }

private:
    // Turns the leaf to the current phase; false (and backs off) when its edges hit
    // something.
    bool MoveLeaf();                                        // 0x00406d10
    void Break(const S_vector *dir);                        // 0x00407bd0 (DS 0x00422a20)
    // Collision boxes of the break model's "VOLB0<n>" frames.
    void AddBreakVolumes(I3D_frame *model);                 // 0x00406740 (DS 0x00420f90)
    enum E_TreeResult {
        TREE_NOT_SWAPPED,           // no damaged model: opens like a door
        TREE_SWAPPED,               // replaced; this object is deleted
        TREE_FAILED,
    };
    // Knocks a tree over: swaps the owner model for its damaged "d<model>"
    // version and kicks that model's copy of this part.
    E_TreeResult KnockOverTree(const S_vector *dir, void *extra);

    S_vector m_center = {0, 0, 0};                          // +0x170 bottom center (world)
    float m_centerW = 0.0f;
    S_vector m_axis = {0, 0, 0};                            // +0x180 rotation axis (world)
    float m_axisW = 0.0f;
    S_vector m_edge1 = {0, 0, 0};                           // +0x190 top corners (local), checked
    float m_edge1W = 0.0f;
    S_vector m_edge2 = {0, 0, 0};                           // +0x1A0   against the world when turning
    float m_edge2W = 0.0f;
    S_vector m_hitDir = {0, 0, 0};                          // +0x1B0
    float m_hitDirW = 0.0f;
    S_quat m_openRot1;                                      // +0x1C0 open one way
    S_quat m_openRot2;                                      // +0x1D0 open the other way
    S_quat m_closedRot;                                     // +0x1E0 rotation when hit
    S_quat m_initialRot;                                    // +0x1F0 restored when deleted
    bool m_moving = false;                                  // +0x200
    bool m_freeSwing = false;                               // +0x201
    bool m_rattling = false;                                // +0x202 pushed: small swing around closed
    bool m_tree = false;                                    // +0x203
    bool m_breakable = false;                               // +0x204
    C_str m_breakModel;                                     // +0x205 (unaligned in the original)
    float m_phase = 0.0f;                                   // +0x20C 0..1 of the current swing
    float m_amplitude = 1.0f;                               // +0x210 shrinks with every swing
    I3D_frame *m_originalFrame = nullptr;                   // +0x214 hidden frame of a broken fence
    I3D_frame *m_brokenPart = nullptr;                      // +0x218
    float m_damping = 0.4f;                                 // +0x21C amplitude kept per swing
    float m_speed = 1.0f;                                   // +0x220 swings per second
    float m_openAngle = 1.0f;                               // +0x224
    float m_angle = 1.5707964f;                             // +0x228 from S_DoorParams
    int m_swingState = 0;                                   // +0x22C rattle: 1..4 = quarter swings
    std::vector<S_BreakVolume *> m_breakVolumes;            // +0x230
};

// Type 6: swings back and forth on its parent when hit ("cross"; 0x2A0 bytes).
class C_PhysSwingingObject : public C_PhysObject {          // vtable 0x0080f55c
public:
    C_PhysSwingingObject(I3D_frame *frame, S_PhysicsHandle *handle, I3D_frame *parent);   // 0x00405ea0 (DS 0x00420490)
    ~C_PhysSwingingObject() override;                       // 0x00406300

    bool Tick(float dt) override;                           // 0x00405ca0
    // Any push makes it swing.
    void ApplyImpulse(const S_vector &impulse, const S_vector &point, float factor) override   // 0x004066a0 (DS 0x00420ea0)
    {
        Hit(PHYS_HIT_PUSH, &impulse, nullptr);
    }
    bool Hit(int kind, const S_vector *dir, void *extra) override;   // 0x00406390

private:
    S_quat m_swingRot1;                                     // +0x170
    S_quat m_swingRot2;                                     // +0x180
    S_quat m_restRot;                                       // +0x190
    S_matrix m_matrix2;                                     // +0x1A0
    S_matrix m_invMatrix2;                                  // +0x1E0
    bool m_swinging = false;                                // +0x220
    float m_phase = 0.0f;                                   // +0x224
    float m_amplitude = 1.0f;                               // +0x228
    S_CollisionVolume m_volume2;                            // +0x230 sphere around it
    I3D_frame *m_parent;                                    // +0x290 linked back to it when deleted
    int m_swingState = 0;                                   // +0x294
};

// Interface of the physics module (vtable 0x0080f690, pure virtual extensions).
class C_PhysicsModuleBase : public C_Module {
public:
    using C_Module::C_Module;
    ~C_PhysicsModuleBase() override = default;              // 0x00410800

    virtual T_PhysicsObject CreateObject(const S_PhysicsObjectDesc &desc) = 0;   // +0x40
    // Deletes the object; `*handle` becomes g_noPhysicsObject.
    virtual bool DestroyObject(T_PhysicsObject *handle) = 0;   // +0x44
    // Wakes the object and pushes it (see C_PhysObject::ApplyImpulse()).
    virtual void ApplyImpulse(T_PhysicsObject object, const S_vector &impulse, const S_vector &point,
                              float factor) = 0;            // +0x48
    // Wakes the object and hits it (E_PhysicsHitKind).
    virtual bool Hit(T_PhysicsObject object, int kind, const S_vector *dir, void *extra) = 0;   // +0x4C
};

class C_PhysicsModule : public C_PhysicsModuleBase {        // "Physics", vtable 0x0080f640, 0x5C bytes
public:
    C_PhysicsModule(int id, const char *name);              // 0x00410640
    ~C_PhysicsModule() override;                            // 0x00410740

    // Server: everything but debris, doors (when also a client) and swinging objects.
    void Tick(int timeDelta) override;                      // 0x00410100
    // Client: doors, debris and swinging objects.
    void Update(int timeDelta) override;                    // 0x004101a0
    void StartMission(bool server) override;                // 0x00410a40
    void StopMission(bool server) override;                 // 0x00410a70 (DS 0x0042df60)
    void RestartMission(bool server) override;              // 0x00410a90
    bool SaveGame(C_chunk &chunk) override { return true; } // 0x00410700 (DS 0x0042dad0)
    bool LoadGame(C_chunk &chunk) override { return true; } // 0x00410710 (DS 0x0042daf0)
    // Opening doors (sent to joining clients as hits).
    void WriteState(C_BitStream *msg) override;             // 0x00410ef0
    void ReadState(C_BitStream *msg) override;              // 0x00410f70

    T_PhysicsObject CreateObject(const S_PhysicsObjectDesc &desc) override;   // 0x0040fb00
    bool DestroyObject(T_PhysicsObject *handle) override;   // 0x00410230
    void ApplyImpulse(T_PhysicsObject object, const S_vector &impulse, const S_vector &point,
                      float factor) override;               // 0x00410420
    bool Hit(T_PhysicsObject object, int kind, const S_vector *dir, void *extra) override;   // 0x00410530

private:
    friend uint32_t __stdcall CreatePhysicsObjectProc(I3D_frame *frame, void *context);

    // Steps the active objects selected by `selected`.
    template <class F> void StepObjects(int timeDelta, F selected);
    // Moves a resting object to the active list; null if it is gone.
    C_PhysObject *Wake(S_PhysicsHandle *handle);
    // Moves active object `index` to the resting list (deleting it if `destroy` or
    // its callback asks for it).
    void Deactivate(int index, bool destroy);               // 0x00410940
    void DeleteAll();                                       // 0x00410830
    // Physics objects of the scene's "phobj" frames.
    void CreateSceneObjects();                              // 0x00410b00

    std::vector<C_PhysObject *> m_active;                   // +0x0C (slots may be null)
    std::vector<C_PhysObject *> m_resting;                  // +0x1C (slots may be null)
    std::vector<C_PhysObject *> m_doors;                    // +0x2C
    std::vector<int> m_unknown3C;                           // +0x3C (not used)
    std::vector<S_PhysicsHandle *> m_handles;               // +0x4C (free ones have index -1)
};

extern C_PhysicsModule g_physicsModule;                     // 0x00867aa0
C_PhysicsModuleBase *GetPhysicsModule();                    // 0x00410630
