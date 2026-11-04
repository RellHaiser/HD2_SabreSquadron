// The campaign tree: what the single player game goes through, in order.
//
// GameData\Gamedata%02d.gdt holds one campaign each (00: Hidden & Dangerous 2, 01: Sabre
// Squadron): a root node with operations, operations with missions, videos and menu screens,
// missions with their objectives and the steps around them (briefing videos, the equipment
// screen, the mission itself, the debriefing). Container nodes keep a cursor; Next / Prev walk
// the whole tree depth first and GetCurrent returns the step the cursor is on (the game's
// flow, C_GameApp, starts the video / menu screen / mission it finds there).
//
// Nodes serialize three ways (vtable slots 1..5): LoadDefinition reads an older definition
// format (the root, operation and mission parsers add the children); Load / Save the complete
// node (the shipped .gdt files are in this format); LoadProgress / SaveProgress the cursors
// and the objectives' states, for the profile's saved games.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"

class C_chunk;

enum E_CampaignNodeType : int {
    CAMPAIGN_NODE_END = 0,                              // the plain node a single mission ends with
    CAMPAIGN_NODE_ROOT = 1,                             // C_CampaignRoot
    CAMPAIGN_NODE_OPERATION = 2,                        // C_CampaignOperation
    CAMPAIGN_NODE_MISSION = 3,                          // C_MissionDef
    CAMPAIGN_NODE_OBJECTIVE = 4,                        // S_MissionObjective
    CAMPAIGN_NODE_VIDEO = 5,                            // C_CampaignVideo
    CAMPAIGN_NODE_SCREEN = 6,                           // C_CampaignScreen
    CAMPAIGN_NODE_PLAY = 7,                             // C_CampaignPlay
    CAMPAIGN_NODE_MP_OBJECTIVE = 12,                    // S_MpObjective
    CAMPAIGN_NODE_MP_MISSION = 13,                      // C_MpMissionDef
};

// Objective states (S_MissionObjective::status).
enum E_ObjectiveStatus : int {
    OBJECTIVE_OPEN = 0,
    OBJECTIVE_COMPLETED = 1,
    OBJECTIVE_FAILED = 2,
    OBJECTIVE_FAILED_CRITICAL = 3,                      // the mission is lost
    OBJECTIVE_INACTIVE = 4,                             // not part of the mission (any more)
};
// S_MpObjective::type values.
enum : int {
    OBJECTIVE_TYPE_VIP = 1,                             // a defender is the VIP
};

// A node of the tree (vtable 0x00815690, 10 slots; 0x10 bytes).
class C_CampaignNode {
public:
    C_CampaignNode() = default;                         // (inline: 0x006b59f0)
    // Copies the type, the id and the text (not the derived parts).
    C_CampaignNode(const C_CampaignNode &other);        // 0x006b9e80
    C_CampaignNode &operator=(const C_CampaignNode &) = delete;
    virtual ~C_CampaignNode();                          // 0x00 0x0066eff0 (0x0066f010)
    virtual bool LoadDefinition(C_chunk &chunk) { return false; }   // 0x04 0x0066efc0
    virtual bool Load(C_chunk &chunk) { return false; }             // 0x08 0x0066efd0
    virtual bool Save(C_chunk &chunk) { return false; }             // 0x0C 0x0066efe0
    virtual bool LoadProgress(C_chunk &chunk) { return false; }     // 0x10 0x0066eef0
    virtual bool SaveProgress(C_chunk &chunk) { return false; }     // 0x14 0x0066ef00
    // Moves the cursor one step on (false: there is nothing more inside this node).
    virtual bool Next() { return false; }               // 0x18 0x0066ef10
    // One step back (false: the cursor was at this node's first step).
    virtual bool Prev() { return false; }               // 0x1C 0x0066ef20
    // The step the cursor is on (a leaf: itself).
    virtual C_CampaignNode *GetCurrent() { return this; }   // 0x20 0x0066ef30
    // Puts the cursor before the first step.
    virtual void Reset() {}                             // 0x24 0x0066ef40

    E_CampaignNodeType GetType() const { return static_cast<E_CampaignNodeType>(m_type); }
    int GetId() const { return m_id; }

protected:
    explicit C_CampaignNode(E_CampaignNodeType type) : m_type(type) {}

    int m_type = CAMPAIGN_NODE_END;                     // +0x04 E_CampaignNodeType
    int m_id = 0;                                       // +0x08 text id; missions: their number
    C_str m_text;                                       // +0x0C copied, never loaded
};

// A video between the steps (vtable 0x00815668, type 5; 0x1C bytes).
class C_CampaignVideo : public C_CampaignNode {
public:
    C_CampaignVideo() : C_CampaignNode(CAMPAIGN_NODE_VIDEO) {}
    ~C_CampaignVideo() override;                        // 0x0066ef50 (0x0066ef70)
    bool LoadDefinition(C_chunk &chunk) override;       // 0x006ba850
    bool Load(C_chunk &chunk) override;                 // 0x006bce50
    bool Save(C_chunk &chunk) override;                 // 0x006bcc30

    const char *GetFileName() const { return m_fileName; }
    bool IsBriefing() const { return m_briefing; }

private:
    friend class C_Campaign;
    friend class C_MovieState;                          // (Play's node)

    C_str m_fileName;                                   // +0x10 e.g. "BriefArctic1a.bik"
    C_str m_title;                                      // +0x14 "Briefing Arktida 1A"
    // A briefing: a single mission plays it too.
    bool m_briefing = false;                            // +0x18
};

// Equipment a screen offers (16 bytes; multiplayer: a map's allowed items).
struct S_CampaignItem {
    int itemId;
    int count;
    int ammo;                                           // -1: the default
    // The count is of the item's ammunition (the equipment screen's stock gets the rounds;
    // about a third of the shipped entries: an item listed twice has it on the larger count,
    // item 2: 5 and 25).
    bool ammunition;
};

// A menu screen between the steps (vtable 0x00815b38, type 6; 0x38 bytes).
class C_CampaignScreen : public C_CampaignNode {
public:
    // The screen kinds (m_kind). Only 3 is identified; the others by where the shipped
    // campaigns put them: 1 before the briefing of three missions, 2 after an operation's
    // intro (a single mission's first screen), 4 a later mission's equipment (fewer items),
    // 5 after each mission, 6 at the end of an operation (Sabre Squadron).
    enum : int {
        KIND_EQUIPMENT = 3,                             // the squad's equipment: m_items
    };

    C_CampaignScreen() : C_CampaignNode(CAMPAIGN_NODE_SCREEN) {}
    ~C_CampaignScreen() override;                       // 0x006b9ed0 (0x006b9ef0)
    bool LoadDefinition(C_chunk &chunk) override;       // 0x006bab50
    bool Load(C_chunk &chunk) override;                 // 0x006bd330
    bool Save(C_chunk &chunk) override;                 // 0x006bd160

    int GetKind() const { return m_kind; }
    // Going back from this screen may cross into the previous mission or operation
    // (C_Campaign::PreviousScreen).
    bool GoesBackAcross() const { return m_backAcross; }
    const std::vector<S_CampaignItem> &GetItems() const { return m_items; }
    // The texts the screen's menu adds to the diary when it starts (C_MenuState::Start).
    const std::vector<int> &GetDiaryTexts() const { return m_diaryTexts; }

private:
    friend class C_Campaign;

    int m_kind = -1;                                    // +0x10 1 .. 6, see above
    bool m_backAcross = false;                          // +0x14
    bool m_unknown15 = false;                           // +0x15
    std::vector<S_CampaignItem> m_items;                // +0x18
    std::vector<int> m_diaryTexts;                      // +0x28 chunk 0x19: text ids the menu adds to the diary
};

// The mission itself (vtable 0x00815b10, type 7; 0x10 bytes): C_GameApp starts the game.
class C_CampaignPlay : public C_CampaignNode {
public:
    C_CampaignPlay() : C_CampaignNode(CAMPAIGN_NODE_PLAY) {}
    ~C_CampaignPlay() override;                         // 0x006b9fd0 (0x006b9ff0)
    bool LoadDefinition(C_chunk &) override { return true; }   // 0x006b9fa0
    bool Load(C_chunk &) override { return true; }             // 0x006b9fb0
    bool Save(C_chunk &) override { return true; }             // 0x006b9fc0
};

// An objective of a mission (vtable 0x00815a48, type 4; 0x18 bytes).
struct S_MissionObjective : public C_CampaignNode {
    S_MissionObjective() : C_CampaignNode(CAMPAIGN_NODE_OBJECTIVE) {}   // 0x006b8c30
    ~S_MissionObjective() override;                     // 0x006b55f0 (0x006b5610)
    // id (text) and status.
    bool LoadDefinition(C_chunk &chunk) override;       // 0x006bae20
    bool Load(C_chunk &chunk) override;                 // 0x006bd810
    // (Resets the status to OBJECTIVE_INACTIVE first.)
    bool Save(C_chunk &chunk) override;                 // 0x006bd620
    bool LoadProgress(C_chunk &chunk) override;         // 0x006beff0
    bool SaveProgress(C_chunk &chunk) override;         // 0x006bee00

    // Sets the status; OBJECTIVE_COMPLETED also marks it rewarded.
    void SetStatus(int newStatus);                      // 0x0069c180

    int status = OBJECTIVE_INACTIVE;                    // +0x10 E_ObjectiveStatus
    bool bonus = false;                                 // +0x14 completing it is worth 1500 experience, not 1000
    bool rewarded = false;                              // +0x15 completed once: the experience was given

protected:
    explicit S_MissionObjective(E_CampaignNodeType type) : C_CampaignNode(type) {}
};

// An objective of a multiplayer map (vtable 0x00815ae8, type 12; 0x30 bytes; the map list's
// OBJECTIVE entries).
struct S_MpObjective : public S_MissionObjective {
    S_MpObjective() : S_MissionObjective(CAMPAIGN_NODE_MP_OBJECTIVE) {}
    ~S_MpObjective() override;                          // 0x006b5fe0 (0x006b6000)

    int team = 0;                                       // +0x18 the attacking side (0 axis, 1 allied)
    int alliedText = 0;                                 // +0x1C the sides' texts
    int axisText = 0;                                   // +0x20
    int points = 0;                                     // +0x24 the score for the team that wins it
    int type = 0;                                       // +0x28 OBJECTIVE_TYPE_*
    int vipUniform = -1;                                // +0x2C the uniform item of the VIP
};

// A node with children and a cursor among them (vtable 0x00815a70, 13 slots; 0x24 bytes).
class C_CampaignContainer : public C_CampaignNode {
public:
    ~C_CampaignContainer() override;                    // 0x006b5920 (0x006b57f0)
    bool Next() override;                               // 0x006b85a0
    bool Prev() override;                               // 0x006b8600
    C_CampaignNode *GetCurrent() override;              // 0x006b8670
    void Reset() override { m_cursor = -1; }            // 0x006b5910
    // Deletes the children.
    virtual void Clear();                               // 0x28 0x006b58c0
    // 0 before the first child, 1 on one, 2 past the last.
    virtual int GetCursorState() const;                 // 0x2C 0x006b86a0
    virtual C_CampaignNode *GetChild(int index) const;  // 0x30 0x006b51c0

    int GetChildCount() const { return static_cast<int>(m_children.size()); }
    // The children of type `type`: their count, the index-th one (`select`: the cursor goes
    // there and the child is reset), the index of the one under the cursor (-1: none).
    int CountChildren(E_CampaignNodeType type) const;   // 0x006b8710 (missions) / 0x006b8800 (operations)
    C_CampaignNode *GetChildOfType(E_CampaignNodeType type, int index, bool select);   // 0x006b8740 / 0x006b8830
    int GetCurrentIndexOfType(E_CampaignNodeType type) const;   // 0x006b87a0 / 0x006b8890

protected:
    explicit C_CampaignContainer(E_CampaignNodeType type) : C_CampaignNode(type) {}   // (0x006bcc00)
    void AddChild(C_CampaignNode *child) { m_children.push_back(child); }
    // The children in the full format (chunks 0x3C operation, 0x32 mission, 0x0A video,
    // 0x14 screen, 0x1E play). In the container's own vtable these four fill the Load / Save /
    // LoadProgress / SaveProgress slots (0x08 .. 0x14); the derived nodes call them directly.
    bool LoadChildren(C_chunk &chunk);                  // 0x006bc8c0
    bool SaveChildren(C_chunk &chunk);                  // 0x006bc5e0
    // The operations' and missions' progress (chunks 0x3C / 0x32, in the children's order).
    bool LoadChildrenProgress(C_chunk &chunk);          // 0x006beb60
    bool SaveChildrenProgress(C_chunk &chunk);          // 0x006be990
    // Chunk 4: the cursor; chunk 5: the children's progress.
    bool LoadCursorProgress(C_chunk &chunk);
    void SaveCursorProgress(C_chunk &chunk);

    std::vector<C_CampaignNode *> m_children;           // +0x10
    int m_cursor = -1;                                  // +0x20

    friend class C_Campaign;
};

// A campaign file's root (vtable 0x00815b60, type 1; 0x24 bytes): the operations, videos and
// screens of the campaign.
class C_CampaignRoot : public C_CampaignContainer {
public:
    C_CampaignRoot() : C_CampaignContainer(CAMPAIGN_NODE_ROOT) {}
    ~C_CampaignRoot() override;                         // 0x006bbbe0 (0x006bbc00)
    bool LoadDefinition(C_chunk &chunk) override;       // 0x006bb3c0
    bool Load(C_chunk &chunk) override;                 // 0x006be6f0
    bool Save(C_chunk &chunk) override;                 // 0x006be590
    bool LoadProgress(C_chunk &chunk) override;         // 0x006bfc90
    bool SaveProgress(C_chunk &chunk) override;         // 0x006bfb30
};

// An operation (vtable 0x00815b94, type 2; 0x28 bytes): its missions, videos and screens.
class C_CampaignOperation : public C_CampaignContainer {
public:
    C_CampaignOperation() : C_CampaignContainer(CAMPAIGN_NODE_OPERATION) {}
    ~C_CampaignOperation() override;                    // 0x006bbf60 (0x006bbf80)
    bool LoadDefinition(C_chunk &chunk) override;       // 0x006bb1b0
    bool Load(C_chunk &chunk) override;                 // 0x006be280
    bool Save(C_chunk &chunk) override;                 // 0x006be090
    bool LoadProgress(C_chunk &chunk) override;         // 0x006bf890
    bool SaveProgress(C_chunk &chunk) override;         // 0x006bf730

    const C_str &GetName() const { return m_name; }

private:
    C_str m_name;                                       // +0x24
};

// A mission (vtable 0x00815a0c, type 3, 15 slots; 0x50 bytes): its objectives and the steps
// around it. C_game plays the one C_Campaign::GetCurrentMission() returns.
class C_MissionDef : public C_CampaignContainer {
public:
    static constexpr int TEXT_COUNT = 4;

    C_MissionDef() : C_MissionDef(CAMPAIGN_NODE_MISSION) {}   // 0x006b5160
    ~C_MissionDef() override;                           // 0x006b5630 (0x006b5650)
    bool LoadDefinition(C_chunk &chunk) override;       // 0x006baf00
    bool Load(C_chunk &chunk) override;                 // 0x006bdcb0
    bool Save(C_chunk &chunk) override;                 // 0x006bda60
    bool LoadProgress(C_chunk &chunk) override;         // 0x006bf440
    bool SaveProgress(C_chunk &chunk) override;         // 0x006bf1e0
    // Also: every objective becomes inactive.
    void Reset() override;                              // 0x006b86d0
    // The objectives and the children.
    void Clear() override;                              // 0x006b51f0
    // Appends a new (inactive) objective.
    virtual void CreateObjective();                     // 0x34 0x006b5440
    virtual void AddObjective(S_MissionObjective *objective);   // 0x38 0x006b5290
    // The children in the definition format (chunks 4 objective, 5 video, 6 screen, 7 play).
    static bool LoadChildrenDefinition(C_chunk *chunk, C_MissionDef *mission);   // 0x006bc320

    // The mission's directory (missions\<name>).
    const char *GetName() const { return m_name; }
    const C_str &GetImage() const { return m_image; }
    int GetText(int index) const { return m_texts[index]; }
    // The objectives; null when `index` is out of range.
    int GetObjectiveCount() const { return static_cast<int>(m_objectives.size()); }
    S_MissionObjective *GetObjective(int index) const;
    // Every objective becomes OBJECTIVE_INACTIVE.
    void DeactivateObjectives();                        // 0x006b86e0
    // Some objective is active and none is still open (the in-game menu's code: game/game_menu.cpp).
    bool AreObjectivesDecided() const;                  // 0x0067fda0
    // (+0x2C; the in-game menu shows its button 6 for it.)
    bool IsTutorial() const { return m_unknown2C; }

protected:
    explicit C_MissionDef(E_CampaignNodeType type);

    C_str m_name;                                       // +0x24
    C_str m_image;                                      // +0x28 the menus' picture, e.g. "e_i_ar1a.bmp"
    bool m_unknown2C = false;                           // +0x2C (set for the tutorial)
    std::vector<S_MissionObjective *> m_objectives;     // +0x30
    int m_texts[TEXT_COUNT] = {-1, -1, -1, -1};         // +0x40 the menus' text ids

    friend class C_Campaign;
};

// The mission of a multiplayer map (vtable 0x00815aa8, type 13, 16 slots; 0x80 bytes): set
// up from the map list's entry (C_Campaign::SetupMultiplayerMission).
class C_MpMissionDef : public C_MissionDef {
public:
    C_MpMissionDef();                                   // 0x006b5940
    ~C_MpMissionDef() override;                         // 0x006b6020 (0x006b5a10)
    // The objectives and the children.
    void Clear() override;                              // 0x006b5bb0
    void CreateObjective() override;                    // 0x006b5e30 (an S_MpObjective)
    virtual void AddMpObjective(S_MissionObjective *objective);   // 0x3C 0x006b5c80

    // The map's objectives are S_MpObjective.
    S_MpObjective *GetMpObjective(int index) const;
    // Team sizes the automatic team balance aims at (the map's BALANCED entry).
    int GetBalanceAxis() const { return m_balanceAxis; }
    int GetBalanceAllied() const { return m_balanceAllied; }
    // The map's name (the info screen shows it).
    const C_str &GetMapName() const { return m_mapName; }
    // The map's allowed presets' names (the in-game menu lists the presets found in it).
    const C_str &GetPresets() const { return m_presets; }
    // The map's allowed items (the in-game menu's stocks).
    const std::vector<S_CampaignItem> &GetItems() const { return m_items; }
    // The map's AIRCRAFTATACK values (the kinds of the air attacks and the artillery the sides
    // call in; an axis caller gets the second, inferred to be the other side's planes).
    int GetAircraftAttackAxis() const { return m_aircraftAttackAxis; }
    int GetAircraftAttackAllied() const { return m_aircraftAttackAllied; }
    // The map's value 4 (not in the shipped mpmaplist: 0): 1 makes the occupation zones be
    // taken in their numbers' order.
    int GetUnknown60() const { return m_unknown60; }
    // The briefing's text id of a cooperative map (the loading screen's "briefing").
    int GetBriefingText() const { return m_briefingText; }

private:
    friend class C_Campaign;

    // The map's AIRCRAFTATACK entry (C_PlayerController::DesignateTarget reads it per side).
    int m_aircraftAttackAxis = -1;                      // +0x50
    int m_aircraftAttackAllied = -1;                    // +0x54
    int m_balanceAllied = -1;                           // +0x58
    int m_balanceAxis = -1;                             // +0x5C
    int m_unknown60 = 0;                                // +0x60
    int m_briefingText = 0;                             // +0x64 the map's value 5 (a cooperative map's briefing)
    C_str m_presets;                                    // +0x68 the map's allowed presets ("Allied gunner" ...)
    C_str m_mapName;                                    // +0x6C
    std::vector<S_CampaignItem> m_items;                // +0x70 the map's allowed items
};

// The campaigns and the mission played (C_GameProfile +0x04, vtable 0x00815a00: Load / Save
// of the profile's saved games; 0xF4 bytes).
class C_Campaign final {
public:
    // The version the .gdt files must have to load completely (else their definitions).
    static constexpr int VERSION = 2;

    C_Campaign() = default;                             // (inline in C_GameProfile's constructor)
    ~C_Campaign();                                      // 0x006b6420
    C_Campaign(const C_Campaign &) = delete;
    C_Campaign &operator=(const C_Campaign &) = delete;

    // The progress (chunks 0x50 .. 0x54).
    virtual bool LoadGame(C_chunk &chunk);              // 0x00 0x006bff30
    virtual bool SaveGame(C_chunk &chunk);              // 0x04 0x006c02f0

    // GameData\Gamedata%02d.gdt, from 00 on until one does not open.
    bool LoadFiles();                                   // 0x006bb860
    // Deletes the campaigns.
    void Clear();                                       // 0x006b8c60

    // --- the flow -------------------------------------------------------------------------
    // Moves the current campaign's cursor on to the next operation / mission (single mission:
    // resets it).
    bool AdvanceToMission();                            // 0x006b8d40
    // The next video, screen or mission step, null at the end.
    C_CampaignNode *NextStep();                         // 0x006b8dc0
    // Back from a screen to the previous one (only within the mission unless the screen
    // allows more), null if there is none.
    C_CampaignNode *PreviousScreen();                   // 0x006b8e40
    // The mission to play: the single mission, the multiplayer map's mission, else the
    // current campaign's.
    C_MissionDef *GetCurrentMission();                  // 0x006b94b0
    // A standalone mission of campaign `campaign`: its objectives, briefings, the equipment
    // of it and the earlier missions of its operation.
    bool SetupSingleMission(int campaign, int missionNumber);   // 0x006b99a0
    // The multiplayer session's map.
    bool SetupMultiplayerMission();                     // 0x006b8a20

    // --- queries (the menus') -------------------------------------------------------------
    int GetCurrentIndex() const { return m_current; }   // +0xE8 the campaign played
    void SetCurrentIndex(int index) { m_current = index; }
    int GetCount() const { return static_cast<int>(m_campaigns.size()); }
    // Number of the current mission in campaign `campaign` (-1: none).
    int GetMissionNumber(int campaign);                 // 0x006b91e0
    int GetOperationIndex(int campaign);                // 0x006b92b0
    int CountOperations(int campaign);                  // 0x006b9320
    // Missions of all operations of campaign `campaign`.
    int CountMissions(int campaign);                    // 0x006b9350
    int CountOperationMissions(int campaign, int operation);   // 0x006b93c0
    int GetOperationId(int campaign, int operation);    // 0x006b8f60
    const C_str &GetOperationName(int campaign, int operation);   // 0x006b8fa0
    int GetMissionId(int campaign, int missionNumber);  // 0x006b9040
    const C_str &GetMissionImage(int campaign, int missionNumber);   // 0x006b90e0
    int GetMissionText(int campaign, int missionNumber, int index);  // 0x006b9420
    // An objective's status (1-based campaign, operation and mission; 0-based objective);
    // -1 when there is no such mission, OBJECTIVE_INACTIVE for no such objective.
    int GetObjectiveStatus(int campaign, int operation, int mission, int objective);   // 0x006b8ed0

private:
    // The children of a root / an operation in the definition format (root: chunks 2
    // operation, 5 video, 6 screen; operation: 3 mission, 5 video, 6 screen); false without
    // a chunk or a node.
    bool LoadRootDefinition(C_chunk *chunk, C_CampaignRoot *root);   // 0x006bbcd0
    bool LoadOperationDefinition(C_chunk *chunk, C_CampaignOperation *operation);   // 0x006bc060
    // Mission number `missionNumber` of campaign `campaign` <-> operation / mission index.
    bool FindMission(int campaign, int missionNumber, int *operation, int *mission);   // 0x006b8980
    bool GetMissionNumberOf(int campaign, int *number, int operation, int mission);   // 0x006b88f0
    // The campaign the flow walks (single mission: m_singleMission).
    C_CampaignContainer *GetFlowNode();
    // Appends the equipment of missions `mission` down to 0 of `operation` to `screen`.
    static bool CollectEquipment(C_CampaignScreen *screen, C_CampaignOperation *operation,
                                 int mission);          // 0x006b96e0

    std::vector<C_CampaignRoot *> m_campaigns;          // +0x04
    C_MpMissionDef m_mpMission;                         // +0x14
    C_MissionDef m_singleMission;                       // +0x94
    int m_singleMissionNumber = -1;                     // +0xE4 the mission a single mission copies
    int m_current = 0;                                  // +0xE8
    int m_version = VERSION;                            // +0xEC
    bool m_unknownF0 = false;                           // +0xF0 cleared by Clear(), not read
};

// Reads the remainder of the current chunk (at most 999 chars) into `out` and leaves the
// chunk (C_chunk 0x00415d00).
void ReadChunkString(C_chunk &chunk, C_str &out);
