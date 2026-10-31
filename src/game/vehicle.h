// C_Vehicle — a car's physics and driving state (C_Car +0x90; vtable 0x008108f8, 0x22D0 bytes;
// source 0x004e2880..0x0051c180). Being ported bottom-up (PROGRESS.md "Vehicles"): so far the
// parameter block (ApplyParams / GetParams, game/vehicle.cpp; the block and its mission-file
// form in game/vehicle_params.h) and the lifetime and setup from the model
// (game/vehicle_setup.cpp; its damage parts and lamps in game/vehicle_parts.cpp), the driving
// controls, Reset and the strut update (game/vehicle_drive.cpp), a run's Start, Stop and Restart
// (game/vehicle_start.cpp), the physics step's drivetrain (game/vehicle_drivetrain.cpp), its
// solver (game/vehicle_solve.cpp), the wheels' contacts (game/vehicle_wheels.cpp), the body
// against the world (game/vehicle_collide.cpp), the lamps (game/vehicle_lamps.cpp), the per-step
// update (game/vehicle_update.cpp), the particle effects (game/vehicle_effects.cpp; the emitter
// pool in game/car_effects.cpp), the skid marks (game/vehicle_skid_marks.cpp), the damage
// (game/vehicle_damage.cpp), the saved state (game/vehicle_state.cpp) and the sounds' loading
// (game/vehicle_sounds.cpp). The members are the ones ported code uses; the `+0xNNN` comments are
// the original offsets (the port's layout is not byte-exact).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/collision.h"                             // S_CollisionVolume
#include "game/vehicle_params.h"
#include "ls3d/math.h"

class C_Actor;
class C_CameraFrameKeeper;
class C_BitStream;
class I3D_frame;
class I3D_sector;
class I3D_sound;

// Flags of a wheel (S_VehicleWheel::flags). The parameters set 0x1, 0x2, 0x4 and 0x800 per axle
// (Setup's default block: 0x1 on the front axle, 0x2 on the second, 0x4 on the last, 0x800 on all).
// Start keeps only the bits in WHEEL_KEPT_BY_START.
enum : uint32_t {
    WHEEL_STEERED = 0x1,                                // (inferred: Start's turning circle takes these at the steering angle, Stop straightens their holders)
    WHEEL_DRIVEN = 0x2,                                 // (inferred: the top speed and the driven count use these)
    WHEEL_HANDBRAKE = 0x4,                              // (inferred: the solver brakes these with the handbrake)
    WHEEL_ON_GROUND = 0x8,                              // the ground under it within reach, a known surface for its tyre and a load on it
    // The solver's: sliding sideways past unknown21C, or locked or spinning, with over 1 m/s at the contact (inferred).
    WHEEL_SLIDING = 0x10,
    WHEEL_LOCKED = 0x20,                                // (inferred: the solver: the brake asks more than the grip along the wheel)
    WHEEL_BUMPED = 0x40,                                // the contact update: bottomed out (or on a steep face) this pass: no bump damping
    // The solver's: the drive asks more than the tyre passes, with the engine's speed 2 m/s off the car's (inferred:
    // wheelspin). Step: a driven wheel with it below the rev limit keeps the gear.
    WHEEL_SPINNING = 0x80,
    WHEEL_TRACKED = 0x100,                              // a track strip follows the wheel (C_Vehicle::m_tracksL / m_tracksR)
    WHEEL_PAST_PEAK = 0x200,                            // the solver: the slip angle past the tyre's peak (its grip falls off)
    WHEEL_DUMMY = 0x400,                                // the wheel's frame is a dummy (no visual): left out of the driven count and the +0x5DC sum
    WHEEL_EFFECTS = 0x800,                              // its axle's parameter: the wheel raises its surface's effects
    WHEEL_ROLLING_FORWARD = 0x10000,                    // the contact moves along the rolling direction
    WHEEL_FLAG_1000000 = 0x1000000,                     // (the wheel effects: with WHEEL_LOCKED sliding over 2 m/s)
    // Hit this step (Deform; once per step): Update bends it by maxBend x the lost share of bendHealth and
    // clears it.
    WHEEL_HIT = 0x10000000,
    WHEEL_ON_REST_PLANE = 0x20000000,                   // the contact update: placed on its plane at rest, no ray this pass
    // The wheel came off (the car's VEHICLE_CALLBACK_WHEEL_OFF sets it and hides the wheel): left out of
    // the contacts and the solver like a dummy.
    WHEEL_DETACHED = 0x40000000,
    // A flat tyre (inferred: the contact update takes the rim's unknown1E8 for the reach, the solver the second set of
    // grip values).
    WHEEL_ON_RIM = 0x80000000,
    WHEEL_KEPT_BY_START = 0x2EFFFF07,
};

// A wheel's skid mark segment (S_VehicleWheel::skidMarks, a ring; made by Start, laid by
// LaySkidMark): a strip piece 0.3 m long at most and a corner piece joining it to the next.
struct S_VehicleSkidMark {                              // 0x0C bytes
    I3D_frame *frame0;                                  // +0x00 the strip ("shlaif01.i3d"; Reset switches it off, unlinks both)
    I3D_frame *frame1;                                  // +0x04 the corner ("shlaif02.i3d") to the next segment
    bool active;                                        // +0x08 being laid
    bool fresh;                                         // +0x09 a mark's first segment: no corner before it (left unset when made)
    bool cornerShown;                                   // +0x0A frame1 switched on
};

// A wheel (0x280 bytes, made per "WH*" frame of the model by AddVehicleWheel 0x004f9be0; C_Vehicle
// keeps them in a list and an array). The model names it "WH<side><axle>" ("WHL0", "WHR0", ..)
// below a holder "DWHL.." / "DWHR..".
struct S_VehicleWheel {
    // Start: the turn about x by unknown154, then about z by unknown158 (negated on the right).
    S_quat unknown000;                                  // +0x000 (the original zeroes its first word when made)
    S_quat unknown010;                                  // +0x010 Start: about z by unknown150 + unknown158 (negated on the left)
    S_vector4 holderPos;                                // +0x020 the holder's local position
    S_vector4 modelPos;                                 // +0x030 the holder in the model's space
    S_vector4 worldPos;                                 // +0x040 the wheel's centre (Start: the frame's world position)
    // The contact point on the ground (off the ground: where the hanging wheel would touch; Start:
    // worldPos lowered along the model's y axis by unknown1E4).
    S_vector4 unknown050;                               // +0x050
    S_vector4 unknown060;                               // +0x060 unknown050 of the pass before (Start: 1 below unknown050)
    // The solver: the load along unknown080, scaled by its lean against the model's y axis (inferred: the
    // suspension's force; zero off the ground).
    S_vector unknown070;                                // +0x070
    S_vector unknown080;                                // +0x080 the ground's normal at the contact
    S_vector unknown090;                                // +0x090 unknown080 of the pass before (a fresh contact: the same)
    S_vector unknown0A0;                                // +0x0A0 the contact's velocity: on the ground the contact point's, else the wheel centre's (0 at setup)
    S_vector unknown0B0;                                // +0x0B0 the unit rolling direction (along the ground; off it the holder's z) (0 at setup)
    S_vector unknown0C0;                                // +0x0C0 the wheel centre's movement, summed (Start: 0)
    S_vector unknown0D0;                                // +0x0D0 the same since the last ray (0 after one) (Start: 0)
    S_vector unknown0E0;                                // +0x0E0 the solver: the tyre's force on the car (inferred)
    // The skid mark being laid (LaySkidMark): its finished segment's end corners (the left one
    // along the mark's direction x the ground's normal), the mark's direction (the contact's
    // velocity when it started), its current segment's start.
    S_vector skidLeft;                                  // +0x0F0
    S_vector skidRight;                                 // +0x100
    S_vector skidDir;                                   // +0x110
    S_vector skidStart;                                 // +0x120
    S_plane groundPlane;                                // +0x130 the ground's plane last found under the wheel (Start: up through 0)
    S_CollisionVolume *groundPlaneVolume;               // +0x140 the volume of that plane (null: none kept)
    std::string name;                                   // +0x144 a copy of the frame's name
    I3D_frame *frame;                                   // +0x148
    I3D_frame *holder;                                  // +0x14C the frame's parent ("DWHL*" / "DWHR*")
    float unknown150;                                   // +0x150 radians
    float unknown154;                                   // +0x154 radians
    float unknown158;                                   // +0x158 radians
    float unknown15C;                                   // +0x15C the frame's local x (in the holder)
    // The suspension: the load (unknown164 - extension) x unknown160 (N), damped by unknown170
    // compressing and unknown174 extending (per m/s).
    float unknown160;                                   // +0x160
    float unknown164;                                   // +0x164 the travel (m)
    float unknown168;                                   // +0x168 the load on the wheel (N; Start: 0)
    float extension;                                    // +0x16C how far the wheel hangs below its holder (0..unknown164; 0 at setup)
    float unknown170;                                   // +0x170
    float unknown174;                                   // +0x174
    float unknown178;                                   // +0x178 radians
    int tyreType;                                       // +0x17C tables\tyres.bin id (-1: none there)
    int tyreIndex;                                      // +0x180 its index in g_tyreTypes (-1: none)
    int surfaceEntry;                                   // +0x184 the tyre type's entry for `surface` (-1: none; Start)
    int surface;                                        // +0x188 (inferred: the ground's surface kind; 0 at Start)
    float unknown18C;                                   // +0x18C
    float unknown190;                                   // +0x190 0..1
    float unknown194;                                   // +0x194 radians
    float unknown198;                                   // +0x198 radians
    float unknown19C;                                   // +0x19C 0..1
    float unknown1A8;                                   // +0x1A8 the solver: 1 upright on the ground .. 0 tilted 10° to its normal (Start: 0)
    float unknown1AC;                                   // +0x1AC the solver: the grip's share left while past the peak slip
    float unknown1B0;                                   // +0x1B0 the solver: the grip along the wheel (N; inferred)
    float unknown1B4;                                   // +0x1B4 the solver: the grip across it (N; inferred)
    // The tyre values (a tyre type's surface entries mirror them from +0x04, S_TyreSurface): the force caps 18C /
    // 1C4, the peak slip 194 and 198 where the grip has fallen to the share 19C, the tilt share 190, the grip
    // factors along 1B8 / 1C8 and across 1BC / 1CC, the rolling resistance 1C0 / 1D0 (inferred; the second of
    // each pair with WHEEL_ON_RIM).
    float unknown1B8;                                   // +0x1B8
    float unknown1BC;                                   // +0x1BC
    float unknown1C0;                                   // +0x1C0
    float unknown1C4;                                   // +0x1C4
    float unknown1C8;                                   // +0x1C8
    float unknown1CC;                                   // +0x1CC
    float unknown1D0;                                   // +0x1D0
    float unknown1D4;                                   // +0x1D4 the solver: the sine of the slip angle (unknown0A0 against unknown0B0)
    float radius;                                       // +0x1D8 half the height of the frame's box
    float innerRadius;                                  // +0x1DC 0.8 x the radius (inferred: the rim)
    float unknown1E0;                                   // +0x1E0 0, + 0.1 per track strip that follows the wheel
    float unknown1E4;                                   // +0x1E4 sqrt(radius² + width²)
    float unknown1E8;                                   // +0x1E8 sqrt(innerRadius² + width²)
    float unknown1EC;                                   // +0x1EC acos(radius / unknown1E4) (radians)
    float unknown1F0;                                   // +0x1F0 (0 at setup)
    float unknown1F4;                                   // +0x1F4 the axle's share of C_Vehicle +0x71C
    float unknown1F8;                                   // +0x1F8 0..1
    uint32_t flags;                                     // +0x1FC WHEEL_*
    S_CollisionVolume *ground;                          // +0x200 the volume under the wheel this pass (Start: null)
    std::vector<S_VehicleSkidMark> skidMarks;           // +0x204 a ring (Start makes them, Stop frees them)
    float width;                                        // +0x214 the box's x extent, or the holder's "[<cm>]" property
    uint32_t skidMark;                                  // +0x218 the ring's current one
    float unknown21C;                                   // +0x21C radians
    float unknown220;                                   // +0x220 radians
    float unknown224;                                   // +0x224 radians
    float maxBend;                                      // +0x228 radians: the bend at no health
    // A damaged wheel's bend (radians): the rolling direction turned about the car's y by it x
    // cos(unknown1F0), the frame tilted by it (ResetCondition: 0; Update: (1 - health /
    // bendHealth) x maxBend once hit below bendHealth).
    float bend;                                         // +0x22C
    // Deform: an impact past minImpulse within 1 m of the wheel, less its absorbed percentage,
    // over fullImpulse x fullHealth is lost (once per step: WHEEL_HIT); at 0 the wheel comes off.
    float health;                                       // +0x230 (ResetCondition: fullHealth)
    float fullHealth;                                   // +0x234
    float absorbed;                                     // +0x238 percent
    float fullImpulse;                                  // +0x23C (m/s x mass)
    float minImpulse;                                   // +0x240 (m/s x mass)
    float bendHealth;                                   // +0x244
    // The surface's bumps (Update; the ranges with C_Vehicle +0x228D, else the surface's
    // S_CarSurface +0x04..+0x14): a smooth time, then a bump of a random height for a time.
    float unknown248;                                   // +0x248 the smooth time's range
    float unknown24C;                                   // +0x24C
    float unknown250;                                   // +0x250 the bump time's range
    float unknown254;                                   // +0x254
    float unknown258;                                   // +0x258 the bump height's range (+-, at least half)
    float unknown25C[3];                                // +0x25C (Start: 0) [0] the smooth time, [1] the bump's, [2] the bump now: more reach (the contact update)
    float unknown268;                                   // +0x268 the next bump's height
    float unknown26C;                                   // +0x26C the time in the cycle (Start: 1000; a new surface: unknown25C[1] + [0] + 1)
    int effect = -1;                                    // +0x270 a car effect slot (-1: none; the original leaves it unset until Start)
    float unknown274;                                   // +0x274 the solver: the tyre's force over what its grip allows (at most 1)
    S_VehicleWheel *next;                               // +0x278 the vehicle's list
};

// The vertices of a track strip ("STRIPL*" / "STRIPR*" visuals of tanks and half-tracks) that
// follow one wheel (C_Vehicle +0x56C left, +0x57C right): the vertices below the wheel's centre
// within 0.05 of its z, with their heights relative to the centre, all shifted by the gap between
// the wheel's bottom and the highest of them.
struct S_VehicleTrack {                                 // 0x14 bytes
    I3D_frame *strip;                                   // +0x00
    std::vector<uint16_t> vertices;                     // +0x04 (count +0x0C) vertex indices
    std::vector<float> offsets;                         // +0x08
    uint16_t wheel;                                     // +0x10 an index into C_Vehicle::m_wheels
};

// The damage part types: the visual's "[DEFORM <type>]" property (S_VehiclePart::type; also the
// ApplyParams parameter group).
enum : uint8_t {
    VEHICLE_PART_BODY = 1,
    VEHICLE_PART_BUMPER = 2,
    VEHICLE_PART_GLASS = 3,
    VEHICLE_PART_MIRROR = 4,
    VEHICLE_PART_LIGHT = 5,
    VEHICLE_PART_WING = 6,
    VEHICLE_PART_PLATE = 7,                             // "SPZ" (Czech: the licence plate)
    VEHICLE_PART_DOOR = 8,
    VEHICLE_PART_ROOF = 9,
    VEHICLE_PART_SPARE_WHEEL = 10,                      // "STEPNEY"
};

// S_VehiclePart::state bits.
enum : uint8_t {
    VEHICLE_PART_STATE_1 = 0x1,                         // gone: shattered (CrackGlass) or come off (the car's CarOnPartOff); RepairParts clears it; a window whose part has it stops moving
    VEHICLE_PART_STATE_2 = 0x2,                         // (Update clears it with C_Vehicle +0x58C bit 2: inferred hit this step)
    VEHICLE_PART_DEFORMED = 0x4,                        // the mesh's vertices differ from the original's (C_Vehicle::m_meshOriginals)
    VEHICLE_PART_GLASS_BROKEN = 0x8,                    // the material shows the cracked texture
};

// A damage part (C_Vehicle +0x664, one per visual with a "[DEFORM <type>]" property; made by
// AddVehiclePart 0x004fa520). "[+DEFORM <type> <group><slot>]" visuals join the part of that type and
// group instead: slot 0 becomes its main frame, slot n its frames[n - 1] (hidden at setup).
struct S_VehiclePart {                                  // 0x3C bytes
    I3D_frame *frame;                                   // +0x00 the main visual
    std::vector<I3D_frame *> *frames;                   // +0x04 owned: a group's other visuals (null slots possible)
    class I3D_mesh *mesh;                               // +0x08 the part's own copy of its mesh (a group's: the visual's)
    // A glass part's other texture (one reference): the cracked one (LoadGlassTextures), or the
    // intact one while the material shows the cracked one (VEHICLE_PART_GLASS_BROKEN).
    class I3D_texture *glassTexture;                    // +0x0C
    char *name;                                         // +0x10 owned (new[]): the short name
    // Deform: an impact's strength (m/s x mass) past minImpulse, less its absorbed percentage,
    // over fullImpulse is the share of a full dent (at most 1) and of fullHealth lost.
    float fullImpulse;                                  // +0x14 (m/s x mass)
    float minImpulse;                                   // +0x18 (m/s x mass)
    float fullHealth;                                   // +0x1C (the body: 100)
    float crackHealth;                                  // +0x20 glass and lights: the glass cracks below it
    float health;                                       // +0x24 (RepairParts: fullHealth; at 0 the part is gone)
    float absorbed;                                     // +0x28 the percentage of an impact it takes no damage from
    S_vector maxDent;                                   // +0x2C a vertex's furthest move from its original along the part's axes (m)
    uint8_t state;                                      // +0x38 VEHICLE_PART_STATE_*
    uint8_t unknown39;
    uint8_t type;                                       // +0x3A VEHICLE_PART_*
    uint8_t group;                                      // +0x3B a "+DEFORM" group's number
};

// The lamp kinds (S_VehicleLamp::kind): the "LIGHT<xx>*" visual's name, left / right by its side of
// the model.
enum : uint16_t {
    VEHICLE_LAMP_IN_LEFT = 1,                           // "LIGHTIN*"
    VEHICLE_LAMP_IN_RIGHT = 2,
    VEHICLE_LAMP_BI_LEFT = 3,                           // "LIGHTBI*" (Czech "blinkr": indicators, inferred)
    VEHICLE_LAMP_BI_RIGHT = 4,
    VEHICLE_LAMP_LI_LEFT = 5,                           // "LIGHTLI*"
    VEHICLE_LAMP_LI_RIGHT = 6,
    VEHICLE_LAMP_RE = 7,                                // "LIGHTRE*" (Czech "reflektor": headlights, inferred)
    VEHICLE_LAMP_BR = 8,                                // "LIGHTBR*" (Czech "brzda": brake lights, inferred)
    VEHICLE_LAMP_BA = 9,                                // "LIGHTBA*"
};
// S_VehicleLamp::flags.
enum : uint32_t {
    VEHICLE_LAMP_BROKEN = 0x1,                          // stays dark (inferred; ResetCondition clears it)
    VEHICLE_LAMP_SHARED = 0x80000000,                   // the projector and light belong to a PROJECTOR (C_Vehicle +0x5A0)
};
// S_VehicleProjector::state: the sides whose lamps work (ResetCondition: both; BreakProjectorSide
// keeps the other side's when a lamp breaks, showing that side's texture); a lamp lit it this
// update (UpdateLamps; the others sharing it leave it).
enum : uint16_t {
    VEHICLE_PROJECTOR_RIGHT = 0x1,                      // textures[2]
    VEHICLE_PROJECTOR_LEFT = 0x2,                       // textures[1]
    VEHICLE_PROJECTOR_LIT = 0x80,
};

// C_Vehicle::Deform's flags.
enum : uint8_t {
    DEFORM_ANY_STRENGTH = 0x1,                          // the lamps' and parts' least impulses ignored
    DEFORM_NO_WEAR = 0x2,                               // the dents only: no health lost (lamps, parts, wheels)
};

// C_Vehicle::m_unknown5B0 (+0x5B0): the light switches (the car sets them; names inferred from
// UpdateLamps and the lamp kinds).
enum : uint32_t {
    VEHICLE_LIGHT_INDICATOR_LEFT = 0x1,                 // the left indicators blink (IN / BI lamps)
    VEHICLE_LIGHT_INDICATOR_RIGHT = 0x2,
    VEHICLE_LIGHT_AUTO_INDICATORS = 0x8,                // the indicators follow the steering past +0x5C4
    VEHICLE_LIGHT_FLASH_LEFT = 0x10,                    // the LI lamps: the left / right ones
    VEHICLE_LIGHT_FLASH_RIGHT = 0x20,
    VEHICLE_LIGHT_FLASHING = 0x80,                      // the LI lamps alternate every +0x5C8 s
    VEHICLE_LIGHT_BRAKE = 0x100,                        // the brake lights (BR, and BI without an indicator)
    VEHICLE_LIGHT_AUTO_BRAKE = 0x800,                   // the brake lights follow the brake
    VEHICLE_LIGHT_REVERSE = 0x1000,                     // the reversing lights (BA)
    VEHICLE_LIGHT_AUTO_REVERSE = 0x8000,                // the reversing lights follow a reverse gear
    VEHICLE_LIGHT_HEADLIGHTS = 0x10000,                 // the headlights (RE)
    // A kind's projected and spot light (without, they keep their state).
    VEHICLE_LIGHT_INDICATOR_BEAMS = 0x100000,           // IN
    VEHICLE_LIGHT_FLASH_BEAMS = 0x200000,               // LI
    VEHICLE_LIGHT_REAR_BEAMS = 0x400000,                // BI
    VEHICLE_LIGHT_HEADLIGHT_BEAMS = 0x800000,           // RE
    VEHICLE_LIGHT_BRAKE_BEAMS = 0x1000000,              // BR
    VEHICLE_LIGHT_REVERSE_BEAMS = 0x2000000,            // BA
    VEHICLE_LIGHT_BLINK = 0x10000000,                   // the indicators' phase: on for +0x5B8 s, off for +0x5BC s
    VEHICLE_LIGHT_FLASH_PHASE = 0x20000000,             // set: the left LI lamps
};

// A lamp (C_Vehicle +0x590; "LIGHT<xx>*" visuals): its own projected light, or with a partner
// below a "PROJECTOR*" dummy the projector's.
struct S_VehicleLamp {                                  // 0x20 bytes
    I3D_frame *frame;                                   // +0x00
    I3D_frame *billboard;                               // +0x04 its "BILLBOARD*" (switched off)
    class I3D_projector_visual *projector;              // +0x08 (null without a "[<texture>]" property)
    class I3D_light *light;                             // +0x0C a spot light
    uint32_t flags;                                     // +0x10 VEHICLE_LAMP_*
    float health;                                       // +0x14 (ResetCondition: C_Vehicle m_lampFullHealth; Deform)
    uint16_t kind;                                      // +0x18 VEHICLE_LAMP_*
    uint16_t part;                                      // +0x1A its damage part (+0x664 index; 0xFFFF none)
    uint16_t projectorIndex;                            // +0x1C its projector (+0x5A0 index) when shared
    uint16_t unknown1E;
};

// A projector ("PROJECTOR*" dummy above two lamps; C_Vehicle +0x5A0): one projected light and spot
// for both, with the three textures of its "[<both>,<left>,<right>]" property (e.g. "[obe.bmp,
// leva.bmp,prava.bmp]": Czech both / left / right).
struct S_VehicleProjector {                             // 0x1C bytes
    I3D_frame *frame;                                   // +0x00
    class I3D_projector_visual *projector;              // +0x04
    class I3D_light *light;                             // +0x08 (may be null)
    class IImage *textures[3];                          // +0x0C
    uint16_t state;                                     // +0x18 (ResetCondition: 3, with textures[0] shown); VEHICLE_PROJECTOR_LIT
    uint16_t unknown1A;
};

// An exhaust ("EXHAUST*" dummies below the body, read in reverse order; +0x744).
struct S_VehicleExhaust {                               // 0x30 bytes
    S_vector4 pos;                                      // +0x00 through the body's local matrix
    S_vector4 dir;                                      // +0x10 the dummy's z axis, rotated the same way
    int effect = -1;                                    // +0x20 a car effect slot (-1: none)
    uint8_t unknown24[12] = {};                         // +0x24 (the original leaves them uninitialised)
};

// What a collision volume is registered with (the special volumes, the CENTRE box +0x2278).
struct S_VehicleCollisionDesc {                         // 8 bytes
    uint32_t unknown00 = 0x40;
    bool unknown04 = true;
    bool hasParent = false;                             // the volume moves with a sub-frame
};

// A special collision volume ("SPECCOLL*" dummies, e.g. on a turret or barrel; +0x2238, the box
// corners in +0x2228, the descriptors in +0x2248).
struct S_VehicleSpecialCollision {                      // 0x60 bytes
    S_matrix invParentMatrix;                           // +0x00 the inverse of the parent's world matrix (identity below the model)
    S_vector4 localPos;                                 // +0x40 the dummy's local position (zero below the model)
    I3D_frame *parent;                                  // +0x50 null below the model
    uint8_t unknown54[12] = {};                         // (the original leaves them uninitialised)
};

// Flags of a door (S_VehicleDoor::flags).
enum : uint8_t {
    DOOR_MOVING = 0x2,                                  // swinging towards targetAngle (UpdateDoors)
    DOOR_FLAG_4 = 0x4,                                  // the frame's properties list an "I" item ("[..,I,..]"), or DOOR_BACKWARD
    DOOR_BACKWARD = 0x8,                                // the frame's z axis points backwards (with DOOR_FLAG_4)
};

// A door ("DOOR<n>" visuals, sorted by n; +0xFB8) and its window (a "WINDOW*" visual below it).
struct S_VehicleDoor {                                  // 0x80 bytes
    S_quat closedRot;                                   // +0x00 the frame's rotation at setup
    S_vector4 pos;                                      // +0x10 through the parent's local matrix
    I3D_frame *frame;                                   // +0x20
    float angle;                                        // +0x24 radians open
    float targetAngle;                                  // +0x28
    float unknown2C;                                    // +0x2C 1.25 (inferred: the open angle)
    float speed;                                        // +0x30 1.45 radians per second
    S_vector4 windowPos;                                // +0x40 the window's local position
    I3D_frame *window;                                  // +0x50
    float windowX;                                      // +0x54 its x
    float windowY;                                      // +0x58 its y
    float windowLowY;                                   // +0x5C its y - 0.5 (inferred: wound down)
    float unknown60;                                    // +0x60 0.3 with a window: its speed per second
    uint16_t windowPart;                                // +0x64 the window's part in +0x664 (0xFFFF none; 0 without a window)
    bool windowMoving;                                  // +0x66 (UpdateWindows)
    bool windowDown;                                    // +0x67 moving down
    uint8_t flags;                                      // +0x70 DOOR_*
    uint16_t doorPart;                                  // +0x72 the door's part in +0x664 (0xFFFF none)
};

// A seat ("SEAT<n>" dummies, sorted by n; +0xFA8).
struct S_VehicleSeat {                                  // 0x40 bytes
    S_vector4 pos;                                      // +0x00 through the parent's local matrix
    S_vector4 dir;                                      // +0x10 the dummy's z axis, rotated the same way
    float load;                                         // +0x20 kg on the seat (SetSeatLoad; 0 at Start)
    // SEAT_FLAG_* (game/actor.h): the car's StartMission sets bit n for each digit n of the dummy's
    // "SEAT<digits>" property; 0x400 while the seat is taken (C_Actor::SetSeatBlocked).
    uint32_t flags;                                     // +0x24
    S_VehicleDoor *door;                                // +0x28 the door of the same number (null past the doors)
    I3D_frame *frame;                                   // +0x2C
    C_Actor *occupant;                                  // +0x30 (a human; C_Car::SetSeatOccupant)
    // The car's StartMission: the keeper of the dummy's "camera3RD*" child (null without one and
    // for SEAT_FLAG_ARMED / SEAT_FLAG_WEAPON seats), and the text of the "name<n>" property (-1
    // without one).
    C_CameraFrameKeeper *camera;                        // +0x34
    int text;                                           // +0x38
};

// A ballast weight ("BALLAST<n>" dummies; +0xFC8; no shipped model has one).
struct S_VehicleBallast {                               // 0x30 bytes
    S_vector4 pos;                                      // +0x00 through the parent's local matrix
    S_vector4 dir;                                      // +0x10 the dummy's z axis, rotated the same way
    float mass;                                         // +0x20 kg (SetSeatLoad adds it in; Setup leaves it unset)
};

// A suspension strut ("AXIS*" visuals; +0x6C4 whole axles "AXIS0<axle>", +0x6D4 one side
// "AXISL<axle>" / "AXISR<axle>"), moved with the wheels by 0x00500fd0.
struct S_VehicleStrut {                                 // 0x20 bytes
    S_vector offset;                                    // +0x00 the frame's "[x y z]" property in cm, / 100 (x negated on the right)
    float unknown0C;                                    // +0x0C
    I3D_frame *frame;                                   // +0x10
    int axle;                                           // +0x14
    bool left;                                          // +0x18
    bool single;                                        // +0x19 a whole axle's strut, or the only one on its side
};

// The body collision boxes ("BODYCOL*" dummies) in a tree that follows the frames: a box's
// children are the boxes below its frame, its points the other dummies right below it. Setup
// moves them into the model's space (or the space of the nearest moving parent) and removes the
// frames (+0x794).
// CollideBody sweeps a sphere of `radius` per point from `start` by `move`.
struct S_BodyCollisionPoint {                           // 0x50 bytes
    S_vector pos;                                       // +0x00
    float unknown0C;
    S_vector4 move;                                     // +0x10 CollideBody: its move in the world over the step
    S_vector4 start;                                    // +0x30 CollideBody: where it was in the world before the step
    I3D_frame *frame;                                   // +0x40 (null after setup)
    float radius;                                       // +0x44 half the dummy's box height x its world y scale
    S_BodyCollisionPoint *next;                         // +0x48
};
// CollideBody sweeps the box from the pose before the step (a 'B' box; the others a sphere of
// `halfHeight` round `pos` from `start` by `move`); its points are tested when it touches.
struct S_BodyCollisionBox {                             // 0x80 bytes
    S_vector min;                                       // +0x00
    float unknown0C;
    S_vector max;                                       // +0x10
    float unknown1C;
    S_vector pos;                                       // +0x20
    float unknown2C;
    S_vector4 move;                                     // +0x30 CollideBody: its move in the world over the step
    S_vector4 start;                                    // +0x50 CollideBody: where it was in the world before the step
    S_BodyCollisionPoint *points;                       // +0x60
    S_BodyCollisionBox *children;                       // +0x64
    I3D_frame *frame;                                   // +0x68 (null after setup)
    I3D_frame *parent;                                  // +0x6C the frame the box moves with (null: the model)
    float halfHeight;                                   // +0x70 half the box's height x its world y scale
    S_BodyCollisionBox *next;                           // +0x74
    bool flagB;                                         // +0x78 the name continues with 'B' ("BODYCOLB..")
};

// A hard contact of the body this tick (C_Vehicle +0x798, 16-byte aligned; CollideBody adds one
// for an impulse over 5 m/s per kg or at over 3 m/s; the first three have effect slots at +0x7A8).
struct S_VehicleBodyContact {                           // 0x20 bytes
    S_vector4 point;                                    // +0x00 the point's sphere's side towards it before the step (w not set)
    int material;                                       // +0x10 the surface's (g_carSurfaceOfMaterial)
    uint8_t unknown14[12];
};

// An original mesh of a deformed damage part (C_Vehicle +0x654, per model file and part name; made
// when a part first deforms, part 1c-iii): RepairParts copies its vertex positions back.
struct S_VehicleMeshOriginal {                          // 0x10 bytes
    class I3D_mesh *mesh;                               // +0x00 one reference
    int refs;                                           // +0x04 the parts deformed from it
    std::string partName;                               // +0x08 (owned char*)
    std::string modelFile;                              // +0x0C (owned char*) C_Vehicle::m_modelFileName
};

// C_Vehicle::m_unknown600 (+0x600): the physics' state bits (names inferred from Step).
enum : uint32_t {
    VEHICLE_PHYSICS_AWAKE = 0x1,                        // set by input, impulses and steering changes; cleared after resting
    VEHICLE_PHYSICS_RESTING = 0x2,                      // idle in gear for over 1 s: Step ends before the solver
    VEHICLE_PHYSICS_4 = 0x4,                            // DriveTorque: in gear while (almost) not moving
    VEHICLE_PHYSICS_AIRBORNE = 0x8,                     // no wheel with WHEEL_ON_GROUND (inferred)
    VEHICLE_PHYSICS_BRAKE_HELD = 0x01000000,            // the brake forced on (the brake controls clear it)
    // A wheel stands on water (Update sets it from +0x2286): every wheel casts its own ray, none
    // keeps its rest plane.
    VEHICLE_PHYSICS_IN_WATER = 0x02000000,
    VEHICLE_PHYSICS_FROZEN = 0x10000000,                // +0x554 above 0: no solver
    VEHICLE_PHYSICS_KEEP_AWAKE = 0x20000000,
};

// C_Vehicle::m_unknownF6C (+0xF6C): the simulation's options (names inferred from their use).
enum : uint32_t {
    VEHICLE_SIM_DOWNFORCE = 0x8,                        // the solver: Integrate's second and third drag terms at the box's ends
    VEHICLE_SIM_BODY_COLLISION = 0x10,                  // the solver: the body against the world (CollideBody)
    // The detailed sounds: a bottomed-out wheel's (the contact update), the handbrake's (Update), the
    // gear changes' and the layered engine (else group 2 plays one engine loop).
    VEHICLE_SIM_DETAIL_SOUNDS = 0x20,
    VEHICLE_SIM_SKID_MARKS = 0x40,                      // Update: sliding wheels lay skid marks
    VEHICLE_SIM_LAMPS = 0x80,                           // the lamps light (UpdateLamps; with +0xF70's, the lamps are updated)
    VEHICLE_SIM_LAMP_PROJECTORS = 0x100,                // a lit lamp's projected light (else switched off)
    VEHICLE_SIM_EFFECTS = 0x200,                        // the particle effects (engine smoke, wheels, exhausts, body contacts)
    VEHICLE_SIM_COLLIDE_EACH_SUBSTEP = 0x400,           // the body collision per substep (else once per step)
    // Every wheel casts its own ray every pass (no axle planes, no kept planes).
    VEHICLE_SIM_RAY_EVERY_WHEEL = 0x800,
    // A lit lamp's spot light (else switched off; Quirk kept: all but the headlights switch their
    // projected light off instead).
    VEHICLE_SIM_LAMP_SPOT_LIGHTS = 0x1000,
    VEHICLE_SIM_IMPACT_DAMAGE = 0x4000,                 // impacts wear the engine and gearbox (ImpactDamage); the engine needs +0x610
    VEHICLE_SIM_DEFORM = 0x8000,                        // impacts deform the body (Deform)
    VEHICLE_SIM_GROUND_FIRST_SUBSTEP = 0x10000,         // the ground is looked for on the first substep only
    // Each wheel keeps its ground plane and casts its ray again in turns (every +0xF50 s half of
    // them; else the two axle rays' planes).
    VEHICLE_SIM_WHEEL_PLANES = 0x20000,
};

// C_Vehicle::m_unknown910 (+0x910): the clutch and gear change bits (names inferred).
enum : uint32_t {
    VEHICLE_CLUTCH_ENGAGING = 0x1,                      // +0x904 rises (+0x8FC per second) to +0x90C
    VEHICLE_CLUTCH_RELEASING = 0x2,                     // +0x904 falls (+0x900 per second); at 0 the requested gear goes in
    VEHICLE_CLUTCH_4 = 0x4,
    VEHICLE_CLUTCH_8 = 0x8,
    VEHICLE_CLUTCH_10 = 0x10,
    VEHICLE_CLUTCH_AUTOMATIC = 0x20,                    // Step works the clutch (Start sets it)
};

// C_Vehicle::Update's result.
enum {
    VEHICLE_UPDATE_LOST = 0,                            // falling away (below -85 m/s): nothing done
    VEHICLE_UPDATE_DONE = 1,
    VEHICLE_UPDATE_IDLE = 2,                            // asleep, quiet, engine off: the step's state left
};

// Reset flags.
enum : uint8_t {
    VEHICLE_RESET_UNLINK_SKID_MARKS = 0x1,              // close the current skid marks and unlink all from the scene
    VEHICLE_RESET_HIDE_SKID_MARKS = 0x2,                // stop and switch off the skid marks (done first)
};

// ApplyParams flags.
enum : uint8_t {
    VEHICLE_PARAMS_FIX = 0x1,                           // replace invalid values (else stop at the first)
    VEHICLE_PARAMS_LOAD_SOUNDS = 0x2,                   // load the block's sounds afterwards
};

// LoadSound flags (names inferred from their use).
enum : uint32_t {
    // Either bit: heard everywhere alike (I3DSOUND_AMBIENT, in the scene's primary sector; most of
    // group 1); without: positional on the car's model with the range (group 2, two of group 1).
    VEHICLE_SOUND_AMBIENT = 0x3,
    VEHICLE_SOUND_OPEN_8 = 0x8,                         // passed on to the cache's open (meaning not identified)
};

// The callback table (C_Vehicle +0x680, 17 slots; the car sets most of them after Setup,
// C_Car::SetFrame 0x004d10a0). Setup calls two slots the car leaves empty.
enum {
    VEHICLE_CALLBACKS = 17,
    // The body against an actor's volume (CollideBody; names inferred from the car's):
    // +0x680 T_VehicleActorImpulseCallback: the impulse for the other actor (the car's 0x004d4230).
    VEHICLE_CALLBACK_ACTOR_IMPULSE = 0,
    // +0x684 T_VehicleActorTouchCallback: after a contact the actor took as the world's (the car's
    // 0x004d4450 runs a human over).
    VEHICLE_CALLBACK_ACTOR_TOUCH = 1,
    // +0x688 T_VehicleActorResponseCallback: the other side of a contact with a moving actor (the
    // car's 0x004d4130 asks another vehicle).
    VEHICLE_CALLBACK_ACTOR_RESPONSE = 2,
    // +0x68C T_VehicleActorFirmCallback: whether the actor counts as the world (the car's 0x004d4620:
    // not other vehicles, nor humans but slow ones below the car).
    VEHICLE_CALLBACK_ACTOR_FIRM = 3,
    // +0x690 T_VehicleImpactCallback: a wheel's bump impulse or the body's (the car's 0x004d4740 hurts
    // the driver).
    VEHICLE_CALLBACK_IMPACT = 4,
    // +0x694 T_VehiclePartOffCallback: a damage part came off (Deform, ShowPartDamage; the car's
    // 0x004d4ca0 throws it off).
    VEHICLE_CALLBACK_PART_OFF = 5,
    // +0x698 T_VehiclePartOffCallback with a wheel's index: the wheel came off (Deform; the car's
    // 0x004d4eb0 sets WHEEL_DETACHED and hides it).
    VEHICLE_CALLBACK_WHEEL_OFF = 6,
    // +0x69C T_VehicleGlassCallback: a glass part shattered (CrackGlass; the car's 0x004d4a10 shows
    // an effect at it and hides it).
    VEHICLE_CALLBACK_GLASS_BROKEN = 7,
    VEHICLE_CALLBACK_SEAT = 8,                          // +0x6A0 __fastcall(context, seat frame)
    VEHICLE_CALLBACK_IMPULSE = 9,                       // +0x6A4 __fastcall(context): after ApplyImpulse
    // +0x6A8 T_VehicleBodyGroundCallback: the volume a body point met; the car's (0x004d4a70) drops
    // some materials, pushes physics objects and breaks destructible ones.
    VEHICLE_CALLBACK_BODY_GROUND = 10,
    // +0x6AC T_VehicleGroundCallback: the volume a wheel's ray found; the car's (0x004d4b50) drops some
    // materials, sets off mines and explosive ground.
    VEHICLE_CALLBACK_WHEEL_GROUND = 11,
    // +0x6B0 T_VehiclePartHitCallback: Deform reached a part (the car's is empty).
    VEHICLE_CALLBACK_PART_HIT = 12,
    VEHICLE_CALLBACK_REPAIRED = 13,                     // +0x6B4 __fastcall(context): after RepairParts
    VEHICLE_CALLBACK_COLLISION = 14,                    // +0x6B8 __fastcall(context, frame, descriptor, index)
    // +0x6BC T_VehicleSetupEmitterCallback / +0x6C0 T_VehicleReuseEmitterCallback (StartEffect,
    // without the context; the car's SetupCarEmitter / ReuseCarEmitter).
    VEHICLE_CALLBACK_SETUP_EMITTER = 15,
    VEHICLE_CALLBACK_REUSE_EMITTER = 16,
};
using T_VehicleCallback = void(__fastcall *)(void *context);
using T_VehicleSetupEmitterCallback = bool(__fastcall *)(struct S_CarEmitter *emitter, uint32_t effect);
using T_VehicleReuseEmitterCallback = int(__fastcall *)(int slot, uint32_t effect);
using T_VehicleSeatCallback = void(__fastcall *)(void *context, I3D_frame *seat);
// A part (or wheel) `index` came off at `velocity` (the body's there); `push`: Deform's (may be null). (The
// car's return true; the result is unused.)
using T_VehiclePartOffCallback = bool(__fastcall *)(void *context, int index, const S_vector *velocity,
                                                    const S_vector *push);
// Glass part `part` shattered: `velocity` (Deform: the impact's direction), the sound's name (null:
// none).
using T_VehicleGlassCallback = void(__fastcall *)(void *context, int part, const S_vector *velocity,
                                                  const char *sound);
// Deform reached a part (its visual and type): the point, the impulse (strength x direction), whether
// a vertex was in reach (a dent).
using T_VehiclePartHitCallback = void(__fastcall *)(void *context, I3D_frame *frame, int type, const S_vector4 *point,
                                                    const S_vector *impulse, bool dented);
using T_VehicleCollisionCallback = void(__fastcall *)(void *context, I3D_frame *frame,
                                                      S_VehicleCollisionDesc *desc, int index);
// The impulse, the largest of the body's this step (the wheels: the impulse again), the point, the
// body point's radius (the wheels: 0.2), the contact's velocity, the actor met (the body against a
// moving actor), the volume met (the body against the world), 0.
using T_VehicleImpactCallback = void(__fastcall *)(void *context, const S_vector *impulse, const S_vector *largest,
                                                   const S_vector4 *point, float radius, const S_vector *velocity,
                                                   C_Actor *actor, const S_CollisionVolume *volume, int unknown);
// The ground a wheel's ray found, or another (null: none).
using T_VehicleGroundCallback = S_CollisionVolume *(__fastcall *)(void *context, S_CollisionVolume *ground,
                                                                  int wheel);
// The volume a body point met (its sphere's centre there and the normal), or another (null: none).
using T_VehicleBodyGroundCallback = S_CollisionVolume *(__fastcall *)(void *context, S_CollisionVolume *hit,
                                                                      S_vector *hitPos, S_vector *normal);
// Whether the actor of `hit` counts as the world; may clear `deform` (no dent, no wear).
using T_VehicleActorFirmCallback = bool(__fastcall *)(void *context, S_CollisionVolume *hit, bool *deform);
// The other side of a contact with a moving actor at `point` (the normal out of it): its lever arm,
// its velocity there, its mass, its turn per impulse (inverse inertia x (arm x normal)), the scale of
// the impulse (the car's: -1.2 for a vehicle), whether its push applies (push2). False: no contact.
// (The original passes the vehicle, not the context; the car's ignores it.)
using T_VehicleActorResponseCallback = bool(__fastcall *)(void *vehicle, S_CollisionVolume *hit,
                                                          const S_vector4 *point, const S_vector *normal,
                                                          S_vector4 *arm, S_vector4 *velocity, float *mass,
                                                          S_vector4 *turn, float *scale, bool *push);
// The impulse for the other actor at `point` (the body's, before the scale), the normal, the mass.
using T_VehicleActorImpulseCallback = void(__fastcall *)(void *context, S_CollisionVolume *hit, const S_vector4 *point,
                                                         const S_vector4 *impulse, const S_vector *normal, float mass);
// After a contact with an actor taken as the world's: the point, the body's velocity, the normal.
using T_VehicleActorTouchCallback = void(__fastcall *)(void *context, S_CollisionVolume *hit, const S_vector4 *point,
                                                       const S_vector4 *velocity, const S_vector *normal);

constexpr int VEHICLE_GEARS = 15;                       // forward gears at most

// Setup's EnumFrames callback for the damage parts (game/vehicle_parts.cpp): a S_VehiclePart in the
// std::vector<S_VehiclePart> `parts` per visual with a "[DEFORM ..]" property.
uint32_t __stdcall AddVehiclePart(I3D_frame *frame, void *parts);              // 0x004fa520

class C_Vehicle {                                       // car physics/driving state (C_Car +0x90)
public:
    C_Vehicle();                                        // 0x004e2900
    ~C_Vehicle();                                       // 0x004e7f10
    C_Vehicle(const C_Vehicle &) = delete;
    C_Vehicle &operator=(const C_Vehicle &) = delete;

    // Builds the vehicle from its model (a FRAME_MODEL, one reference kept): the body, exhausts,
    // collision volumes, doors, seats, wheels and axles, the CENTRE box, track strips, damage
    // parts and lights, then applies the default parameters (their result is returned). Fails
    // without BODY / CENTRE, without a seat or wheels (unless +0xF74 bit 1), or with misnamed
    // wheels.
    bool Setup(I3D_frame *model);                       // 0x004e2e30
    // Frees what Setup built (and releases the model).
    void Release();                                     // 0x004e81a0

    // Readies the vehicle for a run (the car's activation, 0x004d16a0): the matrices and the
    // centre of mass from the model, the turning circle, the collision volumes (put into the
    // world), the driving state, per wheel its pose and skid marks ("shlaif01.i3d" /
    // "shlaif02.i3d", +0x678 per wheel), the sounds, the damage parts' and lamps' condition, the
    // cracked-glass textures, the lights, the struts, the effect slots.
    void Start();                                       // 0x004ea780
    // Ends a run (0x004d25c0): the volumes out of the world and freed, the sounds released, the
    // steered holders straightened, the wheels back in place without skid marks, the parts
    // repaired, one last 0.1 s tick, the lights out of the scene, the effects stopped.
    void Stop();                                        // 0x004ec5c0
    // The physics: one step of `ms` milliseconds (the drivetrain: fuel, clutch, automatic gears,
    // traction easing, the engine's torque; then the solver unless asleep). The car runs it with
    // 83 ms (0x53) steps.
    void Step(uint32_t ms);                             // 0x00509aa0
    // The per-step update after the physics (game/vehicle_update.cpp): the model's sector, the
    // engine and the other sounds, the light level (every 100 ms), the doors and windows, the skid
    // marks (+0xF6C 0x40), awake: the volumes moved and the wheels' surface bumps; the steering
    // (mode 0; mode 2 keeps the steered holders straight), the wheels' spin, the lamps, the
    // steering wheel, the struts and track strips, the effects; then the per-step state reset, the
    // water flag, the automatic box's fallback to gear 1 / 2 when slow. VEHICLE_UPDATE_*.
    int Update(uint32_t ms);                            // 0x0050b180
    // Lets a freshly placed car settle: physics steps of 83 ms (soundless, its vertical speed
    // clamped to -0.4..0.1) for `maxMs` real milliseconds and at least `minSteps` steps, then one
    // Update and the car moving at `speed` m/s along its z axis. False.
    bool Settle(uint32_t maxMs, float speed, int minSteps);   // 0x004f67b0

    // Puts `mass` kg on seat `seat` (the car's passengers) and moves the centre of mass
    // (+0x1E0) and the inverse inertia (+0x220) by the seats' and ballasts' loads.
    void SetSeatLoad(int seat, float mass);             // 0x004f3950

    // Clears the per-run state: the skid marks (VEHICLE_RESET_*), the movement into the body's
    // space (inferred), the lamps' and projectors' objects unlinked, the effect slots, the
    // collision volumes out of the world, the body contacts, the engine sounds.
    void Reset(uint8_t flags);                          // 0x004e95b0
    // Takes the car up again where its model now is, after Reset and a move (the script's teleport
    // to a checkpoint, the car's placing with its start speed, its respawn before Settle): the
    // steered wheels' holders straight and every holder in place, the model's matrices, axes and
    // position (as Start), the car at rest or moving at `speed` m/s along its z axis (the engine
    // then on, else off, at once), in gear 1 with the controls released and the clutch settled,
    // each wheel off the ground with its contact under it at its reach (as Start), the lamps' and
    // projectors' objects back in the model's parent, the wheels' spin, the volumes in the world.
    // `repair`: the wheels whole and shown (no rim scraping), the condition reset, the parts repaired.
    void Restart(float speed, bool repair);             // 0x004e9db0

    // --- the state saved with the game (game/vehicle_state.cpp) ------------------------------
    // C_Car's SaveGame / LoadGame keep it as chunk 10000: a fixed part (version 10, its size, the
    // model's place and turn, the motion, the driving and damage fields), then per lamp its place
    // in the model (packed), flags and health; per dented part (with a kept original) its key,
    // state, health and the vertices moved from the original (2 cm steps, both detail levels); per
    // seat its load and door; per wheel its contact state.
    uint32_t StateSize() const;                         // 0x004f7300
    // Writes the block (StateSize() bytes; the original leaves some bytes unwritten: zero here).
    // True.
    bool SaveState(void *buffer) const;                 // 0x004f7650
    // Reads a block (false for another version): Reset, the model placed and turned (one lying
    // upside down rolled over), Replace, the fields; each lamp record to its lamp, each part record
    // to the part of its key (its original kept anew when there is none; glass cracked by its
    // health, the others' state restored; the damage shown; the vertices moved back to their
    // offsets), the seats and doors, the wheels (their holders and contacts). True.
    bool LoadState(const void *buffer);                 // 0x004f8510
    // Starts or stops the engine. `instant`: at once, without sounds (stopping also silences
    // the engine). Else through the engine sounds (0x00518710), and starting needs more than
    // 0.02 l of fuel (and +0x610 above 0 with +0xF6C bit 0x4000) or plays the failed start.
    // True.
    bool SetEngine(bool on, bool instant);              // 0x004f2530
    bool SetFuel(float litres);                         // 0x004f2670 (at most the tank size; true)
    bool IsEngineOn() const { return m_engineOn; }      // byte +0x22BF
    // The engine's revolutions per minute (+0x864 per second × 60; 0 with the engine off).
    float GetEngineRpm() const;                         // 0x004f4c50
    // The camera's offset over the seat: (0, +0x67C, 0) (inferred: C_CameraModule 0x0046c370 takes
    // its y for the driver's view).
    void GetCameraOffset(S_vector *offset) const;       // 0x004f3630
    // This vehicle's side of another vehicle's body contact (the car's response callback,
    // T_VehicleActorResponseCallback): the lever arm from the centre of mass to `point`, the
    // velocity there (m_unknown210 turns the other way: the velocity less m_unknown210 x the arm),
    // the mass, the turn per impulse along `normal` (the inverse inertia times arm x normal, in the
    // body's space). (The original leaves the arm's and the turn's fourth float uninitialised.)
    void GetContactResponse(const S_vector4 &point, const S_vector &normal, S_vector4 *arm, S_vector4 *velocity,
                            float *mass, S_vector4 *turn) const;   // 0x004f32a0
    // The projectors of the lamps of the kinds `beams` selects (VEHICLE_LIGHT_*_BEAMS) scaled to
    // `scale` (C_Car: all of them to 12 x 12 x 40).
    void SetBeamScale(uint32_t beams, const S_vector &scale);   // 0x004f4e40
    // The model's place taken again after the car placed it (C_Car 0x004d3380): the matrices, axes,
    // position and centre of mass as ReadModelPlace (not the parent, nor the sector), the
    // velocity's direction and the speed; +0x229F cleared.
    void ReadModelState();                              // 0x004f5d10
    // A callback slot (VEHICLE_CALLBACK_*; the original has one 13-byte setter per slot, 0x004fb080
    // .. 0x004fb180; C_Car::SetFrame 0x004d10a0 sets them).
    void SetCallback(int slot, void *callback) { m_callbacks[slot] = callback; }
    float GetFuel() const { return m_fuel; }            // +0xF5C litres
    float GetTankSize() const { return m_tankSize; }    // +0x2220
    // The velocity in m/s (+0x410; C_Car +0x4A0).
    const S_vector4 &GetVelocity() const { return m_velocity; }
    // The model's box extent (`unscaled`) or the same x its world scale at setup.
    const S_vector4 &GetSize(bool unscaled) const;      // 0x004f4c80
    // Silences the engine when switched off (+0x2296, on when made).
    void EnableSounds(bool on);                         // 0x004f2710

    // --- the driving controls (all true unless noted) ------------------------------------------
    // Selects a gear (-1 reverse, 0 neutral, 1..m_gearCount): false out of range, for reverse
    // in mode 1 (+0x550), or when the engine would pass the rev limit in it. Ignored (true) with
    // +0xF6C bit 0x4000 while +0x63C is not above 0.
    bool SetGear(int gear);                             // 0x004f2030
    // The throttle (above 1: 1; below 0.05: 0; 0 with the engine off). In mode 1 `first`
    // picks +0x6FC or +0x700; with the automatic gearbox (inferred, +0x229D) in reverse it
    // brakes instead.
    bool SetThrottle(float value, bool first);          // 0x004f2100
    // -1..1, through the curve +0x5F4 (0: linear) and x +0x934 into +0x948.
    bool SetSteering(float value);                      // 0x004f2420
    // The brake (as the throttle; eased in over +0x720 unless +0x229B); with the automatic
    // gearbox in reverse it is the throttle. False in mode 1.
    bool SetBrake(float value);                         // 0x004f2270
    // On: eased in over +0x70C (`unknown`: the player's throttle is released); off at once.
    bool SetHandbrake(bool on, bool unknown);           // 0x004f26a0
    // 0..1 into +0x908 (through the curve +0x5F8 with +0x229C). The player gives 1 - an input
    // axis, a dead driver 0 (inferred: the clutch).
    bool SetUnknown4f23b0(float value);                 // 0x004f23b0
    // Stores `value` (+0.5 when positive) at +0x790; the AI driver gives back the car's +0x780
    // when it lets go of the car (meaning not identified).
    bool SetUnknown4f2630(float value);                 // 0x004f2630
    // Pushes the car's body by `impulse` at the world point `point`: the velocity by impulse /
    // mass (unless `flags` bit 0) and the turn by the lever arm, the inverse inertia and
    // `factors` (unless bit 1). Then VEHICLE_CALLBACK_IMPULSE.
    void ApplyImpulse(const S_vector &impulse, const S_vector &point, uint8_t flags, const S_vector &factors);   // 0x004f2790
    // The current mass and where ApplyImpulse's lever arms start (read in place by the seat weapons'
    // recoil).
    float GetCurrentMass() const { return m_currentMass; }
    const S_vector4 &GetUnknown1B0() const { return m_unknown1B0; }
    // Adds `force` at the world point `point` to the next steps' own force (+0x270, unless `flags`
    // bit 0) and its torque (the force about the lever arm, in the body's space times `factors`,
    // unless bit 1) to +0x280. Then VEHICLE_CALLBACK_IMPULSE. (The car's collision with a human:
    // only the torque, at the car's place.)
    void AddForce(const S_vector &force, const S_vector &point, uint8_t flags, const S_vector &factors);   // 0x004f2a30
    // Another car's hit (the car's collision with a vehicle `other`): the body pushed by `impulse`
    // x `scale` at `point` (as ApplyImpulse, without factors); with VEHICLE_SIM_DEFORM a dent there
    // against `normal` (radius 0.45, the push's size), with VEHICLE_SIM_IMPACT_DAMAGE ImpactDamage
    // (the push's size) and VEHICLE_CALLBACK_IMPACT (the push twice, radius 0.45, the velocity,
    // `other`, its last argument 1). Then the velocity's direction, VEHICLE_CALLBACK_IMPULSE.
    // `unused`: a stack argument the body ignores.
    void ApplyCarHit(C_Actor *other, float scale, const S_vector4 &point, const S_vector &impulse,
                     const S_vector &normal, int unused);   // 0x004f2b60

    // --- the collision volumes ----------------------------------------------------------------
    // Puts the main volumes (the body's, or the special ones) / the second volume (+0x490)
    // into the collision world or takes them out (only on a change).
    void SetVolumesInWorld(bool main, bool second);     // 0x004f5840
    // Uses the special volumes ("SPECCOLL*") instead of the body's (false without any). The
    // old ones are removed from the world even when they are not in it.
    bool SetSpecialVolumes(bool on);                    // 0x004f5620

    // --- the parameter block ----------------------------------------------------------------
    // Checks the block and copies it into the vehicle and its wheels (VEHICLE_PARAMS_*). Without
    // VEHICLE_PARAMS_FIX it stops at the first invalid value (false; what came before is
    // applied); with it an invalid value is replaced in `params` (from the vehicle or its
    // stored block) and applying goes on. At the end the block becomes the stored one.
    bool ApplyParams(S_VehicleParams &params, uint8_t flags);   // 0x004ed270
    // A copy of the stored block (false for a null target).
    bool GetParams(S_VehicleParams *params) const;      // 0x004f2000
    // Loads the sounds the block names (both sound groups; every name in its handle slot; true).
    bool LoadSounds(const S_VehicleParams &params);     // 0x00517370

private:
    // The car reaches into its vehicle as the original does (C_Car +0x90), and so do its
    // callbacks (game/car_callbacks.cpp), and the decorators' bends (its wheels and tracks).
    friend class C_Car;
    friend class C_DecoratorGrid;
    friend void __fastcall CarOnBodyActorTouch(void *context, S_CollisionVolume *hit, const S_vector4 *point,
                                               const S_vector4 *velocity, const S_vector *normal);
    friend void __fastcall CarOnVehicleImpact(void *context, const S_vector *impulse, const S_vector *largest,
                                              const S_vector4 *point, float radius, const S_vector *velocity,
                                              C_Actor *actor, const S_CollisionVolume *volume, int unknown);
    friend bool __fastcall CarOnPartOff(void *context, int index, const S_vector *velocity, const S_vector *push);
    friend bool __fastcall CarOnWheelOff(void *context, int index, const S_vector *velocity, const S_vector *push);
    friend void __fastcall CarOnGlassBroken(void *context, int part, const S_vector *velocity, const char *sound);
    friend S_CollisionVolume *__fastcall CarOnWheelGround(void *context, S_CollisionVolume *ground, int wheel);
    friend void __fastcall CarNetReadState(void *context, C_BitStream *stream);
    friend void __fastcall CarNetWriteState(void *context, C_BitStream *stream);
    // Start's and Restart's: the model's world matrix and its inverse (both again without the
    // translation: +0x310 / +0x350), its axes and position, the centre of mass in the world
    // (+0x1B0), its parent (+0x544) and its sector.
    void ReadModelPlace();
    // LoadState's: the model's place read again (Update, ReadModelPlace), the turn and movement
    // back into the world's space (from Reset's), each wheel placed with its contact under it, its
    // skid marks in the scene and off the ground; the lamps' and projectors' objects back in the
    // model's parent, the wheels' spin, the volumes in the world, the controls released.
    void Replace();                                     // 0x004e98c0
    // A place in the model packed into `bits` (x and y bits / 3 each, z the rest): its share of the
    // model's box (m_boxOffset, m_size), truncated; and back.
    uint32_t PackPosition(int bits, const S_vector &pos) const;     // 0x004f4040
    S_vector UnpackPosition(int bits, uint32_t packed) const;       // 0x004f4110
    // A part's key: its frame's short name (after the last '.') as 4-byte words XORed (-1: none).
    uint32_t PartKey(uint32_t part) const;              // 0x004f41f0
    // The last part of `key` (-1: none).
    int FindPartByKey(uint32_t key) const;              // 0x004f42f0
    // The kept original (m_meshOriginals) of the part's name and the model file, the last one
    // (-1: none). (Deform's and LoadState's lookup.)
    int FindMeshOriginal(const S_VehiclePart &part) const;

    // --- Setup's steps ----------------------------------------------------------------------
    // The suspension struts ("AXIS*"): m_axleStruts / m_sideStruts.
    void SetupStruts();                                 // 0x004e6de0
    // Shows or hides the strut frames when +0xF6C bit 0 changed against +0xF70; with it set
    // stretches and turns them to the wheels (also per tick).
    void UpdateStruts();                                // 0x00500fd0
    // Ends wheel `wheel`'s current skid mark and moves its ring on.
    void EndSkidMark(int wheel);                        // 0x004fe610
    // Per damage part: unless `healthOnly`, the deformed meshes back from m_meshOriginals, the
    // broken glass whole again, the door's frame given back to its seat, a group's main visual
    // shown and the others hidden; then unknown24 = unknown1C. Then VEHICLE_CALLBACK_REPAIRED.
    void RepairParts(bool healthOnly);                  // 0x004fe910
    // +0x63C / +0x610 back to +0x640 / +0x614, the wheels' +0x230, the lamps' +0x14 and the
    // projectors (state 3, texture 0, power 2).
    void ResetCondition();                              // 0x004feea0
    // The glass parts' cracked texture (`name` with the alpha `alphaName`), where they have none.
    bool LoadGlassTextures(const char *name, const char *alphaName);   // 0x004fe850
    // The lamps' own and the projectors' lights into / out of s_lightSectors (out of the scene
    // as well).
    void AddLightsToSectors();                          // 0x004f6080
    void RemoveLights();                                // 0x004f61e0
    // Every loaded sound into / out of the sound sectors (s_soundSectors), released.
    void AddSoundsToSectors();                          // 0x00517cf0
    void RemoveSoundsFromSectors();                     // 0x005181c0
    void ReleaseSounds();                               // 0x0051c180
    // --- the sounds (game/vehicle_sounds.cpp) -----------------------------------------------
    // The sound file `fileName` into `sound` through the sound cache (the class's only virtual
    // method, vtable slot 0; nothing overrides it).
    long OpenSound(I3D_sound *sound, const char *fileName, uint32_t flags);   // 0x0051bd50
    // The sound `name` into `slot`: no name releases the slot's sound; the same file keeps it;
    // else the slot's (or a new) sound frame opens the file (failing, it is released): off,
    // full volume, `loop`, ambient or positional by VEHICLE_SOUND_* `flags` (the range: near /
    // far and two more values), `atEngine`: placed at the engine's smoke dummy.
    void LoadSound(const char *name, I3D_sound *&slot, uint32_t flags, float nearRange, float farRange,
                   float range2, float range3, bool loop, bool atEngine);   // 0x0051bd80
    // --- the lamps (game/vehicle_lamps.cpp) -------------------------------------------------
    // With VEHICLE_SIM_LAMPS (now or before): the indicators' blink phase, the flashers', the
    // brake / reversing lights / indicators that follow the car (VEHICLE_LIGHT_AUTO_*); then each
    // lamp lit by its kind's switch (dark when broken or without VEHICLE_SIM_LAMPS): its billboard
    // on, its projected and spot light (with its kind's VEHICLE_LIGHT_*_BEAMS) placed at the lamp
    // or its PROJECTOR and turned on (VEHICLE_SIM_LAMP_PROJECTORS / _SPOT_LIGHTS); a dark one's off
    // (a shared projector another lamp lit stays). +0x5B4: the switches of this update. Update and
    // Stop (after clearing the switches, all dark) call it.
    void UpdateLamps(float timeDelta);                  // 0x00501cb0

    // --- Update's parts (game/vehicle_update.cpp) -------------------------------------------
    // A moving door (DOOR_MOVING) swings towards its target angle at +0x30 per second about its
    // y axis (the side and DOOR_BACKWARD give the sense); the door sound as it starts to open and
    // when it closes.
    void UpdateDoors(float timeDelta);                  // 0x00501750
    // A moving window slides up or down at +0x60 per second between its place and +0x5C, 0.1
    // inwards when down (hidden there); stopped when its glass part is broken.
    void UpdateWindows(float timeDelta);                // 0x00501ab0
    // The wheels' visual spin (+0x1F0, wrapped to 0..2 pi): by their movement along the ground,
    // the engine when driven off the ground or spinning, the track speed for tracked wheels; the
    // frame turned by it, then by +0x22C about z, then its rest turn +0x010. +0x0C0 cleared.
    void UpdateWheelSpin(float timeDelta);              // 0x00507a40
    // The track strips: their vertices follow the wheels' holders' heights; their texture
    // scrolls by the track speed (+0x51C / +0x520, kept within -1..1) x +0x518.
    void UpdateTracks(float timeDelta);                 // 0x00506700
    // The particle effects (game/vehicle_effects.cpp), with VEHICLE_SIM_EFFECTS and +0x2292,
    // else stopped; each emitter in its sector, its colours dimmed by the light (+0x548), its
    // velocities its effect's plus the velocity it carries away (and a share of it), placed and
    // updated:
    // - the engine's smoke at the "SMOKE_MOTOR" dummy while +0x610 is below +0x624 under +0x62C
    //   m/s: effect +0x630 (+0x634 below +0x628), true when none is showing;
    bool UpdateEngineSmoke();                           // 0x00506f00
    // - each wheel's at its contact (WHEEL_EFFECTS on the ground): its surface's skid effect when
    //   spinning (or locked or flag 0x1000000 sliding over 2 m/s), its dust over 3 m/s; none in
    //   the campaign's mission 8;
    void UpdateWheelEffects();                          // 0x00508350
    // - each exhaust's (effect +0x754) with the engine on under +0x758 m/s, blown out along it at
    //   +0x75C..+0x760 by the revs;
    void UpdateExhausts();                              // 0x00508ee0
    // - the first three body contacts' (their surface's), all at the car's position.
    void UpdateContactEffects();                        // 0x00509540
    // A sliding wheel's skid mark on a surface that takes them (not on water, not a dummy, on
    // the ground, +0x21C not negative; else it ends): a new one starts at the contact (lifted 0.05
    // along the ground's normal) along the contact's velocity; then its segment stretches to the
    // contact (from 5 cm, up to 0.3 m: a full segment closes and the next starts, joined by a
    // corner piece where it turns), ending past 5 m or against its direction (a new one starts).
    void LaySkidMark(int wheel);                        // 0x004fcbd0
    // Sounds (game/vehicle_sounds.cpp): the engine's (by the revs) and their stop when switched
    // off, the handbrake's (once per pull, VEHICLE_SIM_DETAIL_SOUNDS), two switched loops (+0x2294 /
    // +0x2295: the playing group's on, the other's off), the tyres' squeal (the loudest wheel on a
    // squealing surface, rising by +0x5EC per second into +0x5F0), the rolling one (by the speed:
    // +0xC08.. / +0xEF8..) and the rim's scraping (by the speed along the car: +0xC20 / +0xF20; off:
    // both stop), a door's.
    // The engine's loops by the clamped revs (in gear the on-load and off-load layers, in reverse
    // the third set; group 2 without VEHICLE_SIM_DETAIL_SOUNDS one loop) and the idle loop, faded
    // in over 0.3 s (+0xF48); switches the groups' loops over to m_unknown2297's group.
    void UpdateEngineSound(float timeDelta);            // 0x00518ab0
    // Stops the engine's loops (both groups' layers, idle loops, group 2's single loop).
    void StopEngineLoops();                             // 0x0051b7c0
    void PlayHandbrakeSound();                          // 0x0051a580
    void SwitchLoopSoundA(bool on);                     // 0x0051a6c0
    void SwitchLoopSoundB(bool on);                     // 0x0051a7a0
    void UpdateTyreSqueal(float timeDelta);             // 0x0051a130
    void UpdateRollingSound();                          // 0x00519a60
    void UpdateRimSound(bool on);                       // 0x00519dc0
    // A door's opening or shutting sound at the seat's door (with sounds on).
    void PlayDoorSound(int seat, bool opening);         // 0x0051b660
    // UpdateLamps' inlined steps: a lamp's projected or spot light linked into the scene
    // (m_unknown544) and placed at the lamp, or at its PROJECTOR (marked lit; `sharedDir` then
    // replaces the projector's facing); switched off unless the lamp's PROJECTOR is lit.
    void PlaceLampObject(I3D_frame *object, const S_VehicleLamp &lamp, const S_vector *sharedDir);
    void DarkenLampObject(I3D_frame *object, const S_VehicleLamp &lamp);

    // --- the drivetrain (game/vehicle_drivetrain.cpp) -------------------------------------
    // The steering angle eased towards SetSteering's and the holders turned (Update calls it in
    // mode 0, Step in mode 2).
    void UpdateSteering(float timeDelta);               // 0x00507400
    // The engine's torque at `revs` (per second) for `throttle`: the torque curve (+0x764..+0x770
    // at the revs +0x914 idle, +0x774, +0x778, +0x77C, the limit) plus the engine braking
    // (+0x8E8 x revs / limit) x throttle, minus the engine braking.
    float EngineTorque(float revs, float throttle) const;   // 0x0050d5c0
    // EngineTorque at `rpm` (within the parameters' rpm range) at full throttle (minus the engine
    // braking with `engineBraking`), x gear `gear`'s ratio (not for neutral). False out of range.
    bool GetEngineTorque(float rpm, float *torque, int gear, bool engineBraking) const;   // 0x004f3840
    // The torque at the wheels for `throttle` (the idle throttle near idle, none past the rev or
    // speed limits); eases the engine load +0x78C; 0 with the engine off.
    float DriveTorque(float timeDelta, float throttle); // 0x0050d2c0
    // The engine's revs through the clutch +0x904 and the gear change (+0x910 bits 1 / 2), from the
    // drive's torque and the driven wheels' brake torque.
    void UpdateEngineRevs(float timeDelta, float torque, float brakeTorque);   // 0x0050d760
    // The aerodynamic drag terms and the solver in steps of at most +0xF54 s.
    void Integrate(float timeDelta, float torqueLeft, float torqueRight);   // 0x0050db90
    // --- the solver (game/vehicle_solve.cpp) ----------------------------------------------
    // One step of the rigid body (`drag` against the motion, the downforce `frontDownforce` /
    // `rearDownforce` at the box's ends with +0xF6C bit 8) with its wheels: the tyres' forces
    // within the friction ellipse, the brakes, the drive (its torque split by the grip), the
    // velocity and the turn integrated, the pose moved and kept clear of the world (+0xF6C bit
    // 0x10), the matrices, the wheel contacts for the next step, the engine's revs. True.
    bool Solve(float timeDelta, float drag, float frontDownforce, float rearDownforce, float torqueLeft,
               float torqueRight);                      // 0x0050de60
    // --- the wheels' contacts (game/vehicle_wheels.cpp) ------------------------------------
    // Finds the ground under each wheel along `down` (by the axle rays' planes, its kept plane or
    // its own ray: VEHICLE_SIM_*; at rest on its last plane), places the wheel and its holder,
    // the suspension's load (unknown168, damped), a bottomed-out wheel's impulse (`bumps`: on the
    // body, deforming and wearing it, the impact callback, the bump sound), the contact's velocity
    // and rolling direction for the solver. `headingSum`: the grounded wheels' rolling directions
    // summed.
    void UpdateWheelContacts(S_vector4 *headingSum, float timeDelta, const S_vector4 *down,
                             bool bumps);               // 0x00512390
    // An impact `impulse` (m/s x kg) past +0x620 / +0x64C wears the engine (+0x610, its power
    // +0x60C with it; at 0 it dies) and the gearbox (+0x63C), each once until +0x58C is cleared.
    void ImpactDamage(float impulse);                   // 0x00500eb0
    // --- the damage (game/vehicle_damage.cpp) -------------------------------------------------
    // An impact of `strength` (m/s x mass) at the world `point` along `direction` (the body's
    // points: their radius and the normal; a bottomed-out wheel: 0.2 and the model's y axis):
    // - with wear (VEHICLE_SIM_IMPACT_DAMAGE, not DEFORM_NO_WEAR) the lamps within `radius` lose
    //   health and break (a light part's go with it);
    // - each damage part the impulse passes has its vertices within `radius` dented along the
    //   direction (in both its first detail levels; the first dent keeps its original mesh for
    //   RepairParts), VEHICLE_CALLBACK_PART_HIT; with wear it loses health once per step and shows
    //   it (ShowPartDamage), glass cracks; gone it comes off (VEHICLE_CALLBACK_PART_OFF with `push`)
    //   with its lamps; a bumper passes on only what it does not absorb;
    // - with wear the wheels within 1 m lose health once per step and come off at none
    //   (VEHICLE_CALLBACK_WHEEL_OFF).
    // DEFORM_*: `flags`.
    void Deform(const S_vector4 &point, const S_vector4 &direction, float radius, float strength,
                uint8_t flags, const S_vector *push);   // 0x004ff8b0
    // A part's damage shown (Deform after its health dropped): a door gone breaks its windows; a
    // group shows the visual for its health, its lamps break and the parts on its frame are gone
    // (`detach`: those but glass come off, VEHICLE_CALLBACK_PART_OFF with `push`).
    void ShowPartDamage(uint32_t part, const S_vector *push, bool detach);   // 0x004fefd0
    // A glass part below its crack health shows its cracked texture; at no health it shatters
    // (VEHICLE_CALLBACK_GLASS_BROKEN with `velocity` and the sound group's glass sound).
    void CrackGlass(uint32_t part, const S_vector &velocity);   // 0x00500da0
    // The projector whose visual is `projector` keeps only the `keep` side lit
    // (VEHICLE_PROJECTOR_LEFT / _RIGHT; its texture for that side, its light dimmed). False.
    bool BreakProjectorSide(const I3D_projector_visual *projector, uint16_t keep);   // 0x00500cb0
    // The bump sound at `volume` (with sounds on; while it plays, not for a bump 0.15 quieter).
    void PlayBumpSound(float volume);                   // 0x0051b560
    // --- the body against the world (game/vehicle_collide.cpp) -----------------------------
    // Sweeps the body's boxes from the pose before the step to the pose now, nearest a point far
    // along the velocity first; a box that touches has its points swept as spheres and its child
    // boxes tested. Each point meeting something it moves into answers it (BodyContact): true if
    // any did. `push` / `push2`: the longest ways out of the world / of moving actors (the
    // solver uses them a step later); `maxImpulse` / `maxImpulseVector` the largest impulse;
    // `up` the y axis before the step; `ms` only passed on.
    bool CollideBody(S_BodyCollisionBox *boxes, S_vector *push, S_vector *push2, float *maxImpulse,
                     S_vector *maxImpulseVector, const S_vector4 *up, int ms);   // 0x00514d70
    // A body point's sphere met `hit` (the sphere's centre there `hitPos`, the normal out of it).
    // Moving into it, the impulse that stops the point there (the surface's bounce, or with
    // +0x228C the vehicle's own) and the friction along it; against a moving actor (that does not
    // count as the world) the impulse shared with it. Dents, wear, the impact callback and sound,
    // a hard contact into m_bodyContacts, the way out of a sphere already in it. True if it moved
    // into it (and an actor took part). Inline in the original.
    bool BodyContact(const S_BodyCollisionPoint &point, S_CollisionVolume *hit, const S_vector &hitPos,
                     const S_vector &normal, S_vector *push, S_vector *push2, float *maxImpulse,
                     S_vector *maxImpulseVector);
    // The body's impact sound for `impulse`: the level by the playing group's thresholds (+0xC38..
    // or +0xF38..; none up to the first; a level's flag silences it with `unknown`, m_unknown2293),
    // one of the level's two crash sounds (by rand when both; not over itself nor within 800 ms of
    // the other's start).
    void PlayImpactSound(float impulse, bool unknown);  // 0x0051a950
    // One of the two gear sounds (the second one time in five; with VEHICLE_SIM_DETAIL_SOUNDS, not
    // over the last one).
    void PlayGearSound();                               // 0x0051a480
    // A tyre's burst (C_Car's network message putting wheel `wheel` on its rim): group 2's at the
    // wheel.
    void PlayWheelBurst(int wheel);                     // 0x0051a880
    // The special volumes into the collision world (those on a moving part through its matrix,
    // the others at m_modelPos; only those whose descriptor allows it). Inline in the original.
    void AddSpecialVolumes();
    // A car effect's emitter (g_carEmitters) for `slot` (-1: none yet): the car's callback may
    // switch the slot's own in place; else a free one with the effect, or a free one made the
    // effect (a copy of another emitter with it, or the car's callback sets it up); taken and
    // emitting (-1: none free).
    int StartEffect(int effect, int slot);              // 0x004ece20
    // Lets a car effect's emitter go (false for an invalid slot; `this` unused).
    bool StopEffect(int slot);                          // 0x004ecfc0
    // On: plays the starter (when no starter or stop sound plays) and turns the engine on once
    // they are within 300 ms of their end (called by Update until then). Off: every sound stops,
    // the stop sound plays.
    void SwitchEngine(bool on);                         // 0x00518710
    // The failed start's sound (unless one plays).
    void PlayEngineFail();                              // 0x0051a620
    // Stops every sound of both groups.
    void StopSounds();                                  // 0x0051b8f0
    // After AddVehiclePart: the bumpers to the end of m_parts, each part's main visual shown and
    // its group's others hidden (true).
    bool SetupParts();                                  // 0x004fe6b0
    // The lamps ("LIGHT<xx>*"), their damage parts, the projectors ("PROJECTOR*").
    void SetupLights();                                 // 0x004e7450
    // A PROJECTOR's projected light and spot (`light`) and its record in m_projectors.
    class I3D_projector_visual *CreateProjector(I3D_frame *frame, class I3D_light **light);   // 0x004e8d80
    // Moves the body collision boxes `list` (and their points and children) into the model's
    // space or their moving parent's, and removes their frames from the model.
    void PlaceBodyCollision(S_BodyCollisionBox *list);  // 0x004fa2e0
    // Removes `frame` from the model (unlinked, then the model's RemoveModelFrame).
    void RemoveModelFrame(I3D_frame *frame);

    S_vector m_unknown010;                              // +0x010 (Start: 0)
    S_vector m_unknown020;                              // +0x020 (Start: 0)
    S_vector4 m_unknown030;                             // +0x030 where the resting time counts from (Start: m_modelPos)
    S_vector m_unknown040;                              // +0x040 (Start: 0)
    S_vector4 m_frontAxleCentre;                        // +0x050 Start: the middle of the first axle's wheels (model space)
    S_vector4 m_rearAxleCentre;                         // +0x060 Start: the middle of the last axle's
    // The ground's planes under the axle centres (UpdateWheelContacts' axle rays; at rest the last
    // wheel's plane goes into the front one).
    S_plane m_frontGround;                              // +0x070
    S_plane m_rearGround;                               // +0x080
    // Integrate (with +0xF6C bit 0x10): the model's place and axes before the step.
    S_vector4 m_previousModelPos;                       // +0x0B0
    S_vector4 m_previousAxisZ;                          // +0x0C0
    S_vector4 m_previousAxisY;                          // +0x0D0
    S_vector4 m_previousAxisX;                          // +0x0E0
    S_vector4 m_scaledSize;                             // +0x0F0 m_size x the model's world scale (w: the scale's)
    S_vector4 m_size;                                   // +0x100 the model's hierarchy box extent
    S_vector4 m_scaledBoxOffset;                        // +0x110 m_boxOffset x the model's world scale
    S_vector4 m_boxOffset;                              // +0x120 minus the box's min corner
    S_vector4 m_smokePos;                               // +0x130 the "SMOKE_MOTOR" dummy through the body's local matrix (zero without one)
    S_vector4 m_unknown140;                             // +0x140 (w left as it was: the original stores stack garbage)
    S_vector4 m_headPos;                                // +0x150 the "HEAD" dummy through the body's local matrix (zero without one)
    S_vector4 m_modelPos;                               // +0x160 the model's world position at setup
    S_vector4 m_velocityDir;                            // +0x170 m_velocity normalized (ApplyImpulse)
    S_vector4 m_bodyMin;                                // +0x180 the "CENTRE" dummy's box through its and the body's local matrices
    S_vector4 m_bodyMax;                                // +0x190
    S_vector4 m_steeringAxis;                           // +0x1A0 the "SWHEEL" visual's local z axis
    S_vector4 m_unknown1B0;                             // +0x1B0 where ApplyImpulse's lever arms start (inferred: the world centre of mass)
    S_vector4 m_unknown1C0;                             // +0x1C0 the centre of mass over the box's top (x, y, max z)
    S_vector4 m_unknown1D0;                             // +0x1D0 ... over its bottom (x, y, min z)
    S_vector4 m_unknown1E0;                             // +0x1E0 m_centreOfMass moved by the seats' and ballasts' loads (SetSeatLoad)
    S_vector4 m_centreOfMass;                           // +0x1F0
    S_vector4 m_weight;                                 // +0x200 (0, -mass x gravity, 0)
    S_vector m_unknown210;                              // +0x210 ApplyImpulse subtracts the turn (inferred: the angular velocity)
    S_vector4 m_unknown220;                             // +0x220 m_inverseInertia with the loads (SetSeatLoad)
    S_vector4 m_inverseInertia;                         // +0x230
    S_vector4 m_modelAxisZ;                             // +0x240 rows 2, 0 and 1 of the model's world matrix at setup
    S_vector4 m_modelAxisX;                             // +0x250
    S_vector4 m_modelAxisY;                             // +0x260
    S_vector m_unknown270;                              // +0x270 (Start: 0) AddForce's force (the solver adds it)
    S_vector m_unknown280;                              // +0x280 (Start: 0) AddForce's torque (the solver adds it)
    S_matrix m_modelMatrix;                             // +0x290 the model's world matrix at setup
    S_matrix m_invModelMatrix;                          // +0x2D0 its inverse (taken after the body collision boxes)
    // ApplyImpulse turns the torque into the body's space with m_unknown350 and back with
    // m_unknown310 (inferred: the body's rotation and its inverse); Reset turns m_unknown210 and
    // m_velocity by m_unknown350.
    S_matrix m_unknown310;                              // +0x310
    S_matrix m_unknown350;                              // +0x350
    S_matrix m_previousModelMatrix;                     // +0x390 Integrate (+0xF6C bit 0x10, not 0x400)
    S_matrix m_previousInvModelMatrix;                  // +0x3D0
    S_vector4 m_velocity;                               // +0x410 (inferred: ApplyImpulse adds impulse / mass)
    S_vector4 m_previousVelocity;                       // +0x420 Step: m_velocity before the solver
    // The collision volumes (made by Start, part 1c-ii; SetVolumesInWorld).
    S_CollisionVolume m_bodyVolume;                     // +0x430 the CENTRE box's
    S_CollisionVolume m_volume490;                      // +0x490 the second one
    S_CollisionVolume *m_frontGroundVolume;             // +0x4F0 the volumes of m_frontGround / m_rearGround (null: none; Start: null)
    S_CollisionVolume *m_rearGroundVolume;              // +0x4F4
    uint32_t m_unknown4F8[2];                           // +0x4F8 (Start: 0)
    uint32_t m_unknown500;                              // +0x500 ms within 0.15 m of m_unknown030 (Step; over 4000: not awake)
    float m_unknown504;                                 // +0x504 radians
    float m_unknown508;                                 // +0x508 (Start: 1)
    float m_unknown50C;                                 // +0x50C (Start: 1)
    float m_unknown510;                                 // +0x510
    float m_unknown514;                                 // +0x514
    float m_unknown518;                                 // +0x518
    float m_unknown51C;                                 // +0x51C (Start: 0)
    float m_unknown520;                                 // +0x520 (Start: 0)
    float m_unknown524;                                 // +0x524
    float m_unknown528;                                 // +0x528 (Start: 1)
    float m_unknown52C;                                 // +0x52C (Start: 0)
    float m_unknown530;                                 // +0x530 (Start: 1)
    uint32_t m_unknown534;                              // +0x534 (0xF02 when made)
    int m_unknown538;                                   // +0x538
    int m_unknown53C;                                   // +0x53C (2 when made)
    int m_unknown540;                                   // +0x540
    I3D_frame *m_unknown544;                            // +0x544 Start: the model's parent
    float m_unknown548;                                 // +0x548 (1 when made)
    int m_unknown54C;                                   // +0x54C
    int m_unknown550;                                   // +0x550 a mode: ApplyParams sets 0 or 2; 1 refuses the brake and reverse (SetThrottle's two channels)
    int m_unknown554;                                   // +0x554 above 0: VEHICLE_PHYSICS_FROZEN (Start: 0)
    // The lamps' damage (Deform): an impact past m_lampMinImpulse, less m_lampAbsorbed percent, over
    // m_lampFullImpulse x m_lampFullHealth is lost; below m_lampBreakHealth the lamp breaks.
    float m_lampFullImpulse;                            // +0x558 (m/s x mass)
    float m_lampMinImpulse;                             // +0x55C (m/s x mass)
    float m_lampFullHealth;                             // +0x560 (ResetCondition gives it to the lamps)
    float m_lampBreakHealth;                            // +0x564
    float m_lampAbsorbed;                               // +0x568 percent
    std::vector<S_VehicleTrack> m_tracksL;              // +0x56C the left track strip's wheels
    std::vector<S_VehicleTrack> m_tracksR;              // +0x57C the right one's
    uint32_t m_unknown58C;                              // +0x58C (Start: 0) 1: a part dented; 2: parts / wheels hit this step; 4 / 8: ImpactDamage's once marks (Update clears 2, 4, 8)
    std::vector<S_VehicleLamp> m_lamps;                 // +0x590
    std::vector<S_VehicleProjector> m_projectors;       // +0x5A0
    uint32_t m_unknown5B0;                              // +0x5B0 VEHICLE_LIGHT_* (Start, Stop: 0)
    uint32_t m_unknown5B4;                              // +0x5B4 the switches at the last UpdateLamps (Start: 0)
    float m_unknown5B8;                                 // +0x5B8 the indicators' on time, s (0.4 when made)
    float m_unknown5BC;                                 // +0x5BC their off time (0.3)
    float m_unknown5C0;                                 // +0x5C0 the time in the blink phase (0)
    float m_unknown5C4;                                 // +0x5C4 the steering that sets the indicators (0.2)
    float m_unknown5C8;                                 // +0x5C8 the flashers' period, s (0.3)
    float m_unknown5CC;                                 // +0x5CC the time in the flash phase (0)
    uint32_t m_unknown5D0[3];                           // +0x5D0 (Start: 0)
    float m_unknown5DC;                                 // +0x5DC the sum over the wheels of +0x1F4 / radius
    uint32_t m_unknown5E0;                              // +0x5E0 (Start: 0)
    float m_unknown5E4;                                 // +0x5E4 m/s
    float m_unknown5E8;                                 // +0x5E8 m/s
    float m_unknown5EC;                                 // +0x5EC the tyres' squeal's rise per second
    float m_unknown5F0;                                 // +0x5F0 (0 when made) the tyres' squeal, 0..1
    float m_unknown5F4;                                 // +0x5F4 (0.5)
    float m_unknown5F8;                                 // +0x5F8 (0.5)
    uint32_t m_unknown5FC;                              // +0x5FC (only saved and loaded so far)
    uint32_t m_unknown600;                              // +0x600 (the brake clears bit 0x01000000)
    int m_unknown604;                                   // +0x604 ms trying to rest (negative: a grace period; over 1000: not awake)
    float m_unknown608;                                 // +0x608
    float m_unknown60C;                                 // +0x60C (ResetCondition: 1)
    float m_unknown610;                                 // +0x610 (with +0xF6C bit 0x4000 the engine starts only above 0; the engine smoke below +0x624)
    float m_unknown614;                                 // +0x614 what ResetCondition restores +0x610 to
    float m_unknown618;                                 // +0x618
    float m_unknown61C;                                 // +0x61C (m/s x mass)
    float m_unknown620;                                 // +0x620 (m/s x mass)
    float m_unknown624;                                 // +0x624 the engine smokes with +0x610 below it
    float m_unknown628;                                 // +0x628 with +0x610 below it the second smoke
    float m_unknown62C;                                 // +0x62C m/s the engine smokes below it
    int m_unknown630;                                   // +0x630 the engine smoke's car effect (the parameter's bits)
    int m_unknown634;                                   // +0x634 the second one (0: none)
    int m_unknown638 = -1;                              // +0x638 the engine smoke's car effect slot (-1: none; unset until Start in the original)
    float m_unknown63C;                                 // +0x63C (with +0xF6C bit 0x4000 gears change only above 0)
    float m_unknown640;                                 // +0x640 what ResetCondition restores +0x63C to
    float m_unknown644;                                 // +0x644
    float m_unknown648;                                 // +0x648 (m/s x mass)
    float m_unknown64C;                                 // +0x64C (m/s x mass)
    int m_unknown650;                                   // +0x650 the gear sound played last (0 / 1)
    std::vector<S_VehicleMeshOriginal> m_meshOriginals;   // +0x654 the deformed parts' originals
    std::vector<S_VehiclePart> m_parts;                 // +0x664 the damage parts
    int m_unknown674;                                   // +0x674 a count (the solver compares it as an int; default 20)
    int m_unknown678;                                   // +0x678 skid marks per wheel (50 when made)
    float m_unknown67C;                                 // +0x67C
    void *m_callbacks[VEHICLE_CALLBACKS];               // +0x680 VEHICLE_CALLBACK_*, called with m_callbackContext
    std::vector<S_VehicleStrut> m_axleStruts;           // +0x6C4
    std::vector<S_VehicleStrut> m_sideStruts;           // +0x6D4
    I3D_frame *m_model;                                 // +0x6E4 one reference
    // Start: the turning circle (inferred) from the wheelbase, the track and the steering angle
    // +0x92C of the first and last axles (pi / 2 for an unsteered one), x 1.2 (4 without wheels).
    float m_unknown6E8;                                 // +0x6E8
    float m_unknown6EC;                                 // +0x6EC
    float m_topSpeed;                                   // +0x6F0 m/s (inferred: the largest driven wheel's circumference / the top gear's ratio x the rev limit)
    float m_unknown6F4;                                 // +0x6F4 the body's friction with +0x228C (CollideBody)
    float m_unknown6F8;                                 // +0x6F8 its bounce (1 + the restitution) with +0x228C
    float m_throttle;                                   // +0x6FC 0..1
    float m_unknown700;                                 // +0x700 the throttle's second channel (mode 1)
    float m_unknown704;                                 // +0x704
    float m_unknown708;                                 // +0x708
    float m_unknown70C;                                 // +0x70C the handbrake's easing time (inferred)
    float m_unknown710;                                 // +0x710 the handbrake's eased time
    float m_unknown714;                                 // +0x714 Step: m_unknown85C of the step before
    float m_unknown718;                                 // +0x718 Step: m_unknown860 of the step before
    float m_unknown71C;                                 // +0x71C what the axles' shares divide
    float m_unknown720;                                 // +0x720 the brake's easing time (inferred)
    float m_unknown724;                                 // +0x724 the brake's eased time
    float m_unknown728;                                 // +0x728
    float m_unknown72C;                                 // +0x72C
    float m_unknown730;                                 // +0x730
    float m_unknown734;                                 // +0x734
    float m_unknown738;                                 // +0x738
    float m_drivenWheels;                               // +0x73C the number of WHEEL_DRIVEN wheels (not WHEEL_DUMMY)
    float m_handbrake;                                  // +0x740 0..1
    std::vector<S_VehicleExhaust> m_exhausts;           // +0x744
    int m_unknown754;                                   // +0x754 the exhausts' car effect (0: none)
    float m_unknown758;                                 // +0x758 m/s the exhausts blow below it
    float m_unknown75C;                                 // +0x75C the exhaust's blow at idle (x its direction, m/s)
    float m_unknown760;                                 // +0x760 at the rev limit
    float m_unknown764;                                 // +0x764
    float m_unknown768;                                 // +0x768
    float m_unknown76C;                                 // +0x76C
    float m_unknown770;                                 // +0x770
    float m_unknown774;                                 // +0x774 revolutions per second
    float m_unknown778;                                 // +0x778 revolutions per second
    float m_unknown77C;                                 // +0x77C revolutions per second
    float m_unknown780;                                 // +0x780 (SetGear zeroes it)
    float m_unknown784;                                 // +0x784
    float m_gravity;                                    // +0x788 m/s2
    float m_unknown78C;                                 // +0x78C (Start: 0) the engine's load eased, 0..1 (the engine layers' mix)
    float m_unknown790;                                 // +0x790 SetUnknown4f2630
    S_BodyCollisionBox *m_bodyCollision;                // +0x794 owned tree
    std::vector<S_VehicleBodyContact> m_bodyContacts;   // +0x798 this tick's
    int m_bodyContactEffects[3] = {-1, -1, -1};         // +0x7A8 car effect slots of the first three (unset until Start in the original)
    std::string m_modelName;                            // +0x7B4 the model frame's name
    std::string m_modelFileName;                        // +0x7B8 the model's +0x134 string (inferred: the file it came from)
    int m_axleCount;                                    // +0x7BC
    int m_wheelCount;                                   // +0x7C0 (two per axle)
    float m_unknown7C4;                                 // +0x7C4 what the brakes' easing adds per call (inferred: the tick's time)
    // Per gear the revolutions (per second) to shift up / down at: [gear], 1..15 used.
    float m_shiftUpRevs[VEHICLE_GEARS + 1];             // +0x7CC
    float m_shiftDownRevs[VEHICLE_GEARS + 1];           // +0x810
    float m_unknown850;                                 // +0x850
    float m_unknown854;                                 // +0x854
    float m_unknown858;                                 // +0x858
    float m_unknown85C;                                 // +0x85C (Start: 0)
    float m_unknown860;                                 // +0x860 (Start: 0)
    float m_engineRevs;                                 // +0x864 revolutions per second
    float m_previousRevs;                               // +0x868 Step: m_engineRevs before the step
    float m_unknown86C;                                 // +0x86C (Start: 0)
    float m_unknown870;                                 // +0x870 half the idle revolutions
    int m_gear;                                         // +0x874 the engaged gear (-1 reverse, 0 neutral)
    int m_requestedGear;                                // +0x878 SetGear's
    float m_unknown87C;                                 // +0x87C revolutions per second
    int m_gearCount;                                    // +0x880 1..10
    // The gears' ratios: [0] reverse, [1] neutral (the first gear's), [2..] the forward gears.
    float m_gearRatios[VEHICLE_GEARS + 2];              // +0x884
    float m_speed;                                      // +0x8C8 m/s (the solver; Settle sets it, inferred the speed)
    float m_previousSpeed;                              // +0x8CC Step: m_speed of the step before
    float m_unknown8D0;                                 // +0x8D0
    float m_unknown8D4;                                 // +0x8D4 Step: m_unknown8D0 of the step before
    float m_maxWheelCircumference;                      // +0x8D8 of the driven wheels (at least 0.01)
    float m_maxWheelRadius;                             // +0x8DC its radius (Quirk: the contact update's rest pass leaves a wheel's reach in it)
    float m_unknown8E0;                                 // +0x8E0 (Start: 0)
    float m_brake;                                      // +0x8E4 0..1
    float m_unknown8E8;                                 // +0x8E8
    float m_unknown8EC;                                 // +0x8EC revolutions per second
    float m_unknown8F0;                                 // +0x8F0 revolutions per second
    float m_unknown8F4;                                 // +0x8F4 revolutions per second
    float m_unknown8F8;                                 // +0x8F8
    float m_unknown8FC;                                 // +0x8FC (1 / the parameter)
    float m_unknown900;                                 // +0x900 (1 / the parameter)
    float m_unknown904;                                 // +0x904 (Start: 1)
    float m_unknown908;                                 // +0x908 SetUnknown4f23b0
    float m_unknown90C;                                 // +0x90C (Start: 1)
    uint32_t m_unknown910;                              // +0x910 (SetGear: bit 0x20 swaps bit 1 for bit 2)
    float m_idleRevs;                                 // +0x914 revolutions per second (inferred: the low end of the revs the sounds and shifts use)
    float m_currentMass;                                // +0x918 kg (the parameters set it to the mass; the thresholds scale with it)
    float m_mass;                                       // +0x91C kg
    float m_centreOfMassFraction[3];                    // +0x920 within the body's box, 0..1
    float m_unknown92C;                                 // +0x92C radians (Start's turning circle: inferred the steering angle)
    float m_unknown930;                                 // +0x930 radians
    float m_unknown934;                                 // +0x934 what SetSteering scales by (inferred: the largest steering angle)
    float m_unknown938;                                 // +0x938 radians
    float m_unknown93C;                                 // +0x93C radians
    float m_unknown940;                                 // +0x940 radians
    float m_unknown944;                                 // +0x944 radians
    float m_steering;                                   // +0x948 SetSteering's (x m_unknown934)
    float m_unknown94C;                                 // +0x94C the steering eased towards m_steering (UpdateSteering)
    float m_unknown950;                                 // +0x950 Step: m_unknown94C when it last moved more than 0.035
    float m_unknown954;                                 // +0x954 radians
    float m_unknown958;                                 // +0x958 (Start: 0)
    float m_unknown95C;                                 // +0x95C
    float m_unknown960;                                 // +0x960
    float m_unknown964;                                 // +0x964
    float m_unknown968;                                 // +0x968
    float m_unknown96C;                                 // +0x96C
    float m_unknown970;                                 // +0x970
    float m_unknown974;                                 // +0x974
    float m_unknown978;                                 // +0x978
    I3D_sound *m_sounds1[40];                           // +0x97C sound group 1 (LoadSounds)
    // The sound layers of group 1: [set A/B/C][value][layer], the values as the block's (rows 2..5
    // turned into revolutions per second).
    float m_layers1[3][8][VEHICLE_SOUND_LAYERS];        // +0xA1C
    float m_unknownBFC;                                 // +0xBFC revolutions per second
    float m_unknownC00;                                 // +0xC00 revolutions per second
    float m_unknownC04;                                 // +0xC04 0..1
    // Group 1's rolling sound (UpdateRollingSound): the pitch at the lowest and the top speed, the
    // lowest speed, the top speed (held to it), full volume from +0xC18.
    float m_unknownC08;                                 // +0xC08
    float m_unknownC0C;                                 // +0xC0C at most 2
    float m_unknownC10;                                 // +0xC10 m/s
    float m_unknownC14;                                 // +0xC14 m/s (the top speed)
    float m_unknownC18;                                 // +0xC18 m/s
    float m_unknownC1C;                                 // +0xC1C 0..1
    float m_unknownC20[6];                              // +0xC20 constants and fractions of the top speed ([0..4] the rim sound's, as +0xC08..)
    float m_crash1Thresholds[VEHICLE_CRASH_SOUNDS];     // +0xC38 m/s x mass
    I3D_sound *m_sounds2[41];                           // +0xC48 sound group 2 (LoadSounds)
    float m_unknownCEC;                                 // +0xCEC group 2's single engine loop: the pitch at idle
    float m_unknownCF0;                                 // +0xCF0 at most 2; the pitch at the limit
    float m_unknownCF4;                                 // +0xCF4 revolutions per second (the single loop at full volume)
    float m_unknownCF8;                                 // +0xCF8 0..1 (the single loop's volume)
    float m_layers2[3][8][VEHICLE_SOUND_LAYERS];        // +0xCFC group 2's layers
    float m_unknownEDC;                                 // +0xEDC revolutions per second
    float m_unknownEE0;                                 // +0xEE0 revolutions per second
    float m_unknownEE4;                                 // +0xEE4 0..1
    float m_unknownEE8[4];                              // +0xEE8
    float m_unknownEF8;                                 // +0xEF8 group 2's rolling sound (as +0xC08..)
    float m_unknownEFC;                                 // +0xEFC at most 2
    float m_unknownF00;                                 // +0xF00 m/s
    float m_unknownF04;                                 // +0xF04 m/s (the top speed)
    float m_unknownF08;                                 // +0xF08 m/s
    float m_unknownF0C;                                 // +0xF0C 0..1
    float m_unknownF10[4];                              // +0xF10
    float m_unknownF20[6];                              // +0xF20 constants and fractions of the top speed ([0..4] group 2's rim sound)
    float m_crash2Thresholds[VEHICLE_CRASH_SOUNDS];     // +0xF38 m/s x mass
    // The engine sound's fade-in (s, up to 0.3; SetEngine: 5000 when stopped at once, 0 when
    // started; SwitchEngine: 0 while the starter plays).
    float m_unknownF48;                                 // +0xF48
    float m_unknownF4C;                                 // +0xF4C the wheel planes' clock: past +0xF50 half the wheels cast again (Start: 0)
    float m_unknownF50;                                 // +0xF50 (Start: 0)
    float m_unknownF54;                                 // +0xF54 (0.028 when made)
    I3D_frame *m_steeringWheel;                         // +0xF58 the "SWHEEL" visual (null without one)
    float m_fuel;                                       // +0xF5C litres
    S_VehicleWheel *m_wheelList;                        // +0xF60
    S_VehicleWheel **m_wheels;                          // +0xF64 [m_wheelCount]: per axle the left wheel, then the right (null when missing)
    float m_unknownF68;                                 // +0xF68 (Start: 0)
    // Feature bits (0x8000DFF9 when made): 0x1 the struts shown and moved, 0x200 the body contacts'
    // effects, 0x4000 the engine and the gears need +0x610 / +0x63C above 0.
    uint32_t m_unknownF6C;                              // +0xF6C
    uint32_t m_unknownF70;                              // +0xF70 (UpdateStruts compares bit 0 with +0xF6C's; 0x8000DFF9 when made)
    uint32_t m_unknownF74;                              // +0xF74 (0x1: no skid marks; 0x2: Setup accepts no seat / wheels, ApplyParams no driven wheel, no turning circle)
    void *m_callbackContext;                            // +0xF78 (the car)
    int m_unknownF7C;                                   // +0xF7C
    uint32_t m_unknownF80[10];                          // +0xF80 (Start: 0)
    std::vector<S_VehicleSeat> m_seats;                 // +0xFA8
    std::vector<S_VehicleDoor> m_doors;                 // +0xFB8
    std::vector<S_VehicleBallast> m_ballasts;           // +0xFC8
    I3D_frame *m_body;                                  // +0xFD8 the "BODY" frame
    S_VehicleParams m_params;                           // +0xFDC the stored block
    float m_revLimit;                                   // +0x221C revolutions per second (inferred)
    float m_tankSize;                                   // +0x2220 litres
    float m_unknown2224;                                // +0x2224 (Start: 0)
    std::vector<S_vector4> m_specialCollisionCorners;   // +0x2228 two per special volume (its box's min and max)
    std::vector<S_VehicleSpecialCollision> m_specialCollisions;   // +0x2238
    std::vector<S_VehicleCollisionDesc> m_specialCollisionDescs;  // +0x2248
    // Pointers to m_specialVolumes (Start; Stop empties it).
    std::vector<S_CollisionVolume *> m_specialVolumeList;   // +0x2258
    // The special volumes (Start; not moved once added to the world).
    std::vector<S_CollisionVolume> m_specialVolumes;    // +0x2268
    S_VehicleCollisionDesc m_bodyCollisionDesc;         // +0x2278 the CENTRE box's
    uint16_t m_substeps;                                // +0x2280 Integrate: the solver steps of this step
    uint16_t m_unknown2282;                             // +0x2282 (Integrate: 0)
    uint16_t m_sinceRimScrape;                          // +0x2284 ms since a wheel scraped on its rim (Update: at most 1000; Start, Restart: 30000)
    uint8_t m_wheelOnWater;                             // +0x2286 a wheel's ground or a body point met COLLISION_FLAG_WATER (Start: 0)
    uint8_t m_unknown2287;                              // +0x2287 (Start: 0)
    uint8_t m_unknown2288;                              // +0x2288 (Start: 1)
    uint8_t m_unknown2289;                              // +0x2289 (1 when made)
    bool m_tracked;                                     // +0x228A Start: track strips on either side
    uint8_t m_unknown228B;                              // +0x228B (Start: 0)
    uint8_t m_unknown228C;                              // +0x228C the body's own friction and bounce (+0x6F4) instead of the surface's
    uint8_t m_unknown228D;                              // +0x228D
    uint8_t m_unknown228E;                              // +0x228E (Start: 0) the handbrake sound played for this pull
    uint8_t m_unknown228F;                              // +0x228F
    uint8_t m_unknown2290;                              // +0x2290
    uint8_t m_driven;                                   // +0x2291 a driver sits in the car (C_Car::SetDriver; Start: 0)
    uint8_t m_unknown2292;                              // +0x2292 (1 when made)
    uint8_t m_unknown2293;                              // +0x2293 (Start: 0)
    // The switched loops' (SwitchLoopSoundA / B; only cleared in this exe: Start, Restart, Replace).
    uint8_t m_unknown2294;                              // +0x2294 (Start: 0)
    uint8_t m_unknown2295;                              // +0x2295 (Start: 0)
    bool m_soundsOn;                                    // +0x2296 (on when made; EnableSounds)
    uint8_t m_unknown2297;                              // +0x2297 the sound group wanted (1: m_sounds1)
    uint8_t m_unknown2298;                              // +0x2298 the sound group playing (1: m_sounds1)
    uint8_t m_unknown2299;                              // +0x2299 (Start: 0)
    uint8_t m_unknown229A;                              // +0x229A (Start: 0)
    uint8_t m_unknown229B;                              // +0x229B the brake applies at once (no easing)
    uint8_t m_unknown229C;                              // +0x229C SetUnknown4f23b0 uses the curve
    // (Inferred: an automatic gearbox; in reverse the throttle and brake swap.) On after Setup.
    bool m_automatic;                                   // +0x229D
    uint8_t m_unknown229E;                              // +0x229E (Start: 0)
    uint8_t m_unknown229F;                              // +0x229F (Start: 0)
    uint8_t m_unknown22A0;                              // +0x22A0 (SetEngine clears it)
    // The engine is starting or running (SwitchEngine / SetEngine's instant start set it, a stop
    // clears it).
    uint8_t m_unknown22A2;                              // +0x22A2
    uint8_t m_layers1AFlags[VEHICLE_SOUND_LAYERS];      // +0x22A3
    uint8_t m_layers1BFlags[VEHICLE_SOUND_LAYERS];      // +0x22A8
    uint8_t m_crash1Flags[VEHICLE_CRASH_SOUNDS];        // +0x22AD
    uint8_t m_layers2AFlags[VEHICLE_SOUND_LAYERS];      // +0x22B1
    uint8_t m_layers2BFlags[VEHICLE_SOUND_LAYERS];      // +0x22B6
    uint8_t m_crash2Flags[VEHICLE_CRASH_SOUNDS];        // +0x22BB
    bool m_engineOn;                                    // +0x22BF
    uint8_t m_unknown22C0;                              // +0x22C0 (ApplyImpulse sets it)
    uint8_t m_unknown22C1;                              // +0x22C1 (Start: 0)
    uint8_t m_unknown22C2;                              // +0x22C2 (Start: 0)
    // SetVolumesInWorld / SetSpecialVolumes (all off until Start in the original).
    bool m_specialVolumesOn = false;                    // +0x22C3 m_specialVolumes instead of m_bodyVolume
    bool m_mainVolumesInWorld = false;                  // +0x22C4
    bool m_volume490InWorld = false;                    // +0x22C5
    uint8_t m_unknown22C6;                              // +0x22C6 (SetGear sets it with +0x910 bit 0x20)
    uint8_t m_unknown22C7;                              // +0x22C7 SetHandbrake's second argument while on
    uint8_t m_unknown22C8;                              // +0x22C8 which half of the wheels casts again (flips with +0xF4C; Start: 0)

    // The sectors the lights are added to (0x0089c1b0; Release clears it; nothing in this exe
    // fills it: its add and remove functions 0x004f6350 / 0x004f6550 are unreferenced).
    static std::vector<I3D_sector *> s_lightSectors;
    // The sectors the sounds are added to (0x0089c1c0; nothing in this exe fills it either: its add
    // function 0x004f65b0 is unreferenced).
    static std::vector<I3D_sector *> s_soundSectors;
    // The sounds' volume scale (0x0083bed4 g_vehicleSoundVolume): 1, but the engine's fade-in
    // while UpdateEngineSound runs.
    static float s_soundVolume;
};
