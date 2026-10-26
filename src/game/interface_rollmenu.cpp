// The roll menu of the interface (see interface.h; the original's 0x004b0750..0x004b30b0): the
// numbered actions of the selected soldier on the actor under the crosshair (his vehicle, the body
// he carries), and the command map's instance with the commanded soldier's actions.
#include "game/interface.h"

#include <cstdint>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/controller_messages.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/team.h"
#include "game/text_box.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// The entries' actions besides the human commands (HUMAN_COMMAND_*; those without a name are
// written as numbers, as in the signals).
// (Ints, as they mix with HUMAN_COMMAND_*.)
constexpr int ROLL_PICK_UP = 6;                         // a body (HUMAN_COMMAND_PICK_UP_BODY), an item, a box's item
constexpr int ROLL_INVENTORY = 7;                       // the target's equipment in the inventory
constexpr int ROLL_SEAT = 8;                            // + the seat (to 13): get in there
constexpr int ROLL_ASK_HEAL = 0x1D;                     // the target (a member) heals the soldier
constexpr int ROLL_DISABLED = 0x32;                     // shown greyed, not chosen
constexpr int ROLL_CANCEL = 0x33;

// The text styles (the menus' style table).
enum : uint8_t {
    STYLE_TITLE = 0x2F,
    STYLE_TARGET = 0x31,
    STYLE_NUMBER = 0x32,
    STYLE_ENTRY = 0x33,
    STYLE_DISABLED = 0x34,
    STYLE_SELECTED = 0x35,
};

enum : int {
    TEXT_TITLE = 0x1B69,
    TEXT_LOCKED = 0x1B8C,
    TEXT_SEATS = 0x1BBC,                                // + the seat
    TEXT_CANCEL = 0x1C6A,
};

// The item types the command map's capture needs (one of the three), and the camera.
enum : int {
    ITEM_CAMERA = 0xE5,
};

// The farthest the selected soldier may be from the actor, and the distance given to his vehicle
// or the body he carries (m).
constexpr float REACH = 1.8f;
constexpr float CLOSE = 0.2f;
// The open menu's time (not read).
constexpr int OPEN_TIME = 400;
// The menu's lines: the title's height and a line's (the command map's cursor).
constexpr float TITLE_HEIGHT = 0.045f;
constexpr float LINES_PER_UNIT = 58.8235f;              // 1 / 0.017

C_Team *PlayerTeam()
{
    return g_teamManager.GetPlayerTeam();
}

// A vehicle the soldier can be in (the menu then offers its seats).
bool IsVehicle(const C_Actor *actor)
{
    switch (actor->GetType()) {
    case ACTOR_CAR:
    case ACTOR_SUBMARINE:
    case ACTOR_STATIC_WEAPON:
    case ACTOR_JUNKERS88:
        return true;
    default:
        return false;
    }
}

// The text of a vehicle's seat (its own or the default one).
int SeatText(C_Actor *vehicle, int seat)
{
    const int text = vehicle->GetSeatText(seat);
    return text == -1 ? TEXT_SEATS + seat : text;
}

// The text standing for `actor`'s name (see C_RollMenu::S_Entry).
int NameText(const C_Actor *actor)
{
    return -10 - actor->GetNetId();
}

// A human's short name. Fix: printed as it is (the original passed the names as the format).
C_fstr ShortName(const C_Human *human)
{
    return C_fstr("%s", human->GetShortName());
}

S_HumanCommand Command(int type, int param1, int param2)
{
    S_HumanCommand command;
    command.category = HUMAN_COMMAND_VEHICLE;
    command.type = type;
    command.param1 = param1;
    command.param2 = param2;
    return command;
}

}  // namespace

bool C_RollMenu::Init(C_Interface *ui, I3D_scene *scene, bool commandMap)
{
    m_scene = scene;
    m_ui = ui;
    m_frame = scene->FindFrame("rollmenu", FRAME_MASK_ANY);
    const S_vector &home = m_frame->GetPos();
    m_home = {home.x, home.y, home.z, m_frame->GetPosW()};
    m_mapCursor = scene->FindFrame("kurzor", FRAME_MASK_ANY);
    m_frame->SetOn(false);
    m_text = new C_TextBox;
    m_text->AttachTo(scene, m_frame);
    m_target = nullptr;
    m_commandMap = commandMap;
    m_open = false;
    return true;
}

void C_RollMenu::Done()
{
    m_text->Clear();
    delete m_text;
    m_text = nullptr;
}

bool C_RollMenu::IsEnabled(const S_Entry &entry)
{
    return entry.action != -1 && entry.action != ROLL_DISABLED;
}

void C_RollMenu::SetEntry(int index, int textId, int action)
{
    // Fix: entries past the last are dropped (the original wrote over the chosen entry and the
    // target: a supply box with more than ten items or a vehicle with more than nine seats).
    if (index >= ENTRIES)
        return;
    m_entries[index].action = action;
    m_entries[index].textId = textId;
}

// ---------------------------------------------------------------------------------------------
// The target

void C_RollMenu::SetTarget(C_Actor *actor, float distance)
{
    if (m_commandMap) {
        // The commanded soldier's actions on the actor clicked: a hostile gunner rather than his
        // gun, his own body or vehicle as they are.
        C_Human *commanded = PlayerTeam()->GetCommandedMember();
        if (!commanded) {
            m_target = nullptr;
            m_distance = distance;
            return;
        }
        if (actor->GetType() == ACTOR_STATIC_WEAPON && actor->GetSeatOccupant(0) &&
            static_cast<C_Human *>(actor->GetSeatOccupant(0))->IsHostileTo(commanded)) {
            m_target = actor->GetSeatOccupant(0);
            m_distance = distance;
            return;
        }
        if (actor == commanded->GetCarried()) {
            m_target = commanded->GetCarried();
            m_distance = distance;
            return;
        }
        if (actor == commanded->GetVehicle()) {
            m_target = commanded->GetVehicle();
            m_distance = distance;
            return;
        }
        m_target = actor;
        m_distance = distance;
        return;
    }

    // The selected soldier's body or vehicle come first; other actors only within reach.
    C_Human *selected = PlayerTeam()->GetSelectedMember();
    C_Actor *previous = m_target;
    if (!selected) {
        m_distance = 0.0f;
        m_target = nullptr;
    } else if (selected->GetCarried()) {
        m_target = selected->GetCarried();
        m_distance = CLOSE;
    } else if (selected->GetVehicle() && IsVehicle(selected->GetVehicle())) {
        m_target = selected->GetVehicle();
        m_distance = CLOSE;
    } else {
        m_target = actor;
        m_distance = distance;
        if (!(distance <= REACH)) {
            if (m_open)
                Open(false);
            m_target = nullptr;
        }
    }
    if (previous == m_target)
        return;
    if (m_open) {
        // The open menu follows the new target (reopened on it).
        if (m_target && m_distance <= REACH) {
            Open(false);
            Open(true);
            return;
        }
        Open(false);
        return;
    }
    if (!HasActions(m_target, m_distance))
        m_target = nullptr;
}

bool C_RollMenu::HasActions(C_Actor *actor, float distance)
{
    C_Human *selected = PlayerTeam()->GetSelectedMember();
    if (!selected || selected->GetPlayerViewMode() != 0 || selected->HasOrder())
        return false;
    if (selected->GetCarried())
        return true;
    if (selected->GetVehicle() && IsVehicle(selected->GetVehicle()))
        return true;
    if (!actor || (selected->GetBodyClimbState() != 0 && actor->GetType() != ACTOR_DOOR))
        return false;
    // Not while the weapon in the hands is busy.
    if (selected->GetHandsWeapon() && selected->GetHandsWeapon()->GetReadyTime() > GetGame()->GetMissionTime())
        return false;
    if (!(distance <= REACH))
        return false;
    switch (actor->GetType()) {
    case ACTOR_MINE:
    case ACTOR_DOOR:
    case ACTOR_RADIO:
    case ACTOR_ITEM:
    case ACTOR_SWITCH:
    case ACTOR_PHOTO_PLACE:
    case ACTOR_SUBMARINE:
    case ACTOR_EXPLOSIVE_PLACE:
    case ACTOR_DOCUMENT_PLACE:
    case ACTOR_WIRE_CUT:
        return true;
    case ACTOR_HUMAN: {
        auto *human = static_cast<C_Human *>(actor);
        if (IsLocalGame()) {
            // A body, an unconscious human, an idle member on foot, a surrendered enemy.
            if (human->IsDead() || human->IsUnconscious())
                return true;
            if (PlayerTeam()->IsMember(human))
                return human->GetBodyClimbState() == 0 && !human->IsBodyWorkState5() && !human->GetVehicle();
            return human->GetRole() == HUMAN_ROLE_ENEMY && human->IsSurrendered();
        }
        if (!IsTeamGame())
            return false;
        // Cooperative: the bodies, the unconscious and the surrendered (not players); and any
        // human of the soldier's team.
        if (g_gameProfile.IsCooperativeGame() && human->GetRole() != HUMAN_ROLE_PLAYER &&
            (human->IsDead() || human->IsUnconscious() || human->IsSurrendered()))
            return true;
        return human->GetTeam(false) == selected->GetTeam(false);
    }
    case ACTOR_CAR: {
        auto *car = static_cast<C_Car *>(actor);
        return car->CanEnter() && !car->IsDestroyed();
    }
    case ACTOR_SUPPLY_BOX: {
        auto *box = static_cast<C_SupplyBox *>(actor);
        return !box->IsLocked() && box->IsLimited();
    }
    case ACTOR_STATIC_WEAPON:
        return static_cast<C_StaticWeapon *>(actor)->GetState() == 1;
    default:
        return false;
    }
}

bool C_RollMenu::HasCommandMapActions() const
{
    if (!IsLocalGame() || !m_target)
        return false;
    C_Human *commanded = PlayerTeam()->GetCommandedMember();
    if (!commanded)
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
    case ACTOR_EXPLOSIVE_PLACE:
    case ACTOR_DOCUMENT_PLACE:
    case ACTOR_MELEE_TRACKER:
    case ACTOR_WIRE_CUT:
        return true;
    case ACTOR_HUMAN: {
        auto *human = static_cast<C_Human *>(m_target);
        if (human == PlayerTeam()->GetCommandedMember())
            return human->GetCarried() || (human->GetVehicle() && IsVehicle(human->GetVehicle()));
        if (PlayerTeam()->IsMember(human) &&
            (human->GetBodyClimbState() != 0 || human->IsBodyWorkState5() || human->GetVehicle()))
            return false;
        // (Not a living neutral.)
        return human->GetRole() != HUMAN_ROLE_NEUTRAL || human->IsDead();
    }
    case ACTOR_CAR:
        return !static_cast<C_Car *>(m_target)->IsDestroyed();
    case ACTOR_STATIC_WEAPON:
        return static_cast<C_StaticWeapon *>(m_target)->GetState() == 1;
    case ACTOR_ANIMATION: {
        auto *animation = static_cast<C_AnimationActor *>(m_target);
        return !animation->IsStopped() && !animation->ShowsNoInfo();
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------
// The entries

void C_RollMenu::BuildEntries()
{
    if (m_commandMap)
        BuildCommandMapActions();
    else
        BuildActions();
    m_selected = -1;
    SelectNext();
}

void C_RollMenu::BuildActions()
{
    for (int i = 0; i < ENTRIES; ++i)
        SetEntry(i, -1, -1);
    C_Human *selected = PlayerTeam()->GetSelectedMember();
    if (!selected)
        return;
    const bool local = IsLocalGame();
    if (C_Actor *carried = selected->GetCarried()) {
        // Put the body down.
        m_target = carried;
        SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
    } else if (selected->GetVehicle() && IsVehicle(selected->GetVehicle())) {
        C_Actor *vehicle = selected->GetVehicle();
        m_target = vehicle;
        switch (vehicle->GetType()) {
        case ACTOR_CAR:
        case ACTOR_SUBMARINE:
            // Get out, or over to another seat (not his own or an occupied one).
            SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
            for (int seat = 0; seat < vehicle->GetSeatCount(); ++seat) {
                int text = SeatText(vehicle, seat);
                int action = ROLL_SEAT + seat;
                if (selected->GetVehicleSeat() == seat) {
                    action = ROLL_DISABLED;
                } else if (C_Actor *occupant = vehicle->GetSeatOccupant(seat)) {
                    if (!local) {
                        text = NameText(occupant);
                        action = ROLL_DISABLED;
                    } else if (static_cast<C_Human *>(occupant)->IsDead()) {
                        text = 0x1B71;
                        action = ROLL_DISABLED;
                    } else if (!PlayerTeam()->IsMember(static_cast<C_Human *>(occupant))) {
                        action = ROLL_DISABLED;
                    }
                }
                SetEntry(seat + 1, text, action);
            }
            break;
        case ACTOR_STATIC_WEAPON:
            SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
            break;
        case ACTOR_JUNKERS88:
            // Quirk kept: no way out, and an occupied seat stays offered (named after its
            // occupant).
            for (int seat = 0; seat < vehicle->GetSeatCount(); ++seat) {
                int text = SeatText(vehicle, seat);
                int action = ROLL_SEAT + seat;
                if (selected->GetVehicleSeat() == seat)
                    action = ROLL_DISABLED;
                else if (C_Actor *occupant = vehicle->GetSeatOccupant(seat))
                    text = NameText(occupant);
                SetEntry(seat, text, action);
            }
            break;
        default:
            break;
        }
    } else if (C_Actor *target = m_target) {
        switch (target->GetType()) {
        case ACTOR_MINE:
            SetEntry(0, 0x1B6D, HUMAN_COMMAND_DEFUSE_MINE);
            break;
        case ACTOR_HUMAN: {
            auto *human = static_cast<C_Human *>(target);
            // Picking up a body: crouched, not busy, not in the water.
            const bool canPickUp = !selected->IsBodyWorkState5() && (selected->GetStanceFlags() & 4) && !human->IsInWater();
            if (!local) {
                if (human->GetTeam(false) == selected->GetTeam(false)) {
                    // A team mate: healing.
                    const bool heal = selected->GetMedkit() && !human->IsDead() && human->GetHealthPercent() < 100 &&
                                      !human->GetCarried();
                    SetEntry(0, 0x1B7A, heal ? HUMAN_COMMAND_HEAL : ROLL_DISABLED);
                } else if (g_gameProfile.IsCooperativeGame()) {
                    if (human->IsUnconscious() || human->IsDead())
                        SetEntry(0, 0x1B72, ROLL_INVENTORY);
                    else if (human->GetRole() == HUMAN_ROLE_ENEMY && human->IsSurrendered())
                        SetEntry(0, 0x1B7E, HUMAN_COMMAND_TIE_UP);
                }
                break;
            }
            if (PlayerTeam()->IsMember(human)) {
                // A member: his equipment, asking him for healing, healing him, his body.
                SetEntry(0, 0x1B72, ROLL_INVENTORY);
                if (human->IsDead()) {
                    SetEntry(1, 0x1B7F, ROLL_DISABLED);
                    SetEntry(2, 0x1B7A, ROLL_DISABLED);
                    SetEntry(3, 0x1B71, canPickUp ? ROLL_PICK_UP : ROLL_DISABLED);
                    break;
                }
                if (selected->GetCarried()) {
                    SetEntry(1, 0x1B7F, ROLL_DISABLED);
                    SetEntry(2, 0x1B7A, ROLL_DISABLED);
                } else {
                    SetEntry(1, 0x1B7F,
                             selected->GetHealthPercent() < 100 && human->GetMedkit() ? ROLL_ASK_HEAL : ROLL_DISABLED);
                    SetEntry(2, 0x1B7A,
                             selected->GetMedkit() && human->GetHealthPercent() < 100 ? HUMAN_COMMAND_HEAL
                                                                                      : ROLL_DISABLED);
                }
                SetEntry(3, 0x1B71, ROLL_DISABLED);
                break;
            }
            if (human->IsDead()) {
                SetEntry(0, 0x1B72, ROLL_INVENTORY);
                SetEntry(1, 0x1B71, canPickUp ? ROLL_PICK_UP : ROLL_DISABLED);
            } else if (human->IsUnconscious()) {
                SetEntry(0, 0x1B72, ROLL_INVENTORY);
            } else if (human->GetRole() == HUMAN_ROLE_ENEMY && human->IsSurrendered()) {
                SetEntry(0, 0x1B7E, HUMAN_COMMAND_TIE_UP);
            }
            break;
        }
        case ACTOR_CAR:
        case ACTOR_SUBMARINE:
            // Its seats (the occupied ones: a dead member's body to pick up, else greyed).
            for (int seat = 0; seat < target->GetSeatCount(); ++seat) {
                int text = SeatText(target, seat);
                int action = ROLL_SEAT + seat;
                if (C_Actor *occupant = target->GetSeatOccupant(seat)) {
                    if (!local) {
                        text = NameText(occupant);
                        action = ROLL_DISABLED;
                    } else if (static_cast<C_Human *>(occupant)->IsDead() && (selected->GetStanceFlags() & 4)) {
                        text = 0x1B71;
                        action = ROLL_PICK_UP;
                    } else if (!PlayerTeam()->IsMember(static_cast<C_Human *>(occupant))) {
                        action = ROLL_DISABLED;
                    }
                }
                SetEntry(seat, text, action);
            }
            break;
        case ACTOR_DOOR: {
            auto *door = static_cast<C_Door *>(target);
            if (!door->IsLocked()) {
                // Open (not when open), close (not when closed), lock (closed, with the key).
                SetEntry(0, 0x1B6F, door->GetUnknown224() == 1 ? ROLL_DISABLED : HUMAN_COMMAND_OPEN_DOOR);
                SetEntry(1, 0x1B81, door->GetUnknown224() == 0 ? ROLL_DISABLED : HUMAN_COMMAND_CLOSE_DOOR);
                SetEntry(2, 0x1B6B,
                         door->GetUnknown224() == 0 && selected->FindItem(door->GetLockKey()) ? HUMAN_COMMAND_LOCK_DOOR
                                                                                              : ROLL_DISABLED);
                break;
            }
            SetEntry(0, 0x1B6F, ROLL_DISABLED);
            SetEntry(1, 0x1B81, ROLL_DISABLED);
            C_Human *member = PlayerTeam()->GetSelectedMember();
            SetEntry(2, 0x1B6C, member->FindItem(door->GetLockKey()) ? HUMAN_COMMAND_UNLOCK_DOOR : ROLL_DISABLED);
            SetEntry(3, 0x1B79, door->GetUnknown235() ? HUMAN_COMMAND_PICK_LOCK : ROLL_DISABLED);
            break;
        }
        case ACTOR_RADIO:
            SetEntry(0, 0x1B6E,
                     static_cast<C_Radio *>(target)->CanUse(selected) ? HUMAN_COMMAND_ENTER_VEHICLE : ROLL_DISABLED);
            break;
        case ACTOR_SUPPLY_BOX: {
            auto *box = static_cast<C_SupplyBox *>(target);
            const int maxCarried = box->GetMaxCarried();
            const bool empty = box->GetCharges() == 0;
            if (box->HasItemLists()) {
                // The items of the soldier's side (not one he carries enough of).
                const bool allies = selected->GetTeam(false) != 0;
                const int *items = box->GetItemList(allies);
                for (int i = 0; i < box->GetItemCount(allies); ++i) {
                    C_ItemDef *def = GetItemDef(items[i]);
                    const bool offered = !empty && selected->CountItems(items[i]) < maxCarried;
                    SetEntry(i, def->GetTextId(), offered ? ROLL_PICK_UP : ROLL_DISABLED);
                }
                break;
            }
            // Ammunition for the weapon in the hands.
            C_WeaponItem *weapon = selected->GetHandsWeapon();
            bool offered = false;
            if (!empty && weapon) {
                // Quirk kept: it counts the items of type 1 (0 for weapons whose ammunition is item
                // 0), not the weapon's ammunition.
                const int ammoId = weapon->GetDef()->GetAmmoItemId();
                offered = selected->CountAmmo(ammoId != 0 || ammoId > 0 ? 1 : 0, false) < maxCarried;
            }
            SetEntry(0, 0x1B8D, offered ? ROLL_PICK_UP : ROLL_DISABLED);
            break;
        }
        case ACTOR_ITEM: {
            C_Item *item = static_cast<C_ItemActor *>(target)->GetItem();
            SetEntry(0, 0x1B71, g_itemsModule.CanPutItem(selected, item) > 0 ? ROLL_PICK_UP : ROLL_DISABLED);
            break;
        }
        case ACTOR_SWITCH:
            SetEntry(0, 0x1B6E, HUMAN_COMMAND_ENTER_VEHICLE);
            break;
        case ACTOR_PHOTO_PLACE: {
            C_Human *member = PlayerTeam()->GetSelectedMember();
            SetEntry(0, 0x1B6A, member && member->FindItem(ITEM_CAMERA) ? HUMAN_COMMAND_ENTER_VEHICLE : ROLL_DISABLED);
            break;
        }
        case ACTOR_STATIC_WEAPON: {
            // Man it (a member's or nobody's), or pick up its dead gunner.
            C_Actor *gunner = target->GetSeatOccupant(0);
            if (!local) {
                if (gunner)
                    SetEntry(0, NameText(gunner), ROLL_DISABLED);
                else
                    SetEntry(0, 0x1B6E, ROLL_SEAT);
                break;
            }
            auto *human = static_cast<C_Human *>(gunner);
            if (!human)
                SetEntry(0, 0x1B6E, ROLL_SEAT);
            else if (human->IsDead() && (selected->GetStanceFlags() & 4))
                SetEntry(0, 0x1B71, ROLL_PICK_UP);
            else
                SetEntry(0, 0x1B6E, PlayerTeam()->IsMember(human) ? ROLL_SEAT : ROLL_DISABLED);
            break;
        }
        case ACTOR_EXPLOSIVE_PLACE: {
            auto *place = static_cast<C_ExplosivePlace *>(target);
            switch (place->GetState()) {
            case 0:
                SetEntry(0, 0x1B87,
                         selected->FindItem(place->GetItemId()) ? HUMAN_COMMAND_PLANT_EXPLOSIVE : ROLL_DISABLED);
                SetEntry(1, 0x1B6D, ROLL_DISABLED);
                break;
            case 1:
                SetEntry(0, 0x1B87, ROLL_DISABLED);
                SetEntry(1, 0x1B6D, HUMAN_COMMAND_DISARM_EXPLOSIVE);
                break;
            case 2:
                SetEntry(0, 0x1B87, ROLL_DISABLED);
                SetEntry(1, 0x1B6D, ROLL_DISABLED);
                break;
            default:
                break;
            }
            break;
        }
        case ACTOR_DOCUMENT_PLACE: {
            auto *place = static_cast<C_DocumentPlace *>(target);
            SetEntry(0, place->GetTextId(),
                     selected->FindItem(place->GetItemId()) ? HUMAN_COMMAND_DELIVER_DOCUMENTS : ROLL_DISABLED);
            break;
        }
        case ACTOR_WIRE_CUT:
            SetEntry(0, 0x1B7B, selected->FindBeltItem19() ? HUMAN_COMMAND_CUT_WIRE : ROLL_DISABLED);
            break;
        default:
            break;
        }
    }
    SetEntry(ENTRIES - 1, TEXT_CANCEL, ROLL_CANCEL);
}

void C_RollMenu::BuildCommandMapActions()
{
    for (int i = 0; i < ENTRIES; ++i)
        SetEntry(i, -1, -1);
    C_Human *commanded = PlayerTeam()->GetCommandedMember();
    C_Actor *target = m_target;
    switch (target->GetType()) {
    case ACTOR_MINE:
        SetEntry(0, 0x1B6D, HUMAN_COMMAND_DEFUSE_MINE);
        break;
    case ACTOR_HUMAN: {
        auto *human = static_cast<C_Human *>(target);
        if (human->IsDead()) {
            SetEntry(0, 0x1B71, human->IsInWater() ? ROLL_DISABLED : ROLL_PICK_UP);
            SetEntry(1, 0x1B83, 0x17);
            SetEntry(2, 0x1B84, ROLL_DISABLED);
            SetEntry(3, 0x1B8A, 0x21);
            break;
        }
        if (human->IsUnconscious()) {
            SetEntry(0, 0x1B85, 0x19);
            SetEntry(1, 0x1B83, 0x17);
            SetEntry(2, 0x1B84, human->GetUnknown1A5() ? ROLL_DISABLED : 0x18);
            SetEntry(3, 0x1B8A, 0x21);
            break;
        }
        if (PlayerTeam()->IsMember(human)) {
            if (human != PlayerTeam()->GetCommandedMember()) {
                // Another member: follow, healing.
                SetEntry(0, 0x1B89, 0x20);
                SetEntry(1, 0x1B7A,
                         human->GetHealthPercent() < 100 && commanded->GetMedkit() ? HUMAN_COMMAND_HEAL : ROLL_DISABLED);
                SetEntry(2, 0x1B8A, ROLL_DISABLED);
                break;
            }
            // The commanded soldier himself: put his body down, or leave / change seats.
            if (human->GetCarried()) {
                m_target = human->GetCarried();
                SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
                break;
            }
            C_Actor *vehicle = human->GetVehicle();
            if (vehicle && IsVehicle(vehicle)) {
                m_target = vehicle;
                SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
                for (int seat = 0; seat < m_target->GetSeatCount(); ++seat)
                    SetEntry(seat + 1, TEXT_SEATS + seat, m_target->GetSeatOccupant(seat) ? ROLL_DISABLED : ROLL_SEAT + seat);
            }
            break;
        }
        if (human->GetRole() == HUMAN_ROLE_NPC_TEAM) {
            SetEntry(0, 0x1B89, 0x20);
            break;
        }
        // Others: knock out, capture (with one of the three items), the belt's item 9, tie up.
        SetEntry(0, 0x1B85, 0x19);
        SetEntry(1, 0x1B86,
                 commanded->FindItem(0x16) || commanded->FindItem(7) || commanded->FindItem(0x104) ? 0x1A : ROLL_DISABLED);
        SetEntry(2, 0x1C42, commanded->FindBeltItem(9) ? 0x25 : ROLL_DISABLED);
        SetEntry(3, 0x1B7E, human->IsSurrendered() ? HUMAN_COMMAND_TIE_UP : ROLL_DISABLED);
        break;
    }
    case ACTOR_CAR: {
        auto *car = static_cast<C_Car *>(target);
        if (car->HasEnemyAboard(commanded)) {
            SetEntry(0, 0x1B82, 0x16);
            break;
        }
        if (target == PlayerTeam()->GetCommandedMember()->GetVehicle()) {
            SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
            break;
        }
        // Its seats (a dead occupant's body to pick up), then 0x16 after them.
        int seat = 0;
        for (; seat < target->GetSeatCount(); ++seat) {
            int text = SeatText(target, seat);
            int action = ROLL_SEAT + seat;
            if (C_Actor *occupant = target->GetSeatOccupant(seat)) {
                if (static_cast<C_Human *>(occupant)->IsDead()) {
                    text = 0x1B71;
                    action = ROLL_PICK_UP;
                } else {
                    action = ROLL_DISABLED;
                }
            }
            SetEntry(seat, text, action);
        }
        SetEntry(seat, 0x1B82, 0x16);
        break;
    }
    case ACTOR_DOOR: {
        auto *door = static_cast<C_Door *>(target);
        SetEntry(0, 0x1B6F, HUMAN_COMMAND_OPEN_DOOR);
        SetEntry(1, 0x1B81, HUMAN_COMMAND_CLOSE_DOOR);
        SetEntry(2, 0x1B80, 0x15);
        SetEntry(3, 0x1B7D,
                 !door->GetUnknown118() && (door->GetUnknownFC() != 0 || door->GetUnknown100() != 0) ? 0x12
                                                                                                    : ROLL_DISABLED);
        if (!door->IsLocked()) {
            SetEntry(4, 0x1B6B, commanded->FindItem(door->GetLockKey()) ? HUMAN_COMMAND_LOCK_DOOR : ROLL_DISABLED);
            break;
        }
        SetEntry(4, 0x1B79, door->GetUnknown235() ? HUMAN_COMMAND_PICK_LOCK : ROLL_DISABLED);
        SetEntry(5, 0x1B6C, commanded->FindItem(door->GetLockKey()) ? HUMAN_COMMAND_UNLOCK_DOOR : ROLL_DISABLED);
        break;
    }
    case ACTOR_RADIO: {
        auto *radio = static_cast<C_Radio *>(target);
        const bool usable =
            (radio->GetItemId() < 0 || commanded->FindItem(radio->GetItemId())) && radio->GetState() == 1;
        SetEntry(0, 0x1B6E, usable ? HUMAN_COMMAND_ENTER_VEHICLE : ROLL_DISABLED);
        break;
    }
    case ACTOR_LADDER:
    case ACTOR_SWITCH:
        SetEntry(0, 0x1B6E, HUMAN_COMMAND_ENTER_VEHICLE);
        break;
    case ACTOR_SUPPLY_BOX: {
        auto *box = static_cast<C_SupplyBox *>(target);
        const int maxCarried = box->GetMaxCarried();
        const bool charged = box->GetCharges() != 0;
        if (box->HasItemLists()) {
            const bool allies = commanded->GetTeam(false) != 0;
            const int *items = box->GetItemList(allies);
            for (int i = 0; i < box->GetItemCount(allies); ++i) {
                C_ItemDef *def = GetItemDef(items[i]);
                const bool offered = charged && commanded->CountItems(items[i]) < maxCarried;
                SetEntry(i, def->GetTextId(), offered ? ROLL_PICK_UP : ROLL_DISABLED);
            }
            break;
        }
        C_WeaponItem *weapon = commanded->GetHandsWeapon();
        const bool offered = charged && weapon && weapon->GetDef()->GetAmmoItemId() > 0 &&
                             commanded->CountAmmo(weapon->GetDef()->GetAmmoItemId(), false) < maxCarried;
        SetEntry(0, 0x1B8D, offered ? ROLL_PICK_UP : ROLL_DISABLED);
        break;
    }
    case ACTOR_ITEM:
        SetEntry(0, 0x1B71, ROLL_PICK_UP);
        break;
    case ACTOR_PHOTO_PLACE:
        SetEntry(0, 0x1B6A, HUMAN_COMMAND_ENTER_VEHICLE);
        break;
    case ACTOR_SUBMARINE:
        if (target == PlayerTeam()->GetCommandedMember()->GetVehicle()) {
            SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
            break;
        }
        for (int seat = 0; seat < target->GetSeatCount(); ++seat)
            SetEntry(seat, TEXT_SEATS + seat, target->GetSeatOccupant(seat) ? ROLL_DISABLED : ROLL_SEAT + seat);
        break;
    case ACTOR_STATIC_WEAPON: {
        C_Human *member = PlayerTeam()->GetCommandedMember();
        if (target == member->GetVehicle()) {
            SetEntry(0, 0x1B70, HUMAN_COMMAND_LEAVE_VEHICLE);
            break;
        }
        // Man it, pick up its dead gunner, knock out a hostile one.
        auto *gunner = static_cast<C_Human *>(target->GetSeatOccupant(0));
        if (!gunner)
            SetEntry(0, 0x1B6E, ROLL_SEAT);
        else if (gunner->IsDead())
            SetEntry(0, 0x1B71, ROLL_PICK_UP);
        else if (gunner->IsHostileTo(commanded))
            SetEntry(0, 0x1B85, 0x19);
        else
            SetEntry(0, 0x1B6E, ROLL_DISABLED);
        break;
    }
    case ACTOR_EXPLOSIVE_PLACE: {
        auto *place = static_cast<C_ExplosivePlace *>(target);
        switch (place->GetState()) {
        case 0:
            SetEntry(0, 0x1B87, commanded->FindItem(place->GetItemId()) ? HUMAN_COMMAND_PLANT_EXPLOSIVE : ROLL_DISABLED);
            SetEntry(1, 0x1B6D, ROLL_DISABLED);
            break;
        case 1:
            SetEntry(0, 0x1B87, ROLL_DISABLED);
            SetEntry(1, 0x1B6D, HUMAN_COMMAND_DISARM_EXPLOSIVE);
            break;
        case 2:
            SetEntry(0, 0x1B87, ROLL_DISABLED);
            SetEntry(1, 0x1B6D, ROLL_DISABLED);
            break;
        default:
            break;
        }
        break;
    }
    case ACTOR_DOCUMENT_PLACE: {
        auto *place = static_cast<C_DocumentPlace *>(target);
        SetEntry(0, place->GetTextId(),
                 commanded->FindItem(place->GetItemId()) ? HUMAN_COMMAND_DELIVER_DOCUMENTS : ROLL_DISABLED);
        break;
    }
    case ACTOR_ANIMATION:
        if (!static_cast<C_AnimationActor *>(target)->IsStopped())
            SetEntry(0, 0x1B82, 0x16);
        break;
    case ACTOR_MELEE_TRACKER:
        SetEntry(0, 0x1B85, 0x19);
        SetEntry(1, 0x1B86, 0x1A);
        break;
    case ACTOR_WIRE_CUT:
        SetEntry(0, 0x1B7B, commanded->FindBeltItem19() ? HUMAN_COMMAND_CUT_WIRE : ROLL_DISABLED);
        break;
    default:
        break;
    }
    SetEntry(ENTRIES - 1, TEXT_CANCEL, ROLL_CANCEL);
}

// ---------------------------------------------------------------------------------------------
// Showing it

void C_RollMenu::Open(bool open)
{
    if (!open) {
        m_frame->SetOn(false);
        m_open = false;
        return;
    }
    if (m_commandMap ? !HasCommandMapActions() : !HasActions(m_target, m_distance)) {
        m_open = false;
        return;
    }
    m_open = true;
    BuildEntries();
    Refresh();
    m_openTime = OPEN_TIME;
    m_frame->SetOn(true);
}

void C_RollMenu::Refresh()
{
    m_text->Clear();
    m_text->AddTextId(TEXT_TITLE, STYLE_TITLE);
    // The target's name (a player's short name; a locked door says so).
    if (C_Actor *target = m_target) {
        if (target->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(target)->GetRole() == HUMAN_ROLE_PLAYER) {
            m_text->AddText(ShortName(static_cast<C_Human *>(target)), STYLE_TARGET);
        } else if (target->GetType() == ACTOR_DOOR && static_cast<C_Door *>(target)->IsLocked()) {
            const C_str locked = g_texts.Get(TEXT_LOCKED);
            m_text->AddText(C_fstr("%s - %s", target->GetName()->c_str(), locked.c_str()), STYLE_TARGET);
        } else {
            m_text->AddText(*target->GetName(), STYLE_TARGET);
        }
    }
    // The entries: "1. " .. "9. ", then "0. " and the last.
    for (int i = 0; i < ENTRIES - 1; ++i) {
        const S_Entry &entry = m_entries[i];
        if (entry.action == -1) {
            m_text->AddText(C_fstr(""), STYLE_NUMBER);
            continue;
        }
        m_text->AddText(C_fstr("%d. ", i + 1), STYLE_NUMBER);
        const uint8_t style =
            entry.action == ROLL_DISABLED ? STYLE_DISABLED : (m_selected == i ? STYLE_SELECTED : STYLE_ENTRY);
        if (entry.textId < 0) {
            // A human's name (none if he is gone).
            if (auto *human = static_cast<C_Human *>(g_actorsModule.GetActorById(-10 - entry.textId)))
                m_text->AddText(ShortName(human), style);
            continue;
        }
        if (entry.action >= ROLL_SEAT && entry.action < ROLL_SEAT + 6) {
            // A seat: its occupant's name (unless chosen).
            auto *occupant = static_cast<C_Human *>(m_target->GetSeatOccupant(entry.action - ROLL_SEAT));
            if (m_selected != i && occupant) {
                m_text->AddText(ShortName(occupant), STYLE_ENTRY);
                continue;
            }
        }
        m_text->AddTextId(entry.textId, style);
    }
    m_text->AddText(C_fstr("0. "), STYLE_NUMBER);
    m_text->AddTextId(m_entries[ENTRIES - 1].textId, m_selected == ENTRIES - 1 ? STYLE_SELECTED : STYLE_ENTRY);
}

void C_RollMenu::Render()
{
    if (m_frame->IsOn())
        m_text->Render();
}

void C_RollMenu::Update(int timeDelta)
{
    if (m_commandMap)
        return;
    C_Human *selected = PlayerTeam()->GetSelectedMember();
    if (!selected || selected->IsDead())
        Open(false);
    if (m_openTime != 0)
        m_openTime -= timeDelta;
}

void C_RollMenu::Relayout()
{
    m_text->AttachTo(m_scene, m_frame);
}

void C_RollMenu::Prepare()
{
    m_frame->SetPosInline(S_vector(m_home.x, m_home.y, m_home.z));
    m_frame->Update();
    Relayout();
}

void C_RollMenu::MoveTo(const S_vector &pos)
{
    m_frame->SetPos(pos);
    m_frame->Update();
    Relayout();
}

// ---------------------------------------------------------------------------------------------
// Choosing

void C_RollMenu::HandleInput(float *held, float *pressed)
{
    if (m_commandMap)
        return;
    // The number keys choose the entries (0: the last) and act at once.
    const int key = GetIGraph()->GetAsciiKey();
    const int digit = static_cast<int>(GetIGraph()->KeyToChar(key, 0) & 0xFF) - '0';
    if (digit >= 0 && digit <= 9) {
        const int index = digit == 0 ? ENTRIES - 1 : digit - 1;
        g_controls.ClearKey(key, held, pressed);
        if (!IsEnabled(m_entries[index]))
            return;
        m_selected = index;
        pressed[INPUT_ROLL_MENU] = 1.0f;
    }
    // The right button acts; the wheel and the movement keys move.
    if (pressed[INPUT_ROLL_MENU] != 0.0f) {
        Activate();
        return;
    }
    if (pressed[INPUT_WHEEL] > 0.0f || pressed[INPUT_MOVE_FORWARD] != 0.0f) {
        g_controls.ClearAction(INPUT_WHEEL, held, pressed);
        SelectPrevious();
    } else if (pressed[INPUT_WHEEL] < 0.0f || pressed[INPUT_MOVE_BACK] != 0.0f) {
        g_controls.ClearAction(INPUT_WHEEL, held, pressed);
        SelectNext();
    }
}

void C_RollMenu::SelectPrevious()
{
    int index = m_selected;
    if (index != 0) {
        do {
            --index;
            if (index >= 0 && IsEnabled(m_entries[index])) {
                m_selected = index;
                Refresh();
                break;
            }
        } while (index > 0);
    }
    g_clickSound->SetOn(true);
}

void C_RollMenu::SelectNext()
{
    int index = m_selected;
    if (index < ENTRIES - 1) {
        do {
            ++index;
            if (IsEnabled(m_entries[index])) {
                m_selected = index;
                Refresh();
                break;
            }
        } while (index < ENTRIES - 1);
    }
    g_clickSound->SetOn(true);
}

void C_RollMenu::Activate()
{
    if (m_selected != ENTRIES - 1) {
        C_Human *selected = PlayerTeam()->GetSelectedMember();
        const int index = m_selected;
        // (Without a chosen entry the original read the field before the entries: no action.)
        const int action = index >= 0 ? m_entries[index].action : -1;
        switch (action) {
        case HUMAN_COMMAND_LOCK_DOOR:
        case HUMAN_COMMAND_UNLOCK_DOOR:
        case HUMAN_COMMAND_ENTER_VEHICLE:
        case HUMAN_COMMAND_PLANT_EXPLOSIVE:
        case HUMAN_COMMAND_PICK_LOCK:
        case HUMAN_COMMAND_HEAL:
        case HUMAN_COMMAND_CUT_WIRE:
        case HUMAN_COMMAND_TIE_UP:
        case HUMAN_COMMAND_OPEN_DOOR:
        case HUMAN_COMMAND_CLOSE_DOOR:
        case HUMAN_COMMAND_DELIVER_DOCUMENTS:
            selected->IssueCommand(Command(action, m_target->GetNetId(), 0));
            break;
        case HUMAN_COMMAND_DEFUSE_MINE:
        case HUMAN_COMMAND_DISARM_EXPLOSIVE:
            // (The second parameter: the actor's type.)
            selected->IssueCommand(Command(action, m_target->GetNetId(), m_target->GetType()));
            break;
        case HUMAN_COMMAND_LEAVE_VEHICLE:
            selected->IssueCommand(Command(HUMAN_COMMAND_LEAVE_VEHICLE, -1, 0));
            break;
        case ROLL_PICK_UP:
            switch (m_target->GetType()) {
            case ACTOR_HUMAN:
                selected->IssueCommand(Command(HUMAN_COMMAND_PICK_UP_BODY, m_target->GetNetId(), 0));
                break;
            case ACTOR_CAR:
            case ACTOR_SUBMARINE:
            case ACTOR_STATIC_WEAPON:
                // The body in seat `index`.
                if (C_Actor *body = m_target->GetSeatOccupant(index))
                    selected->IssueCommand(Command(HUMAN_COMMAND_PICK_UP_BODY, body->GetNetId(), 0));
                break;
            case ACTOR_SUPPLY_BOX:
                selected->RequestTakeFromBox(m_target, index);
                break;
            case ACTOR_ITEM:
                selected->RequestPickUpItem(m_target);
                break;
            default:
                break;
            }
            break;
        case ROLL_INVENTORY:
            m_ui->ShowInventory(true, m_target);
            break;
        case ROLL_SEAT:
        case ROLL_SEAT + 1:
        case ROLL_SEAT + 2:
        case ROLL_SEAT + 3:
        case ROLL_SEAT + 4:
        case ROLL_SEAT + 5:
            if (!m_target && selected->GetVehicle() && IsVehicle(selected->GetVehicle()))
                m_target = selected->GetVehicle();
            selected->IssueCommand(Command(HUMAN_COMMAND_ENTER_VEHICLE, m_target->GetNetId(), action - ROLL_SEAT));
            break;
        case ROLL_ASK_HEAL: {
            // The member (the target) heals the soldier.
            S_ActionCommand command;
            command.category = 3;
            command.dir = {0.0f, 0.0f, 0.0f, 0.0f};
            command.action = SQUAD_COMMAND_ON_ACTOR;
            command.command = HUMAN_COMMAND_HEAL;
            command.actor = selected;
            command.value = -1;
            command.member = static_cast<C_Human *>(m_target);
            selected->PlayerSendActionCommand(command);
            break;
        }
        default:
            break;
        }
    }
    m_target = nullptr;
    Open(false);
}

// ---------------------------------------------------------------------------------------------
// The command map's

bool C_RollMenu::IsCursorOver() const
{
    const S_vector &cursor = m_mapCursor->GetWorldPos();
    const I3D_bbox &bound = static_cast<I3D_visual *>(m_frame)->GetWorldBound();
    return bound.min.x < cursor.x && cursor.x < bound.max.x && cursor.y < bound.max.y && bound.min.y < cursor.y;
}

bool C_RollMenu::Select(int index)
{
    // Fix: an index past the entries is not one (the original read past them).
    if (index < 0 || index >= ENTRIES || !IsEnabled(m_entries[index]))
        return false;
    if (m_selected != index) {
        m_selected = index;
        Refresh();
    }
    return true;
}

void C_RollMenu::SelectAt(const float *pos)
{
    const I3D_bbox &bound = static_cast<I3D_visual *>(m_frame)->GetWorldBound();
    if (!(bound.min.x <= pos[0] && pos[0] <= bound.max.x && pos[1] <= bound.max.y && bound.min.y <= pos[1]))
        return;
    // The line under the point, below the title.
    const int index = static_cast<int>((-pos[1] - -bound.max.y - TITLE_HEIGHT) * LINES_PER_UNIT);
    // Fix: a line past the entries (the bottom of the menu's box) chooses none (the original read
    // past them).
    if (index < 0 || index >= ENTRIES || !IsEnabled(m_entries[index])) {
        m_selected = -1;
        return;
    }
    if (m_selected != index) {
        m_selected = index;
        Refresh();
    }
}
