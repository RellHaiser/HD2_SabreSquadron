#include "game/equipment_frames.h"

#include <cstring>

#include "game/human_equipment.h"
#include "ls3d/i3d.h"

namespace equipment {

// (The initializers' bits: 0x3e33ce64 0x3e38e150 0xbe0883ba, 0x3c891700 0x3e0da986 0xbc8f6d91,
// 0xba79e432 0x3e20edc4 0xbe07878b. Fixed in part 9e-i: the port's first values were a little off.)
const S_vector g_longGunModelOffset(0.175592005f, 0.180546999f, -0.133314997f);   // 0x0098ada0
const S_vector g_headModelOffset(0.0167346001f, 0.138341993f, -0.0175083f);   // 0x0098ad90
const S_vector g_backpackModelOffset(-0.000953259994f, 0.157157004f, -0.132352993f);   // 0x0098ad80

const char *const HAND_DUMMY1 = "HW1";
const char *const JOINT_C = "c";
const char *const JOINT_D = "d";
const char *const JOINT_F = "f";
const char *const JOINT_O = "o";
const char *const JOINT_U = "u";
const char *const JOINT_Q = "q";
const char *const BACKLINK_DUMMY = "backlink";
const char *const MAGAZINE_FRAME = "mag1";

const char *const g_holsterSets[] = {                   // 0x0084f7e8
    "def_holster_set", "en_vyb01", "en_vyb02", "en_vyb03", "ge_vyb01",
    "it_vyb01", "ja_vyb", "ru_vyb", "us_vyb", "us_vyb2",
};

const S_HolsterModel g_holsterModels[] = {              // 0x0084f810 (byte size at 0x0084fbd0)
    {"CI01", -1, HOLSTER_NATION_CIVILIAN},
    {"CI02", -1, HOLSTER_NATION_CIVILIAN},
    {"CI03", -1, HOLSTER_NATION_CIVILIAN},
    {"CI04", -1, HOLSTER_NATION_CIVILIAN},
    {"CI05", -1, HOLSTER_NATION_CIVILIAN},
    {"CI06", -1, HOLSTER_NATION_CIVILIAN},
    {"CI07", -1, HOLSTER_NATION_CIVILIAN},
    {"CI08", -1, HOLSTER_NATION_CIVILIAN},
    {"CI09", -1, HOLSTER_NATION_CIVILIAN},
    {"CI10", -1, HOLSTER_NATION_CIVILIAN},
    {"CI11", -1, HOLSTER_NATION_CIVILIAN},
    {"CI12", -1, HOLSTER_NATION_CIVILIAN},
    {"CI13", -1, HOLSTER_NATION_CIVILIAN},
    {"CI14", -1, HOLSTER_NATION_CIVILIAN},
    {"CI15", -1, HOLSTER_NATION_CIVILIAN},
    {"CI16", -1, HOLSTER_NATION_CIVILIAN},
    {"NA01", -1, HOLSTER_NATION_CIVILIAN},
    {"EN01", 1, HOLSTER_NATION_BRITISH},
    {"EN02", 1, HOLSTER_NATION_BRITISH},
    {"EN03", -1, HOLSTER_NATION_BRITISH},
    {"EN04", 2, HOLSTER_NATION_BRITISH},
    {"EN05", -1, HOLSTER_NATION_BRITISH},
    {"EN06", 3, HOLSTER_NATION_BRITISH},
    {"EN07", 2, HOLSTER_NATION_BRITISH},
    {"EN08", 1, HOLSTER_NATION_BRITISH},
    {"EN09", 1, HOLSTER_NATION_BRITISH},
    {"EN10", -1, HOLSTER_NATION_BRITISH},
    {"EN11", -1, HOLSTER_NATION_BRITISH},
    {"EN12", 1, HOLSTER_NATION_BRITISH},
    {"EN13", -1, HOLSTER_NATION_BRITISH},
    {"EN14", -1, HOLSTER_NATION_BRITISH},
    {"NPC_EN08", 1, HOLSTER_NATION_BRITISH},
    {"GE01A", 4, HOLSTER_NATION_GERMAN},
    {"GE01B", 4, HOLSTER_NATION_GERMAN},
    {"GE02A", 4, HOLSTER_NATION_GERMAN},
    {"GE02B", 4, HOLSTER_NATION_GERMAN},
    {"GE02C", 4, HOLSTER_NATION_GERMAN},
    {"GE02D", 4, HOLSTER_NATION_GERMAN},
    {"GE03", 4, HOLSTER_NATION_GERMAN},
    {"GE03B", 4, HOLSTER_NATION_GERMAN},
    {"GE04", 4, HOLSTER_NATION_GERMAN},
    {"GE04B", 4, HOLSTER_NATION_GERMAN},
    {"GE05", 4, HOLSTER_NATION_GERMAN},
    {"GE05B", 4, HOLSTER_NATION_GERMAN},
    {"GE06", 4, HOLSTER_NATION_GERMAN},
    {"GE07", 4, HOLSTER_NATION_GERMAN},
    {"GE08", -1, HOLSTER_NATION_GERMAN},
    {"GE09", 4, HOLSTER_NATION_GERMAN},
    {"GE09B", 4, HOLSTER_NATION_GERMAN},
    {"GE10", 4, HOLSTER_NATION_GERMAN},
    {"GE10B", 4, HOLSTER_NATION_GERMAN},
    {"GE11", 4, HOLSTER_NATION_GERMAN},
    {"GE13", 4, HOLSTER_NATION_GERMAN},
    {"GE13B", 4, HOLSTER_NATION_GERMAN},
    {"GE14", 4, HOLSTER_NATION_GERMAN},
    {"GE15", 4, HOLSTER_NATION_GERMAN},
    {"GE21", -1, HOLSTER_NATION_GERMAN},
    {"GE21b", -1, HOLSTER_NATION_GERMAN},
    {"GE27", 4, HOLSTER_NATION_GERMAN},
    {"GE27B", -1, HOLSTER_NATION_GERMAN},
    {"GE28", -1, HOLSTER_NATION_GERMAN},
    {"GE28B", -1, HOLSTER_NATION_GERMAN},
    {"GE31", 4, HOLSTER_NATION_GERMAN},
    {"GE31B", 4, HOLSTER_NATION_GERMAN},
    {"GE32", -1, HOLSTER_NATION_GERMAN},
    {"NPC_GE05b", 4, HOLSTER_NATION_GERMAN},
    {"NPC_GE10b", 4, HOLSTER_NATION_GERMAN},
    {"IT01", 5, HOLSTER_NATION_ITALIAN},
    {"IT02", 5, HOLSTER_NATION_ITALIAN},
    {"IT03", -1, HOLSTER_NATION_ITALIAN},
    {"IT04", 5, HOLSTER_NATION_ITALIAN},
    {"IT05", 5, HOLSTER_NATION_ITALIAN},
    {"JA01", 6, HOLSTER_NATION_JAPANESE},
    {"JA02", 6, HOLSTER_NATION_JAPANESE},
    {"RU01", 7, HOLSTER_NATION_RUSSIAN},
    {"RU02", 7, HOLSTER_NATION_RUSSIAN},
    {"US01", 8, HOLSTER_NATION_AMERICAN},
    {"US02", 8, HOLSTER_NATION_AMERICAN},
    {"US03", 9, HOLSTER_NATION_AMERICAN},
    {"US04", 8, HOLSTER_NATION_AMERICAN},
};
const std::size_t g_holsterModelCount = sizeof g_holsterModels / sizeof g_holsterModels[0];

const S_HolsterDummy g_holsterDummies[] = {             // 0x0084fbd8
    {0, "1"}, {1, "2"}, {2, "41"}, {2, "42"}, {2, "43"}, {3, "51"}, {3, "52"}, {4, "6"},
    {5, "7"}, {6, "81"}, {6, "82"}, {6, "83"}, {7, "91"}, {7, "92"}, {7, "93"}, {8, "31"},
    {8, "32"}, {8, "33"}, {9, "aa"}, {10, "bb"}, {11, "cc"}, {12, "dd"}, {13, "ee1"}, {13, "ee2"},
    {HOLSTER_SLOT_COUNT, ""},
};

// Matrix of `axis` rotated by `angle`.
S_matrix Rotation(const S_vector &axis, float angle)
{
    S_quat rot;
    rot.Make(axis, angle);
    return rot.RotationMatrix();
}

// Turns a frame by `angle` about `axis` in its own space (the local matrix times the
// rotation; the scale is kept).
void Rotate(I3D_frame *frame, const S_vector &axis, float angle)
{
    const S_matrix local = frame->GetLocalMatrix();
    frame->SetLocalMatrix(local * Rotation(axis, angle));
}

// Turns a frame about the cross product of its y and x axes (its local -z).
void RotateAboutYxX(I3D_frame *frame, float angle)
{
    const S_matrix local = frame->GetLocalMatrix();
    frame->SetLocalMatrix(local * Rotation(local(1).Cross(local(0)), angle));
}

// Position that brings a model's "backlink" dummy to its parent joint: minus the dummy's
// position, through the local matrix of the dummy's parent.
S_vector BacklinkOffset(I3D_frame *backlink)
{
    I3D_frame *parent = backlink->GetParent();
    return -backlink->GetPos() * parent->GetLocalMatrix();
}

// Joint of the body that a holster frame hangs on.
I3D_frame *HolsterJoint(const S_HolsterDummy &dummy, const S_HolsterModel &model, I3D_frame *jointC,
                        I3D_frame *jointD, I3D_frame *jointO)
{
    if (dummy.slot == HOLSTER_SLOT_BACK)
        return jointD;
    const char *name = dummy.name;
    if (_stricmp("EN06", model.model) == 0) {
        if (_stricmp(name, "1") == 0)
            return jointO;
        if (_stricmp(name, "83") == 0 || _stricmp(name, "51") == 0)
            return jointD;
        return jointC;
    }
    if (_stricmp("US03", model.model) == 0) {
        if (_stricmp(name, "81") == 0 || _stricmp(name, "82") == 0 || _stricmp(name, "83") == 0)
            return jointD;
        return jointC;
    }
    if (_stricmp(name, "83") == 0)
        return jointD;
    if (_stricmp(name, "42") == 0 && model.nation == HOLSTER_NATION_BRITISH)
        return jointD;
    return jointC;
}

const S_HolsterModel *FindHolsterModel(const char *model)
{
    for (std::size_t i = 0; i < g_holsterModelCount; ++i) {
        if (_stricmp(g_holsterModels[i].model, model) == 0)
            return &g_holsterModels[i];
    }
    return nullptr;
}

} // namespace equipment
