// The hero stats menu: its screens, the specialization and the skills, the accept (part 9i-i).
#include "game/hero_stats_menu.h"

#include <cstring>
#include <malloc.h>

#include "game/control.h"
#include "game/items_module.h"
#include "game/controls.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/soldier.h"
#include "ls3d/i3d.h"

namespace {

// The hero screen (its id's top 6 bits; the quit question is QUIT_SCREEN).
constexpr uint32_t HERO_SCREEN = 1;
constexpr uint32_t SCREEN_SHIFT = 26, SCREEN_MASK = 0x3F;
// Accept's message (target 0).
constexpr uint32_t HERO_MSG_ACCEPT = 0x200006A;
// What the buttons post: a menu message with the bits 0x04001000.
constexpr uint32_t FROM_SCREEN = 0x4001000;
// The quit question's yes / no: the kept message posted / dropped (1); closed.
constexpr uint32_t QUIT_YES = MENU_MSG_DEFERRED | FROM_SCREEN | MENU_DEFERRED_POST,
                   QUIT_NO = MENU_MSG_DEFERRED | FROM_SCREEN | MENU_DEFERRED_DROP;
constexpr uint32_t QUIT_CLOSE = FROM_SCREEN | MENU_MSG_CLOSE_POPUP;
// Controls' kinds.
constexpr int CONTROL_LIST = 0xB, CONTROL_TEXT = 0x13, CONTROL_BUTTON = 0x14, CONTROL_SKILL = 0x19;
// The styles; the texts.
constexpr uint32_t STYLE_TITLE = 10, STYLE_LABEL = 0xB, STYLE_VALUE = 0xC, STYLE_BUTTON = 0xD;
constexpr uint32_t TEXT_SURE = 0x7D4, TEXT_YES = 0x7D1, TEXT_NO = 0x7D0, TEXT_HERO_STATS = 0xA29,
                   TEXT_SPECIALIZATION = 0xA2D, TEXT_SKILLS = 0xA2E, TEXT_ACCEPT = 0xA2A, TEXT_BACK = 0xA2B;
// Where the quit question's model sits (the main menu's).
const S_vector QUIT_MODEL_POS(0.5f, -0.375f, -0.5f);
// The hero screen's controls: the skills' total ("skill00"), the skill rows (their index in bits 12-19).
constexpr uint32_t SKILL_TOTAL = 0x4300000, SKILL_ROWS = 0x4500000, SKILL_ROW_STEP = 0x1000;
// The list's rows shown, their step down.
constexpr uint32_t LIST_ROWS = 0x14;
constexpr float SKILL_STEP = 0.02f;
// The specialization's buttons.
const char *const SPECIALIZATION_FRAMES[] = {"bsoldier", "bheavysoldier", "bsniper", "bmedic", "bengineer"};
// The skills (their rows in order) and their bases.
constexpr int SKILLS = 18;
const char *const SKILL_NAMES[SKILLS] = {"Health",   "Might",   "Reactions",     "Inteligence",   "Endurance", "Stealth",
                                         "Light Weapons", "Heavy Weapons", "Shooting Rifle", "Throwing", "Morale",
                                         "Driving",  "Swimming", "Traps",        "First Aid",     "Pick Lock", "Pilot",
                                         "German"};
const uint32_t SKILL_BASES[SKILLS] = {80, 75, 60, 50, 75, 30, 80, 40, 75, 70, 80, 50, 80, 30, 70, 40, 50, 60};
// The skill rows kept in the soldier's stats (the row, from 1; the stat).
const struct {
    uint32_t row;
    int stat;
} SKILL_STATS[] = {{1, STAT_HEALTH},   {2, STAT_STRENGTH},  {5, STAT_ENDURANCE}, {6, STAT_STEALTH},  {7, STAT_SHOOTING},
                   {10, STAT_UNKNOWN5}, {11, STAT_MORALE},  {14, STAT_LOCKPICK},  {15, STAT_FIRST_AID}};

void Post(uint32_t target, uint32_t id, uint32_t value)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    PostControlMessage(message);
}

S_ControlDesc *AddButton(S_ScreenDesc *desc, const char *name, uint32_t text)
{
    S_ControlDesc *control = desc->AddControl(CONTROL_BUTTON, name);
    AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_BUTTON, text);
    return control;
}

void AddText(S_ScreenDesc *desc, const char *name, int type, uint32_t style, uint32_t text)
{
    AddDescMessage(desc->AddControl(type, name), 0, CONTROL_MSG_SET_TEXT_ID, style, text);
}

uintptr_t FloatBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

} // namespace

int C_HeroStatsMenu::GetSkillValue(uint32_t id)
{
    // Fix: no screen open: -1 (the original searched from a null screen).
    if (g_menuScreens.empty())
        return -1;
    C_Control *control = g_menuScreens.back()->FindControl(id, true);
    if (!control)
        return -1;
    S_ControlMessage ask;
    ask.id = CONTROL_MSG_GET_NUMBER;
    control->HandleMessage(&ask);
    return static_cast<int>(ask.value);
}

void C_HeroStatsMenu::OnMessage(S_ControlMessage *message)
{
    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_ESCAPE:
        if (!g_menuScreens.empty() && (g_menuScreens.back()->GetId() >> SCREEN_SHIFT & SCREEN_MASK) == QUIT_SCREEN) {
            Post(0, QUIT_NO, 1);
            Post(0, QUIT_CLOSE, 0);
            return;
        }
        Post(0, MENU_MSG_OPEN_POPUP, QUIT_SCREEN);
        Post(0, MENU_MSG_DEFER | MENU_MSG_PREVIOUS, 0);
        return;
    case HERO_MSG_ACCEPT: {
        C_HumanStore *store = g_gameProfile.m_humanStore;
        if (store->GetCount() == 0 && !store->Load(C_str("tables\\HumanStore.def")))
            return;
        S_SoldierProfile *soldier = store->Get(0);
        for (const auto &skill : SKILL_STATS)
            soldier->stats[skill.stat] = static_cast<float>(GetSkillValue(SKILL_ROWS + skill.row * SKILL_ROW_STEP));
        Post(0, MENU_MSG_NEXT, 0);
        return;
    }
    default:
        return;
    }
}

bool C_HeroStatsMenu::BuildScreens()
{
    auto *menu = new S_MenuDesc();
    menu->AddScreen("HERO STATS", "heros_screen");
    menu->AddScreen("CONFIRM", "confirm");
    menu->AddScreen("CONFIRM", "confirm");
    menu->AddScreen("CONFIRM", "confirm");
    for (S_ScreenDesc *desc : menu->screens) {
        I3D_frame *model = LoadModel(desc->name.c_str(), desc->file.c_str());
        if (!model)
            continue;
        if ((desc->id & 0xFC000000) == HERO_SCREEN << SCREEN_SHIFT)
            DescribeHeroScreen(desc, model);
        if ((desc->id & 0xFC000000) == QUIT_SCREEN << SCREEN_SHIFT)
            DescribeQuitQuestion(desc, model);
        auto *screen = new C_Screen(m_scene, model);
        // (Quirk kept: a screen not built is leaked.)
        if (screen->Build(desc)) {
            screen->HandleMessages(desc->messages);
            m_screens.push_back(screen);
        }
    }
    delete menu;
    S_ControlMessage open;
    open.id = MENU_MSG_OPEN_SCREEN;
    open.value = HERO_SCREEN;
    HandleMessage(&open);
    return true;
}

bool C_HeroStatsMenu::DescribeQuitQuestion(S_ScreenDesc *desc, I3D_frame *model)
{
    model->SetPos(QUIT_MODEL_POS);
    AddText(desc, "text", CONTROL_TEXT, STYLE_VALUE, TEXT_SURE);
    S_ControlDesc *yes = AddButton(desc, "byes*", TEXT_YES);
    AddDescMessage(yes, 0, QUIT_YES, 0);
    AddDescMessage(yes, 0, QUIT_CLOSE, 0);
    S_ControlDesc *no = AddButton(desc, "bno*", TEXT_NO);
    AddDescMessage(no, 0, QUIT_NO, 1);
    AddDescMessage(no, 0, QUIT_CLOSE, 0);
    return true;
}

bool C_HeroStatsMenu::DescribeHeroScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    AddText(desc, "text_screen", CONTROL_TEXT, STYLE_TITLE, TEXT_HERO_STATS);
    AddText(desc, "text_specialization", CONTROL_TEXT, STYLE_LABEL, TEXT_SPECIALIZATION);
    AddText(desc, "skill00", CONTROL_SKILL, STYLE_LABEL, TEXT_SKILLS);
    DescribeSpecialization(desc, model);
    DescribeSkills(desc, model);
    AddDescMessage(AddButton(desc, "baccept", TEXT_ACCEPT), 0, FROM_SCREEN | HERO_MSG_ACCEPT, 0);
    AddDescMessage(AddButton(desc, "bback", TEXT_BACK), 0, FROM_SCREEN | MENU_MSG_ESCAPE, 0);
    return true;
}

bool C_HeroStatsMenu::DescribeSpecialization(S_ScreenDesc *desc, I3D_frame *model)
{
    I3D_frame *group = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    I3D_frame *buttons[5];
    for (int i = 0; i < 5; ++i)
        buttons[i] = model->FindChildFrame(SPECIALIZATION_FRAMES[i], FRAME_MASK_DUMMY);
    if (!group || !buttons[0] || !buttons[1] || !buttons[2] || !buttons[3] || !buttons[4])
        return false;
    group->LinkTo(model, 0);
    static_cast<I3D_model *>(model)->AddModelFrame(group);
    group->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    group->SetName("GRSkillSpec");
    m_models.push_back(group);
    for (I3D_frame *button : buttons)
        button->LinkTo(group, 1);
    group->SetOn(true);
    group->Update();
    AddDescMessage(desc->AddControl(CONTROL_LIST, "GRSkillSpec"), 0, CONTROL_MSG_SET_VISIBLE, LIST_ROWS);
    return true;
}

bool C_HeroStatsMenu::DescribeSkills(S_ScreenDesc *desc, I3D_frame *model)
{
    I3D_frame *group = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    I3D_frame *row = model->FindChildFrame("skill01", FRAME_MASK_DUMMY);
    if (!row || !group)
        return false;
    group->LinkTo(model, 0);
    static_cast<I3D_model *>(model)->AddModelFrame(group);
    group->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    group->SetName("GRSkills");
    m_models.push_back(group);
    row->LinkTo(group, 1);
    row->SetOn(false);
    group->SetOn(true);
    row->Update();
    group->Update();
    S_ControlDesc *list = desc->AddControl(CONTROL_LIST, "GRSkills");
    AddDescMessage(list, 0, CONTROL_MSG_SET_VISIBLE, LIST_ROWS);
    AddDescMessage(list, 0, CONTROL_MSG_SET_DOWN_STEP, 0, FloatBits(SKILL_STEP));
    auto *origin = static_cast<S_vector4 *>(_aligned_malloc(sizeof(S_vector4), 16));
    if (origin)
        std::memcpy(origin, &row->GetPos(), sizeof(S_vector4));
    AddDescMessage(list, 0, CONTROL_MSG_SET_ORIGIN, 0, reinterpret_cast<uintptr_t>(origin));
    for (int i = 0; i < SKILLS; ++i)
        AddSkillRow(list, model, C_str(SKILL_NAMES[i]), i, SKILL_STEP);
    Post(SKILL_TOTAL, CONTROL_MSG_SET_TOTAL, LIST_ROWS);
    for (uint32_t i = 1; i <= SKILLS; ++i)
        Post(SKILL_ROWS | (i * SKILL_ROW_STEP & 0xFF000), CONTROL_MSG_SET_BASE, SKILL_BASES[i - 1]);
    return true;
}

void C_HeroStatsMenu::AddSkillRow(S_ControlDesc *list, I3D_frame *model, C_str name, int index, float step)
{
    I3D_frame *group = model->FindChildFrame("GRSkills", FRAME_MASK_DUMMY);
    I3D_frame *source = model->FindChildFrame("skill01", FRAME_MASK_DUMMY);
    if (!source || !group)
        return;
    I3D_frame *row = nullptr;
    CloneFrameTree(row, source, nullptr);
    m_frames.push_back(row);
    row->SetName(name.c_str());
    const S_vector &pos = source->GetPos();
    row->SetPosInline(S_vector(pos.x, pos.y - static_cast<float>(index) * step, pos.z));
    row->LinkTo(group, 0);
    row->SetOn(true);
    row->Update();
    S_ControlDesc *control = list->AddChild(CONTROL_SKILL, name.c_str());
    AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT, STYLE_LABEL, name);
    AddDescMessage(control, 0, CONTROL_MSG_SET_VALUE, SKILL_TOTAL);
}
