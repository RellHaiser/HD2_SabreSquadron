// The inventory of the interface (see interface.h; the original's 0x0049fef0..0x004a4aa0 and the
// scroll bars' 0x004ada20..0x004ae790): the soldier's slots and lists and a searched body's, items
// dragged between them, double-clicked across or thrown away, their descriptions under the cursor
// and the soldier's skills.
#include "game/interface.h"

#include <cmath>
#include <cstdint>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/interface_internal.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/soldier.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"

namespace {

// The text styles (the menus' style table).
enum : uint8_t {
    STYLE_AMOUNT = 0x06,                                // a slot's or a line's count / rounds
    STYLE_LABEL = 0x0B,                                 // the slots' names, captions, skills
    STYLE_DESCRIPTION_NAME = 0x10,
    STYLE_DESCRIPTION = 0x11,
    STYLE_NAME = 0x1B,                                  // the names and the weights
    STYLE_CAPACITY = 0x1D,
};

enum : int {
    TEXT_SOLDIER_STATS = 0xA6B,
    TEXT_SKILLS = 0xA6E,                                // + the skill
    TEXT_SLOTS = 0xA96,                                 // + the slot
    TEXT_BELT = 0xA9C,
    TEXT_BACKPACK = 0xA9D,
    TEXT_WEIGHT = 0xAAA,
    TEXT_ROUNDS = 0xAAB,
    TEXT_VELOCITY = 0xAAC,
    TEXT_ZOOM = 0xAAE,
    TEXT_CAPACITY = 0x1CF2,
};

// The places' frame data (C_MapCursor::GetHoveredId): + the slot or the line.
enum : int {
    ID_SLOTS = 200,
    ID_SLOTS_RIGHT = 210,
    ID_BELT = 220,
    ID_BELT_RIGHT = 230,
    ID_BACKPACK = 240,
    ID_BACKPACK_RIGHT = 250,
    // The scroll bars: 0xF00 | the bar (1..4) << 4 | the part.
    ID_SCROLL_BARS = 0xF00,
};

// FindChildFrame's type masks: visuals by a name pattern (the 0x10000 bit, inferred), and the
// scroll bars' parts (dummies by a name pattern).
constexpr uint32_t PART_MASK = 0x10001;
constexpr uint32_t BAR_PART_MASK = 0x10020;

constexpr int SLOT_DROP = 2;                            // a place to throw items away
constexpr int SLOT_UNIFORM = 4;

// The ms the cursor rests on an item before its description shows, and the ms after a press
// within which another is a double click.
constexpr int DESCRIPTION_DELAY = 400;
constexpr int DOUBLE_CLICK_TIME = 250;

// The slots' frames: the icon, the name's visual, the selection.
const char *const SLOT_FRAMES[6][3] = {
    {"bck*", "text_back*", "selbck*"},
    {"head*", "text_head*", "selhead*"},
    {"drop*", "text_drop*", "seldrop*"},
    {"hand*", "text_hand*", "selhand*"},
    {"uniform*", "text_uniform*", "seluniform*"},
    {"shoulder*", "text_shoulder*", "selshoulder*"},
};

// The skills (the original's 0x0083a9d0) and their frames: the name's visual, the bar, the value's
// visual.
const int SKILL_STATS[7] = {STAT_SHOOTING, STAT_STRENGTH, STAT_HEALTH, STAT_ENDURANCE, STAT_STEALTH, STAT_FIRST_AID,
                            STAT_LOCKPICK};
const char *const SKILL_FRAMES[7][3] = {
    {"text_shooting", "shcpct00", "text_shooting01"},
    {"text_strenght", "stcpct00", "text_strenght01"},
    {"text_health", "hecpct00", "text_health01"},
    {"text_endurance", "encpct00", "text_endurance01"},
    {"text_stealth", "sicpct00", "text_stealth01"},
    {"text_first aid", "facpct00", "text_first aid01"},
    {"text_lockpick", "lpcpct00", "text_lockpick01"},
};

void SetFrameId(I3D_frame *frame, int id)
{
    frame->SetData(reinterpret_cast<S_FrameData *>(static_cast<intptr_t>(id)));
}

int GetFrameId(I3D_frame *frame)
{
    return static_cast<int>(reinterpret_cast<intptr_t>(frame->GetData()));
}

// The container of a slot (ITEM_CONTAINER_COUNT: the drop place's, none).
int SlotContainer(int slot)                             // 0x004a1db0
{
    switch (slot) {
    case 0:
        return ITEM_CONTAINER_BACKPACK;
    case 1:
        return ITEM_CONTAINER_HEAD;
    case 3:
        return ITEM_CONTAINER_HANDS;
    case 4:
        return ITEM_CONTAINER_UNIFORM;
    case 5:
        return ITEM_CONTAINER_LONG_GUN;
    default:
        return ITEM_CONTAINER_COUNT;
    }
}

// (The item pictures are textures.)
I3D_texture *ItemImage(const C_Item *item)
{
    return reinterpret_cast<I3D_texture *>(item->GetDef()->GetImage());
}

// A bar's length (its z scale).
void SetBar(I3D_frame *bar, float value)
{
    bar->SetScale(S_vector(1.0f, 1.0f, value));
}

// The visual's own copy of its mesh made ready (the inventory's icons).
void PrepareIcon(I3D_frame *icon)
{
    static_cast<I3D_visual *>(icon)->GetVisualMesh()->DuplicateMaterials();
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// The scroll bars

bool C_InventoryScrollBar::S_Button::Init(C_InventoryScrollBar *owner_, I3D_frame *frame_, int id_)
{
    frame = frame_;
    id = id_;
    SetFrameId(frame_, id_);
    owner = owner_;
    normal = frame_->FindChildFrame("normal*", PART_MASK);
    active = frame_->FindChildFrame("actived*", PART_MASK);
    inactive = frame_->FindChildFrame("inactived*", PART_MASK);
    if (!normal || !active || !inactive)
        return false;
    // The other looks sit where the normal one does.
    active->SetPosInline(normal->GetPos());
    inactive->SetPosInline(normal->GetPos());
    SetFrameId(normal, id_);
    SetFrameId(active, id_);
    SetFrameId(inactive, id_);
    Show(true, false, false);
    return true;
}

void C_InventoryScrollBar::S_Button::Show(bool showNormal, bool showActive, bool showInactive)
{
    normal->SetOn(showNormal);
    active->SetOn(showActive);
    inactive->SetOn(showInactive);
    normal->Update();
    active->Update();
    inactive->Update();
}

void C_InventoryScrollBar::S_Button::SetDisabled(bool on)
{
    disabled = on;
    Show(!on, false, on);
}

bool C_InventoryScrollBar::S_Button::Activate()
{
    if (disabled)
        return false;
    Show(false, true, false);
    return true;
}

bool C_InventoryScrollBar::S_Button::Deactivate()
{
    if (disabled)
        Show(false, false, true);
    else
        Show(true, false, false);
    return true;
}

bool C_InventoryScrollBar::S_Button::Press()
{
    if (disabled)
        return false;
    owner->Step(id);
    return true;
}

bool C_InventoryScrollBar::S_Slider::Init(C_InventoryScrollBar *owner_, I3D_frame *frame_, int id_)
{
    frame = frame_;
    id = id_;
    SetFrameId(frame_, id_);
    owner = owner_;
    normal = frame_->FindChildFrame("normal*", PART_MASK);
    active = frame_->FindChildFrame("actived*", PART_MASK);
    base = frame_->FindChildFrame("sliderbase*", PART_MASK);
    if (!normal || !active || !base)
        return false;
    active->SetPosInline(normal->GetPos());
    base->SetOn(false);
    base->Update();
    SetFrameId(normal, id_);
    SetFrameId(active, id_);
    Show(true, false);
    // Fix: the range of a second Init replaces the first's (the original's leaked).
    delete[] rect;
    rect = new float[7]();
    Layout();
    return true;
}

void C_InventoryScrollBar::S_Slider::Layout()
{
    // The base's bound less half the slider's: the range of the slider's centre.
    const I3D_bbox own = normal->GetBound().bbox;
    const float halfWidth = (own.max.x - own.min.x) * 0.5f;
    const float halfHeight = (own.min.y - own.max.y) * 0.5f;
    const I3D_bbox range = base->GetBound().bbox;
    rect[0] = range.min.x + halfWidth;
    rect[2] = range.max.x - halfWidth;
    rect[1] = range.min.y - halfHeight;
    rect[3] = range.max.y + halfHeight;
    // Less the slider's place in its frame (whose local z runs along the screen's y).
    const S_vector offset = normal->GetPos();
    rect[0] = rect[0] - offset.x;
    rect[2] = rect[2] - offset.x;
    rect[1] = rect[1] - offset.z;
    rect[3] = rect[3] - offset.z;
    rect[5] = rect[3] - rect[1];
    rect[4] = rect[2] - rect[0];
    if (rect[5] > rect[4]) {
        horizontal = false;
        vertical = true;
    } else {
        horizontal = true;
        vertical = false;
    }
    SetPosition(50.0f);
}

void C_InventoryScrollBar::S_Slider::Show(bool showNormal, bool showActive)
{
    normal->SetOn(showNormal);
    active->SetOn(showActive);
    normal->Update();
    active->Update();
}

void C_InventoryScrollBar::S_Slider::SetDisabled(bool on)
{
    disabled = on;
    Show(!on, false);
}

bool C_InventoryScrollBar::S_Slider::Activate()
{
    if (!disabled)
        Show(false, true);
    return true;
}

bool C_InventoryScrollBar::S_Slider::Deactivate()
{
    Show(true, false);
    return true;
}

void C_InventoryScrollBar::S_Slider::SetPosition(float value)
{
    S_vector pos = frame->GetWorldPos();
    if (position == value)
        return;
    position = value;
    if (value < 0.0f)
        position = 0.0f;
    if (position > 100.0f)
        position = 100.0f;
    if (vertical) {
        pos.y = rect[3] - rect[5] * position * 0.01f;
        if (pos.y < rect[1])
            pos.y = rect[1];
        if (pos.y > rect[3])
            pos.y = rect[3];
    } else if (horizontal) {
        pos.x = rect[4] * position * 0.01f + rect[0];
        if (pos.x < rect[0])
            pos.x = rect[0];
        if (pos.x > rect[2])
            pos.x = rect[2];
    } else {
        return;
    }
    frame->SetPos(pos);
    frame->Update();
}

bool C_InventoryScrollBar::S_Slider::Follow(const float *cursor, const float *move)
{
    S_vector pos = frame->GetWorldPos();
    if (vertical && move[1] != 0.0f) {
        pos.y = cursor[1] - normal->GetPos().z;
        if (pos.y < rect[1])
            pos.y = rect[1];
        if (pos.y > rect[3])
            pos.y = rect[3];
        frame->SetPos(pos);
        frame->Update();
        return SetPositionAt(pos.y);
    }
    if (horizontal && move[0] != 0.0f) {
        pos.x = cursor[0] - normal->GetPos().x;
        if (pos.x < rect[0])
            pos.x = rect[0];
        if (pos.x > rect[2])
            pos.x = rect[2];
        frame->SetPos(pos);
        frame->Update();
        return SetPositionAt(pos.x);
    }
    return false;
}

bool C_InventoryScrollBar::S_Slider::SetPositionAt(float coord)
{
    float value;
    if (vertical)
        value = (rect[3] - coord) / rect[5];
    else
        value = (coord - rect[0]) / rect[4];
    value = value * 100.0f;
    if (value == position)
        return false;
    position = value;
    return true;
}

bool C_InventoryScrollBar::S_Slider::Drag(const float *cursor, const float *move)
{
    if (Follow(cursor, move))
        owner->OnSliderMoved(position);
    return true;
}

bool C_InventoryScrollBar::Init(C_InventoryScreen *owner, I3D_frame *frame, int id)
{
    m_frame = frame;
    m_id = id;
    SetFrameId(frame, id);
    m_owner = owner;
    if (I3D_frame *up = frame->FindChildFrame("barrow up*", BAR_PART_MASK))
        m_up.Init(this, up, id | 1);
    if (I3D_frame *down = frame->FindChildFrame("barrow down*", BAR_PART_MASK))
        m_down.Init(this, down, id | 2);
    if (I3D_frame *slider = frame->FindChildFrame("slider*", BAR_PART_MASK))
        m_slider.Init(this, slider, id | 3);
    SetDisabled(true);
    m_part = -1;
    m_unknown90 = 1;
    m_count = 0;
    m_index = 0;
    m_step = 0.0f;
    return true;
}

void C_InventoryScrollBar::SetDisabled(bool on)
{
    m_disabled = on;
    m_up.SetDisabled(on);
    m_down.SetDisabled(on);
    m_slider.SetDisabled(on);
}

void C_InventoryScrollBar::Step(int id)
{
    const int part = id & 0xF;
    if (part == 1) {
        if (m_index <= 0)
            return;
        --m_index;
        if (m_index < 0)
            m_index = 0;
    } else if (part == 2) {
        if (m_index >= m_count - 1)
            return;
        ++m_index;
        if (m_index > m_count)
            m_index = m_count;
    } else {
        return;
    }
    m_slider.SetPosition(static_cast<float>(m_index) * m_step);
    m_owner->OnScroll(m_id, m_index);
}

void C_InventoryScrollBar::OnSliderMoved(float position)
{
    // (Compared unsigned, as the original did.)
    uint32_t index = static_cast<uint32_t>(static_cast<int>(std::floor(static_cast<double>(position / m_step + 0.5f))));
    const uint32_t last = static_cast<uint32_t>(m_count - 1);
    if (index > last)
        index = last;
    if (static_cast<int>(index) == m_index)
        return;
    m_index = static_cast<int>(index);
    m_owner->OnScroll(m_id, m_index);
}

void C_InventoryScrollBar::SetCount(int count)
{
    if (count < 2) {
        m_count = 0;
        m_index = 0;
        m_step = 100.0f;
        SetDisabled(true);
    } else {
        if (m_count == 0)
            SetDisabled(false);
        m_count = count;
        m_step = 100.0f / static_cast<float>(count - 1);
        if (count <= m_index)
            m_index = count - 1;
    }
    m_slider.SetPosition(static_cast<float>(m_index) * m_step);
}

bool C_InventoryScrollBar::Unhover(int /*id*/)
{
    if (m_part == 1) {
        if (!m_up.Deactivate())
            return false;
    } else if (m_part == 2) {
        if (!m_down.Deactivate())
            return false;
    }
    if (m_part == 3 && !m_slider.Deactivate())
        return false;
    m_part = -1;
    return true;
}

bool C_InventoryScrollBar::Hover(int id)
{
    const int part = id & 0xF;
    if (!IsPart(part))
        return Unhover(id);
    if (m_part != part && Unhover(id)) {
        // (0x004ae5b0: the new part lit.)
        if (!Unhover(id))
            return false;
        m_part = part;
        if (part == 1)
            return m_up.Activate();
        if (part == 2)
            return m_down.Activate();
        return m_slider.Activate();
    }
    return true;
}

bool C_InventoryScrollBar::Press(int /*id*/)
{
    if (m_disabled)
        return true;
    if (m_part == 1)
        return m_up.Press();
    if (m_part == 2)
        return m_down.Press();
    return false;
}

bool C_InventoryScrollBar::Drag(const S_vector4 &pos, const float *move)
{
    if (m_disabled)
        return true;
    if (m_part == 3)
        return m_slider.Drag(&pos.x, move);
    return false;
}

bool C_InventoryScrollBar::Release(int /*id*/)
{
    if (!m_disabled && m_part == 3)
        m_slider.SetPosition(static_cast<float>(m_index) * m_step);
    return true;
}

// ---------------------------------------------------------------------------------------------
// The inventory

C_Human *C_InventoryScreen::Other() const
{
    return static_cast<C_Human *>(m_other);
}

bool C_InventoryScreen::IsShown() const
{
    return m_frame->IsOn();
}

bool C_InventoryScreen::Init(I3D_scene *scene, C_MapCursor *cursor)
{
    m_cursor = cursor;
    m_scene = scene;
    m_frame = scene->FindFrame("inventory", FRAME_MASK_VISUAL);
    m_pages[0] = scene->FindFrame("items00", FRAME_MASK_DUMMY);
    m_pages[1] = scene->FindFrame("items01", FRAME_MASK_DUMMY);
    m_frame->SetPos(S_vector(0.178f, -0.333f, 0.0f));
    m_frame->SetOn(false);
    m_frame->Update();

    // The slots (on both pages the same frames; the icons' ids 200 / 210 + the slot).
    for (int i = 0; i < SLOTS; ++i) {
        for (int page = 0; page < 2; ++page) {
            S_Slot &slot = m_slots[i][page];
            slot.icon = m_pages[page]->FindChildFrame(SLOT_FRAMES[i][0], PART_MASK);
            slot.material = DuplicateMaterial(slot.icon, "X_SQUARE.BMP");
            slot.amount.AttachTo(m_scene, slot.icon);
            PrepareIcon(slot.icon);
            I3D_frame *label = m_pages[page]->FindChildFrame(SLOT_FRAMES[i][1], PART_MASK);
            label->SetOn(false);
            slot.name.SetUnknown90(0);
            slot.name.AttachTo(m_scene, label);
            slot.name.SetTextId(TEXT_SLOTS + i, STYLE_LABEL, 0);
            slot.selection = m_pages[page]->FindChildFrame(SLOT_FRAMES[i][2], PART_MASK);
            slot.selection->SetOn(false);
            SetFrameId(slot.icon, (page ? ID_SLOTS_RIGHT : ID_SLOTS) + i);
        }
    }

    // The lists: the belts (captions "hlstrscaptab*", bars "hlcpct00" / "hlcpct02") and the
    // backpacks ("bkpckcaptab*", "hlcpct01" / "hlcpct03").
    static const char *const BELT_ITEMS[S_List::LINES] = {"item00*", "item01*", "item02*", "item03*"};
    static const char *const BACKPACK_ITEMS[S_List::LINES] = {"item04*", "item05*", "item06*", "item07*"};
    struct S_ListFrames {
        const char *caption;
        const char *capacity;
        const char *const *items;
        const char *bar;
        uint32_t barMask;
        const char *selection;
        int textId;
    };
    const S_ListFrames lists[4] = {
        {"hlstrscaptab*", "text_hlstrscap*", BELT_ITEMS, "hlcpct00", PART_MASK, "selholsters*", TEXT_BELT},
        {"hlstrscaptab*", "text_hlstrscap*", BELT_ITEMS, "hlcpct02", FRAME_MASK_VISUAL, "selholsters*", TEXT_BELT},
        {"bkpckcaptab*", "text_bkpckcap*", BACKPACK_ITEMS, "hlcpct01", FRAME_MASK_VISUAL, "selbackpack*", TEXT_BACKPACK},
        {"bkpckcaptab*", "text_bkpckcap*", BACKPACK_ITEMS, "hlcpct03", FRAME_MASK_VISUAL, "selbackpack*", TEXT_BACKPACK},
    };
    S_List *const listOf[4] = {&m_belts[0], &m_belts[1], &m_backpacks[0], &m_backpacks[1]};
    for (int n = 0; n < 4; ++n) {
        const S_ListFrames &names = lists[n];
        S_List &list = *listOf[n];
        I3D_frame *page = m_pages[n & 1];
        I3D_frame *caption = page->FindChildFrame(names.caption, PART_MASK);
        list.caption.SetUnknown90(0);
        list.caption.AttachTo(m_scene, caption);
        list.caption.SetTextId(names.textId, STYLE_LABEL, 0);
        caption->SetOn(false);
        I3D_frame *capacity = page->FindChildFrame(names.capacity, PART_MASK);
        list.capacity.SetUnknown90(0);
        list.capacity.AttachTo(m_scene, capacity);
        capacity->SetOn(false);
        for (int j = 0; j < S_List::LINES; ++j)
            list.icons[j] = page->FindChildFrame(names.items[j], PART_MASK);
        list.capacityBar = page->FindChildFrame(names.bar, names.barMask);
        list.selection = page->FindChildFrame(names.selection, PART_MASK);
        list.selection->SetOn(false);
    }
    // The lines' icons (ids 220 / 230 / 240 / 250 + the line).
    for (int j = 0; j < S_List::LINES; ++j) {
        const int ids[4] = {ID_BELT + j, ID_BELT_RIGHT + j, ID_BACKPACK + j, ID_BACKPACK_RIGHT + j};
        for (int n = 0; n < 4; ++n) {
            S_List &list = *listOf[n];
            PrepareIcon(list.icons[j]);
            list.amounts[j].AttachTo(m_scene, list.icons[j]);
            list.materials[j] = DuplicateMaterial(list.icons[j], "X_SQUARE.BMP");
        }
        for (int n = 0; n < 4; ++n)
            SetFrameId(listOf[n]->icons[j], ids[n]);
    }
    m_emptyTexture = FindTexture(m_slots[0][0].icon, "X_SQUARE.BMP");

    m_weightBars[0] = m_pages[0]->FindChildFrame("allcpct00", FRAME_MASK_VISUAL);
    m_weightBars[1] = m_pages[1]->FindChildFrame("allcpct01", FRAME_MASK_VISUAL);
    m_names[0].AttachTo(m_scene, m_pages[0]->FindChildFrame("namexx00", PART_MASK));
    m_names[1].AttachTo(m_scene, m_pages[1]->FindChildFrame("namexx01", PART_MASK));
    for (int page = 0; page < 2; ++page) {
        I3D_frame *weight = m_pages[page]->FindChildFrame(page ? "text_allcpct01" : "text_allcpct00", PART_MASK);
        m_weights[page].AttachTo(m_scene, weight);
        weight->SetOn(false);
    }
    m_description = scene->FindFrame("description", FRAME_MASK_VISUAL);
    m_description->SetOn(false);

    // The dragged icon: a copy of the first slot's icon facing the same way, in the scene's
    // sector.
    m_dragIcon = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
    m_dragIcon->Duplicate(m_slots[0][0].icon);
    m_dragIcon->SetDir(m_slots[0][0].icon->GetWorldDir(), 0.0f);
    static_cast<I3D_visual *>(m_dragIcon)->VisualFlags() |= VISFLAGS_1000;
    m_dragIcon->SetName("DraggingFrame");
    static_cast<I3D_object_visual *>(m_dragIcon)->DuplicateMesh();
    PrepareIcon(m_dragIcon);
    m_dragIcon->LinkTo(m_scene->GetPrimarySector(), 0);
    m_dragIcon->SetOn(false);
    m_dragIcon->Update();
    m_dragMaterial = DuplicateMaterial(m_dragIcon, "X_SQUARE.BMP");
    m_scene->AddFrame(m_dragIcon);
    m_dragIcon->Release();
    m_dragItem = nullptr;
    m_pressed = false;
    m_doubleClickTime = 0;

    // The skills.
    m_skills = scene->FindFrame("skills", FRAME_MASK_DUMMY);
    I3D_frame *title = m_frame->FindChildFrame("text_soldier stats", FRAME_MASK_VISUAL);
    m_skillsTitle.AttachTo(m_scene, title);
    m_skillsTitle.SetTextId(TEXT_SOLDIER_STATS, STYLE_LABEL, 0);
    title->SetOn(false);
    for (int i = 0; i < SKILLS; ++i) {
        I3D_frame *name = m_frame->FindChildFrame(SKILL_FRAMES[i][0], FRAME_MASK_VISUAL);
        m_skillNames[i].SetUnknown90(0);
        m_skillNames[i].AttachTo(m_scene, name);
        m_skillNames[i].SetTextId(TEXT_SKILLS + i, STYLE_LABEL, 0);
        name->SetOn(false);
        m_skillBars[i] = m_frame->FindChildFrame(SKILL_FRAMES[i][1], FRAME_MASK_VISUAL);
        I3D_frame *value = m_frame->FindChildFrame(SKILL_FRAMES[i][2], FRAME_MASK_VISUAL);
        m_skillValues[i].SetUnknown90(0);
        m_skillValues[i].AttachTo(m_scene, value);
        value->SetOn(false);
    }

    // The scroll bars: the left belt, the left backpack, the right belt, the right backpack.
    static const char *const BARS[SCROLL_BARS] = {"scroll13", "scroll14", "scroll15", "scroll16"};
    bool done = false;
    for (int i = 0; i < SCROLL_BARS; ++i) {
        I3D_frame *bar = m_frame->FindChildFrame(BARS[i], FRAME_MASK_VISUAL);
        // (The result is the last bar's: false without it.)
        done = bar && m_scrollBars[i].Init(this, bar, ID_SCROLL_BARS | (i + 1) << 4);
    }
    m_activeBar = 0;
    return done;
}

void C_InventoryScreen::Open(int mode, C_Human *human, C_Actor *other)
{
    m_mode = mode | MODE_REFRESH;
    m_human = human;
    m_other = other;
    if (human) {
        const E_HumanRole role = human->GetRole();
        if (role == HUMAN_ROLE_NPC_TEAM || role == HUMAN_ROLE_NPC_TEAM_5 || role == HUMAN_ROLE_NEUTRAL) {
            m_names[0].SetText(C_fstr("%s", human->GetFullName()), STYLE_NAME, 0);
        } else {
            const C_str rank = g_texts.Get(g_rankNames[human->GetProfileUnknown24()].textId);
            m_names[0].SetText(C_fstr("%s %s", rank.c_str(), m_human->GetFullName()), STYLE_NAME, 0);
        }
    }
    if (!m_other || m_other->GetType() != ACTOR_HUMAN) {
        m_names[1].Clear();
        return;
    }
    C_Human *body = Other();
    const E_HumanRole role = body->GetRole();
    if (role == HUMAN_ROLE_NPC_TEAM || role == HUMAN_ROLE_NPC_TEAM_5 || role == HUMAN_ROLE_NEUTRAL ||
        role == HUMAN_ROLE_ENEMY) {
        m_names[1].SetText(C_fstr("%s", body->GetName()->c_str()), STYLE_NAME, 0);
    } else {
        const int rankText = g_rankNames[body->GetProfileUnknown24()].textId;
        const char *name = m_other->GetName()->c_str();
        const C_str rank = g_texts.Get(rankText);
        m_names[1].SetText(C_fstr("%s %s", rank.c_str(), name), STYLE_NAME, 0);
    }
}

void C_InventoryScreen::Show(bool show)
{
    m_frame->SetOn(show);
    if (show) {
        const bool other = (m_mode & MODE_OTHER) != 0;
        m_pages[1]->SetOn(other);
        m_skills->SetOn(!other);
        Refresh();
        return;
    }
    m_description->SetOn(false);
    m_dragItem = nullptr;
    m_dragIcon->SetOn(false);
    m_dragIcon->Update();
}

void C_InventoryScreen::CancelDrag()
{
    m_dragItem = nullptr;
    m_dragIcon->SetOn(false);
    m_dragIcon->Update();
}

void C_InventoryScreen::Refresh()
{
    if (m_dragItem)
        CancelDrag();
    m_weights[0].SetText(C_fstr("%d/%d kg", static_cast<int>(m_human->GetCarriedWeight()), m_human->GetHitPoints()),
                         STYLE_NAME, 0);
    SetBar(m_weightBars[0], m_human->GetCarriedWeight() / static_cast<float>(m_human->GetHitPoints()));
    ShowSlots(m_human, 0);
    SetScrollCount(ID_SCROLL_BARS | 0x10, m_human->GetContainer(ITEM_CONTAINER_BELT));
    ShowList(m_human, m_belts[0], ITEM_CONTAINER_BELT);
    SetScrollCount(ID_SCROLL_BARS | 0x20, m_human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS));
    ShowList(m_human, m_backpacks[0], ITEM_CONTAINER_BACKPACK_CONTENTS);
    if (m_mode & MODE_OTHER) {
        m_weights[1].SetText(
            C_fstr("%d/%d kg", static_cast<int>(m_other->GetCarriedWeight()), m_other->GetHitPoints()), STYLE_NAME, 0);
        SetBar(m_weightBars[1], m_other->GetCarriedWeight() / static_cast<float>(m_other->GetHitPoints()));
        if (m_other->GetType() == ACTOR_HUMAN) {
            C_Human *body = Other();
            ShowSlots(body, 1);
            SetScrollCount(ID_SCROLL_BARS | 0x30, body->GetContainer(ITEM_CONTAINER_BELT));
            ShowList(body, m_belts[1], ITEM_CONTAINER_BELT);
            SetScrollCount(ID_SCROLL_BARS | 0x40, body->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS));
            ShowList(body, m_backpacks[1], ITEM_CONTAINER_BACKPACK_CONTENTS);
        }
    }
    m_mode &= ~MODE_REFRESH;
}

void C_InventoryScreen::ShowSlots(C_Human *human, int page)
{
    if (!human)
        return;
    for (int i = 0; i < SLOTS; ++i) {
        S_Slot &slot = m_slots[i][page];
        const int container = SlotContainer(i);
        C_ItemContainer *items = container == ITEM_CONTAINER_COUNT ? nullptr : human->GetContainer(container);
        C_Item *item = items ? items->GetItem() : nullptr;
        if (!item) {
            slot.item = nullptr;
            slot.material->SetTexture(m_emptyTexture);
            slot.amount.Clear();
            continue;
        }
        slot.item = item;
        if (i == SLOT_UNIFORM) {
            // The searched body's uniform is dimmed unless it can be taken (see DragSlot).
            const bool takeable = page == 0 || (human->IsUnconscious() && !human->GetUnknown1A5());
            static_cast<I3D_object_visual *>(m_slots[SLOT_UNIFORM][page].icon)->SetLightLevel(takeable ? 1.0f : 0.7f);
        }
        slot.material->SetTexture(ItemImage(items->GetItem()));
        if (slot.item->GetKind() == ITEM_KIND_WEAPON && slot.item->GetAmount() != -1.0f)
            slot.amount.SetText(C_fstr("%d", static_cast<int>(slot.item->GetAmount())), STYLE_AMOUNT, 0);
        else
            slot.amount.Clear();
    }
}

void C_InventoryScreen::ShowList(C_Human *human, S_List &list, int container)
{
    C_ItemContainer *items = human->GetContainer(container);
    if (!items) {
        for (int j = 0; j < S_List::LINES; ++j) {
            list.items[j] = nullptr;
            static_cast<I3D_object_visual *>(list.icons[j])->SetAlpha(0.5f);
            list.materials[j]->SetTexture(m_emptyTexture);
            list.amounts[j].Clear();
        }
        list.caption.SetDimmed(true);
        SetBar(list.capacityBar, 0.0f);
        list.capacity.Clear();
        return;
    }
    std::vector<C_Item *> *groups = items->GetSlots();
    SetBar(list.capacityBar, items->GetTotalSpace() / items->GetMaxSpace());
    list.capacityBar->Update();
    list.caption.SetDimmed(false);
    const C_str label = g_texts.Get(TEXT_CAPACITY);
    list.capacity.SetText(C_fstr("%s: %d/%d", label.c_str(), static_cast<int>(items->GetTotalSpace()),
                                 static_cast<int>(items->GetMaxSpace())),
                          STYLE_CAPACITY, 0);
    for (int j = 0; j < S_List::LINES; ++j) {
        const std::vector<C_Item *> &group = groups[list.scroll + j];
        static_cast<I3D_object_visual *>(list.icons[j])->SetAlpha(1.0f);
        if (group.empty()) {
            list.items[j] = nullptr;
            list.materials[j]->SetTexture(m_emptyTexture);
            list.amounts[j].Clear();
            continue;
        }
        C_Item *first = group.front();
        list.items[j] = first;
        list.materials[j]->SetTexture(ItemImage(first));
        const int count = static_cast<int>(group.size());
        if (first->GetKind() == ITEM_KIND_WEAPON) {
            // A weapon's rounds (the count without them).
            const int rounds = static_cast<int>(first->GetAmount());
            list.amounts[j].SetText(C_fstr("%d", rounds == -1 ? count : static_cast<int>(list.items[j]->GetAmount())),
                                    STYLE_AMOUNT, 0);
        } else if (first->GetKind() == ITEM_KIND_AMMO) {
            // The rounds of all the magazines / the magazines.
            int rounds = 0;
            for (C_Item *item : group)
                rounds += static_cast<int>(item->GetAmount());
            list.amounts[j].SetText(C_fstr("%d/%d", rounds, count), STYLE_AMOUNT, 0);
        } else {
            list.amounts[j].SetText(C_fstr("%d", count), STYLE_AMOUNT, 0);
        }
    }
}

void C_InventoryScreen::OnScroll(int id, int index)
{
    if (id == (ID_SCROLL_BARS | 0x10)) {
        m_belts[0].scroll = index;
        ShowList(m_human, m_belts[0], ITEM_CONTAINER_BELT);
    } else if (id == (ID_SCROLL_BARS | 0x20)) {
        m_backpacks[0].scroll = index;
        ShowList(m_human, m_backpacks[0], ITEM_CONTAINER_BACKPACK_CONTENTS);
    } else if (id == (ID_SCROLL_BARS | 0x30)) {
        m_belts[1].scroll = index;
        ShowList(Other(), m_belts[1], ITEM_CONTAINER_BELT);
    } else if (id == (ID_SCROLL_BARS | 0x40)) {
        m_backpacks[1].scroll = index;
        ShowList(Other(), m_backpacks[1], ITEM_CONTAINER_BACKPACK_CONTENTS);
    }
}

void C_InventoryScreen::SetScrollCount(int id, C_ItemContainer *container)
{
    const int groups = container ? container->GetUsedSlotCount() : 0;
    C_InventoryScrollBar &bar = m_scrollBars[((id >> 4) & 0xF) - 1];
    // (Three lines past the first: one position per group beyond them.)
    bar.SetCount(groups - 3);
    const int index = bar.GetIndex();
    if (id == (ID_SCROLL_BARS | 0x10))
        m_belts[0].scroll = index;
    else if (id == (ID_SCROLL_BARS | 0x20))
        m_backpacks[0].scroll = index;
    else if (id == (ID_SCROLL_BARS | 0x30))
        m_belts[1].scroll = index;
    else if (id == (ID_SCROLL_BARS | 0x40))
        m_backpacks[1].scroll = index;
}

bool C_InventoryScreen::IsCursorInside()
{
    if (!m_frame || !m_frame->IsOn())
        return false;
    return m_cursor->IsInRect(&m_frame->GetBound().bbox.min.x);
}

// ---------------------------------------------------------------------------------------------
// Dragging

void C_InventoryScreen::HandleInput(float *held, float *pressed)
{
    HandleScrollBars(held, pressed);
    if (m_doubleClickTime != 0) {
        if (pressed[INPUT_CLICK] != 0.0f)
            OnDoubleClick();
        return;
    }
    if (pressed[INPUT_CLICK] != 0.0f)
        OnPress();
    else if (held[INPUT_CLICK] != 0.0f)
        OnHold();
    else if (m_pressed)
        OnRelease();
    // (A press of INPUT_CLICK2 called an empty function, 0x004a3910.)
}

void C_InventoryScreen::HandleScrollBars(float *held, float *pressed)
{
    const int id = m_cursor->GetHoveredId();
    if (m_activeBar != 0) {
        C_InventoryScrollBar &bar = m_scrollBars[m_activeBar - 1];
        if (pressed[INPUT_CLICK] != 0.0f) {
            bar.Press(id);
        } else if (held[INPUT_CLICK] != 0.0f) {
            float move[3];
            m_cursor->GetMove(move);
            bar.Drag(m_cursor->GetPos(), move);
            return;
        } else if (m_pressed) {
            bar.Release(id);
        }
    }
    if ((id & 0xF00) == ID_SCROLL_BARS) {
        const int barIndex = (id >> 4) & 0xF;
        if (m_activeBar == barIndex) {
            m_scrollBars[m_activeBar - 1].Hover(id);
            return;
        }
        // Another bar takes over while the buttons are up.
        if (held[INPUT_CLICK] != 0.0f || held[INPUT_CLICK2] != 0.0f)
            return;
        if (m_activeBar == 0 || m_scrollBars[m_activeBar - 1].Unhover(id))
            m_activeBar = barIndex;
        return;
    }
    if (m_activeBar != 0 && m_scrollBars[m_activeBar - 1].Unhover(id))
        m_activeBar = 0;
}

void C_InventoryScreen::OnPress()
{
    if (!m_human->IsAlive())
        return;
    if (m_dragItem)
        CancelDrag();
    m_pressed = true;
    m_doubleClickTime = DOUBLE_CLICK_TIME;
    m_pressPos = m_cursor->GetPos();
    if (const int id = m_cursor->GetHoveredId())
        StartDrag(id);
}

void C_InventoryScreen::OnHold()
{
    if (!m_dragItem)
        return;
    // The icon shows once the cursor has moved from the press.
    if (!m_dragIcon->IsOn()) {
        const S_vector4 &pos = m_cursor->GetPos();
        if (m_pressPos.x != pos.x || m_pressPos.y != pos.y || m_pressPos.z != pos.z)
            ShowDragged();
    }
    if (m_dragItem)
        MoveDragged();
}

void C_InventoryScreen::OnRelease()
{
    m_pressed = false;
    if (m_dragItem)
        Drop();
}

void C_InventoryScreen::OnDoubleClick()
{
    if (!m_human->IsAlive())
        return;
    // (The dragged icon is left as it is: it was not shown yet.)
    m_dragItem = nullptr;
    if (const int id = m_cursor->GetHoveredId())
        SendAcross(id);
}

void C_InventoryScreen::StartDrag(int id)
{
    if (!m_human->IsAlive())
        return;
    if (id >= ID_SLOTS && id < ID_SLOTS + SLOTS)
        DragSlot(m_human, 0, id - ID_SLOTS);
    else if (id >= ID_SLOTS_RIGHT && id < ID_SLOTS_RIGHT + SLOTS)
        DragSlot(Other(), 1, id - ID_SLOTS_RIGHT);
    else if (id >= ID_BELT && id < ID_BELT + S_List::LINES)
        DragLine(m_human, m_belts[0], ITEM_CONTAINER_BELT, id - ID_BELT);
    else if (id >= ID_BELT_RIGHT && id < ID_BELT_RIGHT + S_List::LINES)
        DragLine(Other(), m_belts[1], ITEM_CONTAINER_BELT, id - ID_BELT_RIGHT);
    else if (id >= ID_BACKPACK && id < ID_BACKPACK + S_List::LINES)
        DragLine(m_human, m_backpacks[0], ITEM_CONTAINER_BACKPACK_CONTENTS, id - ID_BACKPACK);
    else if (id >= ID_BACKPACK_RIGHT && id < ID_BACKPACK_RIGHT + S_List::LINES)
        DragLine(Other(), m_backpacks[1], ITEM_CONTAINER_BACKPACK_CONTENTS, id - ID_BACKPACK_RIGHT);
}

void C_InventoryScreen::DragSlot(C_Human *human, int page, int index)
{
    if (m_dragItem)
        CancelDrag();
    // Uniforms change hands only with an unconscious body (that has not been marked +0x1A5).
    // Dropped: a copy protection check: in single player from campaign mission 14 on, a failed
    // check kept the uniforms in place (with an unconscious body too).
    if (index == SLOT_UNIFORM && !(m_other && Other()->IsUnconscious() && !Other()->GetUnknown1A5()))
        return;
    S_Slot &slot = m_slots[index][page];
    if (!slot.item)
        return;
    m_dragHuman = human;
    m_dragContainer = SlotContainer(index);
    m_dragItem = slot.item;
    const S_vector &icon = slot.icon->GetWorldPos();
    const S_vector4 &cursor = m_cursor->GetPos();
    m_dragOffset.x = icon.x - cursor.x;
    m_dragOffset.y = icon.y - cursor.y;
    m_dragOffset.z = icon.z - cursor.z;
    m_dragOffset.z = 0.1f;
}

void C_InventoryScreen::DragLine(C_Human *human, S_List &list, int container, int line)
{
    if (m_dragItem)
        CancelDrag();
    if (!list.items[line])
        return;
    m_dragHuman = human;
    m_dragContainer = container;
    m_dragItem = list.items[line];
    m_dragMaterial->SetTexture(ItemImage(m_dragItem));
    const S_vector &icon = list.icons[line]->GetWorldPos();
    const S_vector4 &cursor = m_cursor->GetPos();
    m_dragOffset.x = icon.x - cursor.x;
    m_dragOffset.y = icon.y - cursor.y;
    m_dragOffset.z = icon.z - cursor.z;
    m_dragOffset.z = 0.1f;
}

void C_InventoryScreen::ShowDragged()
{
    if (!m_human->IsAlive())
        return;
    m_dragMaterial->SetTexture(ItemImage(m_dragItem));
    const S_vector4 &cursor = m_cursor->GetPos();
    m_dragIcon->SetPos(S_vector(m_dragOffset.x + cursor.x, m_dragOffset.y + cursor.y, m_dragOffset.z + cursor.z));
    m_dragIcon->SetOn(true);
    m_dragIcon->Update();
}

void C_InventoryScreen::MoveDragged()
{
    if (!m_human->IsAlive()) {
        Drop();
        return;
    }
    const S_vector4 &cursor = m_cursor->GetPos();
    m_dragIcon->SetPos(S_vector(m_dragOffset.x + cursor.x, m_dragOffset.y + cursor.y, m_dragOffset.z + cursor.z));
    m_dragIcon->Update();
}

void C_InventoryScreen::Drop()
{
    if (!m_human->IsAlive()) {
        CancelDrag();
        HideSelections();
        Refresh();
        return;
    }
    // Off the inventory: thrown away (a uniform is not).
    if (!IsCursorInside() && m_dragContainer != ITEM_CONTAINER_UNIFORM)
        m_dragHuman->RequestThrowItem(m_dragItem, m_dragContainer, m_dragHuman->GetViewDir());
    const int id = m_cursor->GetHoveredId();
    if (id == ID_SLOTS + SLOT_DROP || id == ID_SLOTS_RIGHT + SLOT_DROP) {
        if (m_dragContainer != ITEM_CONTAINER_UNIFORM)
            m_dragHuman->RequestThrowItem(m_dragItem, m_dragContainer, m_dragHuman->GetViewDir());
    } else {
        // The place dropped on: its human, container and line, and the item there (slots).
        C_Human *target = nullptr;
        C_Item *there = nullptr;
        int container = 0;
        int line = 0;
        if (id >= ID_SLOTS && id < ID_SLOTS + SLOTS) {
            target = m_human;
            there = m_slots[id - ID_SLOTS][0].item;
            container = SlotContainer(id - ID_SLOTS);
        } else if (id >= ID_SLOTS_RIGHT && id < ID_SLOTS_RIGHT + SLOTS) {
            target = Other();
            there = m_slots[id - ID_SLOTS_RIGHT][1].item;
            container = SlotContainer(id - ID_SLOTS_RIGHT);
        } else if (id >= ID_BELT && id < ID_BELT + S_List::LINES) {
            target = m_human;
            container = ITEM_CONTAINER_BELT;
            line = id - ID_BELT;
        } else if (id >= ID_BELT_RIGHT && id < ID_BELT_RIGHT + S_List::LINES) {
            target = Other();
            container = ITEM_CONTAINER_BELT;
            line = id - ID_BELT_RIGHT;
        } else if (id >= ID_BACKPACK && id < ID_BACKPACK + S_List::LINES) {
            target = m_human;
            container = ITEM_CONTAINER_BACKPACK_CONTENTS;
            line = id - ID_BACKPACK;
        } else if (id >= ID_BACKPACK_RIGHT && id < ID_BACKPACK_RIGHT + S_List::LINES) {
            target = Other();
            container = ITEM_CONTAINER_BACKPACK_CONTENTS;
            line = id - ID_BACKPACK_RIGHT;
        }
        if (target && (target != m_dragHuman || container != m_dragContainer)) {
            // On an occupied slot the two items swap.
            if (there)
                target->RequestTransferItem(there, container, m_dragHuman, m_dragItem, m_dragContainer);
            else
                target->RequestMoveItem(m_dragItem, m_dragHuman, m_dragContainer, container, line);
        }
    }
    CancelDrag();
    HideSelections();
    Refresh();
}

void C_InventoryScreen::HideSelections()
{
    for (int i = 0; i < SLOTS; ++i)
        m_slots[i][0].selection->SetOn(false);
    m_belts[0].selection->SetOn(false);
    m_backpacks[0].selection->SetOn(false);
    if (!m_other)
        return;
    for (int i = 0; i < SLOTS; ++i)
        m_slots[i][1].selection->SetOn(false);
    m_belts[1].selection->SetOn(false);
    m_backpacks[1].selection->SetOn(false);
}

void C_InventoryScreen::SendAcross(int id)
{
    // An item of the soldier goes to the body (to himself, ITEM_CONTAINER_COUNT: wherever it
    // fits, without one); one of the body's to the soldier. Not from the drop places.
    if (id >= ID_SLOTS && id < ID_SLOTS + SLOTS && id != ID_SLOTS + SLOT_DROP) {
        C_Item *item = m_slots[id - ID_SLOTS][0].item;
        if (!item)
            return;
        C_Human *to = m_other ? Other() : m_human;
        to->RequestMoveItem(item, m_human, SlotContainer(id - ID_SLOTS), ITEM_CONTAINER_COUNT, 0);
    } else if (id >= ID_SLOTS_RIGHT && id < ID_SLOTS_RIGHT + SLOTS && id != ID_SLOTS_RIGHT + SLOT_DROP) {
        C_Item *item = m_slots[id - ID_SLOTS_RIGHT][1].item;
        if (!item)
            return;
        m_human->RequestMoveItem(item, m_other, SlotContainer(id - ID_SLOTS_RIGHT), ITEM_CONTAINER_COUNT, 0);
    } else if ((id >= ID_BELT && id < ID_BELT + S_List::LINES) ||
               (id >= ID_BACKPACK && id < ID_BACKPACK + S_List::LINES)) {
        const bool belt = id < ID_BELT + S_List::LINES;
        C_Item *item = belt ? m_belts[0].items[id - ID_BELT] : m_backpacks[0].items[id - ID_BACKPACK];
        if (!item)
            return;
        C_Human *to = m_other ? Other() : m_human;
        to->RequestMoveItem(item, m_human, belt ? ITEM_CONTAINER_BELT : ITEM_CONTAINER_BACKPACK_CONTENTS,
                            ITEM_CONTAINER_COUNT, 0);
    } else if ((id >= ID_BELT_RIGHT && id < ID_BELT_RIGHT + S_List::LINES) ||
               (id >= ID_BACKPACK_RIGHT && id < ID_BACKPACK_RIGHT + S_List::LINES)) {
        const bool belt = id < ID_BELT_RIGHT + S_List::LINES;
        C_Item *item = belt ? m_belts[1].items[id - ID_BELT_RIGHT] : m_backpacks[1].items[id - ID_BACKPACK_RIGHT];
        if (!item)
            return;
        m_human->RequestMoveItem(item, m_other, belt ? ITEM_CONTAINER_BELT : ITEM_CONTAINER_BACKPACK_CONTENTS,
                                 ITEM_CONTAINER_COUNT, 0);
    }
}

// ---------------------------------------------------------------------------------------------
// Updates

void C_InventoryScreen::Update(int timeDelta)
{
    if (m_doubleClickTime != 0) {
        m_doubleClickTime -= timeDelta;
        if (m_doubleClickTime < 0)
            m_doubleClickTime = 0;
    }
    if (m_mode & MODE_REFRESH) {
        Refresh();
        m_mode &= ~MODE_REFRESH;
    }
    // (The body is read as a human whatever it is; only humans are searched.)
    if (m_human->IsInventoryChanged() || (m_other && Other()->IsInventoryChanged()))
        Refresh();
    UpdateSkills();
    UpdateDescription(timeDelta);
}

void C_InventoryScreen::UpdateDescription(int timeDelta)
{
    const S_vector4 &cursor = m_cursor->GetPos();
    const S_vector4 from = {cursor.x, cursor.y, cursor.z + 0.5f, cursor.w};
    I3D_frame *frame = nullptr;
    if (!m_dragItem && IsCursorInside())
        frame = m_scene->PickFrame(from, S_vector(0.0f, 0.0f, 1.0f), 0x40000, 0, 0, 0, 0, 0, 0);
    if (!frame || !GetFrameId(frame)) {
        m_description->SetOn(false);
        m_hoverTime = 0;
        m_hovered = nullptr;
        return;
    }
    if (m_hovered == frame) {
        m_hoverTime += timeDelta;
    } else {
        m_hovered = frame;
        m_description->SetOn(false);
        m_hoverTime = 0;
    }
    if (m_hoverTime <= DESCRIPTION_DELAY || m_description->IsOn())
        return;
    const int id = GetFrameId(m_hovered);
    C_Item *item;
    if (id >= ID_SLOTS && id < ID_SLOTS + SLOTS)
        item = m_slots[id - ID_SLOTS][0].item;
    else if (id >= ID_SLOTS_RIGHT && id < ID_SLOTS_RIGHT + SLOTS)
        item = m_slots[id - ID_SLOTS_RIGHT][1].item;
    else if (id >= ID_BELT && id < ID_BELT + S_List::LINES)
        item = m_belts[0].items[id - ID_BELT];
    else if (id >= ID_BELT_RIGHT && id < ID_BELT_RIGHT + S_List::LINES)
        item = m_belts[1].items[id - ID_BELT_RIGHT];
    else if (id >= ID_BACKPACK && id < ID_BACKPACK + S_List::LINES)
        item = m_backpacks[0].items[id - ID_BACKPACK];
    else if (id >= ID_BACKPACK_RIGHT && id < ID_BACKPACK_RIGHT + S_List::LINES)
        item = m_backpacks[1].items[id - ID_BACKPACK_RIGHT];
    else
        return;
    if (!item)
        return;

    // Next to the cursor: the name, the weight, a weapon's rounds, the muzzle velocity and a
    // scope's magnification.
    m_description->SetOn(true);
    const S_vector4 &at = m_cursor->GetPos();
    m_description->SetPos(S_vector(at.x + 0.15f, at.y + 0.015f, at.z + 0.1f));
    m_description->Update();
    m_descriptionText.AttachTo(m_scene, m_description);
    m_descriptionText.Clear();
    C_ItemDef *def = item->GetDef();
    m_descriptionText.AddText(C_fstr(" "), STYLE_DESCRIPTION);
    m_descriptionText.AddTextId(def->GetTextId(), STYLE_DESCRIPTION_NAME);
    m_descriptionText.AddText(C_fstr(" "), STYLE_DESCRIPTION);
    m_descriptionText.AddText(
        C_fstr("%s : %.2f kg", g_texts.Get(TEXT_WEIGHT).c_str(), static_cast<double>(def->GetWeight())),
        STYLE_DESCRIPTION);
    const int behaviour = def->GetBehaviour();
    if (behaviour < 2 || (behaviour > 5 && behaviour != 12))
        return;
    if (def->GetAmmoItemId() >= 0) {
        m_descriptionText.AddText(C_fstr("%s : %d/%d", g_texts.Get(TEXT_ROUNDS).c_str(),
                                         static_cast<int>(item->GetAmount()), static_cast<int>(item->GetMaxAmount())),
                                  STYLE_DESCRIPTION);
    }
    const E_ItemComponentKind kind = def->GetComponentKind();
    if (kind == ICK_PROJECTILE_WEAPON || kind == ICK_FIREARM) {
        const float velocity = static_cast<C_WeaponComp *>(def->GetComponent())->GetMuzzleVelocity();
        m_descriptionText.AddText(
            C_fstr("%s : %.2f m/s", g_texts.Get(TEXT_VELOCITY).c_str(), static_cast<double>(velocity)),
            STYLE_DESCRIPTION);
    }
    // A rifle's scope (aim modes 0..3): the magnification against the 80 degree view.
    if (def->GetBehaviour() != 4)
        return;
    C_ItemComponent *scope = def->GetSecondaryComponent();
    if (!scope || scope->GetKind() != ICK_SCOPE || static_cast<C_ScopeComp *>(scope)->GetAimMode() >= 4)
        return;
    const float fov = static_cast<C_ScopeComp *>(scope)->GetAimedFovFactor();
    const float zoom = static_cast<float>(std::tan(0.6981317202250164) / std::tan(static_cast<double>(fov * 0.5f)));
    m_descriptionText.AddText(C_fstr("%s : %.2f x", g_texts.Get(TEXT_ZOOM).c_str(), static_cast<double>(zoom)),
                              STYLE_DESCRIPTION);
}

void C_InventoryScreen::UpdateSkills()
{
    if (!m_skills->IsOn())
        return;
    for (int i = 0; i < SKILLS; ++i) {
        const int stat = SKILL_STATS[i];
        float bar;
        if (stat == STAT_HEALTH) {
            const float health = static_cast<float>(m_human->GetHealthPercent());
            m_skillValues[i].SetText(C_fstr("%5d%%", static_cast<int>(health)), STYLE_LABEL, 0);
            bar = health * 0.01f;
        } else if (stat > 0 && stat < 3) {
            // Strength and endurance as they are.
            const float value = static_cast<float>(m_human->GetStat(stat));
            m_skillValues[i].SetText(C_fstr("%5d", static_cast<int>(value)), STYLE_LABEL, 0);
            bar = value * 0.01f;
        } else {
            const float factor = m_human->GetStatFactor(stat);
            m_skillValues[i].SetText(C_fstr("%5d%%", static_cast<int>(factor * 100.0f)), STYLE_LABEL, 0);
            bar = factor;
        }
        SetBar(m_skillBars[i], bar);
        m_skillBars[i]->Update();
    }
}

// ---------------------------------------------------------------------------------------------
// Rendering

void C_InventoryScreen::Render()
{
    if (!m_frame->IsOn())
        return;
    const bool other = (m_mode & MODE_OTHER) != 0;
    for (int i = 0; i < SLOTS; ++i) {
        m_slots[i][0].name.Render();
        m_slots[i][0].amount.Render();
        if (other) {
            m_slots[i][1].name.Render();
            m_slots[i][1].amount.Render();
        }
    }
    m_belts[0].caption.Render();
    m_belts[0].capacity.Render();
    m_backpacks[0].caption.Render();
    m_backpacks[0].capacity.Render();
    for (int j = 0; j < S_List::LINES; ++j) {
        m_belts[0].amounts[j].Render();
        m_backpacks[0].amounts[j].Render();
    }
    m_names[0].Render();
    m_weights[0].Render();
    if (other) {
        m_belts[1].caption.Render();
        // Quirk kept: the left belt's capacity again; the right belt's is never drawn.
        m_belts[0].capacity.Render();
        m_backpacks[1].caption.Render();
        m_backpacks[1].capacity.Render();
        for (int j = 0; j < S_List::LINES; ++j) {
            m_belts[1].amounts[j].Render();
            m_backpacks[1].amounts[j].Render();
        }
        m_names[1].Render();
        m_weights[1].Render();
    }
    RenderSkills();
    if (m_description->IsOn())
        m_descriptionText.Render();
}

void C_InventoryScreen::RenderSkills()
{
    if (!m_skills->IsOn())
        return;
    m_skillsTitle.Render();
    for (int i = 0; i < SKILLS; ++i) {
        m_skillNames[i].Render();
        m_skillValues[i].Render();
    }
}
