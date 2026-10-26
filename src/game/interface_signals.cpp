// The signals panel of the interface (see interface.h; the original's 0x004bf2e0..0x004c1200):
// the menu of squad commands (numbered pages of the tables below) for the whole squad or one
// member, with a page of commands on the actor under the crosshair.
#include "game/interface.h"

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/controller_messages.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/collision.h"
#include "game/squad_commander.h"
#include "game/team.h"
#include "game/text_box.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

using S_Entry = C_SignalsPanel::S_Entry;

// The pages (C_SignalsPanel::m_page; also the main page's actions that open them).
enum : int {
    SIGNAL_PAGE_MAIN = 0x32,
    SIGNAL_PAGE_MOVE = 0x33,
    SIGNAL_PAGE_COMBAT = 0x34,
    SIGNAL_PAGE_TARGET = 0x35,                          // = SQUAD_COMMAND_ON_ACTOR
    SIGNAL_PAGE_MOVE_QUIET = 0x36,
    SIGNAL_PAGE_COMBAT_QUIET = 0x37,
    SIGNAL_PAGE_TARGET_QUIET = 0x38,                    // = SQUAD_COMMAND_ON_ACTOR_QUIET
    SIGNAL_PAGE_FORMATION = 0x39,
    SIGNAL_PAGE_ORDERS = 0x3A,                          // (multiplayer: the team's messages)
    SIGNAL_PAGE_SUPPORT = 0x3B,                         // multiplayer: the radio's fire support
};

// The other actions of the entries.
enum : int {
    SIGNAL_CLOSE = 0x46,
    SIGNAL_BACK = 0x47,
    SIGNAL_DISABLED = 0x48,
    SIGNAL_ACTOR_COMMAND = 100,                         // + an E_HumanCommandType on the target
};
// SetEntry's command shown disabled.
constexpr int SIGNAL_DISABLED_COMMAND = 0x32;

// The tables' pages (the action's page - SIGNAL_PAGE_MAIN, but for the last three).
enum : int {
    TABLE_MAIN = 0,
    TABLE_FORMATION = 7,
    TABLE_SUPPORT = 8,
    TABLE_ORDERS = 9,
    TABLE_NO_GROUP = 10,                                // the main page disabled (nobody to signal)
    TABLE_PAGES = 11,
};

// The texts.
enum : int {
    TEXT_SIGNALS = 0x1C66,                              // the main page's title
    TEXT_SIGNALS_PAGE = 0x1C67,                         // a page's title (then its main entry's text)
    TEXT_WHOLE_SQUAD = 0x1C6C,
    TEXT_FIRE_SUPPORT = 0x1C28,                         // multiplayer: the radio's entry
};

// The text styles (the menus' style table).
enum : uint8_t {
    STYLE_TITLE = 0x2F,
    STYLE_PAGE_TITLE = 0x30,
    STYLE_ADDRESSEE = 0x31,
    STYLE_NUMBER = 0x32,
    STYLE_ENTRY = 0x33,
    STYLE_ENTRY_DISABLED = 0x34,
    STYLE_ENTRY_SELECTED = 0x35,
};

// Item types: the photo camera; the radios that call the fire support (multiplayer).
enum : int {
    ITEM_PHOTO_CAMERA = 0xE5,
    ITEM_SUPPORT_RADIO_F3 = 0xF3,
    ITEM_SUPPORT_RADIO_F4 = 0xF4,
};

// The formation types whose entry is off (the current one), and the one without the order entry.
enum : int {
    FORMATION_ENTRY_0 = 3,
    FORMATION_ENTRY_1 = 0,
    FORMATION_ENTRY_2 = 1,
    FORMATION_NO_ORDERS = 0xD,
};

// The reach of a thrown grenade's point (m).
constexpr float THROW_REACH = 40.0f;

constexpr S_Entry NONE = {-1, -1};

// The single player's table (0x0083b620).
const S_Entry SINGLE_PLAYER_PAGES[TABLE_PAGES][C_SignalsPanel::ENTRIES] = {
    {{0x1C20, 0x33}, {0x1C21, 0x34}, {0x1C22, 0x35}, {0x1C23, 0x36}, {0x1C24, 0x37}, {0x1C25, 0x38},
     {0x1C26, 0x39}, {0x1C27, 0x3A}, NONE, {0x1C6A, SIGNAL_CLOSE}},
    {{0x1C2A, 0x00}, {0x1C2B, 0x04}, {0x1C2F, 0x0A}, {0x1C2C, 0x02}, {0x1C2D, 0x06}, {0x1C2E, 0x08},
     NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C40, 0x10}, {0x1C3F, 0x0E}, {0x1C3E, 0x0C}, {0x1C41, 0x12}, {0x1C42, 0x13}, {0x1C43, 0x14},
     NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C34, 0x01}, {0x1C35, 0x05}, {0x1C39, 0x0B}, {0x1C36, 0x03}, {0x1C37, 0x07}, {0x1C38, 0x09},
     NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C4A, 0x11}, {0x1C49, 0x0F}, {0x1C48, 0x0D}, NONE, NONE, NONE, NONE, NONE, NONE,
     {0x1C6B, SIGNAL_BACK}},
    {NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C52, 0x17}, {0x1C53, 0x15}, {0x1C54, 0x16}, {0x1C55, 0x18}, {0x1C56, 0x19}, {0x1C57, 0x1A},
     NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C70, 0x21}, {0x1C71, 0x22}, NONE, NONE, NONE, NONE, NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C5C, 0x1B}, {0x1C5D, 0x1C}, {0x1C5E, 0x1D}, {0x1C5F, 0x1E}, {0x1C60, 0x1F}, {0x1C61, 0x20},
     NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C20, SIGNAL_DISABLED}, {0x1C21, SIGNAL_DISABLED}, {0x1C22, SIGNAL_DISABLED},
     {0x1C23, SIGNAL_DISABLED}, {0x1C24, SIGNAL_DISABLED}, {0x1C25, SIGNAL_DISABLED},
     {0x1C26, SIGNAL_DISABLED}, {0x1C27, SIGNAL_DISABLED}, NONE, {0x1C6A, SIGNAL_CLOSE}},
};

// The multiplayer table (0x0083b990): the fire support on the main page, the team's messages.
const S_Entry MULTIPLAYER_PAGES[TABLE_PAGES][C_SignalsPanel::ENTRIES] = {
    {{0x1C20, 0x33}, {0x1C21, 0x34}, {0x1C22, 0x35}, {0x1C23, 0x36}, {0x1C24, 0x37}, {0x1C25, 0x38},
     {0x1C26, 0x39}, {0x1C29, 0x3A}, {0x1C28, 0x3B}, {0x1C6A, SIGNAL_CLOSE}},
    {{0x1C2A, 0x00}, {0x1C2B, 0x04}, {0x1C2F, 0x0A}, {0x1C2C, 0x02}, {0x1C2D, 0x06}, {0x1C2E, 0x08},
     NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C40, 0x10}, {0x1C3F, 0x0E}, {0x1C3E, 0x0C}, {0x1C41, 0x12}, {0x1C42, 0x13}, {0x1C43, 0x14},
     NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C34, 0x01}, {0x1C35, 0x05}, {0x1C39, 0x0B}, {0x1C36, 0x03}, {0x1C37, 0x07}, {0x1C38, 0x09},
     NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C4A, 0x11}, {0x1C49, 0x0F}, {0x1C48, 0x0D}, NONE, NONE, NONE, NONE, NONE, NONE,
     {0x1C6B, SIGNAL_BACK}},
    {NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C52, 0x17}, {0x1C53, 0x15}, {0x1C54, 0x16}, {0x1C55, 0x18}, {0x1C56, 0x19}, {0x1C57, 0x1A},
     NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C70, 0x21}, {0x1C71, 0x22}, NONE, NONE, NONE, NONE, NONE, NONE, NONE, {0x1C6B, SIGNAL_BACK}},
    {{0x1C7A, 0x23}, {0x1C7B, 0x24}, {0x1C7C, 0x25}, {0x1C7D, 0x26}, {0x1C81, 0x27}, {0x1C7E, 0x28},
     {0x1C7F, 0x2A}, NONE, {0x1C80, 0x29}, {0x1C6B, SIGNAL_BACK}},
    {{0x1C20, SIGNAL_DISABLED}, {0x1C21, SIGNAL_DISABLED}, {0x1C22, SIGNAL_DISABLED},
     {0x1C23, SIGNAL_DISABLED}, {0x1C24, SIGNAL_DISABLED}, {0x1C25, SIGNAL_DISABLED},
     {0x1C26, SIGNAL_DISABLED}, {0x1C28, SIGNAL_DISABLED}, {0x1C29, SIGNAL_DISABLED},
     {0x1C6A, SIGNAL_CLOSE}},
};

// The table in use (0x0089bcc8, set by Init).
const S_Entry (*s_pages)[C_SignalsPanel::ENTRIES] = MULTIPLAYER_PAGES;

C_Team *PlayerTeam()
{
    return g_teamManager.GetPlayerTeam();
}

bool IsEnabled(const S_Entry &entry)
{
    return entry.action != -1 && entry.action != SIGNAL_DISABLED;
}

// `member` carries the item, or (for the whole squad) one of the team does.
bool HasItem(C_Human *member, int typeId)
{
    if (member && member->FindItem(typeId))
        return true;
    return !member && PlayerTeam()->HasItem(typeId);
}

// The point the selected soldier looks at within `reach` m (what the ray from the camera hits,
// else its end); true: it hit something.
bool AimPoint(float reach, S_vector4 *point)            // 0x004c08a0 (__thiscall, the panel unused)
{
    C_Human *selected = PlayerTeam()->GetSelectedMember();
    const S_vector &viewDir = selected->GetViewDir();
    const S_vector dir(reach * viewDir.x, reach * viewDir.y, reach * viewDir.z);
    GetGame()->GetCameraModule()->GetCameraPos(0, point);
    selected->RemoveCollision(false);
    S_vector hit;
    const S_vector from(point->x, point->y, point->z);
    S_CollisionVolume *volume = g_collisionWorld.RayTest(from, dir, &hit, nullptr, 0x110000, 0);
    selected->RestoreCollision(false);
    if (volume) {
        point->x = hit.x;
        point->y = hit.y;
        point->z = hit.z;
        return true;
    }
    point->x = dir.x + point->x;
    point->y = dir.y + point->y;
    point->z = dir.z + point->z;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------------------------

bool C_SignalsPanel::Init(C_Interface *ui, I3D_scene *scene)
{
    m_scene = scene;
    m_frame = scene->FindFrame("signals", FRAME_MASK_ANY);
    m_ui = ui;
    if (m_frame)
        m_frame->SetOn(false);
    m_page = SIGNAL_PAGE_MAIN;
    m_text = new C_TextBox();
    m_text->AttachTo(scene, m_frame);
    m_selected = 0;
    m_target = nullptr;
    m_member = nullptr;
    m_noGroup = true;
    s_pages = IsLocalGame() ? SINGLE_PLAYER_PAGES : MULTIPLAYER_PAGES;
    return m_frame != nullptr;
}

void C_SignalsPanel::Done()
{
    m_text->Clear();
    delete m_text;
}

void C_SignalsPanel::Open(bool open, C_Actor *target, C_Human *member)
{
    m_target = target;
    m_member = member;
    if (open) {
        // Multiplayer: only for the whole squad, not in a deathmatch.
        if (!IsLocalGame()) {
            if (member)
                return;
            if (g_mpSettings->gameType == MP_GAME_DEATHMATCH)
                return;
        }
        // Without a target: the member's vehicle or the body it carries.
        if (!target && member) {
            if (C_Actor *vehicle = member->GetVehicle())
                m_target = vehicle;
            else if (C_Actor *carried = member->GetCarried())
                m_target = carried;
        }
        C_Human *selected = PlayerTeam()->GetSelectedMember();
        if (!selected || selected->IsDead())
            return;
        if (IsLocalGame()) {
            std::vector<C_Human *> group;
            GetSquadCommander()->CollectGroup(group, C_SquadCommander::GROUP_NEAR);
            if (!member) {
                m_noGroup = group.empty();
            } else {
                // Quirk kept: a member out of the group keeps the last state.
                for (size_t i = group.size(); i-- > 0;) {
                    if (group[i] == member)
                        m_noGroup = false;
                }
            }
        } else {
            m_noGroup = false;
        }
        m_page = SIGNAL_PAGE_MAIN;
        m_selected = -1;
        BuildPage();
        SelectNext();
    } else {
        m_ui->HighlightMembers(nullptr, 5);
    }
    m_frame->SetOn(open);
    m_frame->Update();
}

bool C_SignalsPanel::IsOpen() const
{
    return m_frame->IsOn();
}

void C_SignalsPanel::SetEntry(int index, int textId, int command)
{
    m_entries[index].textId = textId;
    m_entries[index].action = command == SIGNAL_DISABLED_COMMAND ? SIGNAL_DISABLED : command + SIGNAL_ACTOR_COMMAND;
}

bool C_SignalsPanel::HasTargetCommands() const
{
    if (!IsLocalGame() || !m_target)
        return false;
    switch (m_target->GetType()) {
    case ACTOR_MINE:
    case ACTOR_DOOR:
    case ACTOR_RADIO:
    case ACTOR_LADDER:
    case ACTOR_SUPPLY_BOX:
    case ACTOR_ITEM:
    case ACTOR_SWITCH:
    case ACTOR_PHOTO_PLACE:
    case ACTOR_SUBMARINE:
    case ACTOR_STATIC_WEAPON:
    case ACTOR_EXPLOSIVE_PLACE:
    case ACTOR_DOCUMENT_PLACE:
    case ACTOR_PIKE:
    case ACTOR_MELEE_TRACKER:
    case ACTOR_WIRE_CUT:
        return true;
    case ACTOR_HUMAN:
        return m_target != m_member;
    case ACTOR_CAR: {
        C_Human *selected = PlayerTeam()->GetSelectedMember();
        auto *car = static_cast<C_Car *>(m_target);
        if (!car->CanEnter() && !car->HasEnemyAboard(selected))
            return false;
        return !car->IsDestroyed();
    }
    case ACTOR_ANIMATION: {
        auto *animation = static_cast<C_AnimationActor *>(m_target);
        return !animation->IsStopped() && !animation->ShowsNoInfo();
    }
    default:
        return false;
    }
}

void C_SignalsPanel::BuildPage()
{
    auto copyPage = [this](int table) {
        for (int i = 0; i < ENTRIES; ++i)
            m_entries[i] = s_pages[table][i];
    };
    if (m_noGroup) {
        copyPage(TABLE_NO_GROUP);
        return;
    }
    C_Human *selected = PlayerTeam()->GetSelectedMember();
    switch (m_page) {
    case SIGNAL_PAGE_MAIN:
        copyPage(TABLE_MAIN);
        if (!IsLocalGame()) {
            // No target pages; the fire support with one of the radios.
            m_entries[2].action = SIGNAL_DISABLED;
            m_entries[5].action = SIGNAL_DISABLED;
            if (selected->FindItem(ITEM_SUPPORT_RADIO_F4) || selected->FindItem(ITEM_SUPPORT_RADIO_F3)) {
                m_entries[8].action = SIGNAL_PAGE_SUPPORT;
                m_entries[8].textId = TEXT_FIRE_SUPPORT;
            }
            return;
        }
        if (!HasTargetCommands()) {
            m_entries[2].action = SIGNAL_DISABLED;
            m_entries[5].action = SIGNAL_DISABLED;
        }
        if (m_member) {
            if (m_member->GetOrderCount() == 0) {
                // No orders to hold, resume or cancel.
                m_entries[7].action = SIGNAL_DISABLED;
                m_entries[6].action = SIGNAL_DISABLED;
                m_entries[8].action = SIGNAL_DISABLED;
                return;
            }
        } else {
            // The whole squad: the idle members take the signals, the busy ones the orders'.
            bool noIdle = true;
            bool noBusy = true;
            for (int i = PlayerTeam()->GetMemberCount(); i-- > 0;) {
                if (PlayerTeam()->GetMember(i) == selected)
                    continue;
                if (PlayerTeam()->GetMember(i)->IsDead())
                    continue;
                if (PlayerTeam()->GetMember(i)->GetOrderCount() != 0)
                    noBusy = false;
                else
                    noIdle = false;
            }
            if (!noIdle) {
                if (noBusy)
                    m_entries[7].action = SIGNAL_DISABLED;
                // (Multiplayer's check of the radios; single player: off.)
                if (IsLocalGame() ||
                    (!selected->FindItem(ITEM_SUPPORT_RADIO_F4) && !selected->FindItem(ITEM_SUPPORT_RADIO_F3)))
                    m_entries[8].action = SIGNAL_DISABLED;
                if (selected->GetFormationType() == FORMATION_NO_ORDERS)
                    m_entries[6].action = SIGNAL_DISABLED;
                return;
            }
        }
        // Members with orders (or none idle): only the orders and closing.
        m_entries[0].action = SIGNAL_DISABLED;
        m_entries[1].action = SIGNAL_DISABLED;
        m_entries[2].action = SIGNAL_DISABLED;
        m_entries[3].action = SIGNAL_DISABLED;
        m_entries[4].action = SIGNAL_DISABLED;
        m_entries[5].action = SIGNAL_DISABLED;
        m_entries[6].action = SIGNAL_DISABLED;
        m_entries[8].action = SIGNAL_DISABLED;
        return;
    case SIGNAL_PAGE_MOVE:
    case SIGNAL_PAGE_COMBAT:
    case SIGNAL_PAGE_MOVE_QUIET:
    case SIGNAL_PAGE_COMBAT_QUIET:
        copyPage(m_page - SIGNAL_PAGE_MAIN);
        return;
    case SIGNAL_PAGE_TARGET:
    case SIGNAL_PAGE_TARGET_QUIET:
        copyPage(m_page - SIGNAL_PAGE_MAIN);
        BuildTargetPage();
        return;
    case SIGNAL_PAGE_FORMATION: {
        copyPage(TABLE_FORMATION);
        // The current formation's entry off; the spacing's ends.
        const int type = selected->GetFormationType();
        if (type == FORMATION_ENTRY_0)
            m_entries[0].action = SIGNAL_DISABLED;
        else if (type == FORMATION_ENTRY_1)
            m_entries[1].action = SIGNAL_DISABLED;
        else if (type == FORMATION_ENTRY_2)
            m_entries[2].action = SIGNAL_DISABLED;
        if (GetSquadCommander()->IsSpacingMin()) {
            m_entries[3].action = SIGNAL_DISABLED;
            return;
        }
        if (GetSquadCommander()->IsSpacingMax())
            m_entries[4].action = SIGNAL_DISABLED;
        return;
    }
    case SIGNAL_PAGE_ORDERS:
        copyPage(TABLE_ORDERS);
        return;
    case SIGNAL_PAGE_SUPPORT:
        copyPage(TABLE_SUPPORT);
        if (!selected->FindItem(ITEM_SUPPORT_RADIO_F3))
            m_entries[0].action = SIGNAL_DISABLED;
        if (!selected->FindItem(ITEM_SUPPORT_RADIO_F4))
            m_entries[1].action = SIGNAL_DISABLED;
        return;
    default:
        return;
    }
}

void C_SignalsPanel::BuildTargetPage()
{
    C_Human *selected = PlayerTeam()->GetSelectedMember();
    if (!m_target)
        return;
    switch (m_target->GetType()) {
    case ACTOR_SUPPLY_BOX: {
        auto *box = static_cast<C_SupplyBox *>(m_target);
        const bool empty = box->GetCharges() == 0;
        const int maxCarried = box->GetMaxCarried();
        if (!box->HasItemLists()) {
            // Ammunition for the weapon in the hands (not for a member carrying enough).
            if (m_member) {
                C_WeaponItem *weapon = m_member->GetHandsWeapon();
                if (empty || (weapon && weapon->GetDef()->GetAmmoItemId() > 0 &&
                              maxCarried <= m_member->CountItems(weapon->GetDef()->GetAmmoItemId())))
                    SetEntry(0, 0x1B8D, SIGNAL_DISABLED_COMMAND);
                else
                    SetEntry(0, 0x1B8D, 6);
                return;
            }
            SetEntry(0, 0x1B8D, empty ? SIGNAL_DISABLED_COMMAND : 6);
            return;
        }
        // The allied list's items (not one a member cannot take and carries enough of).
        // Fix: at most ENTRIES of them (the original wrote past the entries).
        const int *items = box->GetItemList(true);
        for (int i = 0; i < box->GetItemCount(true) && i < ENTRIES; ++i) {
            C_ItemDef *def = GetItemDef(items[i]);
            const bool offered =
                !empty && (!m_member || g_itemsModule.CanAddItemToInventory(m_member, items[i]) > 0 ||
                           maxCarried > m_member->CountItems(items[i]));
            SetEntry(i, def->GetTextId(), offered ? 6 : SIGNAL_DISABLED_COMMAND);
        }
        return;
    }
    case ACTOR_ANIMATION:
        if (!static_cast<C_AnimationActor *>(m_target)->IsStopped())
            SetEntry(0, 0x1B82, 0x16);
        return;
    case ACTOR_DOOR: {
        auto *door = static_cast<C_Door *>(m_target);
        if (door->IsLocked()) {
            SetEntry(0, 0x1B6F, SIGNAL_DISABLED_COMMAND);
            SetEntry(1, 0x1B81, SIGNAL_DISABLED_COMMAND);
            SetEntry(2, 0x1B80, SIGNAL_DISABLED_COMMAND);
            SetEntry(3, 0x1B7D, SIGNAL_DISABLED_COMMAND);
            SetEntry(4, 0x1B79, door->GetUnknown235() ? HUMAN_COMMAND_PICK_LOCK : SIGNAL_DISABLED_COMMAND);
            SetEntry(5, 0x1B6C, HasItem(m_member, door->GetLockKey()) ? HUMAN_COMMAND_UNLOCK_DOOR
                                                                      : SIGNAL_DISABLED_COMMAND);
            return;
        }
        SetEntry(0, 0x1B6F, HUMAN_COMMAND_OPEN_DOOR);
        SetEntry(1, 0x1B81, HUMAN_COMMAND_CLOSE_DOOR);
        SetEntry(2, 0x1B80, 0x15);
        SetEntry(3, 0x1B7D,
                 !door->GetUnknown118() && (door->GetUnknownFC() != 0 || door->GetUnknown100() != 0)
                     ? 0x12
                     : SIGNAL_DISABLED_COMMAND);
        // Locking: a closed door, the selected soldier has the key.
        if (door->GetUnknown224() == 0 && selected->FindItem(door->GetLockKey()))
            SetEntry(4, 0x1B6B, HUMAN_COMMAND_LOCK_DOOR);
        else
            SetEntry(4, 0x1B6B, SIGNAL_DISABLED_COMMAND);
        return;
    }
    case ACTOR_RADIO: {
        auto *radio = static_cast<C_Radio *>(m_target);
        if (radio->GetState() != 1) {
            SetEntry(0, 0x1B6E, SIGNAL_DISABLED_COMMAND);
            return;
        }
        SetEntry(0, 0x1B6E, HasItem(m_member, radio->GetItemId()) ? HUMAN_COMMAND_ENTER_VEHICLE
                                                                   : SIGNAL_DISABLED_COMMAND);
        return;
    }
    case ACTOR_LADDER:
    case ACTOR_SWITCH:
    case ACTOR_PIKE:
        SetEntry(0, 0x1B6E, HUMAN_COMMAND_ENTER_VEHICLE);
        return;
    case ACTOR_CAR: {
        auto *car = static_cast<C_Car *>(m_target);
        if (car->HasEnemyAboard(selected)) {
            SetEntry(0, 0x1B82, 0x16);
            return;
        }
        if (m_member) {
            if (m_member->GetVehicle() == m_target) {
                SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
                return;
            }
            SetEntry(0, 0x1B6E, HUMAN_COMMAND_ENTER_VEHICLE);
            C_Actor *vehicle = m_member->GetVehicle();
            if (vehicle && vehicle->HasSeatWeapon(m_member->GetVehicleSeat()))
                SetEntry(1, 0x1B82, 0x16);
            return;
        }
        // The whole squad: get in, get out, or both.
        bool aboard = false;
        bool outside = false;
        for (int i = PlayerTeam()->GetMemberCount(); i-- > 0;) {
            C_Human *other = PlayerTeam()->GetMember(i);
            if (other == selected || other->IsDead() || other->GetOrderCount() != 0)
                continue;
            if (other->GetVehicle() == m_target)
                aboard = true;
            else
                outside = true;
        }
        if (aboard) {
            if (outside) {
                SetEntry(0, 0x1B6E, HUMAN_COMMAND_ENTER_VEHICLE);
                SetEntry(1, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
            } else {
                SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
            }
            return;
        }
        SetEntry(0, 0x1B6E, HUMAN_COMMAND_ENTER_VEHICLE);
        SetEntry(1, 0x1B82, 0x16);
        return;
    }
    case ACTOR_SUBMARINE:
    case ACTOR_STATIC_WEAPON:
        if (m_member && m_member->GetVehicle() == m_target)
            SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
        else
            SetEntry(0, 0x1B6E, HUMAN_COMMAND_ENTER_VEHICLE);
        return;
    case ACTOR_HUMAN: {
        auto *human = static_cast<C_Human *>(m_target);
        if (human->IsDead()) {
            if (m_member && m_target == m_member->GetCarried()) {
                SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
                SetEntry(1, 0x1B83, SIGNAL_DISABLED_COMMAND);
                SetEntry(2, 0x1B84, SIGNAL_DISABLED_COMMAND);
                SetEntry(3, 0x1B8A, SIGNAL_DISABLED_COMMAND);
                return;
            }
            SetEntry(0, 0x1B71, HUMAN_COMMAND_PICK_UP_BODY);
            SetEntry(1, 0x1B83, 0x17);
            SetEntry(2, 0x1B84, SIGNAL_DISABLED_COMMAND);
            SetEntry(3, 0x1B8A, 0x21);
            return;
        }
        if (human->IsUnconscious()) {
            SetEntry(0, 0x1B85, 0x19);
            SetEntry(1, 0x1B83, 0x17);
            SetEntry(2, 0x1B84, human->GetUnknown1A5() ? SIGNAL_DISABLED_COMMAND : 0x18);
            SetEntry(3, 0x1B8A, 0x21);
            return;
        }
        if (PlayerTeam()->IsMember(human)) {
            // Healing: the member's medkit, or (the whole squad) another's.
            const bool medkit = (m_member && m_member->GetMedkit()) ||
                                (!m_member && PlayerTeam()->HasMedkitBesides(human));
            SetEntry(0, 0x1B7A, medkit ? HUMAN_COMMAND_HEAL : SIGNAL_DISABLED_COMMAND);
            SetEntry(1, 0x1B8A, 0x21);
            return;
        }
        SetEntry(0, 0x1B85, 0x19);
        SetEntry(1, 0x1B86, 0x1A);
        SetEntry(2, 0x1B7E, human->IsSurrendered() ? HUMAN_COMMAND_TIE_UP : SIGNAL_DISABLED_COMMAND);
        return;
    }
    case ACTOR_MELEE_TRACKER:
        SetEntry(0, 0x1B85, 0x19);
        return;
    case ACTOR_WIRE_CUT:
        // (Without a member: whoever has the cutters.)
        if (m_member && !m_member->FindBeltItem19())
            SetEntry(0, 0x1B7B, SIGNAL_DISABLED_COMMAND);
        else
            SetEntry(0, 0x1B7B, HUMAN_COMMAND_CUT_WIRE);
        return;
    case ACTOR_ITEM:
        SetEntry(0, 0x1B71, HUMAN_COMMAND_PICK_UP_BODY);
        return;
    case ACTOR_MINE:
        SetEntry(0, 0x1B6D, HUMAN_COMMAND_DEFUSE_MINE);
        SetEntry(1, 0x1B82, 0x16);
        return;
    case ACTOR_EXPLOSIVE_PLACE: {
        auto *place = static_cast<C_ExplosivePlace *>(m_target);
        switch (place->GetState()) {
        case 0:
            SetEntry(0, 0x1B87, HasItem(m_member, place->GetItemId()) ? HUMAN_COMMAND_PLANT_EXPLOSIVE
                                                                       : SIGNAL_DISABLED_COMMAND);
            SetEntry(1, 0x1B6D, SIGNAL_DISABLED_COMMAND);
            return;
        case 1:
            SetEntry(0, 0x1B87, SIGNAL_DISABLED_COMMAND);
            SetEntry(1, 0x1B6D, HUMAN_COMMAND_DISARM_EXPLOSIVE);
            return;
        case 2:
            SetEntry(0, 0x1B87, SIGNAL_DISABLED_COMMAND);
            SetEntry(1, 0x1B6D, SIGNAL_DISABLED_COMMAND);
            return;
        default:
            return;
        }
    }
    case ACTOR_DOCUMENT_PLACE: {
        auto *place = static_cast<C_DocumentPlace *>(m_target);
        SetEntry(0, place->GetTextId(), HasItem(m_member, place->GetItemId()) ? HUMAN_COMMAND_DELIVER_DOCUMENTS
                                                                               : SIGNAL_DISABLED_COMMAND);
        return;
    }
    case ACTOR_PHOTO_PLACE:
        SetEntry(0, 0x1B6A, HasItem(m_member, ITEM_PHOTO_CAMERA) ? HUMAN_COMMAND_ENTER_VEHICLE
                                                                 : SIGNAL_DISABLED_COMMAND);
        return;
    default:
        return;
    }
}

void C_SignalsPanel::Refresh()
{
    m_text->Clear();
    if (m_page == SIGNAL_PAGE_MAIN) {
        m_text->AddTextId(TEXT_SIGNALS, STYLE_TITLE);
    } else {
        m_text->AddTextId(TEXT_SIGNALS_PAGE, STYLE_TITLE);
        // (The main page's entry of the page: the title.)
        m_text->AddTextId(s_pages[TABLE_MAIN][m_page - SIGNAL_PAGE_MOVE].textId, STYLE_PAGE_TITLE);
    }
    if (m_page == SIGNAL_PAGE_TARGET || m_page == SIGNAL_PAGE_TARGET_QUIET) {
        // The target's name.
        m_text->AddText(*m_target->GetName(), STYLE_ADDRESSEE);
    } else if (m_member) {
        // Fix: the name as text (the original used it as the format).
        m_text->AddText(C_fstr("%s", m_member->GetShortName()), STYLE_ADDRESSEE);
    } else {
        m_text->AddTextId(TEXT_WHOLE_SQUAD, STYLE_ADDRESSEE);
    }
    int i = 0;
    for (; i < ENTRIES - 1; ++i) {
        const S_Entry &entry = m_entries[i];
        if (entry.textId == -1) {
            m_text->AddText(C_fstr(""), STYLE_NUMBER);
            continue;
        }
        m_text->AddText(C_fstr("%d. ", i + 1), STYLE_NUMBER);
        uint8_t style;
        if (entry.action == SIGNAL_DISABLED)
            style = STYLE_ENTRY_DISABLED;
        else if (m_selected == i)
            style = STYLE_ENTRY_SELECTED;
        else
            style = STYLE_ENTRY;
        m_text->AddTextId(entry.textId, style);
    }
    // The last entry (close / back) is key 0.
    m_text->AddText(C_fstr("0. "), STYLE_NUMBER);
    m_text->AddTextId(m_entries[i].textId, m_selected == i ? STYLE_ENTRY_SELECTED : STYLE_ENTRY);
    m_frame->SetOn(true);
}

void C_SignalsPanel::Activate()
{
    const int action = m_entries[m_selected].action;
    switch (action) {
    case -1:
    case SIGNAL_DISABLED:
        break;
    case SIGNAL_PAGE_MOVE:
    case SIGNAL_PAGE_COMBAT:
    case SIGNAL_PAGE_TARGET:
    case SIGNAL_PAGE_MOVE_QUIET:
    case SIGNAL_PAGE_COMBAT_QUIET:
    case SIGNAL_PAGE_TARGET_QUIET:
    case SIGNAL_PAGE_FORMATION:
    case SIGNAL_PAGE_ORDERS:
    case SIGNAL_PAGE_SUPPORT:
    case SIGNAL_BACK:
        m_page = action == SIGNAL_BACK ? SIGNAL_PAGE_MAIN : action;
        m_selected = -1;
        BuildPage();
        SelectNext();
        break;
    default:
        Issue();
        Open(false, nullptr, nullptr);
        break;
    case SIGNAL_CLOSE:
        Open(false, nullptr, nullptr);
        break;
    }
    g_clickSound->SetOn(true);
}

void C_SignalsPanel::SelectNext()
{
    for (int i = m_selected; i < ENTRIES - 1;) {
        ++i;
        if (IsEnabled(m_entries[i])) {
            m_selected = i;
            break;
        }
    }
    g_clickSound->SetOn(true);
    Refresh();
}

void C_SignalsPanel::SelectPrevious()
{
    // Fix: from nothing chosen (-1) nothing (the original walked back through memory).
    for (int i = m_selected; i > 0;) {
        --i;
        if (IsEnabled(m_entries[i])) {
            m_selected = i;
            break;
        }
    }
    g_clickSound->SetOn(true);
    Refresh();
}

void C_SignalsPanel::HandleInput(float *held, float *pressed)
{
    // The number keys choose the entries (0: the tenth).
    const int key = GetIGraph()->GetAsciiKey();
    const int digit = static_cast<int>(GetIGraph()->KeyToChar(key, 0) & 0xFF) - '0';
    if (digit >= 0 && digit < 10) {
        const int index = digit == 0 ? ENTRIES - 1 : digit - 1;
        g_controls.ClearKey(key, held, pressed);
        if (IsEnabled(m_entries[index])) {
            m_selected = index;
            Activate();
        }
        return;
    }
    // The right button chooses, the wheel moves.
    if (pressed[INPUT_ROLL_MENU] != 0.0f) {
        g_controls.ClearAction(INPUT_ROLL_MENU, held, pressed);
        Activate();
        return;
    }
    if (pressed[INPUT_WHEEL] > 0.0f) {
        g_controls.ClearAction(INPUT_WHEEL, held, pressed);
        SelectPrevious();
        return;
    }
    if (pressed[INPUT_WHEEL] < 0.0f) {
        g_controls.ClearAction(INPUT_WHEEL, held, pressed);
        SelectNext();
    }
}

void C_SignalsPanel::Issue()
{
    C_Human *selected = PlayerTeam()->GetSelectedMember();
    S_ActionCommand command;
    command.member = m_member;
    command.command = -1;
    command.actor = nullptr;
    command.value = -1;
    const int action = m_entries[m_selected].action;
    auto lookDir = [&]() {
        const S_vector &dir = selected->GetViewDir();
        command.dir = {dir.x, dir.y, dir.z, 0.0f};
    };
    auto aimAt = [&](float reach) {
        S_vector4 point = {};
        AimPoint(reach, &point);
        command.dir = point;
    };
    switch (m_page) {
    case SIGNAL_PAGE_MOVE:
    case SIGNAL_PAGE_MOVE_QUIET:
        command.action = action;
        // The places around the point looked at; the others along the view.
        if (action == SQUAD_COMMAND_MOVE_NEAR || action == SQUAD_COMMAND_MOVE_NEAR_QUIET)
            aimAt(GetGame()->GetCameraModule()->GetViewDistance());
        else
            lookDir();
        break;
    case SIGNAL_PAGE_COMBAT:
    case SIGNAL_PAGE_COMBAT_QUIET:
        command.action = action;
        if (action == SQUAD_COMMAND_ENGAGE || action == SQUAD_COMMAND_FIRE_LAUNCHER)
            aimAt(GetGame()->GetCameraModule()->GetViewDistance());
        else if (action == SQUAD_COMMAND_THROW)
            aimAt(THROW_REACH);
        else
            lookDir();
        break;
    case SIGNAL_PAGE_TARGET:
    case SIGNAL_PAGE_TARGET_QUIET:
        // The page is the command; a supply box's item by its entry.
        command.action = m_page;
        command.command = action - SIGNAL_ACTOR_COMMAND;
        command.actor = m_target;
        if (command.command == 6 && m_target && m_target->GetType() == ACTOR_SUPPLY_BOX)
            command.value = m_selected;
        break;
    case SIGNAL_PAGE_FORMATION:
    case SIGNAL_PAGE_ORDERS:
    case SIGNAL_PAGE_SUPPORT:
        command.action = action;
        lookDir();
        break;
    default:
        return;
    }
    selected->PlayerSendActionCommand(command);
}

void C_SignalsPanel::Render()
{
    if (m_frame->IsOn())
        m_text->Render();
}

void C_SignalsPanel::SetTarget(C_Actor *actor)
{
    if (!IsOpen())
        return;
    C_Actor *previous = m_target;
    if (previous == actor)
        return;
    m_target = actor;
    // Without one: the member's vehicle or the body it carries.
    if (!actor && m_member) {
        if (m_member->GetVehicle())
            m_target = m_member->GetVehicle();
        else if (m_member->GetCarried())
            m_target = m_member->GetCarried();
    }
    if (previous == m_target)
        return;
    if (m_page != SIGNAL_PAGE_MAIN) {
        if (m_page != SIGNAL_PAGE_TARGET && m_page != SIGNAL_PAGE_TARGET_QUIET)
            return;
        m_page = SIGNAL_PAGE_MAIN;
    }
    BuildPage();
    Refresh();
}

void C_SignalsPanel::Update(int /*timeDelta*/)
{
    C_Human *selected = PlayerTeam()->GetSelectedMember();
    if (!selected || selected->IsDead()) {
        Open(false, nullptr, nullptr);
        return;
    }
    if (!IsLocalGame())
        return;
    if (m_member && m_member->IsDead())
        Open(false, nullptr, nullptr);
    if (PlayerTeam()->GetAliveCount() <= 1) {
        Open(false, nullptr, nullptr);
        return;
    }
    // The members the page is for highlighted.
    const int group = m_page == SIGNAL_PAGE_MAIN ? 0 : m_page == SIGNAL_PAGE_ORDERS ? 2 : 1;
    m_ui->HighlightMembers(m_member, group);
    // The page again when the group gets or loses its members.
    std::vector<C_Human *> members;
    GetSquadCommander()->CollectGroup(members, C_SquadCommander::GROUP_NEAR);
    if (m_noGroup) {
        if (!members.empty())
            Open(true, m_target, nullptr);
    } else if (members.empty()) {
        Open(true, nullptr, nullptr);
    }
}

void C_SignalsPanel::Relayout()
{
    m_text->AttachTo(m_scene, m_frame);
}

void C_SignalsPanel::OnTargetDeleted(C_Actor *actor)
{
    // Fix: the deleted actor is forgotten (the original kept the pointer and read it again).
    if (m_target == actor)
        m_target = nullptr;
    if (!IsOpen())
        return;
    if (m_page != SIGNAL_PAGE_TARGET && m_page != SIGNAL_PAGE_TARGET_QUIET && m_page != SIGNAL_PAGE_MAIN)
        return;
    // The main page again with its first entry chosen.
    m_page = SIGNAL_PAGE_MAIN;
    m_selected = 0;
    BuildPage();
    Refresh();
}
