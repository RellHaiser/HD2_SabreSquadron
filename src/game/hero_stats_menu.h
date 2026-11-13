// The hero stats menu (C_HeroStatsMenu, vtable 0x00815c30, 0x1B0 bytes: the campaign screen of kind 0,
// none in the shipped campaigns; original 0x00696fb0..0x00698520): the hero's specialization and his 18
// skills ("HERO STATS" on heros_screen), accepted into the first roster soldier's stats; the quit
// question. Part 9i-i.
#pragma once

#include <cstdint>

#include "common/c_str.h"
#include "game/game_state.h"
#include "game/menu_state.h"

class I3D_frame;
struct S_ControlDesc;
struct S_ScreenDesc;

class C_HeroStatsMenu : public C_MenuState {
public:
    // (Inline in C_GameApp::StartStep; the destructor is the base's: 0x006c22d0, deleting 0x006c22b0.)
    C_HeroStatsMenu() = default;

    int GetTypeId() const override { return GAME_STATE_TYPE_HERO_STATS_MENU; }   // 0x1C 0x006c22a0

    // Escape: on the quit question its no (the kept message dropped, closed); else the question asked (the
    // campaign's previous screen kept back). HERO_MSG_ACCEPT: the roster loaded when empty (failing:
    // nothing), the skill rows' numbers into the first soldier's stats, the campaign's next step.
    void OnMessage(S_ControlMessage *message) override; // 0x34 0x00697090
    // "HERO STATS" (heros_screen) and three "CONFIRM" screens (the last the quit question): described, made;
    // the hero screen opened. True.
    bool BuildScreens() override;                       // 0x38 0x00697480

protected:
    // The number of the top screen's control `id` (none: -1).
    static int GetSkillValue(uint32_t id);              // 0x00696fb0 (__stdcall)
    // The quit question: the model placed as the main menu's; "ARE YOU SURE?", yes (the kept message
    // posted), no (dropped), both closing it. True.
    static bool DescribeQuitQuestion(S_ScreenDesc *desc, I3D_frame *model);   // 0x006977e0 (__stdcall)
    // The hero screen: the title, the specialization's and the skills' labels, the specialization, the
    // skills, accept (HERO_MSG_ACCEPT), back (Escape). True.
    bool DescribeHeroScreen(S_ScreenDesc *desc, I3D_frame *model);   // 0x006978d0
    // The specialization's buttons (bsoldier, bheavysoldier, bsniper, bmedic, bengineer; one missing: false)
    // moved into a new group GRSkillSpec under the model; its list control (20 shown). True.
    bool DescribeSpecialization(S_ScreenDesc *desc, I3D_frame *model);   // 0x006979e0
    // The skills: the row "skill01" (none: false) moved into a new group GRSkills, off; the list (20 shown,
    // 0.02 apart, from the row's place), a row a skill; the total's 20 and each skill's base posted. True.
    bool DescribeSkills(S_ScreenDesc *desc, I3D_frame *model);   // 0x00697d20
    // A skill's row: "skill01" copied (named after the skill, `index` steps down) into GRSkills (neither
    // found: nothing); its control in the list (its text, the total's control).
    void AddSkillRow(S_ControlDesc *list, I3D_frame *model, C_str name, int index, float step);   // 0x00698230 (DS 0x006d8ca0)
};
