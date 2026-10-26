// The soldier's status panels of the interface (see interface.h; the original's 0x004a8c10..
// 0x004ab230): the car's gauges, the scopes' reticles, the weapons, the crosshair, the stance and
// the action's progress.
#include "game/interface.h"
#include "game/interface_internal.h"

#include <cstdio>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/soldier.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// The text styles the panels use (the menus' style table; not the info screen's).
enum : uint8_t {
    STYLE_WEAPON_NAME = 0x36,
    STYLE_WEAPON_HANDS = 0x37,
    STYLE_WEAPON_SHOULDER = 0x38,
    STYLE_WEAPON_THROWN = 0x39,
    STYLE_CROSSHAIR_NAME = 0x3D,
    STYLE_STANCE_TIMER = 0x3E,
    STYLE_PROGRESS = 0x42,
};

// The texts.
enum : int {
    TEXT_DEAD_ENEMY = 0x1BF7,                           // the crosshair on a dead enemy (single player)
};

// A frame's x / y moved (the others kept).
void SetPosX(I3D_frame *frame, float x)
{
    const S_vector &pos = frame->GetPos();
    frame->SetPosInline(S_vector(x, pos.y, pos.z));
}

void SetPosY(I3D_frame *frame, float y)
{
    const S_vector &pos = frame->GetPos();
    frame->SetPosInline(S_vector(pos.x, y, pos.z));
}

C_Human *ActivePlayer()
{
    return g_teamManager.GetPlayerTeam()->GetActivePlayer();
}

const float WHITE[4] = {1.0f, 1.0f, 1.0f, 0.0f};
const float BLUE[4] = {0.0f, 0.0f, 1.0f, 0.0f};

} // namespace

// ---------------------------------------------------------------------------------------------
// The shared helpers (interface_internal.h)

I3D_texture *FindTexture(I3D_frame *visual, const char *fileName)
{
    I3D_mesh *mesh = static_cast<I3D_visual *>(visual)->GetVisualMesh();
    for (int i = mesh->GetLevel(0)->NumFaceGroups(); i-- > 0;) {
        I3D_material *material = mesh->GetLevel(0)->GetFaceGroup(i)->material;
        if (_stricmp(material->GetTexture()->GetFileName(), fileName) == 0)
            return mesh->GetLevel(0)->GetFaceGroup(i)->material->GetTexture();
    }
    return nullptr;
}

I3D_material *DuplicateMaterial(I3D_frame *visual, const char *fileName)
{
    static_cast<I3D_object_visual *>(visual)->DuplicateMesh();
    I3D_mesh *mesh = static_cast<I3D_visual *>(visual)->GetVisualMesh();
    for (int i = mesh->GetLevel(0)->NumFaceGroups(); i-- > 0;) {
        I3D_material *material = mesh->GetLevel(0)->GetFaceGroup(i)->material;
        if (!material)
            continue;
        I3D_texture *texture = material->GetTexture();
        if (!texture)
            continue;
        if (_stricmp(mesh->GetLevel(0)->GetFaceGroup(i)->material->GetTexture()->GetFileName(), fileName) != 0)
            continue;
        mesh->GetLevel(0)->GetFaceGroup(i)->DuplicateMaterial();
        return mesh->GetLevel(0)->GetFaceGroup(i)->material;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// C_Speedometer

bool C_Speedometer::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("spee", FRAME_MASK_ANY);
    m_needles[0] = m_frame->FindChildFrame("speed01", FRAME_MASK_ANY);
    m_needles[1] = m_frame->FindChildFrame("speed02", FRAME_MASK_ANY);
    m_needles[2] = m_frame->FindChildFrame("speed03", FRAME_MASK_ANY);
    m_frame->SetOn(false);
    return true;
}

void C_Speedometer::Update(float speed, float rpm, float fuel)
{
    constexpr float DEG_TO_RAD = 0.0174532924f;
    const S_vector axis(0.0f, 0.0f, 1.0f);
    if (speed > 180.0f)
        speed = 180.0f;
    S_quat rot;
    rot.Make(axis, speed * DEG_TO_RAD);
    m_needles[0]->SetRot(rot);
    m_needles[0]->Update();
    // 0.02 degrees per rpm.
    if (rpm > 9000.0f)
        rpm = 9000.0f;
    rot.Make(axis, rpm * 0.02f * DEG_TO_RAD);
    m_needles[1]->SetRot(rot);
    m_needles[1]->Update();
    rot.Make(axis, fuel * 180.0f * DEG_TO_RAD);
    m_needles[2]->SetRot(rot);
    m_needles[2]->Update();
}

// ---------------------------------------------------------------------------------------------
// C_Reticle

bool C_Reticle::Init(I3D_scene *scene)
{
    m_mask = scene->FindFrame("sniper", FRAME_MASK_VISUAL);
    m_binoculars = scene->FindFrame("scope", FRAME_MASK_VISUAL);
    if (m_binoculars)
        m_binoculars->SetOn(false);
    m_reticles[0] = scene->FindFrame("arisaka", FRAME_MASK_DUMMY);
    m_reticles[1] = scene->FindFrame("k98", FRAME_MASK_DUMMY);
    m_reticles[2] = scene->FindFrame("springfield", FRAME_MASK_DUMMY);
    m_reticles[3] = scene->FindFrame("lee_enfield", FRAME_MASK_DUMMY);
    m_mask->SetOn(false);
    // Quirk kept: the binoculars' frame is switched off again without the check.
    m_binoculars->SetOn(false);
    for (int i = 4; i-- > 0;) {
        if (m_reticles[i])
            m_reticles[i]->SetOn(false);
    }
    return true;
}

void C_Reticle::Done()
{
    m_mask = nullptr;
    for (I3D_frame *&reticle : m_reticles)
        reticle = nullptr;
    m_binoculars = nullptr;
}

// (Quirk kept: a reticle is only ever switched on here; mode 6 takes them all off.)
void C_Reticle::SetMode(int mode)
{
    switch (mode) {
    case 0:
    case 1:
    case 2:
    case 3:
        m_mask->SetOn(true);
        m_reticles[mode]->SetOn(true);
        break;
    case 4:
        m_binoculars->SetOn(true);
        break;
    case 6:
        m_mask->SetOn(false);
        m_binoculars->SetOn(false);
        for (int i = 4; i-- > 0;)
            m_reticles[i]->SetOn(false);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// C_WeaponPanel

bool C_WeaponPanel::Init(I3D_scene *scene)
{
    m_scene = scene;
    m_frame = scene->FindFrame("tabl02", FRAME_MASK_ANY);
    m_handsIcon = m_frame->FindChildFrame("i_hands", FRAME_MASK_ANY);
    m_shoulderIcon = m_frame->FindChildFrame("i_shoulder", FRAME_MASK_ANY);
    // The icons' empty picture (kept for an empty hand) and their own materials.
    m_emptyTexture = FindTexture(m_handsIcon, "X_SQUARE.BMP");
    m_emptyTexture->AddRef();
    m_handsMaterial = DuplicateMaterial(m_handsIcon, "X_SQUARE.BMP");
    m_shoulderMaterial = DuplicateMaterial(m_shoulderIcon, "X_SQUARE.BMP");
    m_text = new C_TextBox();
    m_text->SetUnknown90(0);
    m_text->AttachTo(scene, m_frame);
    m_text->AddText(C_fstr(""), STYLE_WEAPON_NAME);
    m_text->AddText(C_fstr(""), STYLE_WEAPON_HANDS);
    m_text->AddText(C_fstr(""), STYLE_WEAPON_SHOULDER);
    m_text->AddText(C_fstr(""), STYLE_WEAPON_THROWN);
    m_thrownItems = -1;
    m_shoulderAmmo = -1;
    m_handsAmmo = -1;
    m_shoulderRounds = -1;
    m_handsRounds = -1;
    m_shoulderDef = nullptr;
    m_handsDef = nullptr;
    return m_frame != nullptr;
}

void C_WeaponPanel::Done()
{
    m_emptyTexture->Release();
    m_text->Clear();
    delete m_text;
}

void C_WeaponPanel::ShowHandsItem(C_ItemDef *def)
{
    if (m_handsDef == def)
        return;
    m_handsDef = def;
    if (!def) {
        m_text->SetText(C_fstr(" "), STYLE_WEAPON_NAME, 0);
        m_handsMaterial->SetTexture(m_emptyTexture);
        return;
    }
    m_text->SetTextId(def->GetTextId(), STYLE_WEAPON_NAME, 0);
    if (I3D_texture *image = reinterpret_cast<I3D_texture *>(def->GetImage()))
        m_handsMaterial->SetTexture(image);
}

void C_WeaponPanel::ShowShoulderItem(C_ItemDef *def)
{
    if (m_shoulderDef == def)
        return;
    m_shoulderDef = def;
    if (!def) {
        m_shoulderMaterial->SetTexture(m_emptyTexture);
        return;
    }
    if (I3D_texture *image = reinterpret_cast<I3D_texture *>(def->GetImage()))
        m_shoulderMaterial->SetTexture(image);
}

void C_WeaponPanel::ShowRounds(int rounds, int ammo, int which)
{
    char text[32];
    if (which == 1) {
        if (rounds == m_handsRounds && ammo == m_handsAmmo)
            return;
        m_handsRounds = rounds;
        m_handsAmmo = ammo;
        std::sprintf(text, "%d/%d", rounds, ammo);
        m_text->SetText(C_fstr(text), STYLE_WEAPON_HANDS, 1);
    } else {
        if (rounds == m_shoulderRounds && ammo == m_shoulderAmmo)
            return;
        m_shoulderAmmo = ammo;
        m_shoulderRounds = rounds;
        std::sprintf(text, "%d/%d", rounds, ammo);
        m_text->SetText(C_fstr(text), STYLE_WEAPON_SHOULDER, 2);
    }
}

void C_WeaponPanel::Update(int /*timeDelta*/)
{
    C_Human *human = ActivePlayer();
    if (!human)
        return;
    // The hands: a seat's weapon (an item definition), else the item held.
    const int seat = human->GetVehicleSeat();
    C_Actor *vehicle = human->GetVehicle();
    if (vehicle && vehicle->HasSeatWeapon(seat)) {
        ShowHandsItem(vehicle->GetSeatWeaponDef(seat));
        const int ammo = vehicle->GetSeatAmmoCount(seat);
        const int rounds = vehicle->GetSeatWeaponRounds(seat);
        ShowRounds(rounds, ammo, 1);
    } else {
        int rounds = 0;
        int ammo = 0;
        if (C_Item *item = human->GetHandsItem()) {
            ShowHandsItem(item->GetDef());
            if (item->GetDef()->GetCarryMode() == 6) {
                rounds = 1;
                ammo = 1;
            } else {
                // Launchers count their rounds in the backpack too.
                const bool backpack = item->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER;
                ammo = human->CountAmmo(item->GetDef()->GetAmmoItemId(), backpack);
                rounds = static_cast<int>(item->GetAmount());
            }
        } else {
            ShowHandsItem(nullptr);
        }
        ShowRounds(rounds, ammo, 1);
    }
    // The shoulder (a firearm's rounds only).
    int rounds = 0;
    int ammo = 0;
    if (C_Item *item = human->GetLongGunItem()) {
        ShowShoulderItem(item->GetDef());
        if (item->GetKind() == ITEM_KIND_WEAPON) {
            const bool backpack = item->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER;
            ammo = human->CountAmmo(item->GetDef()->GetAmmoItemId(), backpack);
            rounds = static_cast<int>(item->GetAmount());
        }
    } else {
        ShowShoulderItem(nullptr);
    }
    ShowRounds(rounds, ammo, 2);
    const int thrown = human->CountThrownItems();
    if (m_thrownItems != thrown) {
        m_thrownItems = thrown;
        m_text->SetText(C_fstr("%d", thrown), STYLE_WEAPON_THROWN, 3);
    }
}

void C_WeaponPanel::Render()
{
    m_text->Render();
}

void C_WeaponPanel::Relayout()
{
    m_text->AttachTo(m_scene, m_frame);
}

// ---------------------------------------------------------------------------------------------
// C_Crosshair

bool C_Crosshair::Init(I3D_scene *scene)
{
    m_target = nullptr;
    m_usable = false;
    m_frame = scene->FindFrame("cross", FRAME_MASK_ANY);
    m_frame->SetPosInline(S_vector(0.5f, -0.375f, -0.14f));
    m_frame->Update();
    m_left = m_frame->FindChildFrame("cross_L", FRAME_MASK_VISUAL);
    m_right = m_frame->FindChildFrame("cross_R", FRAME_MASK_VISUAL);
    m_top = m_frame->FindChildFrame("cross_T", FRAME_MASK_VISUAL);
    m_bottom = m_frame->FindChildFrame("cross_B", FRAME_MASK_VISUAL);
    // Each part: its own unlit material and a fixed light level.
    for (I3D_frame *part : {m_left, m_right, m_top, m_bottom}) {
        I3D_mesh_level *level = static_cast<I3D_visual *>(part)->GetVisualMesh()->GetLevel(0);
        level->FaceGroups()[0].DuplicateMaterial();
        I3D_material *material = level->GetFaceGroup(0)->material;
        material->Flags() |= I3DMATERIAL_TRACE_FLAG;
        material->SetDiffuse(S_vector(0.0f, 0.0f, 0.0f));
        material->SetEmissive(S_vector(0.0f, 0.0f, 0.0f));
        material->SetAmbient(S_vector(1.0f, 1.0f, 1.0f));
        material->SetAlpha(0.0f);
        static_cast<I3D_object_visual *>(part)->SetLightLevel(0.6f);
    }
    // The target's name, doubled, on "cross_name" (or the cross).
    m_name = new C_TextBox();
    if (I3D_frame *nameFrame = m_frame->FindChildFrame("cross_name", FRAME_MASK_VISUAL)) {
        nameFrame->SetScale(S_vector(2.0f, 2.0f, 2.0f));
        m_name->AttachTo(scene, nameFrame);
        nameFrame->SetOn(false);
    } else {
        m_name->AttachTo(scene, m_frame);
    }
    m_spread = 0.0f;
    m_frame->Update();
    Show(true);
    m_color[0] = 0.0f;
    m_color[1] = 0.0f;
    m_color[2] = 0.0f;
    SetColor(WHITE);
    return true;
}

void C_Crosshair::Done()
{
    delete m_name;
}

void C_Crosshair::Show(bool show)
{
    m_shown = show;
    if (show && !g_gameProfile.IsCrosshairAllowed())
        m_shown = false;
    m_frame->SetOn(m_shown);
    m_frame->Update();
}

void C_Crosshair::SetColor(const float *color)
{
    if (!m_frame->IsOn())
        return;
    if (color[0] == m_color[0] && color[1] == m_color[1] && color[2] == m_color[2])
        return;
    for (int i = 0; i < 4; ++i)
        m_color[i] = color[i];
    const S_vector diffuse(color[0], color[1], color[2]);
    FirstMaterial(m_top)->SetDiffuse(diffuse);
    FirstMaterial(m_bottom)->SetDiffuse(diffuse);
    FirstMaterial(m_right)->SetDiffuse(diffuse);
    FirstMaterial(m_left)->SetDiffuse(diffuse);
    m_frame->Update();
}

void C_Crosshair::SetTarget(C_Actor *actor, bool usable)
{
    if (m_target == actor && m_usable == usable)
        return;
    m_name->Clear();
    m_target = actor;
    m_usable = usable;
    C_Human *self = ActivePlayer();
    // Nothing, the own vehicle or the body carried, or a dead soldier: white, no name.
    if (!actor || actor == self->GetVehicle() || actor == self->GetCarried() || self->IsDead()) {
        SetColor(WHITE);
        return;
    }
    C_Team *team = g_teamManager.GetPlayerTeam();
    switch (actor->GetType()) {
    case ACTOR_MINE:
    case ACTOR_DOOR:
    case ACTOR_RADIO:
    case ACTOR_LADDER:
    case ACTOR_SUPPLY_BOX:
    case ACTOR_ITEM:
    case ACTOR_SWITCH:
    case ACTOR_PHOTO_PLACE:
    case ACTOR_EXPLOSIVE_PLACE:
    case ACTOR_DOCUMENT_PLACE:
    case ACTOR_WIRE_CUT:
        // (Quirk kept: not usable leaves the colour and an empty name.)
        if (!m_usable)
            return;
        SetColor(WHITE);
        m_name->AddText(*actor->GetName(), STYLE_CROSSHAIR_NAME);
        return;
    case ACTOR_HUMAN: {
        auto *human = static_cast<C_Human *>(actor);
        if (IsLocalGame()) {
            if (self->IsHostileTo(actor)) {
                SetColor(WHITE);
                if (human->IsDead())
                    m_name->AddTextId(TEXT_DEAD_ENEMY, STYLE_CROSSHAIR_NAME);
                return;
            }
            SetColor(BLUE);
            if (!team->IsMember(human)) {
                m_name->AddText(*actor->GetName(), STYLE_CROSSHAIR_NAME);
                return;
            }
            m_name->AddText(C_fstr("%d. %s", team->IndexOf(human) + 1, human->GetFullName()), STYLE_CROSSHAIR_NAME);
            return;
        }
        if (self->IsHostileTo(actor)) {
            SetColor(WHITE);
            if (human->IsDead())
                m_name->AddText(*actor->GetName(), STYLE_CROSSHAIR_NAME);
            return;
        }
        SetColor(BLUE);
        if (human->GetRole() != HUMAN_ROLE_PLAYER) {
            m_name->AddText(*actor->GetName(), STYLE_CROSSHAIR_NAME);
            return;
        }
        // A player: the rank and the name; a medic also sees the health.
        const C_str rank = g_texts.Get(g_rankNames[human->GetProfileUnknown24()].textId);
        if (self->GetStat(STAT_FIRST_AID) < 50) {
            m_name->AddText(C_fstr("%s %s", rank.c_str(), human->GetFullName()), STYLE_CROSSHAIR_NAME);
        } else {
            m_name->AddText(C_fstr("%s %s - %d%%", rank.c_str(), human->GetFullName(), human->GetHealthPercent()),
                            STYLE_CROSSHAIR_NAME);
        }
        return;
    }
    case ACTOR_CAR: {
        // The last seat's living occupant names it.
        C_Human *occupant = nullptr;
        for (int seat = actor->GetSeatCount(); seat-- > 0;) {
            if (!actor->GetSeatOccupant(seat))
                continue;
            auto *human = static_cast<C_Human *>(actor->GetSeatOccupant(seat));
            if (human->IsAlive()) {
                occupant = human;
                break;
            }
        }
        if (!occupant) {
            SetColor(WHITE);
            m_name->AddText(*actor->GetName(), STYLE_CROSSHAIR_NAME);
            return;
        }
        if (static_cast<C_Car *>(actor)->HasEnemyAboard(self)) {
            SetColor(WHITE);
            return;
        }
        SetColor(BLUE);
        if (!team->IsMember(occupant)) {
            m_name->AddText(*occupant->GetName(), STYLE_CROSSHAIR_NAME);
            return;
        }
        m_name->AddText(C_fstr("%d. %s", team->IndexOf(occupant) + 1, occupant->GetFullName()), STYLE_CROSSHAIR_NAME);
        return;
    }
    case ACTOR_SUBMARINE:
        SetColor(WHITE);
        m_name->AddText(*actor->GetName(), STYLE_CROSSHAIR_NAME);
        return;
    case ACTOR_STATIC_WEAPON: {
        auto *gunner = static_cast<C_Human *>(actor->GetSeatOccupant(0));
        if (!gunner) {
            SetColor(WHITE);
            m_name->AddText(*actor->GetName(), STYLE_CROSSHAIR_NAME);
            return;
        }
        if (self->IsHostileTo(gunner)) {
            SetColor(WHITE);
            return;
        }
        SetColor(BLUE);
        if (!team->IsMember(gunner)) {
            m_name->AddText(*gunner->GetName(), STYLE_CROSSHAIR_NAME);
            return;
        }
        m_name->AddText(C_fstr("%d. %s", team->IndexOf(gunner) + 1, gunner->GetFullName()), STYLE_CROSSHAIR_NAME);
        return;
    }
    case ACTOR_MELEE_TRACKER:
        SetColor(WHITE);
        return;
    default:
        return;
    }
}

// The parts spread with the soldier's accuracy (0.025 of it + 0.01), easing at 0.05 a second.
void C_Crosshair::Update(int timeDelta)
{
    if (!m_frame->IsOn())
        return;
    C_Human *human = ActivePlayer();
    if (!human)
        return;
    const float target = human->GetAccuracy() * 0.025f + 0.01f;
    if (target == m_spread)
        return;
    const float step = static_cast<float>(timeDelta) * 0.001f * 0.05f;
    if (target > m_spread) {
        m_spread += step;
        if (m_spread > target)
            m_spread = target;
    } else {
        m_spread -= step;
        if (m_spread < target)
            m_spread = target;
    }
    SetPosX(m_left, -m_spread);
    m_left->Update();
    SetPosX(m_right, m_spread);
    m_right->Update();
    SetPosY(m_top, m_spread);
    m_top->Update();
    SetPosY(m_bottom, -m_spread);
    m_bottom->Update();
}

void C_Crosshair::Render()
{
    if (m_target)
        m_name->Render();
}

// ---------------------------------------------------------------------------------------------
// C_StancePanel

bool C_StancePanel::Init(I3D_scene *scene)
{
    m_scene = scene;
    m_frame = scene->FindFrame("tabl01", FRAME_MASK_ANY);
    m_kneel = scene->FindFrame("pkneel", FRAME_MASK_ANY);
    m_prone = scene->FindFrame("play", FRAME_MASK_ANY);
    m_walk = scene->FindFrame("mwalk", FRAME_MASK_ANY);
    m_run = scene->FindFrame("mrun", FRAME_MASK_ANY);
    m_sprint = scene->FindFrame("msprint", FRAME_MASK_ANY);
    m_axis = scene->FindFrame("mpaxis", FRAME_MASK_ANY);
    m_allied = scene->FindFrame("mpallied", FRAME_MASK_ANY);
    m_allied->SetOn(false);
    m_axis->SetOn(false);
    m_kneel->SetOn(false);
    m_prone->SetOn(false);
    m_walk->SetOn(false);
    m_run->SetOn(false);
    m_sprint->SetOn(false);
    m_stance = 0;
    m_state = 0;
    m_team = -1;
    m_timer = 0;
    m_text = new C_TextBox();
    m_text->SetUnknown90(0);
    m_text->AttachTo(scene, m_frame);
    return true;
}

void C_StancePanel::Render()
{
    if (m_timer != 0)
        m_text->Render();
}

void C_StancePanel::Done()
{
    delete m_text;
}

void C_StancePanel::Relayout()
{
    m_text->AttachTo(m_scene, m_frame);
}

void C_StancePanel::Update(int timeDelta)
{
    // The scripts' timer: mm:ss while it runs.
    if (m_timer > 0) {
        m_text->Clear();
        m_timer -= timeDelta;
        if (m_timer > 0) {
            const int seconds = m_timer / 1000;
            m_text->AddText(C_fstr("%02d:%02d", seconds / 60, seconds % 60), STYLE_STANCE_TIMER);
        }
    }
    C_Human *human = ActivePlayer();
    if (!human)
        return;
    // Quirk kept: the old stance's frame goes off after the new one's came on (a flag both
    // have ends off).
    const uint32_t stance = human->GetStanceFlags();
    if (stance != m_stance) {
        if (stance & 2)
            m_kneel->SetOn(true);
        else if (stance & 1)
            m_prone->SetOn(true);
        if (m_stance & 2)
            m_kneel->SetOn(false);
        else if (m_stance & 1)
            m_prone->SetOn(false);
        m_stance = stance;
    }
    const uint32_t state = human->GetStateFlags();
    if (state != m_state) {
        m_walk->SetOn(false);
        m_run->SetOn(false);
        m_sprint->SetOn(false);
        switch (state & 0x1F000) {
        case 0x10000:
            m_sprint->SetOn(true);
            m_run->SetOn(true);
            m_walk->SetOn(true);
            break;
        case 0x4000:
            m_run->SetOn(true);
            m_walk->SetOn(true);
            break;
        case 0x2000:
            m_walk->SetOn(true);
            break;
        default:
            break;
        }
        m_state = state;
    }
    // The team games: the soldier's side.
    if (!IsLocalGame() && g_mpSettings->gameType != MP_GAME_DEATHMATCH) {
        const int team = human->GetTeam(false);
        if (m_team != team) {
            m_allied->SetOn(team == TEAM_ALLIES);
            m_axis->SetOn(team != TEAM_ALLIES);
            m_team = team;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// C_ProgressBar

bool C_ProgressBar::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("prgbrbk", FRAME_MASK_VISUAL);
    m_bar = scene->FindFrame("prgbr01", FRAME_MASK_VISUAL);
    m_unused08 = 0;
    m_unused0C = 0;
    m_frame->SetPos(S_vector(0.5f, -0.58f, -10.0f));
    m_frame->Update();
    I3D_frame *name = m_frame->FindChildFrame("progress_name", FRAME_MASK_ANY);
    name->SetOn(false);
    m_textId = -1;
    m_text = new C_TextBox();
    m_text->AttachTo(scene, name);
    m_frame->SetOn(false);
    return true;
}

void C_ProgressBar::Done()
{
    m_frame->SetOn(false);
    delete m_text;
}

void C_ProgressBar::Update(C_Human *human)
{
    int time;
    int total;
    int textId;
    if (!human || !human->GetActionProgress(&time, &total, &textId)) {
        m_frame->SetOn(false);
        return;
    }
    m_bar->SetScale(S_vector(1.0f, 1.0f, static_cast<float>(time) / static_cast<float>(total)));
    m_frame->SetOn(true);
    m_bar->Update();
    if (textId != m_textId) {
        m_text->Clear();
        m_textId = textId;
        if (textId >= 0)
            m_text->AddTextId(textId, STYLE_PROGRESS);
    }
}

void C_ProgressBar::Render()
{
    m_text->Render();
}
