// The squad members' panels of the interface (see interface.h; the original's 0x004b30e0..
// 0x004b48d0): a member's photo, name and weapon, the health, stamina and disguise bars, the
// state and AI mode icons, the marks (controlled, in earshot, silenced weapon, medic, VIP) and the
// visibility.
#include "game/interface.h"

#include <cstdint>
#include <cstdio>

#include "common/c_str.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/soldier.h"
#include "game/team.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// The text styles (the menus' style table).
enum : uint8_t {
    STYLE_MEMBER_NAME = 0x3A,                           // run 0
    STYLE_MEMBER_WEAPON = 0x3B,                         // run 1
    STYLE_MEMBER_VISIBILITY = 0x3C,
};

// The multiplayer rank titles (8 each).
enum : int {
    TEXT_ALLIED_RANKS = 0x27A6,
    TEXT_AXIS_RANKS = 0x27B0,
};

// FindChildFrame's type mask for the parts: visuals by a name pattern ("photo*"; the 0x10000 bit,
// inferred).
constexpr uint32_t PART_MASK = 0x10001;

// The health under which the critical icon shows (per cent), the first aid skill over which the
// medic mark does, and the squared distance (m) within which a member hears the active one.
constexpr float HEALTH_CRITICAL = 50.0f;
constexpr int MEDIC_SKILL = 40;
constexpr float EARSHOT_SQ = 1600.0f;

I3D_frame *FindPart(I3D_frame *panel, const char *name)
{
    I3D_frame *part = panel->FindChildFrame(name, PART_MASK);
    static_cast<I3D_visual *>(part)->VisualFlags() |= VISFLAGS_1000;
    return part;
}

// The material of a part's first face group, made its own.
I3D_material *OwnMaterial(I3D_frame *part)
{
    I3D_mesh_level *level = static_cast<I3D_visual *>(part)->GetVisualMesh()->GetLevel(0);
    level->FaceGroups()[0].DuplicateMaterial();
    return static_cast<I3D_visual *>(part)->GetVisualMesh()->GetLevel(0)->FaceGroups()[0].material;
}

I3D_texture *FirstMaterialTexture(I3D_frame *visual)
{
    return static_cast<I3D_visual *>(visual)->GetVisualMesh()->GetLevel(0)->FaceGroups()[0].material->GetTexture();
}

// A bar: its depth scaled (the part of it covered).
void SetBar(I3D_frame *bar, float depth)
{
    bar->SetScale(S_vector(1.0f, 1.0f, depth));
    bar->Update();
}

// The weapon in the hands as text run 1.
void ShowWeapon(C_TextBox *text, C_Human *human)
{
    if (!human->GetHandsWeapon())
        text->SetText(C_fstr(""), STYLE_MEMBER_WEAPON, 1);
    else
        text->SetTextId(human->GetHandsWeapon()->GetDef()->GetTextId(), STYLE_MEMBER_WEAPON, 1);
}

bool HasSilencedWeapon(C_Human *human)
{
    return human->FindItem(ITEM_SILENCED_16) || human->FindItem(ITEM_SILENCED_7) ||
           human->FindItem(ITEM_SILENCED_104);
}

// The visibility (the distance the member can be seen from) where the text shows.
void ShowVisibility(C_TextBox *text, C_Human *human)
{
    text->AddText(C_fstr("%.0fm", static_cast<double>(human->GetVisibility())), STYLE_MEMBER_VISIBILITY);
}

C_TextBox *NewText(I3D_scene *scene, I3D_frame *frame)
{
    auto *text = new C_TextBox();
    text->SetUnknown90(0);
    text->AttachTo(scene, frame);
    return text;
}

} // namespace

bool C_MemberPanel::Init(I3D_scene *scene, int index, I3D_texture **stateIcons, I3D_texture **stateIcons2)
{
    m_stateIcons = stateIcons;
    m_modeIcons = stateIcons2;
    m_human = nullptr;
    m_index = index;
    m_scene = scene;
    char name[68];
    std::sprintf(name, "Soldier%02d", index);
    m_frame = scene->FindFrame(name, FRAME_MASK_ANY);
    // (The map cursor's id of the panel.)
    m_frame->SetData(reinterpret_cast<S_FrameData *>(static_cast<intptr_t>(index + 100)));
    m_activeMark = FindPart(m_frame, "player0*");
    m_activeMark->SetOn(false);
    m_ear = FindPart(m_frame, "ear*");
    m_ear->SetOn(false);
    m_select = FindPart(m_frame, "select*");
    m_select->SetOn(false);
    m_photo = FindPart(m_frame, "photo*");
    static_cast<I3D_object_visual *>(m_photo)->DuplicateMesh();
    m_photoMaterial = OwnMaterial(m_photo);
    m_healthBar = FindPart(m_frame, "healthbar*");
    m_healthCritical = FindPart(m_frame, "healthcr*");
    m_healthIcon = FindPart(m_frame, "health0*");
    FindPart(m_frame, "stamina0*");
    m_staminaBar = FindPart(m_frame, "staminabar*");
    FindPart(m_frame, "disguise0*");
    m_disguiseBar = FindPart(m_frame, "disguisebar*");
    // (Multiplayer shows no disguise.)
    if (!IsLocalGame()) {
        m_disguiseBar->SetScale(S_vector(1.0f, 1.0f, 1.0f));
        m_disguiseBar->Update();
    }
    m_commandMaterial = OwnMaterial(FindPart(m_frame, "command*"));
    m_mindMaterial = OwnMaterial(FindPart(m_frame, "mind*"));
    // The blank icon (also for the dead).
    I3D_frame *blank = scene->FindFrame("univerz", FRAME_MASK_ANY);
    blank->SetOn(false);
    m_blankTexture = FirstMaterialTexture(blank);
    GetIGraph()->LoadTexture("x_fc_death.bmp", "x_fc_a.bmp", 4, &m_deathImage);
    m_name = NewText(scene, m_frame);
    m_visibility = FindPart(m_frame, "visibility*");
    m_visibilityText = NewText(scene, m_visibility);
    m_visibility->SetOn(false);
    SetSelected(false);
    m_frame->SetOn(false);
    m_aiMode = m_frame->FindChildFrame("AImode*", PART_MASK);
    if (m_aiMode)
        static_cast<I3D_visual *>(m_aiMode)->VisualFlags() |= VISFLAGS_1000;
    m_photoColor = {1.0f, 1.0f, 1.0f, m_photoColor.w};
    m_unknown98 = -1.0f;
    m_silencer = m_frame->FindChildFrame("silence*", PART_MASK);
    m_medic = m_frame->FindChildFrame("medic*", PART_MASK);
    m_vip = m_frame->FindChildFrame("mp_vip*", PART_MASK);
    if (m_vip)
        m_vip->SetOn(false);
    m_rank = 0;
    return true;
}

void C_MemberPanel::Done()
{
    m_deathImage->Release();
    delete m_name;
    delete m_visibilityText;
    m_name = nullptr;
    m_visibilityText = nullptr;
    m_frame = nullptr;
}

void C_MemberPanel::UpdateName()
{
    m_rank = m_human->GetProfileUnknown24();
    if (IsLocalGame()) {
        const E_HumanRole role = m_human->GetRole();
        if (role == HUMAN_ROLE_NPC_TEAM || role == HUMAN_ROLE_NPC_TEAM_5) {
            m_name->SetText(C_fstr("%s", m_human->GetLastName()), STYLE_MEMBER_NAME, 0);
            return;
        }
        const C_str rank = g_texts.Get(g_rankNames[m_rank].textId);
        m_name->SetText(C_fstr("%s %s", rank.c_str(), m_human->GetLastName()), STYLE_MEMBER_NAME, 0);
        return;
    }
    // Multiplayer: the rank titles of the player's side (in a deathmatch: his uniform's side; none
    // counts as the axis).
    bool allied;
    if (g_mpSettings->gameType == MP_GAME_DEATHMATCH) {
        C_Item *uniform = m_human->GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem();
        allied = uniform && uniform->GetDef()->GetTeam() == SIDE_ALLIES;
    } else {
        allied = m_human->GetTeam(false) == 1;
    }
    const C_str rank = g_texts.Get((allied ? TEXT_ALLIED_RANKS : TEXT_AXIS_RANKS) + m_rank);
    m_name->SetText(C_fstr("%s %s", rank.c_str(), m_human->GetShortName()), STYLE_MEMBER_NAME, 0);
}

void C_MemberPanel::SetHuman(C_Human *human)
{
    m_human = human;
    m_name->Clear();
    m_visibilityText->Clear();
    UpdateName();
    if (m_human->IsDead()) {
        m_photoMaterial->SetTexture(reinterpret_cast<I3D_texture *>(m_deathImage));
    } else if (S_SoldierProfile *profile = m_human->GetProfile()) {
        if (profile->faceImage)
            m_photoMaterial->SetTexture(reinterpret_cast<I3D_texture *>(profile->faceImage));
        ShowWeapon(m_name, m_human);
    }
    m_photoMaterial->SetUnknown04(S_vector(1.0f, 1.0f, 1.0f));
    m_photo->Update();
    m_alive = true;
    m_disguise = 1;
    m_stamina = 1;
    m_unknown78 = 1;
    m_health = 1.0f;
    m_unknown88 = false;
    m_selected = false;
    m_select->SetOn(false);
    m_statusIcon = -1;
    m_modeIcon = -1;
    m_silencer->SetOn(HasSilencedWeapon(m_human));
    m_medic->SetOn(m_human->GetStat(STAT_FIRST_AID) > MEDIC_SKILL);
    UpdateStatusIcon();
    UpdateModeIcon();
}

void C_MemberPanel::SetSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    m_select->SetOn(selected);
}

void C_MemberPanel::Update()
{
    if (m_rank != m_human->GetProfileUnknown24())
        UpdateName();
    if (m_human->IsInventoryChanged()) {
        m_silencer->SetOn(HasSilencedWeapon(m_human));
        ShowWeapon(m_name, m_human);
    }
    // (A dead member's panel stays as its death left it.)
    if (!m_alive)
        return;
    if (!IsLocalGame() && m_vip)
        m_vip->SetOn(m_human && m_human->GetNetId() == GetGame()->GetVipSlot());

    const float health = static_cast<float>(m_human->GetHealthPercent());
    if (health != m_health) {
        m_health = health;
        if (health <= 0.0f) {
            // Dead: the marks off, the bars empty, the death photo.
            SetSelected(false);
            m_activeMark->SetOn(false);
            // Fix: "AImode*" may be missing (Init allows it).
            if (m_aiMode)
                m_aiMode->SetOn(false);
            m_ear->SetOn(false);
            SetBar(m_healthBar, 1.0f);
            SetBar(m_staminaBar, 1.0f);
            SetBar(m_disguiseBar, 1.0f);
            m_photoMaterial->SetTexture(reinterpret_cast<I3D_texture *>(m_deathImage));
            m_photoMaterial->SetUnknown04(S_vector(1.0f, 1.0f, 1.0f));
            m_photo->Update();
            m_visibilityText->Clear();
            if (IsLocalGame() || g_gameProfile.IsCooperativeGame())
                ShowVisibility(m_visibilityText, m_human);
            UpdateStatusIcon();
            UpdateModeIcon();
            m_alive = false;
            return;
        }
        const bool critical = health < HEALTH_CRITICAL;
        if (m_healthCritical->IsOn() != critical)
            m_healthCritical->SetOn(critical);
        if (m_healthIcon->IsOn() == critical)
            m_healthIcon->SetOn(!critical);
        m_healthCritical->Update();
        m_healthIcon->Update();
        // The bar's cover never quite closes.
        float lost = 1.0f - m_health * 0.01f;
        if (lost > 0.95f)
            lost = 0.95f;
        SetBar(m_healthBar, lost);
    }

    const int stamina = m_human->GetStaminaPercent();
    if (stamina != m_stamina) {
        m_stamina = stamina;
        SetBar(m_staminaBar, 1.0f - static_cast<float>(stamina) * 0.01f);
    }
    if (IsLocalGame()) {
        const int disguise = m_human->GetDisguisePercent();
        if (disguise != m_disguise) {
            m_disguise = disguise;
            SetBar(m_disguiseBar, 1.0f - static_cast<float>(disguise) * 0.01f);
        }
    }
    const S_vector4 &color = m_human->GetScale();
    if (m_photoColor.x != color.x || m_photoColor.y != color.y || m_photoColor.z != color.z) {
        m_photoMaterial->SetUnknown04(S_vector(color.x, color.y, color.z));
        m_photoColor = color;
        m_photo->Update();
    }
    UpdateStatusIcon();
    UpdateModeIcon();
    if (IsLocalGame() || g_gameProfile.IsCooperativeGame()) {
        m_visibilityText->Clear();
        ShowVisibility(m_visibilityText, m_human);
    }
    if (!IsLocalGame())
        return;

    C_Human *active = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    m_activeMark->SetOn(active == m_human);
    // Fix: "AImode*" may be missing (Init allows it).
    if (m_aiMode)
        m_aiMode->SetOn(!m_human->IsLocalPlayer());
    // The ear: the member has orders or is within 40 m of the active soldier.
    if (m_human->GetOrderCount() != 0) {
        m_ear->SetOn(true);
        return;
    }
    const S_vector &from = m_human->GetFrame()->GetWorldPos();
    const S_vector &to = active->GetFrame()->GetWorldPos();
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    m_ear->SetOn((dz * dz + dy * dy) + dx * dx < EARSHOT_SQ);
}

void C_MemberPanel::Render()
{
    if (m_frame->IsOn())
        m_name->Render();
    if (m_frame->IsOn())
        m_visibilityText->Render();
}

void C_MemberPanel::Relayout()
{
    m_name->AttachTo(m_scene, m_frame);
    m_visibilityText->AttachTo(m_scene, m_visibility);
}

void C_MemberPanel::UpdateStatusIcon()
{
    if (m_human->IsDead()) {
        m_commandMaterial->SetTexture(m_blankTexture);
        return;
    }
    const int icon = m_human->GetSquadStatusIcon();
    if (m_statusIcon == icon)
        return;
    m_statusIcon = icon;
    // (None for the player's own soldier.)
    if (m_human->IsLocalPlayer() || m_statusIcon == -1) {
        m_commandMaterial->SetTexture(m_blankTexture);
        return;
    }
    // (Icons 2 and 3 take each other's place in the table.)
    if (m_statusIcon == 3)
        m_commandMaterial->SetTexture(m_stateIcons[2]);
    else if (m_statusIcon == 2)
        m_commandMaterial->SetTexture(m_stateIcons[3]);
    else
        m_commandMaterial->SetTexture(m_stateIcons[m_statusIcon]);
}

void C_MemberPanel::UpdateModeIcon()
{
    if (m_human->IsDead()) {
        m_mindMaterial->SetTexture(m_blankTexture);
        return;
    }
    const int icon = m_human->GetSquadModeIcon();
    if (m_modeIcon == icon)
        return;
    m_modeIcon = icon;
    if (m_human->IsLocalPlayer() || m_modeIcon == -1) {
        m_mindMaterial->SetTexture(m_blankTexture);
        return;
    }
    m_mindMaterial->SetTexture(m_modeIcons[m_modeIcon]);
}
