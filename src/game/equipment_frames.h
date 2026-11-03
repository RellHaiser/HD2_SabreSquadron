// The frames and the helpers the human's equipment models (game/human_equipment.cpp) and the
// menus' soldier (game/menu_soldier.cpp, part 9e-i, its own copies of the offsets and the code)
// share: the models' offsets from their joints, the joints' and the item models' frame names,
// the holster sets and the frames they hang on, the rotations of the long guns.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ls3d/math.h"

class I3D_frame;

namespace equipment {

// Offsets of the models from their joints (set by the static initializers 0x0078cd60,
// 0x0078cd90 and 0x0078cdc0; the soldier's copies by 0x007d0230, 0x007d0260 and 0x007d0290).
extern const S_vector g_longGunModelOffset;             // 0x0098ada0 (the soldier's 0x0098afc0)
extern const S_vector g_headModelOffset;                // 0x0098ad90 (0x0098afb0)
extern const S_vector g_backpackModelOffset;            // 0x0098ad80 (0x0098afa0)

// Frames of the human model.
extern const char *const HAND_DUMMY1;                   // "HW1"
extern const char *const JOINT_C;                       // "c": most holster frames
extern const char *const JOINT_D;                       // "d": the back
extern const char *const JOINT_F;                       // "f": the head
extern const char *const JOINT_O;                       // "o"
extern const char *const JOINT_U;                       // "u"
extern const char *const JOINT_Q;                       // "q"
// Frames of the item models.
extern const char *const BACKLINK_DUMMY;                // "backlink": where a long gun hangs on the back
extern const char *const MAGAZINE_FRAME;                // "mag1"

// Models with the pouches, holsters and grenades of the uniforms.
extern const char *const g_holsterSets[];               // 0x0084f7e8

enum E_HolsterNation {
    HOLSTER_NATION_BRITISH = 0,
    HOLSTER_NATION_GERMAN = 1,
    HOLSTER_NATION_RUSSIAN = 2,
    HOLSTER_NATION_JAPANESE = 3,
    HOLSTER_NATION_ITALIAN = 4,
    HOLSTER_NATION_AMERICAN = 5,
    HOLSTER_NATION_CIVILIAN = 6,
};

// Holster set of each human model.
struct S_HolsterModel {                                 // 12 bytes
    const char *model;                                  // file name without path and extension
    int set;                                            // g_holsterSets index, -1: none
    int nation;                                         // E_HolsterNation
};

extern const S_HolsterModel g_holsterModels[];          // 0x0084f810
extern const std::size_t g_holsterModelCount;           // (its byte size at 0x0084fbd0)

// Frames of the holster sets and the slots whose items they show.
struct S_HolsterDummy {                                 // 8 bytes
    int slot;                                           // HOLSTER_SLOT_COUNT ends the table
    char name[4];
};

extern const S_HolsterDummy g_holsterDummies[];         // 0x0084fbd8

// The holster slot whose frames hang on the back ("d") instead of the belt ("c").
constexpr int HOLSTER_SLOT_BACK = 4;

// Matrix of `axis` rotated by `angle`.
S_matrix Rotation(const S_vector &axis, float angle);
// Turns a frame by `angle` about `axis` in its own space (the local matrix times the rotation;
// the scale is kept).
void Rotate(I3D_frame *frame, const S_vector &axis, float angle);
// Turns a frame about the cross product of its y and x axes (its local -z).
void RotateAboutYxX(I3D_frame *frame, float angle);
// Position that brings a model's "backlink" dummy to its parent joint: minus the dummy's
// position, through the local matrix of the dummy's parent.
S_vector BacklinkOffset(I3D_frame *backlink);
// Joint of the body that a holster frame hangs on.
I3D_frame *HolsterJoint(const S_HolsterDummy &dummy, const S_HolsterModel &model, I3D_frame *jointC,
                        I3D_frame *jointD, I3D_frame *jointO);
// The holster set entry of a model (its file name without path and extension; none: null).
const S_HolsterModel *FindHolsterModel(const char *model);

} // namespace equipment
