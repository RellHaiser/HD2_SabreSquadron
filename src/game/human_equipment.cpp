// The models of the items on a human's body (original source 0x0078cd20..0x007965e0).
#include "game/first_person_view.h"
#include "game/human_equipment.h"

#include <cmath>
#include <cstring>
#include <string>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_anim_tables.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// Offsets of the models from their joints (set by the static initializers 0x0078cd60,
// 0x0078cd90 and 0x0078cdc0).
const S_vector g_longGunModelOffset(0.17559581f, 0.18054318f, -0.13331501f);   // 0x0098ada0
const S_vector g_headModelOffset(0.016735554f, 0.13834208f, -0.017508307f);   // 0x0098ad90
const S_vector g_backpackModelOffset(-0.00095325f, 0.15715700f, -0.13235313f);   // 0x0098ad80

// Frames of the human model.
const char *const HAND_DUMMY1 = "HW1";
const char *const HAND_DUMMY2 = "HW2";
const char *const JOINT_C = "c";                        // most holster frames
const char *const JOINT_D = "d";                        // the back
const char *const JOINT_F = "f";                        // the head
const char *const JOINT_O = "o";
const char *const JOINT_U = "u";
const char *const JOINT_Q = "q";
// Frames of the item models.
const char *const BACKLINK_DUMMY = "backlink";          // where a long gun hangs on the back
const char *const MAGAZINE_FRAME = "mag1";
const char *const FIRE_DUMMY = "fire";                  // the special object's light
const char *const LENS_FLARE_FRAME = "lensf";
const char *const LIGHT_SMOKE_FRAME = "light_smoke";

// I3D_frame::LinkTo flags used here (meanings not identified).
constexpr uint32_t LINK_1 = 1;
constexpr uint32_t LINK_2 = 2;
constexpr uint32_t LINK_3 = 3;
constexpr uint32_t LINK_6 = 6;

// Sound chapter of the "take into the hands" sounds (C_ItemDef::GetArmedSound groups).
constexpr int SOUND_CHAPTER_ITEMS = 3;

// The model of a special object goes this far in front of the human when put down.
constexpr float PUT_DOWN_DISTANCE = 0.6f;
// ... and this far when thrown (place 9).
constexpr float THROW_DISTANCE = 0.4f;

// How long the special object's light shines (and its lens flare is bright), ms.
const int LIGHT_TIME = static_cast<int>(1500.0f / 1.4f);

// ---- holsters
// The holster frames are placed with the body in the pose of this animation, played on
// this stage for the moment.
const char *const HOLSTER_POSE_ANIMATION = "tecko";
constexpr int HOLSTER_POSE_STAGE = 7;
I3D_animation_set *g_holsterAnimation = nullptr;       // 0x0098adb0

// Models with the pouches, holsters and grenades of the uniforms.
const char *const g_holsterSets[] = {                   // 0x0084f7e8
    "def_holster_set", "en_vyb01", "en_vyb02", "en_vyb03", "ge_vyb01",
    "it_vyb01", "ja_vyb", "ru_vyb", "us_vyb", "us_vyb2",
};

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

// Frames of the holster sets and the slots whose items they show.
struct S_HolsterDummy {                                 // 8 bytes
    int slot;                                           // HOLSTER_SLOT_COUNT ends the table
    char name[4];
};

const S_HolsterDummy g_holsterDummies[] = {             // 0x0084fbd8
    {0, "1"}, {1, "2"}, {2, "41"}, {2, "42"}, {2, "43"}, {3, "51"}, {3, "52"}, {4, "6"},
    {5, "7"}, {6, "81"}, {6, "82"}, {6, "83"}, {7, "91"}, {7, "92"}, {7, "93"}, {8, "31"},
    {8, "32"}, {8, "33"}, {9, "aa"}, {10, "bb"}, {11, "cc"}, {12, "dd"}, {13, "ee1"}, {13, "ee2"},
    {HOLSTER_SLOT_COUNT, ""},
};

// The holster slot whose frames hang on the back ("d") instead of the belt ("c").
constexpr int HOLSTER_SLOT_BACK = 4;

// Times the stages are advanced by to pose the holsters (I3D_model::AdvanceStages): the pose
// stage (HOLSTER_POSE_STAGE) by 100 ms.
struct S_PoseParams {
    int times[8] = {0, 0, 0, 0, 0, 0, 0, 100};
};

// ---- save games
enum : uint16_t {
    CHUNK_EQUIPMENT = 0x309,
    CHUNK_EQUIPMENT_GIVE_TO = 0x01,                     // net id, -1: none
    CHUNK_EQUIPMENT_GIVE_TO_PLACE = 0x02,
    CHUNK_EQUIPMENT_TAKE_FROM = 0x03,                   // net id, -1: none
    CHUNK_EQUIPMENT_TAKE_FROM_PLACE = 0x04,
    CHUNK_EQUIPMENT_TAKE_FROM_ATTACH = 0x05,
    CHUNK_EQUIPMENT_UNKNOWN_2C = 0x09,
    CHUNK_EQUIPMENT_HANDS_SHOWN = 0x0A,
    CHUNK_EQUIPMENT_FLAGS = 0x0B,
    CHUNK_EQUIPMENT_UNKNOWN_D0 = 0x0C,
    CHUNK_EQUIPMENT_SPECIAL_PLACE = 0x0D,
    CHUNK_EQUIPMENT_SPECIAL_ROT = 0x0E,
    CHUNK_EQUIPMENT_SPECIAL_POS = 0x0F,
    CHUNK_EQUIPMENT_LIGHT_TIME = 0x10,
    CHUNK_EQUIPMENT_LENS_FLARE_TIME = 0x11,
    // How the models hung (E_SavedLink).
    CHUNK_EQUIPMENT_HANDS_LINK = 0x12,
    CHUNK_EQUIPMENT_SPECIAL_LINK = 0x13,
    CHUNK_EQUIPMENT_HEAD_LINK = 0x14,
    CHUNK_EQUIPMENT_LONG_GUN_LINK = 0x15,
    CHUNK_EQUIPMENT_BACKPACK_LINK = 0x16,
    CHUNK_EQUIPMENT_HANDS_UID = 0x17,
    CHUNK_EQUIPMENT_HANDS_TYPE = 0x18,
    CHUNK_EQUIPMENT_BACKPACK_TYPE = 0x19,
    CHUNK_EQUIPMENT_LONG_GUN_UID = 0x1A,
    CHUNK_EQUIPMENT_HEAD_UID = 0x1B,
    CHUNK_EQUIPMENT_BACKPACK_UID = 0x1C,
    CHUNK_EQUIPMENT_SPECIAL_TIME = 0x1D,
    CHUNK_EQUIPMENT_SPECIAL_NAME = 0x1E,                // scene frame of the special object
    // Local position and rotation of the models.
    CHUNK_EQUIPMENT_HANDS_POS = 0x1F,
    CHUNK_EQUIPMENT_HANDS_ROT = 0x20,
    CHUNK_EQUIPMENT_SPECIAL_MODEL_POS = 0x21,
    CHUNK_EQUIPMENT_SPECIAL_MODEL_ROT = 0x22,
    CHUNK_EQUIPMENT_HEAD_POS = 0x23,
    CHUNK_EQUIPMENT_HEAD_ROT = 0x24,
    CHUNK_EQUIPMENT_LONG_GUN_POS = 0x25,
    CHUNK_EQUIPMENT_LONG_GUN_ROT = 0x26,
    CHUNK_EQUIPMENT_BACKPACK_POS = 0x27,
    CHUNK_EQUIPMENT_BACKPACK_ROT = 0x28,
    CHUNK_EQUIPMENT_LONG_GUN_TYPE = 0x29,
    CHUNK_EQUIPMENT_HANDS_TIME = 0x2A,
    CHUNK_EQUIPMENT_BACKPACK_ON = 0x2B,
    CHUNK_EQUIPMENT_LONG_GUN_ON = 0x2C,
};

// What a model was linked to when saved.
enum E_SavedLink {
    SAVED_LINK_OTHER = 0,
    SAVED_LINK_HAND2 = 1,
    SAVED_LINK_PLACE = 2,       // its own joint; "HW1" for the hands and special models
    SAVED_LINK_BACK = 3,        // joint "d" (a hands item put away)
    SAVED_LINK_NONE = 4,        // no model
};

// m_specialTime values that tell where the special object comes from when a game is loaded
// (positive: the actor of that net id).
enum : int {
    SPECIAL_FROM_PROP_1 = -1,           // GetActivityProp(1)
    SPECIAL_FROM_PROP_0 = -2,           // GetActivityProp(0)
    SPECIAL_FROM_PROP_2 = -3,           // GetActivityProp(2)
    SPECIAL_FROM_SCENE = -4,            // the scene frame of the saved name
    SPECIAL_FROM_BELT_19 = -5,          // FindBeltItem(19)
    SPECIAL_FROM_THROWN = -6,           // a thrown item of the belt or the backpack contents
    SPECIAL_FROM_BELT_14 = -7,          // FindBeltItem(14)
    SPECIAL_FROM_BELT_TYPE_E5 = -8,     // the belt item of type 0xE5
    SPECIAL_FROM_SCENE_PICKED_UP = -9,  // the scene frame, taken with both hands (place 4)
};
constexpr int SPECIAL_ITEM_TYPE_E5 = 0xE5;

// A model's link, local position and rotation in a save game.
struct S_SavedModel {
    int link = SAVED_LINK_NONE;
    S_vector pos = S_vector(0.0f, 0.0f, 0.0f);
    S_quat rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
};

void WriteQuat(C_chunk &chunk, uint16_t id, const S_quat &rot)
{
    float data[4];
    rot.ToFile(data);
    chunk.WriteChunk(id, data, sizeof(data));
}

S_quat ReadQuat(C_chunk &chunk)
{
    float data[4] = {};
    chunk.Read(data, sizeof(data));
    return S_quat::FromFile(data);
}

void RestoreTransform(I3D_frame *model, const S_SavedModel &saved)
{
    model->SetPosInline(saved.pos);
    model->SetRot(saved.rot);
}

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

// Link code of a hands or special model.
int HandModelLink(const I3D_frame *parent, const I3D_frame *hand1, const I3D_frame *hand2, const I3D_frame *back)
{
    if (parent == hand1)
        return SAVED_LINK_PLACE;
    if (parent == hand2)
        return SAVED_LINK_HAND2;
    return parent == back ? SAVED_LINK_BACK : SAVED_LINK_OTHER;
}

}  // namespace

// ------------------------------------------------------------------ lifetime

C_HumanEquipment::C_HumanEquipment(C_Human *owner) : m_owner(owner)
{
    m_flashlight = nullptr;
    Clear();
    InitHolsters();
    for (I3D_frame *&gear : m_divingGear)
        gear = nullptr;
}

C_HumanEquipment::~C_HumanEquipment()
{
    for (I3D_frame *&gear : m_divingGear) {
        if (gear) {
            gear->Release();
            gear = nullptr;
        }
    }
}

void C_HumanEquipment::Clear()
{
    m_handsTypeId = -1;
    m_longGunTypeId = -1;
    m_backpackTypeId = -1;
    m_handsModel = nullptr;
    m_specialModel = nullptr;
    m_longGunModel = nullptr;
    m_unknown2c = true;
    m_headModel = nullptr;
    m_backpackModel = nullptr;
    m_handsUid = 0;
    m_longGunUid = 0;
    m_headUid = 0;
    m_backpackUid = 0;
    m_flags = 0;
    m_handsShown = true;
    m_unknownD0 = 0.0f;
    m_flashlightTime = NO_TIME;
    m_lensFlareTime = NO_TIME;
    m_takeFrom = nullptr;
    m_giveTo = nullptr;
    m_specialTime = NO_TIME;
    m_longGunTilt = 0.0f;
    m_longGunSlide = 0.0f;
    m_longGunLift = 0;
    m_specialPlace = EQUIPMENT_PLACE_NONE;
    m_handsTime = NO_TIME;
}

void C_HumanEquipment::Init()
{
    Clear();
    m_flashlight = I3DGetDriver()->CreateFrame(FRAME_LIGHT);
    m_flashlight->SetName("m_SpecFrameLight");
    FindJoints(m_owner->GetFrame());

    if (C_Item *item = m_owner->GetContainer(ITEM_CONTAINER_HANDS)->GetItem()) {
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_HANDS, item->GetUid(), NO_TIME, item->GetTypeId());
        m_owner->GetMovement()->SetCarryDelay(NO_TIME, 0);
        m_owner->GetWork()->SetCarryMode(item->GetDef()->GetCarryMode(), false);
        m_owner->GetContainer(ITEM_CONTAINER_HANDS)->SetLockTime(GetGame()->GetMissionTime() + 1000);
        if (GetHolsterSlot(item->GetTypeId()) == HOLSTER_SLOT_SIDEARM)
            AddToHolster(item->GetTypeId(), 1);
    }
    if (C_Item *item = m_owner->GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem()) {
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_LONG_GUN, item->GetUid(), NO_TIME, item->GetTypeId());
        AttachModel(EQUIPMENT_PLACE_LONG_GUN);
    }
    if (C_Item *item = m_owner->GetContainer(ITEM_CONTAINER_BACKPACK)->GetItem()) {
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_BACKPACK, item->GetUid(), NO_TIME, item->GetTypeId());
        AttachModel(EQUIPMENT_PLACE_BACKPACK);
    }
    if (C_Item *item = m_owner->GetContainer(ITEM_CONTAINER_HEAD)->GetItem()) {
        // (The head model keeps no type id.)
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_HEAD, item->GetUid(), NO_TIME, -1);
        AttachModel(EQUIPMENT_PLACE_HEAD);
    }
    AddBeltToHolsters();
}

void C_HumanEquipment::AddBeltToHolsters()
{
    // Belt slots: each holds items of one type.
    C_ItemContainer *belt = m_owner->GetContainer(ITEM_CONTAINER_BELT);
    if (std::vector<C_Item *> *slots = belt->GetSlots()) {
        for (int i = 0; i < belt->GetSlotCount(); ++i) {
            if (!slots[i].empty())
                AddToHolster(slots[i].front()->GetTypeId(), static_cast<int>(slots[i].size()));
        }
    }
}

void C_HumanEquipment::FindJoints(I3D_frame * /*unused*/)
{
    I3D_frame *frame = m_owner->GetFrame();
    m_handDummy1 = frame->FindChildFrame(HAND_DUMMY1, FRAME_MASK_DUMMY);
    m_handDummy2 = frame->FindChildFrame(HAND_DUMMY2, FRAME_MASK_DUMMY);
    if (m_handDummy1 && m_handDummy2) {
        m_handDummy1->SetOn(true);
        m_handDummy2->SetOn(true);
    }
    m_jointD = frame->FindChildFrame(JOINT_D, FRAME_MASK_ANY);
    m_backpackJoint = m_jointD;
    m_jointF = frame->FindChildFrame(JOINT_F, FRAME_MASK_ANY);
    m_jointU = frame->FindChildFrame(JOINT_U, FRAME_MASK_ANY);
    m_jointQ = frame->FindChildFrame(JOINT_Q, FRAME_MASK_ANY);
}

void C_HumanEquipment::HideHolsters()
{
    for (S_HolsterSlot &slot : m_holsters) {
        slot.itemCount = 0;
        for (I3D_frame *frame : slot.frames) {
            if (frame)
                frame->SetOn(false);
        }
    }
}

void C_HumanEquipment::ReleaseHolsters()
{
    for (S_HolsterSlot &slot : m_holsters) {
        slot.frameCount = 0;
        for (I3D_frame *&frame : slot.frames) {
            if (frame) {
                frame->LinkTo(nullptr, 0);
                frame->Release();
                frame = nullptr;
            }
        }
    }
    if (g_holsterAnimation) {
        g_holsterAnimation->Release();
        g_holsterAnimation = nullptr;
    }
}

void C_HumanEquipment::InitHolsters()
{
    // The holster frames are linked while the body stands in the holster pose; the pose
    // stage's own animation is restored afterwards.
    I3D_model *body = static_cast<I3D_model *>(m_owner->GetFrame());
    I3D_animation_set *savedAnimation = body->GetAnimation(HOLSTER_POSE_STAGE);
    // (The weight is kept as a whole number.)
    const int savedWeight = static_cast<int>(body->GetAnimWeight(HOLSTER_POSE_STAGE));
    const int savedTime = body->GetAnimTime(HOLSTER_POSE_STAGE);
    const int savedFlags = body->GetAnimFlags(HOLSTER_POSE_STAGE);
    const bool savedActive = body->IsAnimActive(HOLSTER_POSE_STAGE);
    for (S_HolsterSlot &slot : m_holsters)
        slot = S_HolsterSlot();
    S_PoseParams params;
    I3D_frame *jointC = body->FindChildFrame(JOINT_C, FRAME_MASK_ANY);
    I3D_frame *jointD = body->FindChildFrame(JOINT_D, FRAME_MASK_ANY);
    I3D_frame *jointO = body->FindChildFrame(JOINT_O, FRAME_MASK_ANY);
    if (!g_holsterAnimation)
        g_animationCache.Get(HOLSTER_POSE_ANIMATION, &g_holsterAnimation);
    body->SetAnimation(g_holsterAnimation, HOLSTER_POSE_STAGE, 1.0f, 2);
    body->AdvanceStages(params.times);

    // The holster set of the human's model (its file name without path and extension).
    std::string modelName = body->GetFileName();
    const std::size_t slash = modelName.rfind('\\');
    if (slash != std::string::npos)
        modelName.erase(0, slash + 1);
    const std::size_t dot = modelName.find('.');
    if (dot != std::string::npos)
        modelName.erase(dot);
    const S_HolsterModel *model = nullptr;
    for (const S_HolsterModel &entry : g_holsterModels) {
        if (_stricmp(entry.model, modelName.c_str()) == 0) {
            model = &entry;
            break;
        }
    }
    // (A model missing from the list builds the message "HolsterInit chyba - panak '<model>'
    // nenalezen v seznamu. Oznamte skutecnost TS." -- not in the list, tell the team -- that
    // is never shown.)
    if (model && model->set >= 0) {
        I3D_model *holsters = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
        if (g_modelCache.OpenInto(holsters, g_holsterSets[model->set]) == I3D_OK) {
            holsters->LinkTo(body, 0);
            for (const S_HolsterDummy *dummy = g_holsterDummies; dummy->slot != HOLSTER_SLOT_COUNT; ++dummy) {
                I3D_frame *frame = holsters->FindChildFrame(dummy->name, FRAME_MASK_ANY);
                if (!frame)
                    continue;
                S_HolsterSlot &slot = m_holsters[dummy->slot];
                if (slot.frameCount >= 4)
                    continue;
                slot.frames[slot.frameCount++] = frame;
                frame->AddRef();
                holsters->RemoveModelFrame(frame);
                frame->LinkTo(HolsterJoint(*dummy, *model, jointC, jointD, jointO), LINK_1);
                frame->SetOn(false);
            }
        }
        // (A set that does not load builds the message "HolsterInit chyba - panak '<model>'
        // nenalezl holster set '<set>'. Oznamte skutecnost TS.", never shown either.)
        holsters->LinkTo(nullptr, 0);
        holsters->Release();
    }

    body->SetAnimWeight(HOLSTER_POSE_STAGE, 0.0f);
    body->AdvanceStages(params.times);
    body->StopAnimation(HOLSTER_POSE_STAGE);
    if (savedActive) {
        body->SetAnimation(savedAnimation, HOLSTER_POSE_STAGE, static_cast<float>(savedWeight), savedFlags);
        body->SetAnimTime(HOLSTER_POSE_STAGE, savedTime);
    }
}

void C_HumanEquipment::Done()
{
    SetFlashlight(false);
    m_flashlight->Release();
    m_flashlight = nullptr;
    m_owner->GetMovement()->GetWeaponPose()->SetModel(nullptr, -1);
    for (I3D_frame *&gear : m_divingGear) {
        if (gear) {
            gear->LinkTo(nullptr, 0);
            gear->Release();
            gear = nullptr;
        }
    }
    // A local game leaves the special object where it was taken from.
    if (IsLocalGame() && m_specialModel) {
        m_specialModel->LinkTo(nullptr, 0);
        m_specialModel->SetPos(m_specialPos);
        m_specialModel->SetRot(m_specialRot);
        I3D_scene *scene = GetGame()->GetScene();
        scene->UpdateFrameSector(m_specialModel, m_specialModel->GetWorldPos());
        m_specialModel->Update();
        m_specialModel->Release();
    }
    m_specialModel = nullptr;
    ReleaseHolsters();
}

void C_HumanEquipment::Reset()
{
    Done();
    Init();
}

// ------------------------------------------------------------------ models

void C_HumanEquipment::SetLightLevel(I3D_frame *model) const
{
    if (model->GetVisualType() == I3DVISUAL_OBJECT)
        static_cast<I3D_object_visual *>(model)->SetLightLevel(m_owner->GetMovement()->GetLightLevel());
}

void C_HumanEquipment::SetModel(I3D_frame *model, int place, int uid, int time, int typeId)
{
    if (m_specialTime < 0 || time != NO_TIME)
        m_specialTime = time;
    switch (place) {
    case EQUIPMENT_PLACE_HEAD:
        if (model) {
            m_headModel = model;
            m_headUid = uid;
        }
        break;
    case EQUIPMENT_PLACE_BACKPACK:
        if (model) {
            m_backpackModel = model;
            m_backpackUid = uid;
            m_backpackTypeId = typeId;
        }
        break;
    case EQUIPMENT_PLACE_LONG_GUN:
        if (model) {
            m_longGunModel = model;
            m_longGunUid = uid;
            m_longGunTypeId = typeId;
        }
        break;
    case EQUIPMENT_PLACE_HANDS:
        if (model) {
            m_handsModel = model;
            m_handsTime = 700;
            m_handsShown = true;
            m_handsUid = uid;
            m_handsTypeId = typeId;
        }
        break;
    case 4:
        m_specialTime = -9;
        m_specialModel = model;
        break;
    case 5:
    case 6:
    case 7:
    case 9:
        m_specialModel = model;
        break;
    default:
        break;
    }
}

I3D_frame *C_HumanEquipment::GetModel(int place) const
{
    switch (place) {
    case EQUIPMENT_PLACE_HEAD:
        return m_headModel;
    case EQUIPMENT_PLACE_BACKPACK:
        return m_backpackModel;
    case EQUIPMENT_PLACE_LONG_GUN:
        return m_longGunModel;
    case EQUIPMENT_PLACE_HANDS:
        return m_handsModel;
    case 4:
    case 5:
    case 6:
    case 7:
    case 9:
        return m_specialModel;
    default:
        return nullptr;
    }
}

void C_HumanEquipment::AttachModel(int place)
{
    C_HumanMovement *movement = m_owner->GetMovement();
    switch (place) {
    case EQUIPMENT_PLACE_HEAD: {
        if (!m_headModel)
            return;
        m_headModel->LinkTo(m_jointF, LINK_2);
        m_headModel->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
        m_headModel->SetOn(true);
        m_headModel->SetPosInline(g_headModelOffset);
        m_headModel->Update();
        // Light head gear (caps) does not change the weapon pose.
        const C_Item *item = m_headUid >= 0 ? m_owner->FindItemByUid(m_headUid) : nullptr;
        if (!item || item->GetDef()->GetWeight() > 0.1f)
            movement->GetWeaponPose()->SetStainsVisible(true);
        SetLightLevel(m_headModel);
        UpdateModelNames();
        return;
    }
    case EQUIPMENT_PLACE_BACKPACK: {
        if (!m_backpackModel)
            return;
        if (m_backpackTypeId != -1) {
            // An aqualung: the diving gear's four models (mask on the head, two on the joints
            // "u" and "q", the bottles in place of the backpack model).
            if (I3D_frame **gear = GetItemDef(m_backpackTypeId)->GetDivingGear()) {
                for (int i = 0; i < 4; ++i) {
                    if (!m_divingGear[i] && gear[i])
                        CloneFrameTree(m_divingGear[i], gear[i], nullptr);
                }
                m_divingGear[0]->LinkTo(m_jointF, LINK_2);
                m_divingGear[0]->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
                m_divingGear[0]->SetOn(true);
                m_divingGear[0]->SetPosInline(g_headModelOffset);
                m_divingGear[0]->Update();
                m_divingGear[1]->LinkTo(m_jointU, LINK_2);
                m_divingGear[1]->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
                m_divingGear[1]->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
                m_divingGear[1]->SetOn(true);
                m_divingGear[1]->Update();
                m_divingGear[2]->LinkTo(m_jointQ, LINK_2);
                m_divingGear[2]->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
                m_divingGear[2]->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
                m_divingGear[2]->SetOn(true);
                m_divingGear[2]->Update();
                m_backpackModel = m_divingGear[3];
                for (I3D_frame *model : m_divingGear)
                    SetLightLevel(model);
            }
        }
        m_backpackModel->LinkTo(m_backpackJoint, LINK_2);
        m_backpackModel->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
        if (movement->GetSeatMode() < 1)
            m_backpackModel->SetOn(true);
        m_backpackModel->SetPosInline(g_backpackModelOffset);
        m_backpackModel->Update();
        SetLightLevel(m_backpackModel);
        UpdateModelNames();
        return;
    }
    case EQUIPMENT_PLACE_LONG_GUN: {
        if (!m_longGunModel)
            return;
        I3D_frame *backlink = m_longGunModel->FindChildFrame(BACKLINK_DUMMY, FRAME_MASK_DUMMY);
        I3D_frame *magazine = m_longGunModel->FindChildFrame(MAGAZINE_FRAME, FRAME_MASK_ANY);
        if (!backlink)
            return;
        m_longGunTilt = 0.0f;
        m_longGunSlide = 0.0f;
        m_longGunModel->LinkTo(m_jointD, LINK_2);
        m_longGunModel->SetWorldDir(S_vector(0.0f, 1.0f, 0.0f), 0.0f);
        if (movement->GetSeatMode() < 1)
            m_longGunModel->SetOn(true);
        m_longGunModel->SetPosInline(g_longGunModelOffset);
        m_longGunModel->Update();
        const S_vector link = BacklinkOffset(backlink);
        m_longGunModel->SetPosInline(S_vector(link.x + 0.0275f, link.y, link.z + 0.09f));
        m_longGunModel->Update();
        if (magazine && GetItemDef(m_longGunTypeId)->GetBehaviour() != 4) {
            // Rifles with a magazine hang turned, the magazine out.
            m_longGunMaxTilt = 0.29f;
            m_longGunMaxSlide = 0.038f;
            S_vector pos = m_longGunModel->GetPos();
            pos.x -= 0.01f;
            pos.z -= 0.08f;
            RotateAboutYxX(m_longGunModel, -1.9634955f);
            m_longGunModel->SetPosInline(pos);
            m_longGunModel->Update();
            pos = m_longGunModel->GetPos();
            Rotate(m_longGunModel, m_longGunModel->GetLocalMatrix()(0), -0.35f);
            m_longGunModel->SetPosInline(pos);
            m_longGunModel->Update();
        } else {
            m_longGunMaxTilt = 0.4f;
            m_longGunMaxSlide = 0.068f;
            S_vector pos = m_longGunModel->GetPos();
            RotateAboutYxX(m_longGunModel, -0.25f);
            m_longGunModel->SetPosInline(pos);
            m_longGunModel->Update();
            pos = m_longGunModel->GetPos();
            Rotate(m_longGunModel, m_longGunModel->GetLocalMatrix()(1), 0.1f);
            m_longGunModel->SetPosInline(pos);
            m_longGunModel->Update();
        }
        Rotate(m_longGunModel, S_vector(1.0f, 0.0f, 0.0f), magazine ? 0.15f : 0.25f);
        m_longGunModel->Update();
        if (m_longGunLift != 0) {
            // Lifted: straight into the end pose.
            m_longGunTilt = m_longGunMaxTilt;
            Rotate(m_longGunModel, S_vector(1.0f, 0.0f, 0.0f), -m_longGunMaxTilt);
            m_longGunSlide = m_longGunMaxSlide;
            const S_vector pos = m_longGunModel->GetPos();
            m_longGunModel->SetPosInline(S_vector(pos.x, pos.y, pos.z - m_longGunMaxSlide));
        } else {
            m_longGunTilt = 0.0f;
            m_longGunSlide = 0.0f;
        }
        // (No UpdateModelNames for the long gun.)
        SetLightLevel(m_longGunModel);
        return;
    }
    case EQUIPMENT_PLACE_HANDS: {
        if (m_handsModel && m_handDummy1) {
            m_handsTime = NO_TIME;
            if (m_flags & EQUIPMENT_HANDS_PUT_AWAY) {
                AttachModel(10);
                return;
            }
            m_handsModel->SetOn(true);
            m_handsModel->LinkTo(m_handDummy1, LINK_6);
            m_handsModel->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
            m_handsModel->SetWorldDir(S_vector(1.0f, 0.0f, 0.0f), 0.0f);
            m_handsModel->SetOn(true);
            m_handsModel->Update();
            C_ItemDef *def = GetItemDef(m_handsTypeId);
            int behaviour = -1;
            if (!def) {
                movement->SetUnknown610_78(2);
            } else {
                int value;
                if (def->GetComponent() && def->GetComponent()->GetKind() == ICK_MELEE)
                    value = 1;
                else
                    value = def->GetBehaviour() == ITEM_BEHAVIOUR_THROWN ? 1 : 2;
                movement->SetUnknown610_78(value);
                behaviour = def->GetBehaviour();
            }
            movement->GetWeaponPose()->SetModel(m_handsModel, behaviour);
            movement->SetUnknown610_64(2);
            // The sound of taking it into the hands (not for the local player's first person
            // view). Fix: the original also plays it for an unknown type id (null definition).
            if (m_handsTypeId != -1 && def && !(m_owner->IsLocalPlayer() && GetGame()->IsFirstPersonView())) {
                S_SoundRequest request;
                request.pos = S_vector(0.0f, 0.0f, 0.0f);
                // (The original leaves posW at whatever the stack held.)
                request.chapter = SOUND_CHAPTER_ITEMS;
                request.group = def->GetArmedSound();
                request.source = m_owner;
                request.attachTo = m_owner->GetHandsItemFrame();
                request.flags = 0;
                GetSoundModule()->Play(request);
            }
            SetLightLevel(m_handsModel);
        }
        UpdateModelNames();
        return;
    }
    case 4: {
        // Picks a special object up with both hands: its bound's centre goes between the
        // hand dummies, 0.1 m to the front.
        if (!m_specialModel)
            return;
        m_specialPlace = 4;
        m_specialModel->AddRef();
        const I3D_bound_volume &bound = m_specialModel->GetBound();
        if (!(bound.bbox.min.x <= bound.bbox.max.x))
            return;
        m_specialRot.Make(m_specialModel->GetMatrix());
        m_specialPos = m_specialModel->GetWorldPos();
        const S_vector center = bound.bbox.min + (bound.bbox.max - bound.bbox.min) * 0.5f;
        const S_vector &hand1 = m_handDummy1->GetWorldPos();
        const S_vector &hand2 = m_handDummy2->GetWorldPos();
        S_vector between = (hand2 - hand1) * 0.5f + hand1;
        const S_vector facing = m_owner->GetDirection();
        const S_vector dir = m_specialModel->GetWorldDir();
        between.x += facing.x * 0.1f;
        between.z += facing.z * 0.1f;
        m_specialModel->SetPos(between - center + m_specialModel->GetWorldPos());
        m_specialModel->LinkTo(m_handDummy1, LINK_3);
        m_specialModel->SetDir(dir, 0.0f);
        m_specialModel->SetOn(!m_owner->IsFadedOut());
        m_specialModel->Update();
        movement->SetUnknown610_64(2);
        movement->SetUnknown610_78(2);
        SetLightLevel(m_specialModel);
        return;
    }
    case 5:
    case 6:
    case 7:
    case 8: {
        // A special object in one hand.
        if (!m_specialModel)
            return;
        m_specialPlace = place;
        m_specialModel->AddRef();
        m_specialRot.Make(m_specialModel->GetMatrix());
        if (place != 6 && place != 7)
            m_specialPos = m_specialModel->GetWorldPos();
        m_specialModel->LinkTo(nullptr, 0);
        I3D_frame *hand = (place == 8 || m_unknown2c) ? m_handDummy1 : m_handDummy2;
        m_specialModel->LinkTo(hand, LINK_2);
        m_specialModel->SetOn(!m_owner->IsFadedOut());
        m_specialModel->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
        m_specialModel->Update();
        SetLightLevel(m_specialModel);
        return;
    }
    case 9: {
        if (!m_specialModel)
            return;
        m_specialModel->AddRef();
        m_specialPlace = 9;
        I3D_model *model = static_cast<I3D_model *>(m_specialModel);
        model->SetAnimation(g_humanAnimTable->GetUnknownD88(), 6, 1.0f, 2);
        model->GetAnimation(6)->SetPlayMode(2);
        m_specialModel->Tick(1000);
        Unknown7938f0(100);
        SetLightLevel(m_specialModel);
        return;
    }
    case 10: {
        // The hands item put away for an action: a rifle goes on the back (beside the long
        // gun when there is one), smaller things are hidden.
        if (m_handsTime != NO_TIME && m_owner->GetHealth() > 0) {
            const bool putAway = (m_flags & EQUIPMENT_HANDS_PUT_AWAY) != 0;
            if (putAway)
                m_flags &= ~EQUIPMENT_HANDS_PUT_AWAY;
            AttachModel(EQUIPMENT_PLACE_HANDS);
            if (putAway)
                m_flags |= EQUIPMENT_HANDS_PUT_AWAY;
        }
        m_handsTime = NO_TIME;
        I3D_frame *backlink = m_handsModel->FindChildFrame(BACKLINK_DUMMY, FRAME_MASK_DUMMY);
        const int behaviour = GetItemDef(m_handsTypeId)->GetBehaviour();
        if (behaviour < 3 || (behaviour > 7 && behaviour != 12)) {
            m_handsModel->SetOn(false);
            return;
        }
        if (!backlink)
            return;
        m_handsModel->LinkTo(m_jointD, LINK_2);
        m_handsModel->SetWorldDir(S_vector(0.0f, 1.0f, 0.0f), 0.0f);
        if (movement->GetSeatMode() < 1)
            m_handsModel->SetOn(true);
        S_vector offset = g_longGunModelOffset;
        if (m_longGunModel)
            offset.x = -offset.x;
        m_handsModel->SetPosInline(offset);
        m_handsModel->Update();
        const S_vector link = BacklinkOffset(backlink);
        S_vector pos;
        if (!m_longGunModel)
            pos = S_vector(link.x + 0.0275f, link.y, link.z + 0.09f);
        else
            pos = S_vector((m_backpackModel ? 0.01f : 0.025f) + link.x, link.y, link.z + 0.06f);
        m_handsModel->SetPosInline(pos);
        m_handsModel->Update();
        I3D_frame *magazine = m_handsModel->FindChildFrame(MAGAZINE_FRAME, FRAME_MASK_ANY);
        if (!magazine) {
            if (!m_longGunModel) {
                const S_vector p = m_handsModel->GetPos();
                Rotate(m_handsModel, m_handsModel->GetLocalMatrix()(1), 0.1f);
                m_handsModel->SetPosInline(p);
                m_handsModel->Update();
            }
        } else {
            S_vector p = m_handsModel->GetPos();
            RotateAboutYxX(m_handsModel, m_longGunModel ? -1.4280f : -1.9634955f);
            m_handsModel->SetPosInline(S_vector(p.x - 0.01f, p.y, p.z - 0.08f));
            m_handsModel->Update();
            if (!m_longGunModel) {
                p = m_handsModel->GetPos();
                Rotate(m_handsModel, m_handsModel->GetLocalMatrix()(0), -0.35f);
                m_handsModel->SetPosInline(p);
                m_handsModel->Update();
            }
        }
        Rotate(m_handsModel, S_vector(1.0f, 0.0f, 0.0f), (magazine || m_longGunModel) ? 0.15f : 0.25f);
        m_handsModel->Update();
        SetLightLevel(m_handsModel);
        return;
    }
    default:
        return;
    }
}

void C_HumanEquipment::DetachModel(int place)
{
    C_HumanMovement *movement = m_owner->GetMovement();
    I3D_scene *scene = GetGame()->GetScene();
    switch (place) {
    case EQUIPMENT_PLACE_HEAD:
        if (!m_headModel)
            return;
        m_headModel->LinkTo(nullptr, 0);
        m_headModel = nullptr;
        movement->GetWeaponPose()->SetStainsVisible(false);
        UpdateModelNames();
        return;
    case EQUIPMENT_PLACE_BACKPACK:
        if (!m_backpackModel)
            return;
        m_backpackModel->LinkTo(nullptr, 0);
        m_backpackModel = nullptr;
        for (I3D_frame *gear : m_divingGear) {
            if (gear)
                gear->LinkTo(nullptr, 0);
        }
        UpdateModelNames();
        return;
    case EQUIPMENT_PLACE_LONG_GUN:
        if (!m_longGunModel)
            return;
        m_longGunModel->LinkTo(nullptr, 0);
        m_longGunModel = nullptr;
        return;
    case EQUIPMENT_PLACE_HANDS:
        if (!m_handsModel)
            return;
        if (movement->GetWeaponPose()->IsSupportHandOn())
            OnAnimationEvent(EQUIPMENT_EVENT_1);
        m_handsModel->LinkTo(nullptr, 0);
        m_handsModel = nullptr;
        movement->GetWeaponPose()->SetModel(nullptr, -1);
        movement->SetUnknown610_78(0);
        movement->SetUnknown610_64(0);
        UpdateModelNames();
        return;
    case 4:
    case 8: {
        // Puts the special object down in front of the human.
        if (!m_specialModel)
            return;
        m_specialTime = NO_TIME;
        m_specialModel->LinkTo(nullptr, LINK_1);
        m_specialModel->LinkTo(scene->GetPrimarySector(), LINK_3);
        scene->UpdateFrameSector(m_specialModel, m_specialModel->GetWorldPos());
        m_specialModel->SetRot(m_specialRot);
        const S_vector dir = m_owner->GetFrame()->GetWorldDir() * PUT_DOWN_DISTANCE;
        const S_vector &ownerPos = m_owner->GetFrame()->GetWorldPos();
        S_vector pos(dir.x + ownerPos.x, ownerPos.y, dir.z + ownerPos.z);
        m_specialModel->SetPos(pos);
        m_specialModel->Update();
        const I3D_bound_volume &bound = m_specialModel->GetBound();
        // (Its bottom is meant to rest on the ground; the original adds the height twice.)
        pos.y = (pos.y - bound.bbox.min.y) + pos.y;
        m_specialModel->SetPos(pos);
        m_specialModel->Update();
        m_specialModel->Release();
        m_specialModel = nullptr;
        movement->SetUnknown610_64(0);
        movement->SetUnknown610_78(0);
        if (place == 8)
            m_specialPlace = EQUIPMENT_PLACE_NONE;
        return;
    }
    case 5:
        // Back where it was taken from.
        if (!m_specialModel)
            return;
        m_specialTime = NO_TIME;
        m_specialModel->LinkTo(nullptr, LINK_1);
        m_specialModel->LinkTo(scene->GetPrimarySector(), 0);
        m_specialModel->SetRot(m_specialRot);
        m_specialModel->SetPosInline(m_specialPos);
        m_specialModel->Update();
        scene->UpdateFrameSector(m_specialModel, m_specialModel->GetWorldPos());
        m_specialModel->Update();
        m_specialModel->Release();
        m_specialModel = nullptr;
        m_specialPlace = EQUIPMENT_PLACE_NONE;
        return;
    case 6:
    case 7:
        if (!m_specialModel)
            return;
        if (m_specialPlace != place && m_specialPlace != EQUIPMENT_PLACE_NONE) {
            DetachModel(m_specialPlace);
            return;
        }
        // (A model of place 7 still linked somewhere loses one more reference.)
        if (place == 7 && m_specialModel->GetParent())
            m_specialModel->Release();
        m_specialTime = NO_TIME;
        m_specialModel->LinkTo(nullptr, LINK_1);
        m_specialModel->SetRot(m_specialRot);
        m_specialModel->Update();
        m_specialModel->Release();
        m_specialModel = nullptr;
        m_specialPlace = EQUIPMENT_PLACE_NONE;
        return;
    case 9: {
        // Thrown 0.4 m to the front (or kept close when something is in the way).
        if (!m_specialModel)
            return;
        m_specialTime = NO_TIME;
        I3D_model *model = static_cast<I3D_model *>(m_specialModel);
        model->SetAnimation(g_humanAnimTable->GetUnknownDAC(), 6, 1.0f, 2);
        m_specialModel->Tick(1000);
        I3D_frame *ownerFrame = m_owner->GetFrame();
        S_vector dir = ownerFrame->GetWorldDir();
        const S_vector p = ownerFrame->GetWorldPos();
        const S_vector target(p.x + dir.x * THROW_DISTANCE, p.y + 0.5f, p.z + dir.z * THROW_DISTANCE);
        movement->RemoveCollision(false);
        const S_vector from(p.x, p.y + 0.5f, p.z);
        const S_vector move(target.x - p.x, (target.y - p.y) + 0.5f, target.z - p.z);
        S_vector hit;
        S_vector pos = target;
        if (CollisionSphereTest(from, move, 0.33f, &hit, nullptr, 0xC8002, 0x230000)) {
            const float distance = S_vector(hit.x - p.x, 0.0f, hit.z - p.z).Magnitude();
            if (distance >= 0.5f) {
                pos = from;
            } else {
                const float back = 0.5f - distance;
                dir = ownerFrame->GetWorldDir();
                pos = S_vector(p.x - dir.x * back, p.y + 0.5f - back * dir.y, p.z - back * dir.z);
            }
        }
        movement->RestoreCollision(false);
        m_specialModel->SetPos(pos);
        scene->Unknown144(ownerFrame);
        m_specialModel->Update();
        m_specialModel->Release();
        m_specialModel = nullptr;
        m_specialPlace = EQUIPMENT_PLACE_NONE;
        return;
    }
    default:
        return;
    }
}

// ------------------------------------------------------------------ holsters

int C_HumanEquipment::GetHolsterSlot(int typeId)
{
    C_ItemDef *def = GetItemDef(typeId);
    // Fix: the original reads the definition of an unknown type id (null).
    if (!def)
        return HOLSTER_SLOT_COUNT;
    switch (def->GetBehaviour()) {
    case 0:
        return 1;
    case ITEM_BEHAVIOUR_SIDEARM:
        return HOLSTER_SLOT_SIDEARM;
    case ITEM_BEHAVIOUR_THROWN:
        if (typeId == 0x3D)
            return 7;
        return typeId == 0x3F ? 8 : 6;
    case ITEM_BEHAVIOUR_MINE:
    case ITEM_BEHAVIOUR_STICKY_CHARGE:
    case ITEM_BEHAVIOUR_MISSION_EXPLOSIVE:
        if (typeId == 0x42 || typeId == 0xFB)
            return 10;
        return typeId == 0x43 ? 11 : 9;
    case ITEM_BEHAVIOUR_MEDKIT:
        if (typeId == 0x35)
            return 12;
        if (typeId == 0x36)
            return 13;
        return HOLSTER_SLOT_COUNT;
    case 14:
        return 4;
    default:
        break;
    }
    if (def->GetRecordType() == ITEM_AMMO) {
        // Ammunition by its pouch kind.
        if (def->GetUnknown0C() == 0)
            return 2;
        return def->GetUnknown0C() == 1 ? 3 : HOLSTER_SLOT_COUNT;
    }
    if (typeId < 0xEF || typeId == 0xFB || typeId == 0xFD)
        return 5;
    return HOLSTER_SLOT_COUNT;
}

// Items per holster frame: pouches (slots 2 and 3) show one frame per three items.
static int ItemsPerFrame(int slot)
{
    return (slot == 2 || slot == 3) ? 3 : 1;
}

void C_HumanEquipment::RemoveFromHolster(int typeId, int count)
{
    const int slot = GetHolsterSlot(typeId);
    if (slot == HOLSTER_SLOT_COUNT)
        return;
    S_HolsterSlot &holster = m_holsters[slot];
    holster.itemCount -= count;
    if (holster.itemCount < 0) {
        holster.itemCount = 0;
        return;
    }
    int shown = holster.itemCount;
    const int perFrame = ItemsPerFrame(slot);
    if (shown != 0 && perFrame != 1)
        shown = (shown - 1) / perFrame + 1;
    for (int i = 0; i < holster.frameCount; ++i)
        holster.frames[i]->SetOn(i < shown);
}

void C_HumanEquipment::AddToHolster(int typeId, int count)
{
    const int slot = GetHolsterSlot(typeId);
    if (slot == HOLSTER_SLOT_COUNT)
        return;
    S_HolsterSlot &holster = m_holsters[slot];
    const int perFrame = ItemsPerFrame(slot);
    if (holster.itemCount / perFrame < holster.frameCount) {
        for (int i = holster.itemCount; i < holster.itemCount + count; ++i) {
            if (i / perFrame < holster.frameCount) {
                I3D_frame *frame = holster.frames[i / perFrame];
                frame->SetOn(true);
                SetLightLevel(frame);
            }
        }
    }
    holster.itemCount += count;
}

void C_HumanEquipment::SetHolster4Visible(bool on)
{
    S_HolsterSlot &holster = m_holsters[4];
    if (holster.itemCount > 0 && holster.frameCount > 0)
        holster.frames[0]->SetOn(on);
}

void C_HumanEquipment::Unknown793b40()
{
    // The human's model changed: the holsters are rebuilt for it.
    ReleaseHolsters();
    InitHolsters();
    AddBeltToHolsters();
    if (C_Item *item = m_owner->GetContainer(ITEM_CONTAINER_HANDS)->GetItem()) {
        if (GetHolsterSlot(item->GetTypeId()) == HOLSTER_SLOT_SIDEARM)
            AddToHolster(item->GetTypeId(), 1);
    }
    FindJoints(m_owner->GetFrame());   // (inline in the original)
}

// ------------------------------------------------------------------ the hands

void C_HumanEquipment::OnAnimationEvent(int event)
{
    C_HumanWeaponPose *pose = m_owner->GetMovement()->GetWeaponPose();
    switch (event) {
    case EQUIPMENT_EVENT_0:
        if (!pose->IsSupportHandOn()) {
            if (!(m_flags & EQUIPMENT_HANDS_PUT_AWAY))
                pose->SetSupportHand(true);
            else
                m_flags |= EQUIPMENT_EVENT0_PENDING;
        }
        return;
    case EQUIPMENT_EVENT_1:
        if (pose->IsSupportHandOn()) {
            if (!(m_flags & EQUIPMENT_HANDS_PUT_AWAY))
                pose->SetSupportHand(false);
            else
                m_flags &= ~EQUIPMENT_EVENT0_PENDING;
        }
        return;
    case EQUIPMENT_EVENT_GIVE: {
        I3D_frame *given = nullptr;
        if (m_handsModel && pose->IsSupportHandOn())
            OnAnimationEvent(EQUIPMENT_EVENT_1);
        if (m_giveTo) {
            given = m_giveTo->GetEquipment()->GetModel(m_giveToPlace);
            m_giveTo->GetEquipment()->AttachModel(m_giveToPlace);
            m_giveTo = nullptr;
        }
        if (m_handsModel && given != m_handsModel)
            DetachModel(EQUIPMENT_PLACE_HANDS);
        m_handsModel = nullptr;
        return;
    }
    case EQUIPMENT_EVENT_TAKE:
        if (m_takeFrom) {
            if (!m_takeFromAttach)
                m_takeFrom->GetEquipment()->DetachModel(m_takeFromPlace);
            else
                m_takeFrom->GetEquipment()->AttachModel(m_takeFromPlace);
            m_takeFrom = nullptr;
            m_takeFromAttach = false;
        }
        if (m_handsModel)
            AttachModel((m_flags & EQUIPMENT_HANDS_PUT_AWAY) ? 10 : EQUIPMENT_PLACE_HANDS);
        return;
    case EQUIPMENT_EVENT_PROJECTILE_HAND2:
    case EQUIPMENT_EVENT_PROJECTILE_HAND1:
    case EQUIPMENT_EVENT_PROJECTILE_WEAPON: {
        C_Item *item = m_owner->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
        if (!item)
            return;
        I3D_frame *dummy = event == EQUIPMENT_EVENT_PROJECTILE_HAND2 ? m_handDummy2
                           : event == EQUIPMENT_EVENT_PROJECTILE_HAND1 ? m_handDummy1 : nullptr;
        item->AttachProjectile(dummy);
        return;
    }
    default:
        break;
    }
    if (event >= EQUIPMENT_EVENT_SPECIAL_FIRST && event <= EQUIPMENT_EVENT_SPECIAL_LAST) {
        const int specialPlace = EQUIPMENT_PLACE_SPECIAL_FIRST + (event - EQUIPMENT_EVENT_SPECIAL_FIRST) / 2;
        if ((event - EQUIPMENT_EVENT_SPECIAL_FIRST) % 2 == 0)
            DetachModel(specialPlace);
        else
            AttachModel(specialPlace);
    }
}

bool C_HumanEquipment::ShowHandsItem(bool show, int /*unused*/, bool toggle)
{
    if (toggle)
        show = !m_handsShown;
    if (m_handsShown == show || !m_handsModel)
        return false;
    C_HumanMovement *movement = m_owner->GetMovement();
    const int mode = toggle ? 2 : (show ? 1 : 0);
    if (!m_handsShown) {
        movement->GetWeaponPose()->SetMode(mode);
        m_handsModel->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
        m_handsModel->Update();
        movement->SetUnknown610_64(0);
        movement->SetUnknown610_78(2);
        m_handsShown = !m_handsShown;
        return true;
    }
    OnAnimationEvent(EQUIPMENT_EVENT_1);
    movement->GetWeaponPose()->SetMode(mode);
    m_handsModel->Update();
    movement->SetUnknown610_78(0);
    movement->SetUnknown610_64(2);
    m_handsShown = !m_handsShown;
    // (The original returns false here although the state changed.)
    return false;
}

void C_HumanEquipment::PutAwayHandsItem(bool putAway)
{
    C_HumanMovement *movement = m_owner->GetMovement();
    C_HumanWork *work = m_owner->GetWork();
    const bool hudForThis = IsClient() && g_teamManager.GetPlayerTeam()->GetActivePlayer() == m_owner;
    if (!putAway) {
        if (!(m_flags & EQUIPMENT_HANDS_PUT_AWAY))
            return;
        m_flags &= ~EQUIPMENT_HANDS_PUT_AWAY;
        if (m_handsModel) {
            AttachModel(EQUIPMENT_PLACE_HANDS);
            if (C_Item *item = m_owner->FindItemByUid(m_handsUid)) {
                work->SetUnknownB0(true);
                work->SetCarryMode(item->GetDef()->GetCarryMode(), true);
                work->SetUnknownB0(false);
                movement->SetHandsCarry(item->GetDef()->GetCarryMode(), 3);
            }
        }
        if (m_flags & EQUIPMENT_EVENT0_PENDING)
            OnAnimationEvent(EQUIPMENT_EVENT_0);
        m_flags = 0;
        if (hudForThis && m_handsModel)
            GetGame()->GetFirstPersonView()->OnInventoryEvent(FPV_EVENT_DRAW, 0);
        return;
    }
    if (m_handsUid >= 0) {
        if (C_Item *item = m_owner->FindItemByUid(m_handsUid)) {
            const int behaviour = item->GetDef()->GetBehaviour();
            if (behaviour < 3 || (behaviour > 7 && behaviour != 12)) {
                // Small things are only hidden.
                m_flags |= EQUIPMENT_HANDS_PUT_AWAY;
                if (hudForThis)
                    GetGame()->GetFirstPersonView()->OnInventoryEvent(FPV_EVENT_PUT_AWAY, 0);
                if (m_handsModel)
                    m_handsModel->SetOn(false);
                goto releaseHands;
            }
            m_flags &= ~EQUIPMENT_HANDS_PUT_AWAY;
        }
    }
    if (m_flags & EQUIPMENT_HANDS_PUT_AWAY)
        return;
    m_flags |= EQUIPMENT_HANDS_PUT_AWAY;
    if (movement->GetWeaponPose()->IsSupportHandOn())
        OnAnimationEvent(EQUIPMENT_EVENT_1);
    if (!m_handsModel)
        return;
    if (hudForThis)
        GetGame()->GetFirstPersonView()->OnInventoryEvent(FPV_EVENT_PUT_AWAY, 0);
    AttachModel(10);
releaseHands:
    movement->GetWeaponPose()->SetModel(nullptr, -1);
    movement->SetHandsCarry(0, 0);
    work->SetUnknownB0(true);
    work->SetCarryMode(100, true);
    work->SetUnknownB0(false);
}

void C_HumanEquipment::Unknown792310()
{
    C_HumanMovement *movement = m_owner->GetMovement();
    if (movement->GetUnknown1E8() == 0x20)
        movement->FadeOutMixerTrack(1, static_cast<int>(350.0f / 1.4f));
}

S_vector C_HumanEquipment::GetDropPosition(int place)
{
    // (Item model offset plus the joint's own local position, through the joint's world
    // matrix, as in the original.)
    switch (place) {
    case EQUIPMENT_PLACE_HEAD:
        return (g_headModelOffset + m_jointF->GetPos()) * m_jointF->GetMatrix();
    case EQUIPMENT_PLACE_BACKPACK:
        return (g_backpackModelOffset + m_backpackJoint->GetPos()) * m_backpackJoint->GetMatrix();
    case EQUIPMENT_PLACE_LONG_GUN:
        return (g_longGunModelOffset + m_jointD->GetPos()) * m_jointD->GetMatrix();
    default:
        break;
    }
    if (m_handDummy1)
        // Fix: the original also writes the matrix's w into the 4 bytes after the result.
        return m_handDummy1->GetWorldPos();
    return S_vector(0.0f, 0.0f, 0.0f);
}

void C_HumanEquipment::Unknown7938f0(int time)
{
    // The special object follows the hand in world space (it is linked to the sector).
    if (!m_specialModel)
        return;
    m_specialModel->SetPos(m_handDummy1->GetWorldPos());
    m_specialModel->SetDir(m_handDummy1->GetWorldDir(), 0.0f);
    m_specialModel->Update();
    GetGame()->GetScene()->Unknown144(m_specialModel);
    m_specialModel->LinkTo(m_owner->GetMovement()->GetSectorFrame(), LINK_1);
    m_specialModel->SetOn(m_owner->GetFrame()->IsOn());
    if (m_unknownD0 > 0.0f && time > 0)
        m_specialModel->Tick(static_cast<int>(static_cast<float>(time) * m_unknownD0 * 2.0f));
}

// ------------------------------------------------------------------ inventory changes

void C_HumanEquipment::OnItemCreated(C_Item *item, int container)
{
    switch (container) {
    case ITEM_CONTAINER_HEAD:
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_HEAD, item->GetUid(), NO_TIME, -1);
        AttachModel(EQUIPMENT_PLACE_HEAD);
        return;
    case ITEM_CONTAINER_BELT:
        AddToHolster(item->GetTypeId(), 1);
        return;
    case ITEM_CONTAINER_LONG_GUN:
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_LONG_GUN, item->GetUid(), NO_TIME, item->GetTypeId());
        AttachModel(EQUIPMENT_PLACE_LONG_GUN);
        return;
    case ITEM_CONTAINER_HANDS: {
        if (GetHolsterSlot(item->GetTypeId()) == HOLSTER_SLOT_SIDEARM)
            AddToHolster(item->GetTypeId(), 1);
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_HANDS, item->GetUid(), NO_TIME, item->GetTypeId());
        C_HumanWork *work = m_owner->GetWork();
        m_owner->GetMovement()->SetHandsCarry(item->GetDef()->GetCarryMode(), 3);
        work->SetUnknownB0(true);
        work->SetCarryMode(item->GetDef()->GetCarryMode(), false);
        work->SetUnknownB0(false);
        m_owner->GetMovement()->Unknown7585d0(true);
        return;
    }
    case ITEM_CONTAINER_BACKPACK:
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_BACKPACK, item->GetUid(), NO_TIME, item->GetTypeId());
        AttachModel(EQUIPMENT_PLACE_BACKPACK);
        return;
    default:
        return;
    }
}

void C_HumanEquipment::OnItemAdded(C_Item *item, int container, C_Actor *from, int fromContainer, bool unknown)
{
    // From the hands of a living human: a hand-over (only that human's animation event shows
    // the model here, unless its hands item is put away).
    C_HumanEquipment *giver = nullptr;
    if (fromContainer == ITEM_CONTAINER_HANDS && from && static_cast<C_Human *>(from)->GetHealth() > 0)
        giver = static_cast<C_Human *>(from)->GetEquipment();
    const bool attachNow = !giver || (giver->m_flags & EQUIPMENT_HANDS_PUT_AWAY) != 0;

    switch (container) {
    case ITEM_CONTAINER_HEAD:
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_HEAD, item->GetUid(), NO_TIME, -1);
        if (attachNow)
            AttachModel(EQUIPMENT_PLACE_HEAD);
        if (giver && (giver->m_flags & EQUIPMENT_HANDS_PUT_AWAY))
            giver->ResetHands();
        return;
    case ITEM_CONTAINER_BELT:
        AddToHolster(item->GetTypeId(), 1);
        break;
    case ITEM_CONTAINER_LONG_GUN:
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_LONG_GUN, item->GetUid(), NO_TIME, item->GetTypeId());
        if (attachNow)
            AttachModel(EQUIPMENT_PLACE_LONG_GUN);
        break;
    case ITEM_CONTAINER_HANDS: {
        if (GetHolsterSlot(item->GetTypeId()) == HOLSTER_SLOT_SIDEARM)
            AddToHolster(item->GetTypeId(), 1);
        if (m_owner->GetHealth() < 1) {
            SetModel(item->GetFrame(0), EQUIPMENT_PLACE_HANDS, item->GetUid(), NO_TIME, item->GetTypeId());
            if (!giver)
                m_owner->GetEquipment()->AttachModel(EQUIPMENT_PLACE_HANDS);
            return;
        }
        I3D_frame *previous = GetModel(EQUIPMENT_PLACE_HANDS);
        const int previousUid = m_handsUid;
        const int previousTypeId = m_handsTypeId;
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_HANDS, item->GetUid(), NO_TIME, item->GetTypeId());
        if (attachNow) {
            if (!(m_flags & EQUIPMENT_HANDS_PUT_AWAY)) {
                // The take animation's event finishes it (EQUIPMENT_EVENT_TAKE).
                m_takeFrom = static_cast<C_Human *>(from);
                m_takeFromAttach = unknown;
                if (unknown)
                    OnAnimationEvent(EQUIPMENT_EVENT_1);
                switch (fromContainer) {
                case ITEM_CONTAINER_HEAD:
                    m_takeFromPlace = EQUIPMENT_PLACE_HEAD;
                    break;
                case ITEM_CONTAINER_BELT:
                    // From the belt: the previous hands model goes away at once.
                    m_takeFrom = nullptr;
                    if (previous) {
                        m_handsTime = NO_TIME;
                        SetModel(previous, EQUIPMENT_PLACE_HANDS, previousUid, NO_TIME, previousTypeId);
                        DetachModel(EQUIPMENT_PLACE_HANDS);
                        m_handsTime = NO_TIME;
                        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_HANDS, item->GetUid(), NO_TIME, item->GetTypeId());
                    }
                    break;
                case ITEM_CONTAINER_LONG_GUN:
                    m_takeFromPlace = EQUIPMENT_PLACE_LONG_GUN;
                    break;
                case ITEM_CONTAINER_HANDS:
                    m_takeFromPlace = EQUIPMENT_PLACE_HANDS;
                    break;
                case ITEM_CONTAINER_BACKPACK:
                    m_takeFromPlace = EQUIPMENT_PLACE_BACKPACK;
                    break;
                default:
                    m_takeFrom = nullptr;
                    break;
                }
            } else if (fromContainer == ITEM_CONTAINER_HANDS && from &&
                       !(static_cast<C_Human *>(from)->GetEquipment()->m_flags & EQUIPMENT_HANDS_PUT_AWAY)) {
                // From the hands of a dead human: that human's give event attaches the model.
                // Fix: the original tests the giver's flags first and reads a null `from`.
                // (It sets this side's m_giveToPlace, not the giver's.)
                static_cast<C_Human *>(from)->GetEquipment()->m_giveTo = m_owner;
                m_giveToPlace = EQUIPMENT_PLACE_HANDS;
            } else {
                AttachModel(EQUIPMENT_PLACE_HANDS);
            }
        }
        C_HumanMovement *movement = m_owner->GetMovement();
        C_HumanWork *work = m_owner->GetWork();
        movement->SetUnknownDC8(3);
        movement->SetHandsCarry(0, 0);
        work->SetUnknownB0(true);
        work->SetCarryMode(item->GetDef()->GetCarryMode(), false);
        work->SetUnknownB0(false);
        return;
    }
    case ITEM_CONTAINER_BACKPACK:
        SetModel(item->GetFrame(0), EQUIPMENT_PLACE_BACKPACK, item->GetUid(), NO_TIME, item->GetTypeId());
        if (attachNow)
            AttachModel(EQUIPMENT_PLACE_BACKPACK);
        break;
    default:
        return;
    }
    if (giver && (giver->m_flags & EQUIPMENT_HANDS_PUT_AWAY))
        giver->ResetHands();
}

void C_HumanEquipment::OnItemRemoved(C_Item *item, int container, C_Actor *to, int toContainer)
{
    // Into the hands of a living human whose hands item is not put away: that human's take
    // animation moves the model (EQUIPMENT_EVENT_TAKE).
    const bool handOver = toContainer == ITEM_CONTAINER_HANDS && to && static_cast<C_Human *>(to)->GetHealth() > 0 &&
                          !(static_cast<C_Human *>(to)->GetEquipment()->m_flags & EQUIPMENT_HANDS_PUT_AWAY);
    switch (container) {
    case ITEM_CONTAINER_HEAD:
        if (!handOver) {
            DetachModel(EQUIPMENT_PLACE_HEAD);
            m_headModel = nullptr;
        }
        return;
    case ITEM_CONTAINER_BELT:
        RemoveFromHolster(item->GetTypeId(), 1);
        return;
    case ITEM_CONTAINER_LONG_GUN:
        if (!handOver) {
            DetachModel(EQUIPMENT_PLACE_LONG_GUN);
            m_longGunModel = nullptr;
        }
        return;
    case ITEM_CONTAINER_HANDS: {
        if (GetHolsterSlot(item->GetTypeId()) == HOLSTER_SLOT_SIDEARM)
            RemoveFromHolster(item->GetTypeId(), 1);
        C_HumanWork *work = m_owner->GetWork();
        if (handOver) {
            m_handsModel = nullptr;
            m_owner->GetMovement()->GetWeaponPose()->SetModel(nullptr, -1);
        } else {
            if (!to) {
                m_owner->GetEquipment()->DetachModel(EQUIPMENT_PLACE_HANDS);
                m_owner->GetMovement()->GetWeaponPose()->SetModel(nullptr, -1);
                work->SetUnknownB0(true);
                work->SetCarryMode(100, false);
                work->SetUnknownB0(false);
                return;
            }
            if (m_owner->GetHealth() < 1) {
                m_owner->GetEquipment()->DetachModel(EQUIPMENT_PLACE_HANDS);
                return;
            }
            // The give animation's event finishes it (EQUIPMENT_EVENT_GIVE).
            m_giveTo = static_cast<C_Human *>(to);
            switch (toContainer) {
            case ITEM_CONTAINER_HEAD:
                m_giveToPlace = EQUIPMENT_PLACE_HEAD;
                break;
            case ITEM_CONTAINER_LONG_GUN:
                m_giveToPlace = EQUIPMENT_PLACE_LONG_GUN;
                break;
            case ITEM_CONTAINER_HANDS:
                m_giveToPlace = EQUIPMENT_PLACE_HANDS;
                break;
            case ITEM_CONTAINER_BACKPACK:
                m_giveToPlace = EQUIPMENT_PLACE_BACKPACK;
                break;
            default:
                m_giveTo = nullptr;
                break;
            }
        }
        work->SetUnknownB0(true);
        work->SetCarryMode(0, false);
        work->SetUnknownB0(false);
        return;
    }
    case ITEM_CONTAINER_BACKPACK:
        if (!handOver) {
            DetachModel(EQUIPMENT_PLACE_BACKPACK);
            m_backpackModel = nullptr;
        }
        return;
    default:
        return;
    }
}

// ------------------------------------------------------------------ time

void C_HumanEquipment::Update(int time)
{
    float tiltStep = static_cast<float>(time) * 0.0007f;
    float slideStep = static_cast<float>(time) * 0.0012f;
    if (m_handsTime != NO_TIME && (m_handsTime -= time) < 1) {
        // The hands model shows once the take animation is far enough.
        if (m_owner->GetHealth() < 1) {
            m_handsTime = NO_TIME;
        } else {
            AttachModel(EQUIPMENT_PLACE_HANDS);
            if (C_ItemDef *def = GetItemDef(m_handsTypeId)) {
                const int behaviour = def->GetBehaviour();
                if (behaviour != ITEM_BEHAVIOUR_THROWN && behaviour != ITEM_BEHAVIOUR_SIDEARM)
                    OnAnimationEvent(EQUIPMENT_EVENT_0);
            }
            if (m_owner->GetWork()->GetUnknown5C() == 1)
                m_owner->GetMovement()->GetWeaponPose()->RaiseWeapon(300);
        }
    }

    if (I3D_frame *gun = m_longGunModel) {
        // The long gun on the back slides out and tilts while lifted, and back.
        if (m_longGunLift == 1) {
            if (m_longGunSlide < m_longGunMaxSlide) {
                if (m_longGunMaxSlide < slideStep + m_longGunSlide)
                    slideStep = m_longGunMaxSlide - m_longGunSlide;
                m_longGunSlide += slideStep;
                const S_vector pos = gun->GetPos();
                gun->SetPosInline(S_vector(pos.x, pos.y, pos.z - slideStep));
            }
        } else if (m_longGunSlide > 0.0f && m_longGunTilt <= 0.0f) {
            if (m_longGunSlide - slideStep < 0.0f)
                slideStep = m_longGunSlide;
            m_longGunSlide -= slideStep;
            const S_vector pos = gun->GetPos();
            gun->SetPosInline(S_vector(pos.x, pos.y, pos.z + slideStep));
        }
        if (m_longGunLift == 1) {
            if (m_longGunTilt < m_longGunMaxTilt) {
                if (m_longGunMaxTilt < tiltStep + m_longGunTilt)
                    tiltStep = m_longGunMaxTilt - m_longGunTilt;
                m_longGunTilt += tiltStep;
                Rotate(gun, S_vector(1.0f, 0.0f, 0.0f), -tiltStep);
            }
        } else if (m_longGunTilt > 0.0f) {
            if (m_longGunTilt - tiltStep < 0.0f)
                tiltStep = m_longGunTilt;
            m_longGunTilt -= tiltStep;
            Rotate(gun, S_vector(1.0f, 0.0f, 0.0f), tiltStep);
        }
    }

    if (m_flashlightTime != NO_TIME && (m_flashlightTime -= time) < 1) {
        // The flashlight's sectors are left.
        I3D_light *light = static_cast<I3D_light *>(m_flashlight);
        m_flashlightTime = NO_TIME;
        for (int i = 0; i < light->NumSectors(); ++i)
            light->GetSector(i)->RemoveLight(light);
    }
    if (m_lensFlareTime != NO_TIME && (m_lensFlareTime -= time) < 1)
        UpdateLensFlare(false);
}

int C_HumanEquipment::GetHandsProjectileKind()
{
    C_ItemContainer *hands = m_owner->GetContainer(ITEM_CONTAINER_HANDS);
    if (!hands->GetItem())
        return 0;
    return hands->GetItem()->GetProjectileKind();
}

void C_HumanEquipment::UpdateModelNames()
{
    // The movement refreshes its copies of the attached models (RefreshAttachedModels) by
    // their names, so they are renamed after their kind for the moment: "sm" / "me" / "lo"
    // (small / medium / long weapons) and "panzer", "bigbag" / "smlbag", "helm" / "brig" /
    // "lod" (helmet / field cap / cap).
    // (A model without a known item gets an empty name back.)
    C_str handsName;
    C_str backpackName;
    C_str headName;
    if (m_handsModel && m_handsTypeId >= 0) {
        C_ItemDef *def = GetItemDef(m_handsTypeId);
        handsName = m_handsModel->GetName();
        if (def) {
            switch (def->GetBehaviour()) {
            case 0:
            case 1:
            case 2:
                m_handsModel->SetName("sm");
                break;
            case 3:
                m_handsModel->SetName("me");
                break;
            case 4:
            case 5:
            case 7:
            case 12:
                m_handsModel->SetName("lo");
                break;
            case 6:
                m_handsModel->SetName("panzer");
                break;
            default:
                break;
            }
        }
    }
    if (m_backpackModel && m_backpackTypeId >= 0) {
        if (C_ItemDef *def = GetItemDef(m_backpackTypeId)) {
            backpackName = m_backpackModel->GetName();
            if (def->GetComponentKind() == ICK_BACKPACK) {
                const auto *backpack = static_cast<const C_BackpackComp *>(def->GetComponent());
                m_backpackModel->SetName(backpack->GetMaxWeight() > 30.0f ? "bigbag" : "smlbag");
            }
        }
    }
    if (m_headModel && m_headUid >= 0) {
        if (C_Item *item = m_owner->FindItemByUid(m_headUid)) {
            I3D_frame *frame = item->GetFrame(0);
            headName = frame->GetName();
            C_ItemDef *def = GetItemDef(item->GetTypeId());
            if (def && def->GetComponentKind() == ICK_HEADGEAR) {
                switch (static_cast<const C_HeadgearComp *>(def->GetComponent())->GetHeadgearType()) {
                case 0:
                    frame->SetName("helm");
                    break;
                case 1:
                    frame->SetName("brig");
                    break;
                case 2:
                    frame->SetName("lod");
                    break;
                default:
                    break;
                }
            }
        }
    }

    C_HumanMovement *movement = m_owner->GetMovement();
    I3D_frame *body = m_owner->GetFrame();
    const bool wasOn = body->IsOn();
    const bool shown = wasOn || (m_owner->IsLocalPlayer() && GetGame()->IsFirstPersonView());
    body->SetOn(true);
    movement->RefreshAttachedModels();
    if (m_handsModel)
        m_handsModel->SetName(handsName);
    if (m_backpackModel)
        m_backpackModel->SetName(backpackName);
    if (m_headModel)
        m_headModel->SetName(headName);
    body->SetOn(wasOn);
    bool visible = false;
    if (shown) {
        const int a44 = movement->GetSeatMode();
        visible = a44 < 1 || (a44 >= 6 && movement->GetState() != 0x9F);
    }
    movement->SetModelVisible(visible);
}

// ------------------------------------------------------------------ the special object's light

void C_HumanEquipment::SetFlashlight(bool on)
{
    if (!IsLocalGame() || !m_specialModel)
        return;
    I3D_frame *fire = m_specialModel->FindChildFrame(FIRE_DUMMY, FRAME_MASK_ANY);
    I3D_frame *lensFlare = m_specialModel->FindChildFrame(LENS_FLARE_FRAME, FRAME_MASK_VISUAL);
    I3D_light *light = static_cast<I3D_light *>(m_flashlight);
    if (fire) {
        if (!on) {
            // Out: the object is dropped (its smoke goes away).
            if (I3D_frame *smoke = m_specialModel->FindChildFrame(LIGHT_SMOKE_FRAME, FRAME_MASK_ANY)) {
                smoke->LinkTo(nullptr, 0);
                smoke->Release();
            }
            m_flashlightTime = NO_TIME;
            for (int i = 0; i < light->NumSectors(); ++i)
                light->GetSector(i)->RemoveLight(light);
            UpdateLensFlare(false);
            m_specialModel->LinkTo(nullptr, 0);
            // (Two references: AttachModel's and the one SetModel's caller gave.)
            m_specialModel->Release();
            m_specialModel->Release();
            m_specialModel = nullptr;
            return;
        }
        light->SetLightType(I3DLIGHT_POINT);
        light->SetColor(S_vector(0.35f, 0.35f, 0.1f));
        light->SetPower(0.4f);
        light->SetUnknown8C(0.0f);
        light->SetUnknown90(0);
        light->SetUnknown9C(0.3f, 0.0f, 0.0f);
        light->SetRange(1.5f);
        light->LinkTo(fire, 0);
        m_flashlightTime = LIGHT_TIME;
        GetGame()->GetScene()->GetSector(light->GetWorldPos())->AddLight(light);
    }
    if (lensFlare) {
        lensFlare->SetOn(on);
        lensFlare->Update();
    }
}

void C_HumanEquipment::UpdateLensFlare(bool on)
{
    if (!m_specialModel)
        return;
    auto *flare = static_cast<I3D_lensflare_visual *>(m_specialModel->FindChildFrame(LENS_FLARE_FRAME, FRAME_MASK_VISUAL));
    if (!flare)
        return;
    if (on) {
        m_lensFlareTime = LIGHT_TIME;
        flare->SetUnknownParams(0.75f, 0.13f, 100.0f, 0.22f);
        flare->SetOn(true);
        flare->Update();
        return;
    }
    m_lensFlareTime = NO_TIME;
    flare->SetUnknownParams(0.75f, 0.1f, 50.0f, 0.15f);
    flare->Update();
}

// ------------------------------------------------------------------ save games

bool C_HumanEquipment::SaveGame(C_chunk &chunk)
{
    S_SavedModel hands;
    S_SavedModel special;
    S_SavedModel head;
    S_SavedModel longGun;
    S_SavedModel backpack;
    bool backpackOn = true;
    bool longGunOn = true;
    // (The original writes whatever its stack held as the position and rotation of a
    // missing model.)
    if (m_handsModel) {
        hands.link = HandModelLink(m_handsModel->GetParent(), m_handDummy1, m_handDummy2, m_jointD);
        hands.pos = m_handsModel->GetPos();
        hands.rot = m_handsModel->GetRot();
    }
    if (m_specialModel) {
        special.link = HandModelLink(m_specialModel->GetParent(), m_handDummy1, m_handDummy2, m_jointD);
        special.pos = m_specialModel->GetPos();
        special.rot = m_specialModel->GetRot();
    }
    if (m_headModel) {
        head.link = m_headModel->GetParent() == m_jointF ? SAVED_LINK_PLACE : SAVED_LINK_OTHER;
        head.pos = m_headModel->GetPos();
        head.rot = m_headModel->GetRot();
    }
    if (m_longGunModel) {
        // (A long gun elsewhere counts as none.)
        longGun.link = m_longGunModel->GetParent() == m_jointD ? SAVED_LINK_PLACE : SAVED_LINK_NONE;
        longGun.pos = m_longGunModel->GetPos();
        longGun.rot = m_longGunModel->GetRot();
        longGunOn = m_longGunModel->IsOn();
    }
    if (m_backpackModel) {
        backpack.link = m_backpackModel->GetParent() == m_backpackJoint ? SAVED_LINK_PLACE : SAVED_LINK_OTHER;
        backpack.pos = m_backpackModel->GetPos();
        backpack.rot = m_backpackModel->GetRot();
        backpackOn = m_backpackModel->IsOn();
    }
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_EQUIPMENT))
        return false;
    chunk.WriteChunk(CHUNK_EQUIPMENT_GIVE_TO, m_giveTo ? m_giveTo->GetNetId() : -1);
    chunk.WriteChunk(CHUNK_EQUIPMENT_GIVE_TO_PLACE, m_giveToPlace);
    chunk.WriteChunk(CHUNK_EQUIPMENT_TAKE_FROM, m_takeFrom ? m_takeFrom->GetNetId() : -1);
    chunk.WriteChunk(CHUNK_EQUIPMENT_TAKE_FROM_PLACE, m_takeFromPlace);
    chunk.WriteChunk(CHUNK_EQUIPMENT_TAKE_FROM_ATTACH, static_cast<int>(m_takeFromAttach));
    chunk.WriteChunk(CHUNK_EQUIPMENT_UNKNOWN_2C, static_cast<int>(m_unknown2c));
    chunk.WriteChunk(CHUNK_EQUIPMENT_HANDS_SHOWN, static_cast<int>(m_handsShown));
    chunk.WriteChunk(CHUNK_EQUIPMENT_FLAGS, m_flags);
    chunk.WriteChunk(CHUNK_EQUIPMENT_UNKNOWN_D0, m_unknownD0);
    chunk.WriteChunk(CHUNK_EQUIPMENT_SPECIAL_PLACE, m_specialPlace);
    WriteQuat(chunk, CHUNK_EQUIPMENT_SPECIAL_ROT, m_specialRot);
    chunk.WriteChunk(CHUNK_EQUIPMENT_SPECIAL_POS, m_specialPos);
    chunk.WriteChunk(CHUNK_EQUIPMENT_LIGHT_TIME, m_flashlightTime);
    chunk.WriteChunk(CHUNK_EQUIPMENT_LENS_FLARE_TIME, m_lensFlareTime);
    chunk.WriteChunk(CHUNK_EQUIPMENT_HANDS_LINK, hands.link);
    chunk.WriteChunk(CHUNK_EQUIPMENT_SPECIAL_LINK, special.link);
    chunk.WriteChunk(CHUNK_EQUIPMENT_HEAD_LINK, head.link);
    chunk.WriteChunk(CHUNK_EQUIPMENT_LONG_GUN_LINK, longGun.link);
    chunk.WriteChunk(CHUNK_EQUIPMENT_BACKPACK_LINK, backpack.link);
    chunk.WriteChunk(CHUNK_EQUIPMENT_HANDS_UID, m_handsUid);
    chunk.WriteChunk(CHUNK_EQUIPMENT_HANDS_TYPE, m_handsTypeId);
    chunk.WriteChunk(CHUNK_EQUIPMENT_BACKPACK_TYPE, m_backpackTypeId);
    chunk.WriteChunk(CHUNK_EQUIPMENT_LONG_GUN_UID, m_longGunUid);
    chunk.WriteChunk(CHUNK_EQUIPMENT_HEAD_UID, m_headUid);
    chunk.WriteChunk(CHUNK_EQUIPMENT_BACKPACK_UID, m_backpackUid);
    chunk.WriteChunk(CHUNK_EQUIPMENT_SPECIAL_TIME, m_specialTime);
    // The scene frame a special object taken from the scene is found by again.
    const char *name = "";
    if (m_specialTime == SPECIAL_FROM_SCENE || m_specialTime == SPECIAL_FROM_SCENE_PICKED_UP) {
        if (m_handsModel)
            name = m_handsModel->GetName();
        else if (m_specialModel)
            name = m_specialModel->GetName();
    }
    chunk.WriteChunk(CHUNK_EQUIPMENT_SPECIAL_NAME, name, static_cast<int>(std::strlen(name)) + 1);
    chunk.WriteChunk(CHUNK_EQUIPMENT_HANDS_POS, hands.pos);
    WriteQuat(chunk, CHUNK_EQUIPMENT_HANDS_ROT, hands.rot);
    chunk.WriteChunk(CHUNK_EQUIPMENT_SPECIAL_MODEL_POS, special.pos);
    WriteQuat(chunk, CHUNK_EQUIPMENT_SPECIAL_MODEL_ROT, special.rot);
    chunk.WriteChunk(CHUNK_EQUIPMENT_HEAD_POS, head.pos);
    WriteQuat(chunk, CHUNK_EQUIPMENT_HEAD_ROT, head.rot);
    chunk.WriteChunk(CHUNK_EQUIPMENT_LONG_GUN_POS, longGun.pos);
    WriteQuat(chunk, CHUNK_EQUIPMENT_LONG_GUN_ROT, longGun.rot);
    chunk.WriteChunk(CHUNK_EQUIPMENT_BACKPACK_POS, backpack.pos);
    WriteQuat(chunk, CHUNK_EQUIPMENT_BACKPACK_ROT, backpack.rot);
    chunk.WriteChunk(CHUNK_EQUIPMENT_LONG_GUN_TYPE, m_longGunTypeId);
    chunk.WriteChunk(CHUNK_EQUIPMENT_HANDS_TIME, m_handsTime);
    chunk.WriteChunk(CHUNK_EQUIPMENT_BACKPACK_ON, static_cast<int>(backpackOn));
    chunk.WriteChunk(CHUNK_EQUIPMENT_LONG_GUN_ON, static_cast<int>(longGunOn));
    chunk.Ascend();
    // (The original always reports failure.)
    return false;
}

bool C_HumanEquipment::LoadGame(C_chunk &chunk)
{
    S_SavedModel hands;
    S_SavedModel special;
    S_SavedModel head;
    S_SavedModel longGun;
    S_SavedModel backpack;
    bool backpackOn = true;
    bool longGunOn = true;
    C_str specialName;
    for (I3D_frame *gear : m_divingGear) {
        if (gear)
            gear->LinkTo(nullptr, 0);
    }
    Clear();
    HideHolsters();
    AddBeltToHolsters();
    if (C_Item *item = m_owner->GetContainer(ITEM_CONTAINER_HANDS)->GetItem()) {
        if (GetHolsterSlot(item->GetTypeId()) == HOLSTER_SLOT_SIDEARM)
            AddToHolster(item->GetTypeId(), 1);
    }

    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_EQUIPMENT) {
        chunk.Ascend();
        return false;
    }
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        int value = 0;
        switch (id) {
        case CHUNK_EQUIPMENT_GIVE_TO:
            chunk.Read(value);
            m_giveTo = value == -1 ? nullptr : static_cast<C_Human *>(g_actorsModule.GetActor(value));
            break;
        case CHUNK_EQUIPMENT_GIVE_TO_PLACE:
            chunk.Read(m_giveToPlace);
            break;
        case CHUNK_EQUIPMENT_TAKE_FROM:
            chunk.Read(value);
            m_takeFrom = value == -1 ? nullptr : static_cast<C_Human *>(g_actorsModule.GetActor(value));
            break;
        case CHUNK_EQUIPMENT_TAKE_FROM_PLACE:
            chunk.Read(m_takeFromPlace);
            break;
        case CHUNK_EQUIPMENT_TAKE_FROM_ATTACH:
            chunk.Read(value);
            m_takeFromAttach = value != 0;
            break;
        case CHUNK_EQUIPMENT_UNKNOWN_2C:
            chunk.Read(value);
            m_unknown2c = value != 0;
            break;
        case CHUNK_EQUIPMENT_HANDS_SHOWN:
            chunk.Read(value);
            m_handsShown = value != 0;
            break;
        case CHUNK_EQUIPMENT_FLAGS:
            chunk.Read(m_flags);
            break;
        case CHUNK_EQUIPMENT_UNKNOWN_D0:
            chunk.Read(m_unknownD0);
            break;
        case CHUNK_EQUIPMENT_SPECIAL_PLACE:
            chunk.Read(m_specialPlace);
            break;
        case CHUNK_EQUIPMENT_SPECIAL_ROT:
            m_specialRot = ReadQuat(chunk);
            break;
        case CHUNK_EQUIPMENT_SPECIAL_POS:
            // (The original also reads 4 bytes past the vector into +0xAC.)
            chunk.Read(m_specialPos);
            break;
        case CHUNK_EQUIPMENT_LIGHT_TIME:
            chunk.Read(m_flashlightTime);
            break;
        case CHUNK_EQUIPMENT_LENS_FLARE_TIME:
            chunk.Read(m_lensFlareTime);
            break;
        case CHUNK_EQUIPMENT_HANDS_LINK:
            chunk.Read(hands.link);
            break;
        case CHUNK_EQUIPMENT_SPECIAL_LINK:
            chunk.Read(special.link);
            break;
        case CHUNK_EQUIPMENT_HEAD_LINK:
            chunk.Read(head.link);
            break;
        case CHUNK_EQUIPMENT_LONG_GUN_LINK:
            chunk.Read(longGun.link);
            break;
        case CHUNK_EQUIPMENT_BACKPACK_LINK:
            chunk.Read(backpack.link);
            break;
        case CHUNK_EQUIPMENT_HANDS_UID:
            chunk.Read(m_handsUid);
            break;
        case CHUNK_EQUIPMENT_HANDS_TYPE:
            chunk.Read(m_handsTypeId);
            break;
        case CHUNK_EQUIPMENT_BACKPACK_TYPE:
            chunk.Read(m_backpackTypeId);
            break;
        case CHUNK_EQUIPMENT_LONG_GUN_UID:
            chunk.Read(m_longGunUid);
            break;
        case CHUNK_EQUIPMENT_HEAD_UID:
            chunk.Read(m_headUid);
            break;
        case CHUNK_EQUIPMENT_BACKPACK_UID:
            chunk.Read(m_backpackUid);
            break;
        case CHUNK_EQUIPMENT_SPECIAL_TIME:
            chunk.Read(m_specialTime);
            break;
        case CHUNK_EQUIPMENT_SPECIAL_NAME: {
            char buffer[1000];
            chunk.ReadString(buffer, sizeof(buffer));
            specialName = buffer;
            break;
        }
        case CHUNK_EQUIPMENT_HANDS_POS:
            chunk.Read(hands.pos);
            break;
        case CHUNK_EQUIPMENT_HANDS_ROT:
            hands.rot = ReadQuat(chunk);
            break;
        case CHUNK_EQUIPMENT_SPECIAL_MODEL_POS:
            chunk.Read(special.pos);
            break;
        case CHUNK_EQUIPMENT_SPECIAL_MODEL_ROT:
            special.rot = ReadQuat(chunk);
            break;
        case CHUNK_EQUIPMENT_HEAD_POS:
            chunk.Read(head.pos);
            break;
        case CHUNK_EQUIPMENT_HEAD_ROT:
            head.rot = ReadQuat(chunk);
            break;
        case CHUNK_EQUIPMENT_LONG_GUN_POS:
            chunk.Read(longGun.pos);
            break;
        case CHUNK_EQUIPMENT_LONG_GUN_ROT:
            longGun.rot = ReadQuat(chunk);
            break;
        case CHUNK_EQUIPMENT_BACKPACK_POS:
            chunk.Read(backpack.pos);
            break;
        case CHUNK_EQUIPMENT_BACKPACK_ROT:
            backpack.rot = ReadQuat(chunk);
            break;
        case CHUNK_EQUIPMENT_LONG_GUN_TYPE:
            chunk.Read(m_longGunTypeId);
            break;
        case CHUNK_EQUIPMENT_HANDS_TIME:
            chunk.Read(m_handsTime);
            break;
        case CHUNK_EQUIPMENT_BACKPACK_ON:
            chunk.Read(value);
            backpackOn = value != 0;
            break;
        case CHUNK_EQUIPMENT_LONG_GUN_ON:
            chunk.Read(value);
            longGunOn = value != 0;
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();

    // The hands model.
    C_HumanMovement *movement = m_owner->GetMovement();
    C_HumanWeaponPose *pose = movement->GetWeaponPose();
    m_handsModel = nullptr;
    m_specialModel = nullptr;
    C_Item *handsItem = hands.link != SAVED_LINK_NONE ? m_owner->FindItemByUid(m_handsUid) : nullptr;
    if (!handsItem) {
        pose->SetModel(nullptr, -1);
    } else {
        m_handsModel = handsItem->GetFrame(0);
        pose->SetModel(m_handsModel, handsItem->GetDef()->GetBehaviour());
        if (hands.link == SAVED_LINK_BACK) {
            m_handsModel->LinkTo(m_jointD, 0);
            RestoreTransform(m_handsModel, hands);
        } else if (hands.link == SAVED_LINK_HAND2) {
            pose->SetMode(0);
            m_handsModel->LinkTo(m_handDummy2, 0);
            RestoreTransform(m_handsModel, hands);
        } else {
            pose->SetMode(1);
            m_handsModel->LinkTo(m_handDummy1, 0);
            RestoreTransform(m_handsModel, hands);
            if ((m_flags & EQUIPMENT_HANDS_PUT_AWAY) && hands.link == SAVED_LINK_PLACE)
                m_handsModel->SetOn(false);
        }
        m_handsModel->Update();
    }

    // The special object, found again where it came from.
    if (special.link != SAVED_LINK_NONE) {
        // (The original builds the message "TS problem: ve scene nenalezen frame, ktery se ma
        // panakovi linknout do ruky ('<name>') <human>" -- the scene frame for the hand is
        // missing -- when the scene has no such frame, and never shows it.)
        I3D_scene *scene = GetGame()->GetScene();
        switch (m_specialTime) {
        case SPECIAL_FROM_SCENE_PICKED_UP:
            if (I3D_frame *frame = scene->FindFrame(specialName, FRAME_MASK_ANY)) {
                m_specialModel = frame;
                AttachModel(EQUIPMENT_PLACE_SPECIAL_FIRST);
            }
            break;
        case SPECIAL_FROM_BELT_TYPE_E5: {
            C_ItemContainer *belt = m_owner->GetContainer(ITEM_CONTAINER_BELT);
            const int count = belt->GetUsedSlotCount();
            // (With no used belt slot the hands item is taken.)
            C_Item *found = count == 0 ? handsItem : nullptr;
            for (int i = count; i-- > 0;) {
                const std::vector<C_Item *> &slot = belt->GetSlots()[i];
                if (!slot.empty() && slot.front()->GetTime() <= GetGame()->GetMissionTime() &&
                    slot.front()->GetTypeId() == SPECIAL_ITEM_TYPE_E5) {
                    found = slot.front();
                    break;
                }
            }
            if (found)
                m_specialModel = found->GetFrame(0);
            break;
        }
        case SPECIAL_FROM_BELT_14:
            if (C_Item *item = m_owner->FindBeltItem(14))
                m_specialModel = item->GetFrame(0);
            break;
        case SPECIAL_FROM_THROWN: {
            C_Item *found = m_owner->FindBeltItem(ITEM_BEHAVIOUR_THROWN);
            C_ItemContainer *contents = m_owner->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS);
            if (!found && contents) {
                for (int i = contents->GetUsedSlotCount(); i-- > 0;) {
                    const std::vector<C_Item *> &slot = contents->GetSlots()[i];
                    if (!slot.empty() && slot.front()->GetTime() <= GetGame()->GetMissionTime() &&
                        slot.front()->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_THROWN) {
                        found = slot.front();
                        break;
                    }
                }
            }
            if (found) {
                m_specialModel = found->GetFrame(0);
                // (One reference more than the other kinds get.)
                m_specialModel->AddRef();
            }
            break;
        }
        case SPECIAL_FROM_BELT_19:
            if (C_Item *item = m_owner->FindBeltItem(19))
                m_specialModel = item->GetFrame(0);
            break;
        case SPECIAL_FROM_SCENE:
            if (I3D_frame *frame = scene->FindFrame(specialName, FRAME_MASK_ANY))
                m_specialModel = frame;
            break;
        case SPECIAL_FROM_PROP_2:
        case SPECIAL_FROM_PROP_0:
        case SPECIAL_FROM_PROP_1: {
            // (The original makes a copy of the activity prop and keeps none of it: the
            // special model stays null.) Fix: the copy is released instead of leaked.
            const int index = m_specialTime == SPECIAL_FROM_PROP_2 ? 2 : m_specialTime == SPECIAL_FROM_PROP_0 ? 0 : 1;
            if (I3D_frame *prop = GetActivityProp(index)) {
                I3D_frame *copy = I3DGetDriver()->CreateFrame(FRAME_MODEL);
                copy->Duplicate(prop);
                copy->Release();
            }
            break;
        }
        default:
            if (m_specialTime > 0 && g_actorsModule.GetActor(m_specialTime))
                m_specialModel = g_actorsModule.GetActor(m_specialTime)->GetFrame();
            break;
        }
        if (m_specialModel && m_specialTime != SPECIAL_FROM_SCENE_PICKED_UP) {
            m_specialModel->AddRef();
            if (special.link == SAVED_LINK_HAND2)
                m_specialModel->LinkTo(m_handDummy2, LINK_1);
            else if (special.link != SAVED_LINK_OTHER)
                m_specialModel->LinkTo(m_handDummy1, LINK_1);
            RestoreTransform(m_specialModel, special);
            m_specialModel->Update();
        }
    }

    // The models on the body.
    m_headModel = nullptr;
    if (head.link != SAVED_LINK_NONE && m_headUid != -1) {
        if (C_Item *item = m_owner->FindItemByUid(m_headUid)) {
            m_headModel = item->GetFrame(0);
            AttachModel(EQUIPMENT_PLACE_HEAD);
            RestoreTransform(m_headModel, head);
            m_headModel->Update();
        }
    }
    m_longGunModel = nullptr;
    if (longGun.link != SAVED_LINK_NONE && m_longGunUid != -1) {
        if (C_Item *item = m_owner->FindItemByUid(m_longGunUid)) {
            m_longGunModel = item->GetFrame(0);
            AttachModel(EQUIPMENT_PLACE_LONG_GUN);
            RestoreTransform(m_longGunModel, longGun);
            m_longGunModel->Update();
            m_longGunModel->SetOn(longGunOn);
        }
    }
    m_backpackModel = nullptr;
    if (backpack.link != SAVED_LINK_NONE && m_backpackUid != -1) {
        if (C_Item *item = m_owner->FindItemByUid(m_backpackUid)) {
            m_backpackModel = item->GetFrame(0);
            AttachModel(EQUIPMENT_PLACE_BACKPACK);
            RestoreTransform(m_backpackModel, backpack);
            m_backpackModel->Update();
            m_backpackModel->SetOn(backpackOn);
        }
    }
    if (movement->GetUnknownA30() > 0 || movement->GetState() == 0x5F) {
        if (m_handsModel)
            m_handsModel->SetOn(false);
        if (m_longGunModel)
            m_longGunModel->SetOn(false);
    }
    UpdateModelNames();
    return true;
}
