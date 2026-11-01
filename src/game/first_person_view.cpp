// The first-person view (see first_person_view.h): the hands and the weapon in front of the camera.
#include "game/first_person_view.h"

#include <cmath>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/campaign.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/interface.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/math.h"

namespace {

constexpr float PI = 3.14159274f;                       // 0x0080f7ac

// The camera's second field of view (I3D_camera +0x12C): 65 degrees, 40 through the sights; the
// binoculars' field of view: 10 degrees.
constexpr float FOV2_NORMAL = 1.13446403f;              // 0x3F91361E
constexpr float FOV2_AIM = 0.69813174f;                 // 0x3F32B8C3
constexpr float FOV_BINOCULARS = 0.17453294f;           // 0x3E32B8C3
// The fields of view ease to their targets at pi / 1000 per ms.
constexpr float FOV_SPEED = 0.00314159f;                // 0x0080ff1c

// How far the weapon drops: lowered, lowered again after a refresh, put away for a belt grenade.
constexpr float DROP_LOWERED = -0.2f;
constexpr float DROP_REFRESH_LOWERED = -0.3f;
constexpr float DROP_GRENADE = -0.8f;

// The sway: its phase advances 2 per second; the offset is the soldier's speed times the wave
// times 1/1000 (1/2000 through the sights).
constexpr float SWAY_RATE = 0.002f;
constexpr float SWAY_SCALE = 0.001f;
constexpr float SWAY_SCALE_AIM = 0.0005f;

// The animations blend in / out at 5 weight per second; the muzzle flash stays 50 ms.
constexpr float BLEND_SPEED = 5.0f;
constexpr float FLASH_TIME = 50.0f;

// The Panzerfaust (item type 38): its "ammo" frame is the warhead.
constexpr int ITEM_TYPE_PANZERFAUST = 0x26;

const char *const ANIM_TABLE_FILE = "tables\\FpvAnims.sav";
const char *const DEFAULT_HANDS = "FPV_hands";

// tables\FpvAnims.sav: 0x3039 { 100 + item type { 2000 + event (13 of them) { a variant (4 at
// most) { 1000 { the animation's file }, int ratio } } } }.
enum : uint16_t {
    CHUNK_FPV_ANIMS = 0x3039,
    CHUNK_FPV_ITEM_BASE = 100,
    CHUNK_FPV_EVENT_BASE = 2000,
    CHUNK_FPV_ANIM_FILE = 1000,
};

// The scene's frames the model is linked to.
I3D_frame *FirstPersonRoot()
{
    return GetGame()->GetScene()->GetFirstPersonRoot();
}

I3D_frame *PrimarySector()
{
    return reinterpret_cast<I3D_frame *>(GetGame()->GetScene()->GetPrimarySector());
}

// A frame's position cleared and its direction straight ahead (the weapon in the model).
void ResetPlacement(I3D_frame *frame)
{
    frame->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    frame->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    frame->SetOn(true);
    frame->Update();
}

// The sleeve textures of the uniforms.
bool IsSleeveTexture(I3D_texture *texture)
{
    const char *name = texture->GetFileName();
    return std::strstr(name, "ARM") || std::strstr(name, "ARS");
}

I3D_mesh_level *ModelMesh(I3D_frame *model)
{
    I3D_frame *visual = model->FindChildFrame("a", FRAME_MASK_ANY);
    return static_cast<I3D_visual *>(visual)->GetVisualMesh()->GetLevel(0);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// S_FpvAnim

S_FpvAnim::~S_FpvAnim()
{
    for (I3D_animation_set *anim : anims) {
        if (anim)
            anim->Release();
    }
}

// ---------------------------------------------------------------------------------------------
// Construction, mission start and end

C_FirstPersonView::C_FirstPersonView() = default;

C_FirstPersonView::~C_FirstPersonView() = default;

void C_FirstPersonView::LoadHands(const char *fileName)
{
    if (!IsClient() || !m_initialized)
        return;
    if (m_hands)
        m_hands->Release();
    m_hands = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (g_modelCache.OpenInto(m_hands, fileName) < 0) {
        m_hands->Release();
        m_hands = nullptr;
        return;
    }
    m_hands->SetName("FPV_HANDS");
    m_hands->LinkTo(FirstPersonRoot(), 0);
    m_hands->SetOn(false);
}

void C_FirstPersonView::Init()
{
    if (!IsClient())
        return;
    m_initialized = true;
    m_pose = FPV_POSE_NONE;
    // (The original also reads the performance counter and its frequency, and drops them.)
    m_unused00 = {0.0f, 0.0f, 0.0f, 0.0f};
    m_magazine = nullptr;
    m_weapon = nullptr;
    m_on = false;
    m_unused10.Make(S_vector(0.0f, 0.0f, 0.0f), 0.0f);
    m_fading.clear();
    m_current.anim = nullptr;
    m_itemType = 0;
    m_nextEvent = FPV_EVENT_IDLE;
    m_event = FPV_EVENT_IDLE;
    m_variant = 0;
    m_on = false;
    m_binoculars = false;
    m_aimView = false;
    m_lightLevel = 1.0f;
    LoadAnimTable();
    m_unused20 = {0.0f, 0.0f, 0.0f, 0.0f};
    m_swayPhase = 0.0f;
    m_flash = nullptr;
    m_flashTime = -1.0f;
    Refresh();
    ResetAimView();
}

void C_FirstPersonView::Done()
{
    if (!IsClient())
        return;
    if (m_model) {
        if (m_weapon) {
            for (int i = m_weapon->NumChildren(); i-- > 0;) {
                m_weapon->GetChild(i);                  // (called twice in the original)
                m_model->RemoveModelFrame(m_weapon->GetChild(i));
            }
            m_model->RemoveModelFrame(m_weapon);
            m_weapon->LinkTo(nullptr, 0);
        }
        if (m_magazine) {
            m_model->RemoveModelFrame(m_magazine);
            m_magazine->LinkTo(nullptr, 0);
        }
        StopAnims();
        m_model->StopAnimation(0);
        m_model->Update();
        m_model->Release();
    }
    if (m_hands)
        m_hands->Release();
    m_hands = nullptr;
    m_model = nullptr;
    m_magazine = nullptr;
    m_weapon = nullptr;
    m_on = false;
    m_fading.clear();
    m_current.anim = nullptr;
    m_human = nullptr;
    m_initialized = false;
}

void C_FirstPersonView::Reset()
{
    if (!IsClient())
        return;
    Done();
    Init();
    Show(g_hudOption->shown);
}

bool C_FirstPersonView::SaveGame(C_chunk &)
{
    return true;
}

bool C_FirstPersonView::LoadGame(C_chunk &)
{
    Refresh();
    g_teamManager.GetPlayerTeam()->UpdateCameraTarget();
    // (Reports a failure; the game does not look at it.)
    return false;
}

void C_FirstPersonView::LoadAnimTable()
{
    if (!IsClient())
        return;
    C_chunk chunk;
    if (!chunk.Open(ANIM_TABLE_FILE, false)) {
        // (The original formats "C_FirstPersonView: LoadAnimTable : Could not open file ..." and
        // drops it.)
        chunk.Close();
        return;
    }
    uint16_t id = 0xFFFF;
    chunk.Descend(id);
    if (id != CHUNK_FPV_ANIMS) {
        chunk.Close();
        return;
    }
    // Quirks kept: the 13 event chunks are read whether the item's chunk has them or not, and a
    // variant whose first chunk is not 1000 leaves the reader a level too deep.
    while (chunk.RemainingSize() > 0) {
        uint16_t itemId = 0;
        chunk.Descend(itemId);
        for (int e = 0; e < FPV_EVENT_COUNT; ++e) {
            uint16_t eventId = 0;
            chunk.Descend(eventId);
            if (chunk.RemainingSize() > 0) {
                for (int variant = 0; chunk.RemainingSize() > 0;) {
                    uint16_t variantId = 0;
                    chunk.Descend(variantId);
                    if (chunk.RemainingSize() > 0) {
                        uint16_t fileId = variantId;
                        chunk.Descend(fileId);
                        if (fileId == CHUNK_FPV_ANIM_FILE && chunk.RemainingSize() != 0) {
                            const int item = itemId - CHUNK_FPV_ITEM_BASE;
                            const int event = eventId - CHUNK_FPV_EVENT_BASE;
                            // Fix: the original writes wherever the chunk ids point.
                            if (item >= 0 && item < MAX_ITEM_TYPES && event >= 0 && event < FPV_EVENT_COUNT) {
                                S_FpvAnim &anim = m_anims[item][event];
                                ReadChunkString(chunk, anim.names[variant]);
                                chunk.Read(&anim.ratios[variant], sizeof(int));
                            }
                        }
                    }
                    chunk.Ascend();
                    if (++variant > 3)
                        break;
                }
            }
            chunk.Ascend();
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    chunk.Close();
}

// ---------------------------------------------------------------------------------------------
// The model

void C_FirstPersonView::SetWeaponView(I3D_model *weapon, I3D_frame *magazine, int itemType)
{
    if (!IsClient() || !m_initialized || !m_hands)
        return;
    if (m_model) {
        // The old weapon goes back to the scene (hidden).
        if (m_weapon) {
            m_model->RemoveModelFrame(m_weapon);
            m_weapon->LinkTo(PrimarySector(), 0);
            for (int i = m_weapon->NumChildren(); i-- > 0;)
                m_model->RemoveModelFrame(m_weapon->GetChild(i));
            m_weapon->SetOn(false);
        }
        if (m_magazine) {
            m_model->RemoveModelFrame(m_magazine);
            m_magazine->LinkTo(PrimarySector(), 0);
            m_magazine->SetOn(false);
        }
        m_model->Release();
        m_model = nullptr;
    }
    m_weapon = weapon;
    m_magazine = magazine;
    m_itemType = itemType;
    if (!weapon) {
        m_flash = nullptr;
        m_flashTime = -1.0f;
        return;
    }
    // A copy of the hands with the weapon's frames in it.
    m_model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    m_model->Duplicate(m_hands);
    if (m_on)
        m_model->LinkTo(FirstPersonRoot(), 1);
    m_model->AddModelFrame(m_weapon);
    m_weapon->LinkTo(m_model, 1);
    m_weapon->SetOn(true);
    for (int i = m_weapon->NumChildren(); i-- > 0;)
        m_model->AddModelFrame(m_weapon->GetChild(i));
    if (m_magazine) {
        m_model->AddModelFrame(m_magazine);
        m_magazine->LinkTo(m_model, 1);
        m_magazine->SetOn(true);
    }
    m_flash = m_model->FindChildFrame("flash", FRAME_MASK_VISUAL);
    if (!m_flash)
        m_flashTime = -1.0f;
    else
        m_flash->SetOn(false);
    ResetPlacement(m_weapon);
    if (m_magazine)
        ResetPlacement(m_magazine);
    m_model->SetOn(m_on);
    m_model->Update();
}

void C_FirstPersonView::ShowWarhead(bool on)
{
    I3D_frame *warhead = m_model->FindChildFrame("ammo", FRAME_MASK_VISUAL);
    // Fix: the original takes the frame for granted.
    if (!warhead)
        return;
    warhead->SetOn(on);
    warhead->Update();
}

void C_FirstPersonView::Refresh()
{
    if (!IsClient() || !m_initialized)
        return;
    SetPose(FPV_POSE_NONE);
    if (m_human && m_human->GetVehicle())
        m_human->GetVehicle()->ShowSeatView(m_human->GetVehicleSeat(), false);
    m_human = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    if (!m_human)
        return;
    if (C_Actor *vehicle = m_human->GetVehicle())
        vehicle->ShowSeatView(m_human->GetVehicleSeat(), m_on);
    ResetAimView();

    // The hands: the uniform's first-person model without its extension ("w_default" ones and
    // no uniform: "FPV_hands"). (A 256-byte buffer in the original.)
    std::string hands = DEFAULT_HANDS;
    if (C_Item *uniform = m_human->GetUniformItem()) {
        // Fix: a uniform without a first-person model.
        const char *name = uniform->GetDef()->GetFpvModelName();
        hands = name ? name : "";
        if (hands.find("w_default") != std::string::npos) {
            hands = DEFAULT_HANDS;
        } else {
            const std::string::size_type dot = hands.rfind('.');
            if (dot != std::string::npos)
                hands.erase(dot);
        }
    }
    LoadHands(hands.c_str());

    C_WeaponItem *weapon = m_human->GetHandsWeapon();
    StopAnims();
    if (!weapon || m_human->IsHandsItemPutAway()) {
        SetWeaponView(nullptr, nullptr, -1);
    } else {
        C_AnimatedItemDef *def = static_cast<C_AnimatedItemDef *>(weapon->GetDef());
        SetWeaponView(static_cast<I3D_model *>(def->GetFpvModel()), def->GetFpvMagazine(), weapon->GetTypeId());
        if (weapon->GetTypeId() == ITEM_TYPE_PANZERFAUST)
            ShowWarhead(weapon->GetAmount() > 0.0f);
        OnInventoryEvent(FPV_EVENT_IDLE, FPV_EVENT_IDLE);
    }
    if (m_model) {
        // Hidden for a car's driver and in the binoculars' view.
        C_Actor *vehicle = m_human->GetVehicle();
        if (vehicle && vehicle->GetType() == ACTOR_CAR && m_human->GetVehicleSeat() == 0)
            m_model->SetOn(false);
        else if (m_binoculars)
            m_model->SetOn(false);
        else if (m_on)
            m_model->SetOn(true);
        ApplySleeves();
        m_model->Update();
    }
    SetPose(m_human->GetWeaponStance());
    m_drop = m_pose == FPV_POSE_LOWERED ? DROP_REFRESH_LOWERED : 0.0f;
}

void C_FirstPersonView::RefreshWeapon()
{
    if (!IsClient() || !m_initialized || !m_human)
        return;
    ResetAimView();
    C_WeaponItem *weapon = m_human->GetHandsWeapon();
    if (!weapon)
        return;
    StopAnims();
    C_AnimatedItemDef *def = static_cast<C_AnimatedItemDef *>(weapon->GetDef());
    SetWeaponView(static_cast<I3D_model *>(def->GetFpvModel()), def->GetFpvMagazine(), weapon->GetTypeId());
    if (weapon->GetTypeId() == ITEM_TYPE_PANZERFAUST)
        ShowWarhead(weapon->GetAmount() > 0.0f);
    if (m_model) {
        C_Actor *vehicle = m_human->GetVehicle();
        if (vehicle && vehicle->GetType() == ACTOR_CAR && m_human->GetVehicleSeat() == 0)
            m_model->SetOn(false);
        else if (m_binoculars)
            m_model->SetOn(false);
        else if (m_on)
            m_model->SetOn(true);
    }
    m_drop = m_pose == FPV_POSE_LOWERED ? DROP_REFRESH_LOWERED : 0.0f;
}

void C_FirstPersonView::ApplySleeves()
{
    if (!IsClient() || !m_human)
        return;
    // The last face group of the soldier's model with a sleeve texture gives it to the hands'.
    I3D_mesh_level *body = ModelMesh(m_human->GetFrame());
    I3D_texture *sleeves = nullptr;
    for (int i = body->NumFaceGroups(); i-- > 0;) {
        I3D_texture *texture = body->GetFaceGroup(i)->material->GetTexture();
        if (texture && IsSleeveTexture(texture)) {
            sleeves = texture;
            break;
        }
    }
    if (sleeves) {
        I3D_mesh_level *hands = ModelMesh(m_model);
        for (int i = hands->NumFaceGroups(); i-- > 0;) {
            I3D_texture *texture = hands->GetFaceGroup(i)->material->GetTexture();
            if (texture && IsSleeveTexture(texture))
                hands->GetFaceGroup(i)->material->SetTexture(sleeves);
        }
    }
    m_model->Update();
}

// ---------------------------------------------------------------------------------------------
// Showing

void C_FirstPersonView::Show(bool on)
{
    if (!IsClient() || m_on == on)
        return;
    // (Network games: not in the free camera.)
    if (!IsLocalGame() && GetGame()->GetCameraMode() == GAME_CAMERA_FREE)
        return;
    C_Human *human = m_human;
    m_on = on;
    if (human && human->GetVehicle())
        human->GetVehicle()->ShowSeatView(human->GetVehicleSeat(), on);
    g_teamManager.GetPlayerTeam()->UpdateCameraTarget();
    if (m_model) {
        if (on) {
            m_model->LinkTo(FirstPersonRoot(), 0);
        } else {
            m_model->LinkTo(nullptr, 0);
            ResetAimView();
        }
        m_model->SetOn(on);
        m_model->Update();
    }
    GetGEffects()->SetLensFlareOn(!on);
}

void C_FirstPersonView::ShowBinoculars(bool on)
{
    if (!IsClient() || !m_initialized)
        return;
    m_binoculars = on;
    if (on) {
        GetCameraModule()->SetFov(FOV_BINOCULARS);
        if (m_model) {
            m_model->SetOn(false);
            m_model->Update();
        }
    } else {
        // (Show with the current state does nothing; Update shows the model again.)
        Show(m_on);
        GetCameraModule()->SetFov(GetGame()->GetCutsceneFOV());
    }
    g_teamManager.GetPlayerTeam()->UpdateCameraTarget();
    GetGame()->GetInterface()->SetViewMode(m_human->GetPlayerViewMode());
}

void C_FirstPersonView::ResetAimView()
{
    if (!IsClient() || !m_initialized)
        return;
    m_aimView = false;
    GetGame()->GetCameraModule()->SetSpecialMode(CAMERA_MODE_AIM, false, nullptr);
    m_fov = GetGame()->GetCutsceneFOV();
    m_fov2 = FOV2_NORMAL;
    GetCameraModule()->SetFov(m_fov);
    GetCameraModule()->SetNearSectorFov(m_fov2);
    GetGame()->GetInterface()->SetViewMode(C_PlayerController::VIEW_NORMAL);
}

void C_FirstPersonView::SetAimView(bool on, float fov)
{
    if (!IsClient() || !m_initialized || m_aimView == on)
        return;
    m_aimView = on;
    GetGame()->GetCameraModule()->SetSpecialMode(CAMERA_MODE_AIM, on, nullptr);
    m_fov = fov;
    if (on) {
        PlayAnim(FPV_EVENT_AIM_IN);
        m_fov2 = FOV2_AIM;
        // The iron sights' view mode at once, the scope's when its animation ends (UpdateAnim).
        if (m_human->GetPlayerViewMode() == C_PlayerController::VIEW_IRON_SIGHTS)
            GetGame()->GetInterface()->SetViewMode(m_human->GetPlayerViewMode());
    } else {
        PlayAnim(FPV_EVENT_AIM_OUT);
        m_fov2 = FOV2_NORMAL;
        GetGame()->GetInterface()->SetViewMode(C_PlayerController::VIEW_NORMAL);
    }
}

// ---------------------------------------------------------------------------------------------
// Updates

void C_FirstPersonView::Update(int timeDelta)
{
    if (!IsClient() || !m_initialized || m_binoculars || !m_model || !m_human)
        return;
    if (m_human->IsDead()) {
        if (m_model->IsOn()) {
            m_model->SetOn(false);
            m_model->Update();
        }
        return;
    }
    if (m_on && !m_model->IsOn())
        m_model->SetOn(true);
    UpdateView(timeDelta);
    UpdateAnim(timeDelta);
    UpdateBlend(timeDelta);
    UpdateLight();
    if (m_flashTime >= 0.0f) {
        // Quirk kept: at 0 the flash goes off and the time to -1, which the frame's time takes
        // below 0 and back to 0; it stays there (the flash is switched off every frame).
        if (m_flashTime == 0.0f) {
            m_flashTime = -1.0f;
            m_flash->SetOn(false);
            m_flash->Update();
        }
        m_flashTime -= static_cast<float>(timeDelta);
        if (m_flashTime < 0.0f)
            m_flashTime = 0.0f;
    }
    if (m_model && m_current.anim) {
        m_model->Tick(timeDelta);
        m_model->Update();
    }
}

void C_FirstPersonView::SetPose(int pose)
{
    if (IsClient() && m_pose != pose)
        m_pose = pose;
}

// Moves the model to the camera with the sway, turns it along the camera's direction dropped by
// the pose, and eases the camera's fields of view to the view's.
void C_FirstPersonView::UpdateView(int timeDelta)
{
    if (!IsClient() || !m_initialized)
        return;
    const S_vector &velocity = m_human->GetVelocity();
    const float speed = MagnitudeBy(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
    const float time = static_cast<float>(timeDelta);
    m_swayPhase = time * SWAY_RATE + m_swayPhase;
    // (FSIN on the x87 stack: the sine in more than float precision.)
    const float wave = static_cast<float>(std::fabs(std::sin(static_cast<double>(PI * m_swayPhase))));
    const int halfCycle = static_cast<int>(m_swayPhase);
    S_vector offset(0.0f, 0.0f, 0.0f);
    if (speed != 0.0f) {
        const float k = speed * wave * (m_aimView ? SWAY_SCALE_AIM : SWAY_SCALE);
        offset.x = (halfCycle & 1) ? k : -k;
        offset.y = -k;
    }
    offset = offset.RotateByMatrix(m_model->GetMatrix());
    C_CameraModule *camera = GetGame()->GetCameraModule();
    S_vector4 cameraPos;
    camera->GetCameraPos(0, &cameraPos);
    m_model->SetPos(S_vector(cameraPos.x + offset.x, cameraPos.y + offset.y, cameraPos.z + offset.z));
    S_vector4 cameraDir;
    GetGame()->GetCameraModule()->GetCameraDir(0, &cameraDir);

    switch (m_pose) {
    case FPV_POSE_GRENADE_OUT:
        // The weapon drops away at 4 per second; at the bottom the belt grenade comes up.
        if (m_drop == DROP_GRENADE)
            break;
        m_drop -= static_cast<float>(timeDelta * 4) * 0.001f;
        if (m_drop <= DROP_GRENADE) {
            m_drop = DROP_GRENADE;
            if (C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN)) {
                SetPose(FPV_POSE_GRENADE);
                StopAnims();
                SetWeaponView(static_cast<I3D_model *>(grenade->GetDef()->GetFpvModel()), nullptr, grenade->GetTypeId());
                PlayAnim(FPV_EVENT_FIRE);
                m_drop = 0.0f;
            }
        }
        break;
    case FPV_POSE_WEAPON_BACK:
        // Back up at 3 per second to the controller's stance. Quirk kept: toward a lowered
        // weapon the test is the wrong way round, so the weapon jumps to -0.2 at once.
        if (m_human->GetWeaponStance() == WEAPON_STANCE_LOWERED) {
            if (m_drop == DROP_LOWERED)
                break;
            m_drop = static_cast<float>(timeDelta * 3) * 0.001f + m_drop;
            if (!(m_drop <= DROP_LOWERED))
                break;
            m_drop = DROP_LOWERED;
        } else {
            if (m_drop == 0.0f)
                break;
            m_drop = static_cast<float>(timeDelta * 3) * 0.001f + m_drop;
            if (m_drop < 0.0f)
                break;
            m_drop = 0.0f;
        }
        SetPose(m_human->GetWeaponStance());
        break;
    case FPV_POSE_LOWERED:
        // Down to -0.2, slower near the bottom.
        if (m_drop != DROP_LOWERED) {
            m_dropSpeed = m_drop * 3.0f + 1.0f;
            m_drop = m_drop - m_dropSpeed * time * 0.001f;
            if (m_drop <= DROP_LOWERED)
                m_drop = DROP_LOWERED;
        }
        break;
    case FPV_POSE_RAISED:
        // Up at 4 per second.
        if (m_drop != 0.0f) {
            m_drop = static_cast<float>(timeDelta * 4) * 0.001f + m_drop;
            if (m_drop >= 0.0f)
                m_drop = 0.0f;
        }
        break;
    default:
        break;
    }

    S_vector dir(cameraDir.x, cameraDir.y, cameraDir.z);
    if (m_drop != 0.0f) {
        dir.y += m_drop;
        NormalizeBy(dir, (dir.z * dir.z + dir.x * dir.x) + dir.y * dir.y);
    }
    m_model->SetDir(dir, 0.0f);

    // The fields of view (Ghidra: C_CameraModule slots 0x50..0x60).
    float fov = GetCameraModule()->GetFov();
    if (fov != m_fov) {
        if (fov <= m_fov) {
            fov = fov + time * FOV_SPEED;
            if (fov > m_fov)
                fov = m_fov;
        } else {
            fov = fov - time * FOV_SPEED;
            if (fov < m_fov)
                fov = m_fov;
        }
        GetCameraModule()->SetFov(fov);
    }
    float fov2 = GetCameraModule()->GetNearSectorFov();
    if (fov2 == m_fov2)
        return;
    if (fov2 <= m_fov2) {
        fov2 = fov2 + time * FOV_SPEED;
        if (fov2 > m_fov2)
            fov2 = m_fov2;
    } else {
        fov2 = fov2 - time * FOV_SPEED;
        if (fov2 < m_fov2)
            fov2 = m_fov2;
    }
    GetCameraModule()->SetNearSectorFov(fov2);
}

// When the animation playing ends within this frame: what follows it.
void C_FirstPersonView::UpdateAnim(int timeDelta)
{
    if (!IsClient() || !m_initialized || !m_current.anim)
        return;
    const int time = m_model->GetAnimTime(m_current.stage);
    if (m_current.anim->GetEndTime() - time >= timeDelta)
        return;
    if (m_event == FPV_EVENT_PUT_AWAY) {
        // Put away: the next item drawn, or nothing in the hands.
        ResetAimView();
        if (m_nextEvent == FPV_EVENT_DRAW) {
            Refresh();
            StopAnims();
            m_nextEvent = FPV_EVENT_IDLE;
            PlayAnim(FPV_EVENT_DRAW);
            return;
        }
        SetWeaponView(nullptr, nullptr, -1);
        m_event = -1;
        m_nextEvent = -1;
        return;
    }
    if (m_pose == FPV_POSE_GRENADE && m_event == FPV_EVENT_FIRE) {
        // The belt grenade is thrown: the weapon comes back (from the bottom).
        StopAnims();
        RefreshWeapon();
        m_drop = DROP_GRENADE;
        if (m_human->IsHandsItemPutAway()) {
            SetPose(m_human->GetWeaponStance());
            SetWeaponView(nullptr, nullptr, -1);
            m_event = -1;
            m_nextEvent = -1;
        } else {
            SetPose(FPV_POSE_WEAPON_BACK);
            m_nextEvent = FPV_EVENT_IDLE;
        }
    }
    if (m_aimView) {
        if (m_event == FPV_EVENT_AIM_IN && m_human->GetPlayerViewMode() == C_PlayerController::VIEW_SCOPE)
            GetGame()->GetInterface()->SetViewMode(m_human->GetPlayerViewMode());
        // Through the sights the last frame stays instead of the idle animation.
        if (m_nextEvent == FPV_EVENT_IDLE || m_nextEvent == FPV_EVENT_IDLE_EMPTY) {
            m_event = FPV_EVENT_IDLE;
            return;
        }
    }
    if (m_nextEvent == FPV_EVENT_IDLE) {
        C_WeaponItem *weapon = m_human->GetHandsWeapon();
        if (weapon && m_human->GetHandsWeapon()->GetAmount() == 0.0f)
            m_nextEvent = FPV_EVENT_IDLE_EMPTY;
    }
    PlayAnim(m_nextEvent);
}

void C_FirstPersonView::UpdateBlend(int timeDelta)
{
    if (!IsClient() || !m_initialized || !m_current.anim || !m_model)
        return;
    if (m_current.weight == 1.0f) {
        // Fully in: the ones blending out stop.
        while (!m_fading.empty()) {
            m_model->SetAnimation(nullptr, m_fading.back().stage, 1.0f, 0);
            m_fading.pop_back();
        }
        return;
    }
    const float time = static_cast<float>(timeDelta);
    m_current.weight = time * m_current.speed * 0.001f + m_current.weight;
    if (m_current.weight > 1.0f)
        m_current.weight = 1.0f;
    m_model->SetAnimWeight(m_current.stage, m_current.weight);
    for (std::size_t i = 0; i < m_fading.size();) {
        S_FpvStage &stage = m_fading[i];
        stage.weight = stage.weight - time * stage.speed * 0.001f;
        if (stage.weight > 0.0f) {
            m_model->SetAnimWeight(stage.stage, stage.weight);
            ++i;
        } else {
            m_model->SetAnimation(nullptr, stage.stage, 1.0f, 0);
            m_fading.erase(m_fading.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    m_model->Update();
}

void C_FirstPersonView::UpdateLight()
{
    if (!IsClient() || !m_initialized || !m_human || !m_model)
        return;
    const float level = m_human->GetBodyLightLevel();
    if (level == m_lightLevel)
        return;
    m_lightLevel = level;
    GetGEffects()->SetModelLightLevel(m_model, m_lightLevel);
}

// ---------------------------------------------------------------------------------------------
// Animations

void C_FirstPersonView::OnInventoryEvent(int event, int nextEvent)
{
    if (!IsClient() || !m_initialized)
        return;
    // (Nothing while a belt grenade is thrown.)
    if (m_pose == FPV_POSE_GRENADE_OUT || m_pose == FPV_POSE_GRENADE || m_pose == FPV_POSE_WEAPON_BACK)
        return;
    if (event == FPV_EVENT_DRAW) {
        RefreshWeapon();
    } else if (event == FPV_EVENT_PUT_AWAY) {
        if (m_aimView)
            SetAimView(false, GetGame()->GetCutsceneFOV());
    } else if (event == FPV_EVENT_CLEARED) {
        ResetAimView();
        SetWeaponView(nullptr, nullptr, -1);
        m_event = -1;
        SetPose(FPV_POSE_NONE);
        return;
    }
    // Shots and clicks do not cut the sights' animations.
    if ((m_event == FPV_EVENT_AIM_IN || m_event == FPV_EVENT_AIM_OUT) &&
        (event == FPV_EVENT_JAMMED || event == FPV_EVENT_FIRE || event == FPV_EVENT_FIRE_LAST))
        return;
    if (!m_model)
        return;
    if (!m_aimView) {
        if (event == FPV_EVENT_BELT_GRENADE) {
            SetPose(FPV_POSE_GRENADE_OUT);
            return;
        }
    } else {
        if (event == FPV_EVENT_FIRE) {
            PlayAnim(FPV_EVENT_AIM_FIRE);
            m_nextEvent = nextEvent;
            return;
        }
        if (event == FPV_EVENT_FIRE_LAST) {
            PlayAnim(FPV_EVENT_AIM_FIRE_LAST);
            m_nextEvent = nextEvent;
            return;
        }
        if (event == FPV_EVENT_IDLE || event == FPV_EVENT_JAMMED || event == FPV_EVENT_BELT_GRENADE)
            return;
    }
    if (event == FPV_EVENT_IDLE) {
        // Fix: the original takes the hands weapon for granted.
        C_WeaponItem *weapon = m_human->GetHandsWeapon();
        if (weapon && weapon->GetAmount() == 0.0f)
            event = FPV_EVENT_IDLE_EMPTY;
    }
    PlayAnim(event);
    m_nextEvent = nextEvent;
}

void C_FirstPersonView::PlayAnim(int event)
{
    if (!IsClient() || !m_model)
        return;
    PlayEventSound(event);
    if (event == FPV_EVENT_FIRE || event == FPV_EVENT_AIM_FIRE || event == FPV_EVENT_FIRE_LAST ||
        event == FPV_EVENT_AIM_FIRE_LAST) {
        if (m_itemType == ITEM_TYPE_PANZERFAUST)
            ShowWarhead(false);
        if (m_flash) {
            // The muzzle flash: 50 ms, 0.7..1.1 of its size, a random roll.
            m_flashTime = FLASH_TIME;
            m_flash->SetHierarchyLightLevel(1.0f);
            const float scale = static_cast<float>(RandFloat() * 0.4f + 0.7);
            m_flash->SetScale(S_vector(scale, scale, scale));
            const S_vector dir = m_flash->GetLocalDir();
            if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 1.0f)
                m_flash->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), RandFloat() * PI);
            m_flash->SetOn(true);
            m_flash->Update();
        }
        if (m_event == event) {
            // The same shot again: the animation starts over, blending in.
            m_current.weight = 0.0f;
            m_current.speed = BLEND_SPEED;
            m_model->SetAnimTime(m_current.stage, 0);
            m_model->StartAnimation(m_current.stage);
            m_model->Update();
            return;
        }
    }
    // Fix: an event or item type outside the table read outside it.
    if (event < 0 || event >= FPV_EVENT_COUNT || m_itemType < 0 || m_itemType >= MAX_ITEM_TYPES)
        return;
    S_FpvAnim &anims = m_anims[m_itemType][event];
    // A variant with a ratio and a file is needed.
    int variant = 0;
    while (anims.ratios[variant] < 1 || anims.names[variant].IsEmpty()) {
        if (++variant > 3)
            return;
    }
    // The first variant whose ratio is at least a random 0..99 (cumulative percentages; with
    // ratios short of 100 nothing may be picked).
    const int pick = static_cast<int>(RandFloat() * 100.0f);
    for (variant = 0; variant < 4; ++variant) {
        if (pick <= anims.ratios[variant])
            break;
    }
    if (variant == 4) {
        dbgPrintf("C_FirstPersonView: PlayAnim %d: bad values in the ratio, could not select random anim !!!", event);
        return;
    }
    if (!anims.anims[variant]) {
        if (!anims.names[variant].IsEmpty())
            g_animationCache.Get(anims.names[variant].c_str(), &anims.anims[variant]);
        if (!anims.anims[variant]) {
            if (!anims.names[variant].IsEmpty())
                dbgPrintf("C_FirstPersonView: PlayAnim %d (%d) : could not load %s anim", event, variant,
                          anims.names[variant].c_str());
            else
                dbgPrintf("C_FirstPersonView: PlayAnim %d (%d) : could not load unknow anim", event, variant);
            return;
        }
    }
    if (!m_current.anim || !m_model->IsAnimActive(m_current.stage)) {
        m_current.weight = 1.0f;
        m_current.speed = 0.0f;
    } else {
        // The one playing blends out; the new one blends in.
        m_fading.push_back(m_current);
        m_current.weight = 0.0f;
        m_current.speed = BLEND_SPEED;
    }
    m_event = event;
    m_variant = variant;
    m_current.anim = anims.anims[variant];
    // The first stage 0..2 none of the blending out ones uses (3 when they use all).
    int stage = 0;
    for (;;) {
        bool free = true;
        for (const S_FpvStage &fading : m_fading) {
            if (fading.stage == stage)
                free = false;
        }
        if (free || ++stage >= 3)
            break;
    }
    m_current.stage = stage;
    m_model->SetAnimation(m_current.anim, stage, m_current.weight, 0);
    m_model->Update();
}

void C_FirstPersonView::StopAnims()
{
    if (!IsClient() || !m_model)
        return;
    for (int stage = 8; stage-- > 0;) {
        m_model->SetAnimTime(stage, 0);
        m_model->StopAnimation(stage);
        m_model->SetAnimation(nullptr, stage, 1.0f, 0);
    }
    m_fading.clear();
    m_current.anim = nullptr;
    m_model->Tick(100);
    m_model->Update();
    m_event = FPV_EVENT_IDLE;
}

// The sounds the view adds: a grenade's throw and the drawing of a weapon (at the camera).
void C_FirstPersonView::PlayEventSound(int event)
{
    if (!m_on && !m_binoculars)
        return;
    if (event < FPV_EVENT_FIRE)
        return;
    S_SoundRequest request;
    request.flags = SOUND_SILENT_TO_AI | SOUND_AMBIENT;
    request.attachTo = nullptr;
    request.source = m_human;
    if (event <= FPV_EVENT_AIM_FIRE_LAST) {
        C_ItemDef *def = GetItemDef(m_itemType);
        if (!def || def->GetBehaviour() != ITEM_BEHAVIOUR_THROWN)
            return;
        request.chapter = SOUND_CHAPTER_WEAPON_SHOOTING;
        request.group = static_cast<C_WeaponComp *>(def->GetComponent())->GetShotSound();
    } else if (event == FPV_EVENT_DRAW) {
        C_ItemDef *def = GetItemDef(m_itemType);
        // Fix: the original reads the sound of a missing definition.
        if (!def)
            return;
        request.chapter = SOUND_CHAPTER_WEAPON_MANIPULATION;
        request.group = def->GetArmedSound();
    } else {
        return;
    }
    GetGame()->GetCameraModule()->GetCameraPos(0, reinterpret_cast<S_vector4 *>(&request.pos));
    GetSoundModule()->Play(request);
}

// ---------------------------------------------------------------------------------------------
// Queries

bool C_FirstPersonView::CanUseWeapon() const
{
    if (m_pose == FPV_POSE_GRENADE_OUT || m_pose == FPV_POSE_GRENADE || m_pose == FPV_POSE_WEAPON_BACK)
        return false;
    return m_event != FPV_EVENT_RELOAD && (m_event < FPV_EVENT_DRAW || m_event > FPV_EVENT_AIM_OUT);
}

I3D_frame *C_FirstPersonView::GetCartridgeDummy()
{
    if (!IsClient() || !m_model)
        return nullptr;
    return m_model->FindChildFrame("cardum", FRAME_MASK_DUMMY);
}

I3D_frame *C_FirstPersonView::GetBlastDummy()
{
    if (!IsClient() || !m_model)
        return nullptr;
    return m_model->FindChildFrame("blsdum", FRAME_MASK_DUMMY);
}

S_vector4 C_FirstPersonView::GetAimOrigin() const
{
    S_vector4 origin;
    if (IsClient()) {
        if (C_ItemDef *def = GetItemDef(m_itemType)) {
            // A grenade: from the first-person model.
            if (def->GetBehaviour() == ITEM_BEHAVIOUR_THROWN) {
                const S_matrix &matrix = m_weapon->GetMatrix();
                return {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2], matrix.m[3][3]};
            }
            GetGame()->GetCameraModule()->GetCameraPos(0, &origin);
            return origin;
        }
    }
    GetGame()->GetCameraModule()->GetCameraPos(0, &origin);
    return origin;
}

S_vector4 C_FirstPersonView::GetAimDir() const
{
    S_vector4 dir;
    GetGame()->GetCameraModule()->GetCameraDir(1, &dir);
    return dir;
}
