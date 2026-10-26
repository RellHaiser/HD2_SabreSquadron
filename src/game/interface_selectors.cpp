// The interface's pickers (see interface.h; the original's 0x004ab230..0x004ad250): the command
// map's queue of orders, the cursor of the maps and the inventory, and the weapon selector.
#include "game/interface.h"
#include "game/interface_internal.h"

#include <cstdint>
#include <cstdio>

#include "common/c_str.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/squad_order.h"
#include "game/team.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// The weapon selector's amounts (the menus' style table).
constexpr uint8_t STYLE_WEAPON_AMOUNT = 0x4B;

// The state icons (the interface's table) the queue shows for the orders.
enum : int {
    STATE_ICON_MOVE = 10,
    STATE_ICON_WATCH = 11,
    STATE_ICON_WAIT = 13,
    STATE_ICON_ORDER_7 = 14,
    STATE_ICON_FOLLOW = 19,
};

// The cursor's shapes (C_MapCursor::SetMode; 0x0083aed0).
const char *const CURSOR_SHAPES[] = {
    "kurzor", "k_move", "k_rotate", "t_rotleft", "t_rotright", "t_rotup", "t_rotdown", "t_left",
    "t_right", "t_forward", "t_backward", "t_up", "t_down", "k_movepos", "k_move", "k_move",
};

// The screen the cursor stays in (the interface's coordinates).
constexpr float CURSOR_MAX_X = 0.995f;
constexpr float CURSOR_MIN_Y = -0.745f;
// How far inside a clip rectangle it stays.
constexpr float CLIP_MARGIN = 0.002f;

// The weapon selector's keys repeat every 200 ms while held, and the list closes after 2 s.
constexpr int REPEAT_TIME = 200;
constexpr int IDLE_TIME = 2000;

C_Team *PlayerTeam()
{
    return g_teamManager.GetPlayerTeam();
}

// A position kept inside [min + margin, max - margin].
float ClipToRange(float value, float min, float max)
{
    if (value < min + CLIP_MARGIN)
        return min + CLIP_MARGIN;
    if (max - CLIP_MARGIN < value)
        return max - CLIP_MARGIN;
    return value;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// C_InfoSelector

bool C_InfoSelector::Init(I3D_scene *scene, I3D_texture **stateIcons, I3D_texture **actionIcons)
{
    for (int i = 0; i < ICONS; ++i) {
        char name[64];
        std::sprintf(name, "info%02d", i);
        m_icons[i] = scene->FindFrame(name, FRAME_MASK_ANY);
        // Its own material (the icon's texture changes).
        static_cast<I3D_visual *>(m_icons[i])->GetVisualMesh()->GetLevel(0)->FaceGroups()[0].DuplicateMaterial();
        m_icons[i]->SetOn(false);
    }
    m_human = nullptr;
    m_stateIcons = stateIcons;
    m_actionIcons = actionIcons;
    m_marker = scene->FindFrame("infosel", FRAME_MASK_ANY);
    m_marker->SetOn(false);
    return true;
}

void C_InfoSelector::Update(C_Human *human)
{
    if (m_human == human && !human->HaveOrdersChanged())
        return;
    m_human = human;
    m_marker->SetOn(false);
    // Quirk kept: an order of another kind (5) shows the previous order's icon. Fix: none for the
    // first one (the original's was not set).
    I3D_texture *icon = nullptr;
    int i = 0;
    // Fix: at most ICONS orders shown (the original went on past its icons).
    for (; i < human->GetOrderCount() && i < ICONS; ++i) {
        S_HumanOrder *order = human->GetOrder(i);
        switch (order->kind) {
        case HUMAN_ORDER_MOVE:
            icon = m_stateIcons[STATE_ICON_MOVE];
            break;
        case HUMAN_ORDER_WATCH:
            icon = m_stateIcons[STATE_ICON_WATCH];
            break;
        case HUMAN_ORDER_ACT:
            icon = m_actionIcons[order->arg];
            break;
        case HUMAN_ORDER_FOLLOW:
            icon = m_stateIcons[STATE_ICON_FOLLOW];
            break;
        case HUMAN_ORDER_6:
            icon = m_stateIcons[STATE_ICON_WAIT];
            break;
        case HUMAN_ORDER_7:
            icon = m_stateIcons[STATE_ICON_ORDER_7];
            break;
        default:
            break;
        }
        I3D_frame *frame = m_icons[i];
        if (!order->selected) {
            frame->SetScale(S_vector(1.0f, 1.0f, 1.0f));
        } else {
            // The chosen order larger, the marker in front of it.
            frame->SetScale(S_vector(1.1f, 1.1f, 1.1f));
            S_vector pos = frame->GetWorldPos();
            pos.z = pos.z - 1.0f;
            m_marker->SetScale(S_vector(1.1f, 1.1f, 1.1f));
            m_marker->SetPos(pos);
            m_marker->SetOn(true);
            m_marker->Update();
        }
        if (icon)
            FirstMaterial(frame)->SetTexture(icon);
        frame->SetOn(true);
        frame->Update();
    }
    for (; i < ICONS; ++i)
        m_icons[i]->SetOn(false);
}

void C_InfoSelector::MarkIcon(int index)
{
    // Fix: no icon past the last one (the original read past them).
    if (index >= ICONS)
        return;
    S_vector pos = m_icons[index]->GetWorldPos();
    pos.z = pos.z - 1.0f;
    m_marker->SetPos(pos);
    m_marker->SetOn(true);
    m_marker->Update();
}

void C_InfoSelector::SelectOrder(S_HumanOrder *order)
{
    if (m_human != order->human)
        Update(order->human);
    m_marker->SetOn(false);
    // (The icons keep their sizes until the next change of the queue.)
    for (int i = 0; i < m_human->GetOrderCount(); ++i) {
        S_HumanOrder *other = m_human->GetOrder(i);
        if (other == order) {
            other->selected = true;
            MarkIcon(i);
        } else {
            other->selected = false;
        }
    }
}

void C_InfoSelector::SelectOrderIndex(int index)
{
    m_marker->SetOn(false);
    for (int i = 0; i < m_human->GetOrderCount(); ++i) {
        S_HumanOrder *order = m_human->GetOrder(i);
        if (i == index) {
            order->selected = true;
            MarkIcon(i);
        } else {
            order->selected = false;
        }
    }
}

S_HumanOrder *C_InfoSelector::GetSelectedOrder() const
{
    if (!m_human)
        return nullptr;
    for (int i = 0; i < m_human->GetOrderCount(); ++i) {
        S_HumanOrder *order = m_human->GetOrder(i);
        if (order->selected)
            return order;
    }
    return nullptr;
}

int C_InfoSelector::GetSelectedIndex() const
{
    if (!m_human)
        return -1;
    for (int i = 0; i < m_human->GetOrderCount(); ++i) {
        if (m_human->GetOrder(i)->selected)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------
// C_MapCursor

bool C_MapCursor::Init(I3D_scene *ui, I3D_scene *gameScene)
{
    m_scene = ui;
    m_gameScene = gameScene;
    for (int i = 0; i < MODES; ++i) {
        m_shapes[i] = m_scene->FindFrame(CURSOR_SHAPES[i], FRAME_MASK_VISUAL);
        m_shapes[i]->SetOn(false);
    }
    m_scale = 1.0f;
    m_targetScale = 1.0f;
    m_pos.x = 0.5f;
    m_pos.y = -0.375f;
    m_pos.z = -10.0f;
    m_frame = nullptr;
    m_mode = -1;
    m_clip = nullptr;
    m_prevPos = m_pos;
    return true;
}

void C_MapCursor::SetMode(int mode)
{
    if (mode == m_mode)
        return;
    if (m_frame)
        m_frame->SetOn(false);
    m_mode = mode;
    if (mode == -1)
        return;
    m_frame = m_shapes[mode];
    m_frame->SetPosInline(S_vector(m_pos.x, m_pos.y, m_pos.z));
    m_frame->SetOn(true);
    m_frame->Update();
}

void C_MapCursor::HandleInput(float *held, float * /*pressed*/)
{
    // (Mode 2, the rotation, holds it.)
    if (!m_frame || !m_frame->IsOn() || m_mode == 2)
        return;
    m_prevPos = m_pos;
    m_pos.x = held[INPUT_CURSOR_X] + m_pos.x;
    m_pos.y = m_pos.y - held[INPUT_CURSOR_Y];
    if (!m_clip) {
        // At the screen's edges the motion stops.
        if (m_pos.x < 0.0f) {
            m_pos.x = 0.0f;
            held[INPUT_CURSOR_X] = 0.0f;
        }
        if (0.0f < m_pos.y) {
            m_pos.y = 0.0f;
            held[INPUT_CURSOR_Y] = 0.0f;
        }
        if (CURSOR_MAX_X < m_pos.x) {
            m_pos.x = CURSOR_MAX_X;
            held[INPUT_CURSOR_X] = 0.0f;
        }
        if (m_pos.y < CURSOR_MIN_Y) {
            held[INPUT_CURSOR_Y] = 0.0f;
            m_pos.y = CURSOR_MIN_Y;
        }
    } else {
        m_pos.x = ClipToRange(m_pos.x, m_clip[0], m_clip[4]);
        m_pos.y = ClipToRange(m_pos.y, m_clip[1], m_clip[5]);
    }
    m_frame->SetPosInline(S_vector(m_pos.x, m_pos.y, m_pos.z));
}

int C_MapCursor::GetHoveredId()
{
    // A ray into the interface scene from just in front of the cursor.
    const S_vector4 from = {m_pos.x, m_pos.y, m_pos.z + 0.5f, m_pos.w};
    const S_vector dir(0.0f, 0.0f, 1.0f);
    I3D_frame *frame = m_scene->PickFrame(from, dir, 0x4040000, 0, 0, 0, 0, 0, 0);
    if (!frame)
        return 0;
    // (The interface's visuals carry their id as the frame data.)
    return static_cast<int>(reinterpret_cast<intptr_t>(frame->GetData()));
}

void C_MapCursor::Update(int timeDelta)
{
    if (m_scale == m_targetScale)
        return;
    // 5 per second.
    if (m_targetScale <= m_scale) {
        m_scale = m_scale - static_cast<float>(timeDelta * 5) * 0.001f;
        if (!(m_targetScale <= m_scale))
            m_scale = m_targetScale;
    } else {
        m_scale = static_cast<float>(timeDelta * 5) * 0.001f + m_scale;
        if (!(m_scale <= m_targetScale))
            m_scale = m_targetScale;
    }
    m_frame->SetScale(S_vector(m_scale, m_scale, m_scale));
}

void C_MapCursor::GetMove(float *move) const
{
    move[0] = m_pos.x - m_prevPos.x;
    move[1] = m_pos.y - m_prevPos.y;
    move[2] = m_pos.z - m_prevPos.z;
}

void C_MapCursor::GetScreenPos(int *x, int *y) const
{
    const float width = static_cast<float>(GetIGraph()->ScrnX());
    const int height = GetIGraph()->ScrnY();
    *x = static_cast<int>(m_pos.x * width);
    *y = static_cast<int>(static_cast<float>(m_pos.y * -1.3333333333333333) * static_cast<float>(height));
}

bool C_MapCursor::IsInRect(const float *rect) const
{
    return m_pos.x > rect[0] && m_pos.y > rect[1] && m_pos.x < rect[4] && m_pos.y < rect[5];
}

void C_MapCursor::SetFromWorld(const S_vector4 &point)
{
    S_vector4 screen;
    m_gameScene->TransformPoints(&point, &screen, 1);
    const float width = static_cast<float>(GetIGraph()->ScrnX());
    const int height = GetIGraph()->ScrnY();
    const float x = screen.x / width;
    m_pos.x = x;
    const float y = screen.y / static_cast<float>(height) * -0.75f;
    m_pos.y = y;
    // (Without a clip rectangle not kept on the screen.)
    if (m_clip) {
        m_pos.x = ClipToRange(x, m_clip[0], m_clip[4]);
        m_pos.y = ClipToRange(y, m_clip[1], m_clip[5]);
    }
    m_frame->SetPosInline(S_vector(m_pos.x, m_pos.y, m_pos.z));
    m_frame->Update();
}

void C_MapCursor::ClampMotion(float *held, float * /*pressed*/) const
{
    const float x = m_pos.x + held[INPUT_CURSOR_X];
    const float y = m_pos.y - held[INPUT_CURSOR_Y];
    if (!m_clip) {
        if (x < 0.0f)
            held[INPUT_CURSOR_X] = 0.0f;
        if (0.0f < y)
            held[INPUT_CURSOR_Y] = 0.0f;
        if (CURSOR_MAX_X < x)
            held[INPUT_CURSOR_X] = 0.0f;
        if (y < CURSOR_MIN_Y)
            held[INPUT_CURSOR_Y] = 0.0f;
    } else {
        if (x < m_clip[0] + CLIP_MARGIN)
            held[INPUT_CURSOR_X] = 0.0f;
        if (m_clip[4] - CLIP_MARGIN < x)
            held[INPUT_CURSOR_X] = 0.0f;
        if (y < m_clip[1] + CLIP_MARGIN)
            held[INPUT_CURSOR_Y] = 0.0f;
        if (m_clip[5] - CLIP_MARGIN < y)
            held[INPUT_CURSOR_Y] = 0.0f;
    }
}

// ---------------------------------------------------------------------------------------------
// C_WeaponSelector

// An entry of the list: a copy of the template showing the item's picture, a shade over it
// (off on the chosen one) and the amount's text on the shade.
struct C_WeaponSelector::S_Entry {                      // 0xB0 bytes
    // 0x004ac190 (`container`: where the item is, ITEM_CONTAINER_*; ITEM_CONTAINER_COUNT for the
    // empty hands' entry).
    S_Entry(C_Item *item_, int count_, int container_, I3D_frame *entryTemplate, I3D_frame *shadeTemplate);
    ~S_Entry();                                         // 0x004ac370
    S_Entry(const S_Entry &) = delete;
    S_Entry &operator=(const S_Entry &) = delete;

    C_Item *item;                                       // null: the empty hands
    int count;                                          // +0x04 the belt slot's items (not read)
    int container;                                      // +0x08
    I3D_frame *visual = nullptr;                        // +0x0C
    I3D_frame *shade = nullptr;                         // +0x10
    C_TextBox text;                                     // +0x14
};

C_WeaponSelector::S_Entry::S_Entry(C_Item *item_, int count_, int container_, I3D_frame *entryTemplate,
                                   I3D_frame *shadeTemplate)
    : item(item_), count(count_), container(container_)
{
    visual = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
    visual->Duplicate(entryTemplate);
    if (item) {
        I3D_material *material = DuplicateMaterial(visual, "i_frehnd.bmp");
        material->SetTexture(reinterpret_cast<I3D_texture *>(item->GetDef()->GetImage()));
    }
    visual->LinkTo(entryTemplate->GetParent(), 0);
    visual->SetOn(true);
    visual->Update();
    shade = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
    shade->Duplicate(shadeTemplate);
    shade->LinkTo(visual, 0);
    shade->SetOn(true);
    shade->Update();
}

C_WeaponSelector::S_Entry::~S_Entry()
{
    visual->Release();
    shade->Release();
}

bool C_WeaponSelector::Init(I3D_scene *scene)
{
    m_scene = scene;
    m_frame = scene->FindFrame("select_weapon", FRAME_MASK_DUMMY);
    m_marker = scene->FindFrame("selector_weapon", FRAME_MASK_VISUAL);
    m_shadeTemplate = m_frame->FindChildFrame("drkr1", FRAME_MASK_VISUAL);
    m_shadeTemplate->SetOn(false);
    m_entryTemplate = m_frame->FindChildFrame("select_weapon00", FRAME_MASK_VISUAL);
    m_entryTemplate->SetOn(false);
    // The model's other entries are not used.
    scene->FindFrame("tabl03", FRAME_MASK_VISUAL)->SetOn(false);
    scene->FindFrame("drkr2", FRAME_MASK_VISUAL)->SetOn(false);
    m_frame->FindChildFrame("select_weapon01", FRAME_MASK_VISUAL)->SetOn(false);
    m_frame->FindChildFrame("select_weapon02", FRAME_MASK_VISUAL)->SetOn(false);
    m_frame->SetOn(false);
    m_human = nullptr;
    return true;
}

void C_WeaponSelector::Done()
{
    for (size_t i = m_entries.size(); i-- > 0;)
        delete m_entries[i];
    m_entries.clear();
}

void C_WeaponSelector::AddEntry(C_Item *item, int count, int container)
{
    auto *entry = new S_Entry(item, count, container, m_entryTemplate, m_shadeTemplate);
    entry->text.AttachTo(m_scene, entry->shade);
    entry->text.SetUnknown90(0);
    m_entries.push_back(entry);
}

void C_WeaponSelector::Build()
{
    m_selected = 0;
    // The hands' item first (or the empty hands), then the long gun, then the belt's weapons
    // and medkits.
    if (C_Item *item = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem())
        AddEntry(item, 1, ITEM_CONTAINER_HANDS);
    else
        AddEntry(nullptr, 0, ITEM_CONTAINER_COUNT);
    if (C_Item *item = m_human->GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem())
        AddEntry(item, 1, ITEM_CONTAINER_LONG_GUN);
    C_ItemContainer *belt = m_human->GetContainer(ITEM_CONTAINER_BELT);
    std::vector<C_Item *> *slots = belt->GetSlots();
    for (int i = 0; i < belt->GetUsedSlotCount(); ++i) {
        C_Item *item = slots[i].front();
        if (item->GetKind() == ITEM_KIND_WEAPON || item->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT)
            AddEntry(item, static_cast<int>(slots[i].size()), ITEM_CONTAINER_BELT);
    }
    m_marker->SetOn(true);
}

void C_WeaponSelector::Layout()
{
    // Quirk kept: the entries (children of the template's parent) are placed at the template's
    // world position.
    S_vector pos = m_entryTemplate->GetWorldPos();
    for (size_t i = 0; i < m_entries.size(); ++i) {
        S_Entry *entry = m_entries[i];
        entry->visual->SetOn(true);
        entry->visual->SetPos(pos);
        if (static_cast<int>(i) == m_selected) {
            m_marker->LinkTo(entry->visual, 0);
            m_marker->Update();
            entry->visual->SetScale(S_vector(1.1f, 1.1f, 1.1f));
            entry->shade->SetOn(false);
        } else {
            entry->visual->SetScale(S_vector(1.0f, 1.0f, 1.0f));
            entry->shade->SetOn(true);
        }
        entry->visual->Update();
        pos.y = pos.y - 0.045f;
    }
    UpdateAmounts();
}

void C_WeaponSelector::UpdateAmounts()
{
    // (The counts are the active player's; the list is the selected member's.)
    C_Human *human = PlayerTeam()->GetActivePlayer();
    for (S_Entry *entry : m_entries) {
        entry->text.AttachTo(m_scene, entry->shade);
        C_Item *item = entry->item;
        if (!item)
            continue;
        C_ItemDef *def = item->GetDef();
        switch (def->GetBehaviour()) {
        case ITEM_BEHAVIOUR_MELEE:
        case 1:
            break;
        case ITEM_BEHAVIOUR_LAUNCHER: {
            // The rounds loaded / the ammo carried (the backpack's too).
            const int ammo = human->CountAmmo(def->GetAmmoItemId(), true);
            const int rounds = static_cast<int>(item->GetAmount());
            entry->text.SetText(C_fstr("%d/%d", rounds, ammo), STYLE_WEAPON_AMOUNT, 0);
            break;
        }
        case ITEM_BEHAVIOUR_THROWN:
        case ITEM_BEHAVIOUR_MINE:
        case ITEM_BEHAVIOUR_11:
            entry->text.SetText(C_fstr("%d", human->CountItems(item->GetTypeId())), STYLE_WEAPON_AMOUNT, 0);
            break;
        case ITEM_BEHAVIOUR_MEDKIT: {
            // The charges left in per cent / the medkits carried.
            const float capacity = static_cast<C_MedkitComp *>(def->GetComponent())->GetHealingCapacity();
            const float percent = item->GetCharges() / capacity * 100.0f;
            const int count = human->CountItems(item->GetTypeId());
            entry->text.SetText(C_fstr("%.0f/%d", static_cast<double>(percent), count), STYLE_WEAPON_AMOUNT, 0);
            break;
        }
        default: {
            const int ammo = human->CountAmmo(def->GetAmmoItemId(), false);
            const int rounds = static_cast<int>(item->GetAmount());
            entry->text.SetText(C_fstr("%d/%d", rounds, ammo), STYLE_WEAPON_AMOUNT, 0);
            break;
        }
        }
    }
}

void C_WeaponSelector::Show(bool show)
{
    m_idleTime = 0;
    if (show) {
        // The selected soldier's list (not while he holds his breath or carries out an order).
        m_human = PlayerTeam()->GetSelectedMember();
        if (m_human && !m_human->IsPlayerHoldingBreath() && !m_human->HasOrder()) {
            Build();
            Layout();
            m_frame->SetOn(true);
        }
        return;
    }
    m_human = nullptr;
    Done();
    m_frame->SetOn(false);
}

void C_WeaponSelector::HandleInput(float *held, float *pressed)
{
    if (pressed[INPUT_ROLL_MENU] != 0.0f) {
        // The chosen entry taken in hand.
        C_ItemContainer *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS);
        if (hands->IsLocked() || m_human->HasOrder())
            return;
        if (m_entries.size() > 1) {
            S_Entry *entry = m_entries[m_selected];
            C_Item *handsItem = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
            if (!entry->item) {
                // The empty hands: the item put away.
                if (handsItem)
                    m_human->RequestMoveItem(handsItem, m_human, ITEM_CONTAINER_HANDS, ITEM_CONTAINER_COUNT, 0);
            } else if (entry->item->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT) {
                // A medkit is used on himself.
                if (m_human && !m_human->GetCarried()) {
                    S_HumanCommand command;
                    command.category = HUMAN_COMMAND_VEHICLE;
                    command.type = HUMAN_COMMAND_HEAL_SELF;
                    command.param1 = entry->item->GetUid();
                    command.param2 = 0;
                    m_human->IssueCommand(command);
                }
            } else if (entry->item != handsItem) {
                if (!handsItem)
                    m_human->RequestMoveItem(entry->item, m_human, entry->container, ITEM_CONTAINER_HANDS, 0);
                else
                    m_human->RequestTransferItem(handsItem, ITEM_CONTAINER_HANDS, m_human, entry->item, entry->container);
            }
        }
        pressed[INPUT_ROLL_MENU] = 0.0f;
        held[INPUT_ROLL_MENU] = 0.0f;
        Show(false);
    }
    // The keys repeat while held.
    if (held[INPUT_WEAPON_NEXT] == 0.0f) {
        m_nextRepeat = -1;
    } else {
        if (m_nextRepeat == 0) {
            pressed[INPUT_WEAPON_NEXT] = 1.0f;
            m_nextRepeat = REPEAT_TIME;
        }
        m_idleTime = 0;
    }
    if (held[INPUT_WEAPON_PREV] == 0.0f) {
        m_prevRepeat = -1;
    } else {
        if (m_prevRepeat == 0) {
            pressed[INPUT_WEAPON_PREV] = 1.0f;
            m_prevRepeat = REPEAT_TIME;
        }
        m_idleTime = 0;
    }
    if (pressed[INPUT_WEAPON_NEXT] != 0.0f) {
        if (++m_selected == static_cast<int>(m_entries.size()))
            m_selected = 0;
        Layout();
        m_nextRepeat = REPEAT_TIME;
        m_idleTime = 0;
    } else if (pressed[INPUT_WEAPON_PREV] != 0.0f) {
        if (--m_selected < 0)
            m_selected = static_cast<int>(m_entries.size()) - 1;
        Layout();
        m_prevRepeat = REPEAT_TIME;
        m_idleTime = 0;
    }
}

void C_WeaponSelector::Update(int timeDelta)
{
    C_Human *human = m_human;
    if (human == PlayerTeam()->GetSelectedMember() && !human->IsPlayerHoldingBreath() && !m_human->HasOrder() &&
        (m_idleTime += timeDelta) <= IDLE_TIME) {
        if (m_human->IsInventoryChanged()) {
            Done();
            Build();
            Layout();
            return;
        }
        if (m_human->IsAlive()) {
            UpdateAmounts();
            // Quirk kept: one key's repeat time runs at a time.
            if (m_nextRepeat > 0) {
                m_nextRepeat -= timeDelta;
                if (m_nextRepeat < 0)
                    m_nextRepeat = 0;
                return;
            }
            if (m_prevRepeat < 1)
                return;
            m_prevRepeat -= timeDelta;
            if (m_prevRepeat < 0)
                m_prevRepeat = 0;
            return;
        }
    }
    Show(false);
}

void C_WeaponSelector::Render()
{
    if (!m_frame->IsOn())
        return;
    for (S_Entry *entry : m_entries)
        entry->text.Render();
}
