// The soldier of the equipment menus (original 0x007d0200..0x007d3650).
#include "game/menu_soldier.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game/equipment_frames.h"
#include "game/game.h"
#include "game/geffects.h"
#include "game/human_weapon_pose.h"
#include "game/item_defs.h"
#include "game/items_module.h"
#include "game/resource_cache.h"
#include "ls3d/i3d.h"

using namespace equipment;

namespace {

// The animations of the soldier (0x008637d4): idles 0..2, the poses of the items in the hands
// (3 + their carry mode; 5 also for carry mode 6) and the rest.
const char *const g_animNames[] = {
    "!static02NEW", "!walkFN6c4", "!vpredP", "!drzgran1", "!klidMG", "(kliddrz21", "(pzbp",
    "!klidMG", "!bazKLIDst",
};

// The model without a uniform.
const char *const NAKED_MODEL = "mp_Na01";
// The uniforms' models are the menu copies "mp_<model name>".
const char *const BODY_MODEL_PREFIX = "mp_";
// Both lights' name.
const char *const LIGHT_NAME = "C_AeMenu1";
const char *const FLASH_FRAME = "flash";

// I3D_frame::LinkTo flags used here (meanings not identified).
constexpr uint32_t LINK_1 = 1;
constexpr uint32_t LINK_2 = 2;

// Stages of the model's animations: the idles blend between 0 and 1, the poses between 2 and 3.
constexpr int POSE_STAGE_A = 2;
constexpr int POSE_STAGE_B = 3;
constexpr int ANIM_FLAGS = 2;
constexpr int IDLE_FADE = 800;                          // ms
constexpr int POSE_FADE = 400;                          // ms
// The pose of the empty hands, of the items in the hands (POSE_HANDS + carry mode) and of
// carry mode 6.
constexpr int POSE_HANDS = 3;
constexpr int POSE_CARRY_6 = 5;
constexpr int CARRY_MODE_6 = 6;
// How fast the soldier turns (radians per tick).
constexpr float TURN_SPEED = 0.009f;

// Driver states the soldier's scene renders with (0x0066e100's 5 and 6; meanings not identified).
constexpr int DRIVER_STATE_5 = 5;
constexpr int DRIVER_STATE_6 = 6;

// The pose stage that is not `stage` (2 <-> 3).
int OtherPoseStage(int stage)
{
    return stage == POSE_STAGE_A ? POSE_STAGE_B : POSE_STAGE_A;
}

int ItemsPerFrame(int slot)
{
    return (slot == 2 || slot == 3) ? 3 : 1;
}

void SetPosInline(I3D_frame *frame, float x, float y, float z)
{
    frame->SetPosInline(S_vector(x, y, z));
}

} // namespace

void C_MenuSoldier::Setup(float left, float top, float right, float bottom)
{
    I3D_driver *driver = I3DGetDriver();
    // In front of the game's camera.
    if (GetGame() && GetGame()->GetScene() && GetGame()->GetScene()->GetActiveCamera()) {
        const S_matrix &matrix = GetGame()->GetScene()->GetActiveCamera()->GetMatrix();
        m_pos = matrix(3);
        m_posW = matrix.m[3][3];
    }
    m_scene = reinterpret_cast<I3D_scene *>(driver->CreateFrame(FRAME_SCENE));
    if (!m_scene)
        return;
    m_scene->SetViewport(static_cast<int>(left), static_cast<int>(top), static_cast<int>(right),
                         static_cast<int>(bottom));
    m_scene->SetUnknown210(S_vector(0.0f, 0.0f, 0.0f));

    // An ambient light, then a spot light from above.
    auto *ambient = static_cast<I3D_light *>(driver->CreateFrame(FRAME_LIGHT));
    if (!ambient)
        return;
    ambient->SetName(LIGHT_NAME);
    ambient->SetLightType(I3DLIGHT_AMBIENT);
    SetPosInline(ambient, m_pos.x, m_pos.y + 1.0f, m_pos.z - 5.0f);
    ambient->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    ambient->SetPower(0.5f);
    ambient->SetRange(1000.0f);
    ambient->Update();
    m_scene->AddFrame(ambient);
    m_scene->GetPrimarySector()->AddLight(ambient);
    ambient->Release();

    auto *spot = static_cast<I3D_light *>(driver->CreateFrame(FRAME_LIGHT));
    if (!spot)
        return;
    spot->SetName(LIGHT_NAME);
    spot->SetLightType(I3DLIGHT_SPOT);
    SetPosInline(spot, m_pos.x, m_pos.y + 6.0f, m_pos.z - 6.0f);
    spot->SetColorRGB(1.0f, 1.0f, 1.0f);
    spot->SetWorldDir(S_vector(0.0f, -1.0f, 1.0f), 0.0f);
    spot->SetPower(0.9f);
    spot->SetRange(50.0f);
    spot->SetCone(0.3f, 0.4f);
    spot->Update();
    m_scene->AddFrame(spot);
    m_scene->GetPrimarySector()->AddLight(spot);
    spot->Release();

    // The camera: 1.4 m up and 6 m behind, looking slightly down.
    auto *camera = static_cast<I3D_camera *>(driver->CreateFrame(FRAME_CAMERA));
    if (!camera)
        return;
    // (The direction's normalisation folded by the compiler: its length squared is 0.8345.)
    const float scale = 1.0f / static_cast<float>(std::sqrt(0.8345));
    const S_vector dir(0.0f * scale, -0.08f * scale, 0.91f * scale);
    SetPosInline(camera, m_pos.x, m_pos.y + 1.4f, m_pos.z - 6.0f);
    camera->SetWorldDir(dir, 0.0f);
    camera->SetFOV(0.23f);
    camera->SetOrthogonal(false);
    camera->SetAspectRatio((right - left) / (bottom - top));
    camera->SetRange(4.2f, 8.6f);
    m_scene->AddFrame(camera);
    m_scene->SetActiveCamera(camera);
    camera->Update();
    camera->Release();

    // (The original prints the cache's errors for an animation that does not load.)
    for (int i = 0; i < ANIM_COUNT; ++i) {
        if (g_animNames[i][0] != '\0' && !m_anims[i])
            g_animationCache.Get(g_animNames[i], &m_anims[i]);
    }

    auto *model = static_cast<I3D_model *>(driver->CreateFrame(FRAME_MODEL));
    g_modelCache.OpenInto(model, NAKED_MODEL);
    SetModel(model);
}

void C_MenuSoldier::Release()
{
    if (m_model) {
        SetModel(nullptr);
        m_model = nullptr;
    }
    if (m_scene) {
        m_scene->Release();
        m_scene = nullptr;
    }
    for (I3D_animation_set *&anim : m_anims) {
        if (anim) {
            anim->Release();
            anim = nullptr;
        }
    }
}

void C_MenuSoldier::ReleaseDivingGear()
{
    for (I3D_frame *&gear : m_divingGear) {
        if (gear) {
            gear->Release();
            gear = nullptr;
        }
    }
}

void C_MenuSoldier::HideHolsters()
{
    for (S_HolsterSlot &slot : m_holsters) {
        slot.itemCount = 0;
        for (I3D_frame *frame : slot.frames) {
            if (frame)
                frame->SetOn(false);
        }
    }
}

void C_MenuSoldier::ReleaseHolsters()
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
}

void C_MenuSoldier::InitHolsters()
{
    // Like C_HumanEquipment::InitHolsters, without the holster pose (the menu model stands in
    // it). (The frames of the last model are dropped, not released: SetModel replacing a model
    // leaves them behind.)
    for (S_HolsterSlot &slot : m_holsters)
        slot = S_HolsterSlot();
    I3D_frame *jointC = m_model->FindChildFrame(JOINT_C, FRAME_MASK_ANY);
    I3D_frame *jointD = m_model->FindChildFrame(JOINT_D, FRAME_MASK_ANY);
    I3D_frame *jointO = m_model->FindChildFrame(JOINT_O, FRAME_MASK_ANY);

    // The holster set of the model: its file name without path and extension, past the "mp_"
    // of the menu copies.
    std::string modelName = m_model->GetFileName() ? m_model->GetFileName() : "";
    const std::size_t slash = modelName.rfind('\\');
    if (slash != std::string::npos)
        modelName.erase(0, slash + 1);
    const std::size_t dot = modelName.find('.');
    if (dot != std::string::npos)
        modelName.erase(dot);
    // Fix: the original reads past the end of a name shorter than the prefix.
    modelName.erase(0, std::strlen(BODY_MODEL_PREFIX) < modelName.size() ? std::strlen(BODY_MODEL_PREFIX)
                                                                        : modelName.size());
    const S_HolsterModel *model = FindHolsterModel(modelName.c_str());
    // (Not in the list: the message "HolsterInit chyba - panak '<model>' nenalezen v seznamu.
    // Oznamte skutecnost TS." is built and never shown.)
    if (!model || model->set < 0)
        return;
    auto *holsters = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (g_modelCache.OpenInto(holsters, g_holsterSets[model->set]) == I3D_OK) {
        holsters->LinkTo(m_model, 0);
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
    // (A set that does not load: "HolsterInit chyba - panak '<model>' nenalezl holster set
    // '<set>'. Oznamte skutecnost TS.", never shown.)
    holsters->LinkTo(nullptr, 0);
    holsters->Release();
}

void C_MenuSoldier::RemoveFromHolster(int typeId, int count)
{
    const int slot = C_HumanEquipment::GetHolsterSlot(typeId);
    if (slot == HOLSTER_SLOT_COUNT)
        return;
    S_HolsterSlot &holster = m_holsters[slot];
    holster.itemCount -= count;
    // (Unlike the human's, none left still hides the frames.)
    if (holster.itemCount < 0)
        holster.itemCount = 0;
    int shown = holster.itemCount;
    const int perFrame = ItemsPerFrame(slot);
    if (shown != 0 && perFrame != 1)
        shown = (shown - 1) / perFrame + 1;
    for (int i = 0; i < holster.frameCount; ++i)
        holster.frames[i]->SetOn(i < shown);
}

void C_MenuSoldier::AddToHolster(int typeId, int count)
{
    AddToHolsterSlot(C_HumanEquipment::GetHolsterSlot(typeId), count);
}

void C_MenuSoldier::AddToHolsterSlot(int slot, int count)
{
    if (slot == HOLSTER_SLOT_COUNT)
        return;
    S_HolsterSlot &holster = m_holsters[slot];
    const int perFrame = ItemsPerFrame(slot);
    if (holster.itemCount / perFrame < holster.frameCount) {
        for (int i = holster.itemCount; i < holster.itemCount + count; ++i) {
            if (i / perFrame < holster.frameCount)
                holster.frames[i / perFrame]->SetOn(true);
        }
    }
    holster.itemCount += count;
}

void C_MenuSoldier::SetModel(I3D_model *model)
{
    if (!m_scene)
        return;
    if (!model) {
        // (ResetItems loads the naked model and sets it, which is removed again below.)
        ReleaseHolsters();
        ResetItems();
        m_pos = S_vector(0.0f, 0.0f, 0.0f);
        // (The original leaves m_posW at whatever its stack held.)
        m_poseStage = POSE_STAGE_A;
        m_idleTime = 6000;
        m_idle = -1;
        m_poseAnim = -1;
        m_angle = 0.0f;
        m_supportHand = false;
        m_handDummy = nullptr;
        m_jointD = nullptr;
        m_jointF = nullptr;
        m_backpackJoint = nullptr;
        m_handsModel = nullptr;
        m_longGunModel = nullptr;
        m_headModel = nullptr;
        m_backpackModel = nullptr;
        m_idleFade = 0;
        m_idleStage = 0;
        m_poseFade = 0;
        for (I3D_frame *&gear : m_divingGear)
            gear = nullptr;
        delete m_pose;
        m_pose = nullptr;
        m_scene->RemoveFrame(m_model);
        m_model = nullptr;
        return;
    }

    // A model replaced: its holsters' item counts carry over.
    bool restore = false;
    int counts[HOLSTER_SLOT_COUNT] = {};
    if (m_model) {
        for (int i = 0; i < HOLSTER_SLOT_COUNT; ++i)
            counts[i] = m_holsters[i].itemCount;
        restore = true;
        m_scene->RemoveFrame(m_model);
        m_model = nullptr;
    }
    delete m_pose;
    m_pose = new C_HumanWeaponPose();                   // _aligned_malloc(0x6950, 16) in the original
    m_pose->Attach(model, nullptr, false);
    m_model = model;
    m_handDummy = model->FindChildFrame(HAND_DUMMY1, FRAME_MASK_DUMMY);
    m_jointD = model->FindChildFrame(JOINT_D, FRAME_MASK_ANY);
    m_backpackJoint = m_jointD;
    m_jointF = model->FindChildFrame(JOINT_F, FRAME_MASK_ANY);
    m_jointU = model->FindChildFrame(JOINT_U, FRAME_MASK_ANY);
    m_jointQ = model->FindChildFrame(JOINT_Q, FRAME_MASK_ANY);
    m_supportHand = false;
    if (m_face)
        SetFace(m_face);
    InitHolsters();
    if (restore) {
        for (int i = 0; i < HOLSTER_SLOT_COUNT; ++i) {
            if (counts[i] > 0)
                AddToHolsterSlot(i, counts[i]);
        }
    }
    AddItem(MENU_SOLDIER_HEAD, m_headTypeId);
    AddItem(MENU_SOLDIER_LONG_GUN, m_longGunTypeId);
    AddItem(MENU_SOLDIER_HANDS, m_handsTypeId);
    AddItem(MENU_SOLDIER_BACKPACK, m_backpackTypeId);
    AddItem(MENU_SOLDIER_HOLSTERS, -1);

    m_scene->AddFrame(m_model);
    m_scene->UpdateFrameSector(m_model, m_model->GetWorldPos());
    m_model->Release();
    m_model->SetDir(S_vector(0.0f, 0.0f, -1.0f), 0.0f);
    m_model->SetPosInline(m_pos);
    PlayIdle(m_idle);
    PlayPose(m_poseAnim);
    UpdateAnims(1000);
    m_model->Update();
}

void C_MenuSoldier::AddItem(uint32_t place, int typeId)
{
    C_ItemDef *def = GetItemDef(typeId);
    switch (place) {
    case MENU_SOLDIER_HEAD:
        if (def) {
            if (m_headModel) {
                m_headModel->LinkTo(nullptr, 0);
                ReleaseFrameTree(m_headModel);
            }
            CloneFrameTree(m_headModel, def->GetModel(), nullptr);
        }
        m_headTypeId = typeId;
        if (m_headModel) {
            m_headModel->LinkTo(m_jointF, 0);
            m_headModel->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
            m_headModel->SetPosInline(g_headModelOffset);
            m_headModel->Update();
        }
        return;
    case MENU_SOLDIER_BODY: {
        auto *model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
        const std::string name = std::string(BODY_MODEL_PREFIX) + def->GetModelName();
        g_modelCache.OpenInto(model, name.c_str());
        SetModel(model);
        return;
    }
    case MENU_SOLDIER_HANDS:
        if (def) {
            if (m_handsModel) {
                m_handsModel->LinkTo(nullptr, 0);
                if (m_supportHand) {
                    m_pose->SetSupportHand(false);
                    m_supportHand = false;
                }
                ReleaseFrameTree(m_handsModel);
                m_pose->SetModel(nullptr, -1);
            }
            CloneFrameTree(m_handsModel, def->GetModel(), nullptr);
            // The muzzle flash's frame goes.
            if (I3D_frame *flash = m_handsModel->FindChildFrame(FLASH_FRAME, FRAME_MASK_ANY)) {
                flash->LinkTo(nullptr, 0);
                flash->Release();
            }
        }
        m_handsTypeId = typeId;
        if (!m_handsModel)
            return;
        // Fix: the original takes the behaviour of a null definition (an unknown type id with
        // a model in the hands).
        m_pose->SetModel(m_handsModel, def ? def->GetBehaviour() : -1);
        m_handsModel->LinkTo(m_handDummy, 0);
        m_handsModel->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
        if (!def)
            return;
        if (def->GetCarryMode() == CARRY_MODE_6) {
            PlayPose(POSE_CARRY_6);
            m_handsModel->SetPosInline(S_vector(0.02f, 0.0f, 0.0f));
            m_handsModel->SetWorldDir(S_vector(0.0f, 1.0f, 0.0f), 0.0f);
        } else {
            PlayPose(def->GetCarryMode() + POSE_HANDS);
        }
        m_handsModel->Update();
        if (def->GetCarryMode() != 0 && def->GetCarryMode() != 2) {
            m_pose->SetSupportHand(true);
            m_supportHand = true;
        }
        return;
    case MENU_SOLDIER_BACKPACK:
        if (def) {
            if (m_backpackModel) {
                m_backpackModel->LinkTo(nullptr, 0);
                for (I3D_frame *gear : m_divingGear) {
                    if (gear)
                        gear->LinkTo(nullptr, 0);
                }
                // Fix: the bottles of the diving gear are the backpack model; the original kept
                // the released frame in the gear and linked it again next time.
                if (m_divingGear[3] == m_backpackModel)
                    m_divingGear[3] = nullptr;
                ReleaseFrameTree(m_backpackModel);
            }
            CloneFrameTree(m_backpackModel, def->GetModel(), nullptr);
        }
        if (typeId != -1) {
            // The diving gear: the mask on the head, two parts on the joints "u" and "q", the
            // bottles in place of the backpack model.
            if (I3D_frame **gear = GetItemDef(typeId)->GetDivingGear()) {
                for (int i = 0; i < DIVING_GEAR_COUNT; ++i) {
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
            }
        }
        m_backpackTypeId = typeId;
        if (m_backpackModel) {
            m_backpackModel->LinkTo(m_backpackJoint, 0);
            m_backpackModel->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
            m_backpackModel->SetPosInline(g_backpackModelOffset);
            m_backpackModel->Update();
        }
        return;
    case MENU_SOLDIER_LONG_GUN: {
        if (def) {
            if (m_longGunModel)
                ReleaseFrameTree(m_longGunModel);
            CloneFrameTree(m_longGunModel, def->GetModel(), nullptr);
            if (I3D_frame *flash = m_longGunModel->FindChildFrame(FLASH_FRAME, FRAME_MASK_ANY)) {
                flash->LinkTo(nullptr, 0);
                flash->Release();
            }
        }
        m_longGunTypeId = typeId;
        if (!m_longGunModel)
            return;
        // As the human's long gun on the back (C_HumanEquipment::AttachModel), without its tilt.
        I3D_frame *backlink = m_longGunModel->FindChildFrame(BACKLINK_DUMMY, FRAME_MASK_DUMMY);
        I3D_frame *magazine = m_longGunModel->FindChildFrame(MAGAZINE_FRAME, FRAME_MASK_ANY);
        if (!backlink)
            return;
        m_longGunModel->LinkTo(m_jointD, LINK_2);
        m_longGunModel->SetWorldDir(S_vector(0.0f, 1.0f, 0.0f), 0.0f);
        m_longGunModel->SetOn(true);
        m_longGunModel->SetPosInline(g_longGunModelOffset);
        m_longGunModel->Update();
        const S_vector link = BacklinkOffset(backlink);
        m_longGunModel->SetPosInline(S_vector(link.x + 0.0275f, link.y, link.z + 0.09f));
        m_longGunModel->Update();
        if (magazine && GetItemDef(typeId)->GetBehaviour() != 4) {
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
        return;
    }
    case MENU_SOLDIER_HOLSTERS:
        if (def)
            AddToHolster(typeId, 1);
        return;
    default:
        return;
    }
}

void C_MenuSoldier::RemoveItem(uint32_t place, int typeId, bool all)
{
    switch (place) {
    case MENU_SOLDIER_HEAD:
        if (m_headModel) {
            m_headModel->LinkTo(nullptr, 0);
            ReleaseFrameTree(m_headModel);
            m_headModel = nullptr;
            m_headTypeId = -1;
        }
        return;
    case MENU_SOLDIER_BODY:
        SetModel(nullptr);
        return;
    case MENU_SOLDIER_HANDS:
        if (m_handsModel) {
            if (m_supportHand) {
                m_pose->SetSupportHand(false);
                m_supportHand = false;
            }
            m_pose->SetModel(nullptr, -1);
            m_handsModel->LinkTo(nullptr, 0);
            ReleaseFrameTree(m_handsModel);
            m_handsModel = nullptr;
            m_handsTypeId = -1;
        }
        PlayPose(POSE_HANDS);
        return;
    case MENU_SOLDIER_BACKPACK:
        if (m_backpackModel) {
            m_backpackModel->LinkTo(nullptr, 0);
            // Fix: as in AddItem, the diving gear's bottles were the frame released.
            if (m_divingGear[3] == m_backpackModel)
                m_divingGear[3] = nullptr;
            ReleaseFrameTree(m_backpackModel);
            m_backpackModel = nullptr;
            m_backpackTypeId = -1;
        }
        return;
    case MENU_SOLDIER_LONG_GUN:
        if (m_longGunModel) {
            m_longGunModel->LinkTo(nullptr, 0);
            ReleaseFrameTree(m_longGunModel);
            m_longGunModel = nullptr;
            m_longGunTypeId = -1;
        }
        return;
    case MENU_SOLDIER_HOLSTERS:
        if (all)
            HideHolsters();
        else
            RemoveFromHolster(typeId, 1);
        return;
    default:
        return;
    }
}

void C_MenuSoldier::ResetItems()
{
    RemoveItem(MENU_SOLDIER_HEAD, -1, false);
    RemoveItem(MENU_SOLDIER_LONG_GUN, -1, false);
    RemoveItem(MENU_SOLDIER_HANDS, -1, false);
    RemoveItem(MENU_SOLDIER_BACKPACK, -1, false);
    RemoveItem(MENU_SOLDIER_HOLSTERS, -1, true);
    auto *model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    g_modelCache.OpenInto(model, NAKED_MODEL);
    SetModel(model);
}

void C_MenuSoldier::PlayIdle(int index)
{
    if (index < 0) {
        m_idle = 0;
        m_model->SetAnimation(m_anims[0], m_idleStage, 1.0f, ANIM_FLAGS);
        m_idleFade = 0;
        return;
    }
    m_idleStage = m_idleStage == 0 ? 1 : 0;
    m_idle = index;
    m_model->SetAnimation(m_anims[index], m_idleStage, 0.0f, ANIM_FLAGS);
    m_idleFade = IDLE_FADE;
}

void C_MenuSoldier::PlayPose(int index)
{
    if (index < 0 || index >= ANIM_COUNT)
        return;
    m_poseStage = OtherPoseStage(m_poseStage);
    m_poseAnim = index;
    m_model->SetAnimation(m_anims[index], m_poseStage, 0.0f, ANIM_FLAGS);
    m_poseFade = POSE_FADE;
}

void C_MenuSoldier::SetFace(const char *texture)
{
    GetGEffects()->SwitchFaceTexture(m_model, texture);
    m_face = texture;
}

void C_MenuSoldier::UpdateAnims(int time)
{
    // The new animation fades in over the old one.
    if (m_idleFade > 0) {
        const int other = m_idleStage == 0 ? 1 : 0;
        const float weight = static_cast<float>(m_idleFade) * 0.00125f;
        m_model->SetAnimWeight(other, weight);
        m_model->SetAnimWeight(m_idleStage, 1.0f - weight);
        m_idleFade -= time;
        if (m_idleFade < 1) {
            m_model->SetAnimWeight(other, 0.0f);
            m_model->SetAnimWeight(m_idleStage, 1.0f);
        }
    }
    if (m_poseFade > 0) {
        const int other = OtherPoseStage(m_poseStage);
        const float weight = static_cast<float>(m_poseFade) * 0.000625f;
        m_model->SetAnimWeight(other, weight);
        m_model->SetAnimWeight(m_poseStage, 1.0f - weight);
        m_poseFade -= time;
        if (m_poseFade < 1) {
            m_model->SetAnimWeight(other, 0.0f);
            m_model->SetAnimWeight(m_poseStage, 1.0f);
        }
    }
    m_model->Tick(static_cast<int>(static_cast<float>(time) / 1.4f));
    // Another idle animation every 11 to 13 seconds.
    m_idleTime -= time;
    if (m_idleTime < 1) {
        PlayIdle(std::rand() % 3);
        m_idleTime = std::rand() % 2000 + 11000;
    }
}

void C_MenuSoldier::Tick(int time)
{
    if (!m_scene || !m_model)
        return;
    m_angle += TURN_SPEED;
    S_quat rot;
    rot.Make(m_axis, m_angle);
    m_model->SetRot(rot);
    m_model->Update();
    UpdateAnims(time);
    m_pose->Update(time);
    m_scene->Tick(time);
}

void C_MenuSoldier::Render()
{
    if (!m_scene)
        return;
    const uint32_t state5 = I3DGetDriver()->GetState(DRIVER_STATE_5);
    const uint32_t state6 = I3DGetDriver()->GetState(DRIVER_STATE_6);
    I3DGetDriver()->SetState(DRIVER_STATE_5, 0);
    I3DGetDriver()->SetState(DRIVER_STATE_6, 1);
    m_scene->Render(0);
    I3DGetDriver()->SetState(DRIVER_STATE_5, state5);
    I3DGetDriver()->SetState(DRIVER_STATE_6, state6);
}
