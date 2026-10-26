// The car's parameter block (S_VehicleParams) and its form in the mission files.
//
// A car keeps the block in its vehicle (C_Vehicle +0xFDC = C_Car +0x106C). ApplyParams
// (0x004ed270) checks each value and copies it into the vehicle and its wheels. In the
// mission file (the car's actor properties) the block is a packed blob of 0x124B bytes: the
// fields in order without padding, and the per-axle and per-sound-layer arrays stored one
// record at a time. The blob is run-length coded ("COMP") behind a version (3) and its size.
//
// Most fields are not identified yet; their comments give what ApplyParams checks and where
// the value goes (v = the vehicle, w = each wheel of the axle). A name of 16 characters has no
// terminator: it reads on into the next field (as in the original).
#pragma once

#include <cstdint>
#include <vector>

constexpr int VEHICLE_AXLES = 12;                       // the per-axle arrays (two wheels each)
constexpr int VEHICLE_GEAR_SLOTS = 16;                  // reverse and 15 forward gears
constexpr int VEHICLE_SOUND_LAYERS = 5;
constexpr int VEHICLE_CRASH_SOUNDS = 4;
constexpr int VEHICLE_SOUND_NAME = 16;                  // the sound / texture name slots
constexpr uint32_t VEHICLE_PARAMS_BLOB_SIZE = 0x124B;
constexpr uint32_t VEHICLE_PROPERTIES_VERSION = 3;

// The sounds of one group: the named sounds, two sets of layers with 8 values and a flag
// each, a set without flags, more named sounds and four crash sounds (two names, a value, a
// flag). The two groups differ in their extra values (see S_VehicleParams).
struct S_VehicleParams {                                // 0x1240 bytes
    float mass;                                         // +0x000 kg (> 0; v+0x91C, the weight v+0x204)
    // The engine (ApplyParams): [2] revolutions per minute (rpm; v+0x87C per second), [3] the rev
    // limit (rpm, above [2]), [4] the idle revolutions (rpm, up to [2]), [18] the number of
    // forward gears (1..10).
    float unknown004[19];                               // +0x004
    // Per gear (the blob stores the three together, gear by gear): the ratio ([0] reverse,
    // [1..15] the forward gears) and the revolutions to shift up / down at, in percent of
    // idle..limit.
    float gearRatios[VEHICLE_GEAR_SLOTS];               // +0x050
    float shiftUpPercent[VEHICLE_GEAR_SLOTS];           // +0x090
    float shiftDownPercent[VEHICLE_GEAR_SLOTS];         // +0x0D0
    float unknown110[13];                               // +0x110
    float centreOfMass[3];                              // +0x144 within the body's box, 0..1 per axis (v+0x920)
    float gravity;                                      // +0x150 m/s2 (> 0; v+0x788)
    float tankSize;                                     // +0x154 litres (>= 1; v+0x2220)
    float unknown158;                                   // +0x158

    // --- per axle (the blob stores each axle's values together, 0x70 bytes) -------------
    float axleUnknown15C[VEHICLE_AXLES];                // +0x15C > 0, x1000 -> w+0x160
    float axleUnknown18C[VEHICLE_AXLES];                // +0x18C >= 0.009 -> w+0x164
    float axleUnknown1BC[VEHICLE_AXLES];                // +0x1BC > 0 -> w+0x170
    float axleUnknown1EC[VEHICLE_AXLES];                // +0x1EC > 0 -> w+0x174
    float axleUnknown21C[VEHICLE_AXLES];                // +0x21C degrees -10..10 -> w+0x178 (radians)
    float axleUnknown24C[VEHICLE_AXLES];                // +0x24C degrees -10..10 -> w+0x150 (radians)
    float axleUnknown27C[VEHICLE_AXLES];                // +0x27C degrees >= 0 -> w+0x154 (radians)
    float axleUnknown2AC[VEHICLE_AXLES];                // +0x2AC degrees >= 0 -> w+0x158 (radians)
    int axleTyreType[VEHICLE_AXLES];                    // +0x2DC tables\tyres.bin type id (w+0x17C)
    float axleUnknown30C[VEHICLE_AXLES];                // +0x30C > 0, x1000 -> w+0x18C
    float axleUnknown33C[VEHICLE_AXLES];                // +0x33C degrees >= 1 -> w+0x194 (radians)
    float axleUnknown36C[VEHICLE_AXLES];                // +0x36C degrees >= 1 -> w+0x198 (radians)
    float axleUnknown39C[VEHICLE_AXLES];                // +0x39C 0..1 -> w+0x19C
    float axleUnknown3CC[VEHICLE_AXLES];                // +0x3CC 0..1 -> w+0x190
    float axleUnknown3FC[VEHICLE_AXLES];                // +0x3FC > 0 -> w+0x1B8
    float axleUnknown42C[VEHICLE_AXLES];                // +0x42C > 0 -> w+0x1BC
    float axleUnknown45C[VEHICLE_AXLES];                // +0x45C >= 0 -> w+0x1C0
    float axleUnknown48C[VEHICLE_AXLES];                // +0x48C > 0, x1000 -> w+0x1C4
    float axleUnknown4BC[VEHICLE_AXLES];                // +0x4BC > 0 -> w+0x1C8
    float axleUnknown4EC[VEHICLE_AXLES];                // +0x4EC > 0 -> w+0x1CC
    float axleUnknown51C[VEHICLE_AXLES];                // +0x51C >= 0 -> w+0x1D0
    float axleUnknown54C[VEHICLE_AXLES];                // +0x54C percent 0..100 -> w+0x1F8 (0..1)
    float axleLeftShare[VEHICLE_AXLES];                 // +0x57C percent >= 0 of v+0x71C -> the left wheel's +0x1F4
    float axleRightShare[VEHICLE_AXLES];                // +0x5AC the same for the right wheel
    float axleUnknown5DC[VEHICLE_AXLES];                // +0x5DC degrees 1..90 -> w+0x21C (radians)
    float axleUnknown60C[VEHICLE_AXLES];                // +0x60C degrees 1..90, <= +0x63C -> w+0x220 (radians)
    float axleUnknown63C[VEHICLE_AXLES];                // +0x63C degrees 1..90 -> w+0x224 (radians)

    float inertia[3];                                   // +0x66C (> 0; v gets 1 / value: +0x234, +0x238, +0x230)
    float unknown678;                                   // +0x678 >= 0 -> v+0x71C (the axle shares' base)
    float unknown67C;                                   // +0x67C > 0 -> v+0x720
    float unknown680;                                   // +0x680 >= 0 -> v+0x708
    float unknown684;                                   // +0x684 > 0 -> v+0x70C
    float unknown688;                                   // +0x688 >= 0 -> v+0x6F4
    float unknown68C;                                   // +0x68C >= 1 -> v+0x6F8
    int unknown690;                                     // +0x690 -> v+0x674 (a count; Setup's default 20)
    float unknown694;                                   // +0x694 -> v+0x518
    uint8_t unknown698;                                 // +0x698 -> v+0x2289
    float unknown69C;                                   // +0x69C -> v+0x510
    float unknown6A0;                                   // +0x6A0 -> v+0x514
    int unknown6A4;                                     // +0x6A4 >= 0 -> v+0x754
    float unknown6A8;                                   // +0x6A8 km/h >= 0 -> v+0x758 (m/s)
    float unknown6AC;                                   // +0x6AC >= 0 -> v+0x75C
    float unknown6B0;                                   // +0x6B0 >= +0x6AC -> v+0x760
    float unknown6B4;                                   // +0x6B4 degrees -> v+0x504 (radians)
    float unknown6B8[3];                                // +0x6B8 -> v+0x140
    float unknown6C4;                                   // +0x6C4 -> v+0x67C
    float unknown6C8[5];                                // +0x6C8 -> every wheel's +0x24C, +0x248, +0x254, +0x250, +0x258
    float unknown6DC[20];                               // +0x6DC
    float unknown72C[21];                               // +0x72C
    char glassTexture[VEHICLE_SOUND_NAME];              // +0x780 the glass parts' cracked texture, e.g. "2ft19r.bmp"
    char glassTextureAlpha[VEHICLE_SOUND_NAME];         // +0x790 its alpha, e.g. "2ft19r+.bmp"
    float unknown7A0[61];                               // +0x7A0

    // --- sound group 1 (editor defaults like "engine_on.wav"; most cars leave it empty) --
    char sounds1[3][VEHICLE_SOUND_NAME];                // +0x894 e.g. on / off / "engine_on_nf.wav"
    char layers1A[VEHICLE_SOUND_LAYERS][VEHICLE_SOUND_NAME];   // +0x8C4
    char layers1B[VEHICLE_SOUND_LAYERS][VEHICLE_SOUND_NAME];   // +0x914
    char layers1C[VEHICLE_SOUND_LAYERS][VEHICLE_SOUND_NAME];   // +0x964
    char sound1_9B4[VEHICLE_SOUND_NAME];                // +0x9B4 (ApplyParams reads +0xCF4 only when set)
    char sound1_9C4[VEHICLE_SOUND_NAME];                // +0x9C4
    char sound1_9D4[VEHICLE_SOUND_NAME];                // +0x9D4
    char sounds1_9E4[9][VEHICLE_SOUND_NAME];            // +0x9E4
    char crash1A[VEHICLE_CRASH_SOUNDS][VEHICLE_SOUND_NAME];    // +0xA74
    char crash1B[VEHICLE_CRASH_SOUNDS][VEHICLE_SOUND_NAME];    // +0xAB4
    char sound1_AF4[VEHICLE_SOUND_NAME];                // +0xAF4
    char sound1_B04[VEHICLE_SOUND_NAME];                // +0xB04
    float layers1AValues[8][VEHICLE_SOUND_LAYERS];      // +0xB14
    float layers1BValues[8][VEHICLE_SOUND_LAYERS];      // +0xBB4
    float layers1CValues[8][VEHICLE_SOUND_LAYERS];      // +0xC54
    float unknownCF4;                                   // +0xCF4 0..100.001
    float unknownCF8;                                   // +0xCF8
    float unknownCFC[5];                                // +0xCFC
    float crash1Values[VEHICLE_CRASH_SOUNDS];           // +0xD10 km/h (m/s x mass -> v+0xC38..)

    // --- sound group 2 (the car's own sounds: "blitz_on.wav", "blitz_1.wav", gears, skid,
    // "pneu.wav", "carcrx2_a.wav", ...) ----------------------------------------------------
    char sounds2[4][VEHICLE_SOUND_NAME];                // +0xD20 e.g. on / off / "bad_truck.wav" / -
    char layers2A[VEHICLE_SOUND_LAYERS][VEHICLE_SOUND_NAME];   // +0xD60 e.g. "blitz_1.wav"..
    char layers2B[VEHICLE_SOUND_LAYERS][VEHICLE_SOUND_NAME];   // +0xDB0
    char layers2C[VEHICLE_SOUND_LAYERS][VEHICLE_SOUND_NAME];   // +0xE00
    char sound2_E50[VEHICLE_SOUND_NAME];                // +0xE50 e.g. "blitz_0.wav"
    char sound2_E60[VEHICLE_SOUND_NAME];                // +0xE60 e.g. "afodrive.wav"
    char sound2_E70[VEHICLE_SOUND_NAME];                // +0xE70
    char sounds2_E80[9][VEHICLE_SOUND_NAME];            // +0xE80 e.g. handbrake, gears, skid, .., "pneu.wav"
    char crash2A[VEHICLE_CRASH_SOUNDS][VEHICLE_SOUND_NAME];    // +0xF10 e.g. "carcrx2_a.wav"
    char crash2B[VEHICLE_CRASH_SOUNDS][VEHICLE_SOUND_NAME];    // +0xF50
    char sound2_F90[VEHICLE_SOUND_NAME];                // +0xF90
    char sound2_FA0[VEHICLE_SOUND_NAME];                // +0xFA0
    float unknownFB0[4];                                // +0xFB0
    float layers2AValues[8][VEHICLE_SOUND_LAYERS];      // +0xFC0
    float layers2BValues[8][VEHICLE_SOUND_LAYERS];      // +0x1060
    float layers2CValues[8][VEHICLE_SOUND_LAYERS];      // +0x1100
    float unknown11A0[6];                               // +0x11A0
    float unknown11B8[9];                               // +0x11B8
    float crash2Values[VEHICLE_CRASH_SOUNDS];           // +0x11DC km/h (m/s x mass -> v+0xF38..)

    // --- flags ----------------------------------------------------------------------------
    uint8_t axleFlag2[VEHICLE_AXLES];                   // +0x11EC sets the wheels' flag 0x2
    uint8_t axleFlag1[VEHICLE_AXLES];                   // +0x11F8 sets the wheels' flag 0x1
    uint8_t axleFlag4[VEHICLE_AXLES];                   // +0x1204 sets the wheels' flag 0x4
    uint8_t axleFlag800[VEHICLE_AXLES];                 // +0x1210 sets the wheels' flag 0x800
    uint8_t unknown121C;                                // +0x121C -> v+0x228C
    uint8_t unknown121D;                                // +0x121D -> v+0x228F
    uint8_t unknown121E;                                // +0x121E -> v+0x2290
    uint8_t unknown121F;                                // +0x121F set: v+0x550 = 2 (else 0)
    uint8_t unknown1220;                                // +0x1220 -> v+0x228D
    uint8_t layers1AFlags[VEHICLE_SOUND_LAYERS];        // +0x1221
    uint8_t layers1BFlags[VEHICLE_SOUND_LAYERS];        // +0x1226
    uint8_t crash1Flags[VEHICLE_CRASH_SOUNDS];          // +0x122B
    uint8_t layers2AFlags[VEHICLE_SOUND_LAYERS];        // +0x122F
    uint8_t layers2BFlags[VEHICLE_SOUND_LAYERS];        // +0x1234
    uint8_t crash2Flags[VEHICLE_CRASH_SOUNDS];          // +0x1239
};

// Run-length codes `size` bytes (0x004d0110, packing): "COMP", the size, the coded size (the
// header included) and the escape byte (the least frequent byte), then the data: runs of 4..255
// equal bytes, and every escape byte, as {escape, byte, count}; other bytes as they are.
bool PackVehicleBlob(const uint8_t *data, uint32_t size, std::vector<uint8_t> &packed);
// The reverse (0x004d0110, unpacking); `data` gets the size the header gives. False when the
// header is not "COMP".
bool UnpackVehicleBlob(const uint8_t *packed, uint32_t packedSize, std::vector<uint8_t> &data);

// The blob (VEHICLE_PARAMS_BLOB_SIZE bytes) <-> the block. With `tyreIndices` the blob holds
// each axle's tyre type as its index in tables\tyres.bin instead of its id (no caller in the
// game sets it).
void __stdcall ReadVehicleParams(const uint8_t *blob, S_VehicleParams &params, bool tyreIndices);    // 0x004d7d80
void __stdcall WriteVehicleParams(uint8_t *blob, const S_VehicleParams &params, bool tyreIndices);   // 0x004d6c50
