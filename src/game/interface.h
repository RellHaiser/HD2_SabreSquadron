// The game interface (C_game +0x58; the original's 0x00493680..0x004c11d0, game/interface*.cpp): the
// HUD's panels in the "gameinterface" model, drawn in the interface's own scene with an orthogonal
// camera over the game. The interface itself (interface.cpp) creates the scene and the panels,
// passes them the frame's updates and the keys, picks the actor under the crosshair and keeps the
// lower panel's size; the compass (interface_compass.cpp), the message log
// (interface_messages.cpp) and the mission information / scores (interface_info.cpp) are ported
// with it. The other panels are declared here until their parts of the port.
#pragma once

#include <cstdint>
#include <vector>

#include "game/text_box.h"
#include "ls3d/math.h"

class C_Actor;
class C_Checkpoints;
class C_chunk;
class C_Human;
class C_Interface;
class I3D_frame;
class I3D_model;
class I3D_scene;
class I3D_material;
class I3D_sound;
class I3D_texture;
class IImage;
class ISND_stream;
class C_Item;
class C_ItemContainer;
class C_ItemDef;
struct S_HumanOrder;
struct S_NetPlayerState;
struct S_Vector4Array;

// C_Interface::HighlightMembers() groups besides the squad commander's.
enum : int {
    HIGHLIGHT_COMMANDED = 3,
    HIGHLIGHT_NONE = 5,
};

// C_Interface::OnActorEvent() events.
enum {
    INTERFACE_ACTOR_DELETED = 1,
    INTERFACE_ACTOR_DROPPED = 2,                        // a carried body was put down
};

// C_Interface::m_flags.
enum : uint32_t {
    INTERFACE_ACTIVE = 0x001,                           // Init
    INTERFACE_MEMBERS = 0x004,                          // squad member panels are shown
    INTERFACE_PANEL_UP = 0x008,                         // the lower panel's three positions
    INTERFACE_PANEL_LOW = 0x010,
    INTERFACE_PANEL_DOWN = 0x020,
    INTERFACE_PANEL_MASK = 0x038,
    INTERFACE_FPV_SAVED = 0x040,                        // m_savedFlags: the first-person view was on
    INTERFACE_SCREEN_1 = 0x080,                         // SetScreen(1) (the network game's spectating)
    INTERFACE_SCREEN_2 = 0x100,                         // SetScreen(2)
};

// ---------------------------------------------------------------------------------------------
// The compass (+0x10854; 0x00493680..0x00494950): the needle swings toward the active soldier's
// view with a spring and a wobble of his steps, the marks around it flash where sounds were heard
// (blue: footsteps, red: the rest), and the radar shows the squad and the enemies they see.

struct S_RadarBlip {                                    // 0x10 bytes
    // A copy of the ally / enemy template (by the human's role), as far from the centre as it.
    S_RadarBlip(C_Human *human, I3D_frame *ally, I3D_frame *enemy);   // 0x004936c0
    ~S_RadarBlip();                                     // 0x00493b10
    S_RadarBlip(const S_RadarBlip &) = delete;
    S_RadarBlip &operator=(const S_RadarBlip &) = delete;
    // Fades in / out and turns the blip to the human's bearing from the active soldier; false
    // when it has faded out.
    bool Update(int timeDelta, C_Human *active);        // 0x00493860
    void FadeOut();                                     // 0x00493af0

    I3D_frame *visual = nullptr;
    C_Human *human = nullptr;                           // +0x04
    float radius = 0.0f;                                // +0x08
    float alpha = 1.0f;                                 // +0x0C the target (0: fading out)
};

class C_Radar {                                         // 0x18 bytes
public:
    explicit C_Radar(I3D_scene *scene);                 // 0x00493b30
    ~C_Radar();                                         // 0x00493bc0
    C_Radar(const C_Radar &) = delete;
    C_Radar &operator=(const C_Radar &) = delete;

    void Update(int timeDelta);                         // 0x00493c60
    // Single player, below the hardest difficulty: a blip for the human.
    void Add(C_Human *human);                           // 0x00493d10
    // The human's blip fades out.
    void Remove(C_Human *human);                        // 0x00493f40
    // The human's blips go at once: it is being deleted (not in the original, which kept them).
    void Forget(const C_Human *human);

private:
    I3D_frame *m_ally = nullptr;                        // "kul_us"
    I3D_frame *m_enemy = nullptr;                       // +0x04 "kul_en"
    std::vector<S_RadarBlip *> m_blips;                 // +0x08
};

class C_Compass {                                       // 0xA4 bytes
public:
    bool Init(I3D_scene *scene);                        // 0x00493f80
    void Done();                                        // 0x00494140
    void Show(bool show);                               // 0x00494170
    void Update(int timeDelta);                         // 0x00494190
    // A sound heard from `dir` (xz): its direction's mark flashes for 500 ms.
    void ShowSound(const S_vector &dir, int chapter);   // 0x004947e0
    C_Radar *GetRadar() const { return m_radar; }

private:
    float UpdateWobble(int timeDelta);                  // 0x00494360
    void UpdateHeading();                               // 0x00494590
    void UpdateSoundMarks(int timeDelta);               // 0x00494750

    float m_heading = 0.0f;                             // the active soldier's view (radians)
    float m_needle = 0.0f;                              // +0x04
    float m_wobble = 0.0f;                              // +0x08 the step's kick, decaying
    float m_needleSpeed = 0.0f;                         // +0x0C
    I3D_frame *m_frame = nullptr;                       // +0x10 "kompas"
    I3D_frame *m_card = nullptr;                        // +0x14 "kmpscvr" (turns with the needle)
    I3D_frame *m_arrow = nullptr;                       // +0x18 "strelka" (the wobble)
    C_Radar *m_radar = nullptr;                         // +0x1C
    I3D_frame *m_soundMarks[8][2] = {};                 // +0x20 [direction][blue, red]
    int m_soundTimes[8][2] = {};                        // +0x60 ms
    bool m_leftFoot = false;                            // +0xA0 the active soldier's leading foot
};

// ---------------------------------------------------------------------------------------------
// The message log (+0x2F90; 0x00494950..0x00496360): the last lines (aged every 2 s, older ones
// higher up), the history to scroll through, and the chat line ("Say: " / "SayTeam: " or a console
// command) drawn with the font's glyphs.

// A pre-transformed vertex of the glyph quads.
struct S_TLVertex {                                     // 0x20 bytes
    float x, y, z, rhw;
    uint32_t color;                                     // +0x10 ARGB
    uint32_t specular;                                  // +0x14 (not set)
    float u, v;                                         // +0x18
};

struct S_LogLine {                                      // 0x18 bytes
    char *text;                                         // owned
    int r, g, b;                                        // +0x04 0..255
    int age;                                            // +0x10 2 s steps since it was added
    int font;                                           // +0x14 (3)
};

class C_MessageLog {                                    // 0xD390 bytes
public:
    static constexpr int MAX_VERTICES = 1680;
    static constexpr int INPUT_SIZE = 256;

    C_MessageLog();                                     // 0x00494980
    ~C_MessageLog();                                    // 0x004949e0
    C_MessageLog(const C_MessageLog &) = delete;
    C_MessageLog &operator=(const C_MessageLog &) = delete;

    void Init(I3D_scene *scene, int maxLines);          // 0x00495f40
    void Done();                                        // 0x004960f0
    void Update(int timeDelta);                         // 0x00496230
    void Render();                                      // 0x00495f00
    // A line in `color` (0..255 components).
    void AddMessage(const S_vector &color, const char *text);   // 0x00494a40
    // 0: the last lines, 1: the history (single player pauses meanwhile).
    void SetMode(int mode);                             // 0x00496120
    int GetMode() const { return m_mode; }
    void StartSay();                                    // 0x004961c0
    void StartSayTeam();                                // 0x004961a0
    // The chat line open (`open`) or closed and cleared.
    void OpenInput(bool open);                          // 0x004961e0
    bool IsInputOpen() const { return m_inputOpen; }

private:
    void AddLine(const char *text, const S_vector &color);   // 0x00494a70
    void HandleKeys();                                  // 0x00494d20
    void Clear();                                       // 0x004950c0
    void AgeLines();                                    // 0x00495150
    // The first character of the chat line to show so that its end fits.
    int GetInputScroll();                               // 0x00495190
    void AddVertex(float x, float y, float u, float v, uint32_t color);   // 0x004952c0
    // Glyph quads of one character from `x`; the advance.
    int AddGlyph(const S_Glyph &glyph, int x, int y, int height, uint32_t color);
    void DrawInput(int x, int y, int height, int right);   // 0x00495360
    void DrawLines();                                   // 0x00495a00
    void Flush(C_Font *font);
    // The area frame's rectangle in pixels.
    void GetAreaRect(float *left, float *top, float *right, float *bottom);   // 0x00495900

    S_vector m_color = S_vector(255.0f, 255.0f, 255.0f);
    int m_mode = 0;                                     // +0x10
    C_FontManager *m_fonts = nullptr;                   // +0x14
    I3D_frame *m_area = nullptr;                        // +0x18 "Text"
    std::vector<S_LogLine> m_lines;                     // +0x1C
    int m_maxLines = 50;                                // +0x2C
    int m_scroll = 0;                                   // +0x30
    int m_repeatKey = 0;                                // +0x34
    int m_repeatTime = 0;                               // +0x38
    int m_repeatDelay = 150;                            // +0x3C
    float m_screenWidth = 0.0f;                         // +0x40
    float m_screenHeight = 0.0f;                        // +0x44
    float m_rect[4] = {};                               // +0x48 left, top (the first line), right, bottom
    float m_historyRect[4] = {};                        // +0x58
    char m_input[INPUT_SIZE] = {};                      // +0x68
    int m_inputScroll = 0;                              // +0x168
    S_TLVertex m_vertices[MAX_VERTICES];                // +0x16C
    int m_vertexCount = 0;                              // +0xD36C
    float m_lineHeight = 0.022f;                        // +0xD370 of the screen height
    int m_unknownD374 = 0;
    int m_unknownD378 = 0;
    int m_ageTime = 0;                                  // +0xD37C
    int m_caretTime = 0;                                // +0xD380
    bool m_unknownD384 = true;
    bool m_inputOpen = false;                           // +0xD385
    bool m_sayAll = false;                              // +0xD386
    bool m_sayTeam = false;                             // +0xD387
};

// ---------------------------------------------------------------------------------------------
// The mission information / scores (+0x6B3A0, "ingame_info"; 0x00496360..0x0049a200): single
// player the mission's objectives and the scripts' hints; multiplayer the players' scores by game
// type, the round's time and limits; redrawn every 500 ms while shown.
class C_InfoScreen {                                    // 0x144 bytes
public:
    static constexpr int STYLE_COUNT = 73;

    // Fix: false (instead of a crash) when "ingame_info" or its "game_info" is missing.
    bool Init(I3D_scene *scene);                        // 0x00496450
    void Done();                                        // 0x00496dc0
    void Update(int timeDelta);                         // 0x00499210
    void Render();                                      // 0x00496e00
    void Show(bool show);                               // 0x00496e40
    bool IsShown() const;
    // The scripts' hints (text ids; INFO_ADDHINT / INFO_CLEARHINTS, saved).
    void AddHint(int textId) { m_hints.push_back(textId); }
    void ClearHints() { m_hints.clear(); }              // 0x00498ec0
    bool SaveHints(C_chunk &chunk) const;               // 0x00498ef0
    bool LoadHints(C_chunk &chunk);                     // 0x00499020

private:
    void Fill();                                        // 0x00496e70
    void FillSinglePlayer();                            // 0x00496ed0
    void FillDeathmatch();                              // 0x004970f0
    void FillOccupation();                              // 0x00497130
    void FillObjectiveGame();                           // 0x00497170
    void FillCoop();                                    // 0x004971c0
    void AddHints();                                    // 0x00496f00
    void AddObjectives();                               // 0x00497210
    void AddCoopPlayers();                              // 0x00497380
    void AddDeathmatchPlayers();                        // 0x004977e0
    // The two teams side by side: the allies left, the axis right.
    void AddTeamPlayers();                              // 0x00497e60
    // A player of the left (0: allies; null: an empty row; a line break follows) or right (1:
    // axis) column (the original's 0x00498430 and 0x00498730, one per column).
    void AddTeamPlayer(S_NetPlayerState *player, bool local, int column);
    void AddServer();                                   // 0x004996b0
    void AddMissionName();                              // 0x00499770
    void AddRoundTime();                                // 0x004997b0
    void AddLimits();                                   // 0x004998c0
    void AddBlankLine();                                // 0x00498e80
    void AddTitle(int textId);                          // 0x00498ea0
    void AddNumber(int value, int style);
    uint8_t Style(int index) const { return static_cast<uint8_t>(m_styles[index]); }

    std::vector<int> m_hints;
    int m_kind = 0;                                     // +0x10 0 single player, 1 + the game type
    I3D_frame *m_frame = nullptr;                       // +0x14 "game_info"
    C_TextBox *m_text = nullptr;                        // +0x18
    int m_styles[STYLE_COUNT] = {};                     // +0x1C
    int m_refreshTime = 0;                              // +0x140
};

// ---------------------------------------------------------------------------------------------
// The inventory (+0x30; 0x0049fef0..0x004a4aa0 and its scroll bars 0x004ada20..0x004ae790,
// interface_inventory.cpp): the soldier's equipment on the left page ("items00"): six slots (the
// backpack, the head, a drop place, the hands, the uniform, the long gun) and two lists of four
// lines (the belt and the backpack's contents). With a searched human his equipment fills the
// right page ("items01"); otherwise that page gives way to the soldier's skills. Items are dragged
// between the places (dropped off the inventory or on a drop place: thrown away), a double click
// sends one across, and the cursor resting on an item shows its description.

class C_InventoryScreen;
class C_MapCursor;

// A scroll bar of the inventory's lists ("scroll13".."scroll16"): an arrow at each end and a
// slider between them, whose frames carry the bar's id | 1 (up), | 2 (down) and | 3 (the slider)
// as their data. It scrolls over `count` positions (the lines past the three shown, see
// C_InventoryScreen::SetScrollCount) and reports the index to its owner.
class C_InventoryScrollBar {                            // 0x94 bytes (vtable 0x008104e8: its destructor)
public:
    C_InventoryScrollBar() = default;                   // 0x004a0270
    ~C_InventoryScrollBar() { delete[] m_slider.rect; } // 0x004a0350
    C_InventoryScrollBar(const C_InventoryScrollBar &) = delete;
    C_InventoryScrollBar &operator=(const C_InventoryScrollBar &) = delete;

    // `id`: 0xF10..0xF40.
    bool Init(C_InventoryScreen *owner, I3D_frame *frame, int id);   // 0x004ae2e0
    // The positions to scroll over (below 2: none, and the bar is disabled); the index is kept
    // within them.
    void SetCount(int count);                           // 0x004ae4d0
    int GetIndex() const { return m_index; }            // 0x004ae570
    // The cursor over the bar's frame data `id` (a part lights; other ids let the part go).
    bool Hover(int id);                                 // 0x004ae6a0
    // (Unhover, Press and Release get the id under the cursor, which they do not use.)
    // The lit part goes back to its normal look.
    bool Unhover(int id);                               // 0x004ae640
    // A click: the lit arrow steps the index.
    bool Press(int id);                                 // 0x004ae6f0
    // The lit slider follows the cursor (`move`: its last motion).
    bool Drag(const S_vector4 &pos, const float *move); // 0x004ae730
    // The slider let go: it snaps to the index.
    bool Release(int id);                               // 0x004ae760

private:
    // An arrow ("barrow up*" / "barrow down*").
    struct S_Button {                                   // 0x20 bytes (vtable 0x008104f0)
        bool Init(C_InventoryScrollBar *owner, I3D_frame *frame, int id);   // 0x004ada70
        // Which of its looks are shown ("normal*", "actived*", "inactived*").
        void Show(bool normal, bool active, bool inactive);   // 0x004ada20
        void SetDisabled(bool disabled);                // 0x004adba0
        bool Activate();                                // 0x004adbc0 (__fastcall)
        bool Deactivate();                              // 0x004adbe0 (__fastcall)
        bool Press();                                   // 0x004adc10 (__fastcall)

        I3D_frame *frame = nullptr;                     // +0x04
        int id = 0;                                     // +0x08
        bool disabled = false;                          // +0x0C
        C_InventoryScrollBar *owner = nullptr;          // +0x10
        I3D_frame *normal = nullptr;                    // +0x14
        I3D_frame *active = nullptr;                    // +0x18
        I3D_frame *inactive = nullptr;                  // +0x1C
    };
    // The slider ("slider*"), moving along its base ("sliderbase*") vertically or horizontally,
    // whichever is longer; its position runs 0..100.
    struct S_Slider {                                   // 0x2C bytes (vtable 0x008104ec)
        bool Init(C_InventoryScrollBar *owner, I3D_frame *frame, int id);   // 0x004adc90
        // Its range from the bounds of the slider and its base.
        void Layout();                                  // 0x004addd0 (__fastcall)
        void Show(bool normal, bool active);            // 0x004adc50
        void SetDisabled(bool disabled);                // 0x004ae290
        bool Activate();                                // 0x004ae230 (__fastcall)
        bool Deactivate();                              // 0x004ae250 (__fastcall)
        void SetPosition(float position);               // 0x004ae0f0
        // Moves with the cursor; true when the position changed.
        bool Follow(const float *pos, const float *move);   // 0x004adf40
        // The position at `coord` along the range; true when it changed.
        bool SetPositionAt(float coord);                // 0x004ae0b0
        // Follow() and the bar told.
        bool Drag(const float *pos, const float *move); // 0x004ae260

        I3D_frame *frame = nullptr;                     // +0x04
        int id = 0;                                     // +0x08
        bool disabled = false;                          // +0x0C
        C_InventoryScrollBar *owner = nullptr;          // +0x10
        I3D_frame *normal = nullptr;                    // +0x14
        I3D_frame *active = nullptr;                    // +0x18
        I3D_frame *base = nullptr;                      // +0x1C "sliderbase*"
        // x0, y0, x1, y1 of the slider's centre, the width and the height (+ a spare float).
        float *rect = nullptr;                          // +0x20 (new float[7])
        bool horizontal = false;                        // +0x24
        bool vertical = false;                          // +0x25
        float position = 0.0f;                          // +0x28
    };

    void SetDisabled(bool disabled);                    // 0x004ae790
    // An arrow clicked (`part` 1 up, 2 down).
    void Step(int part);                                // 0x004ae3b0
    // The slider moved to `position`.
    void OnSliderMoved(float position);                 // 0x004ae470
    // The part ids 1..3.
    static bool IsPart(int part) { return part > 0 && part < 4; }   // 0x004ae620

    I3D_frame *m_frame = nullptr;                       // +0x04
    int m_id = 0;                                       // +0x08
    bool m_disabled = false;                            // +0x0C
    C_InventoryScreen *m_owner = nullptr;               // +0x10
    S_Button m_up;                                      // +0x14
    S_Button m_down;                                    // +0x34
    S_Slider m_slider;                                  // +0x54
    float m_step = 0.0f;                                // +0x80 the slider's position per index
    int m_count = 0;                                    // +0x84
    int m_index = 0;                                    // +0x88
    int m_part = -1;                                    // +0x8C the lit part
    int m_unknown90 = 1;                                // +0x90 (not read)
};

class C_InventoryScreen {                               // +0x30, 0x2E20 bytes
public:
    // (Mode bits.)
    enum : uint32_t {
        MODE_OWN = 0x1,                                 // always set by the interface
        MODE_OTHER = 0x2,                               // a searched human on the right page
        MODE_REFRESH = 0x8,                             // refreshed by the next Update
    };

    C_InventoryScreen() = default;                      // 0x0049fef0
    ~C_InventoryScreen() = default;                     // 0x004a0bc0
    C_InventoryScreen(const C_InventoryScreen &) = delete;
    C_InventoryScreen &operator=(const C_InventoryScreen &) = delete;

    bool Init(I3D_scene *scene, C_MapCursor *cursor);   // 0x004a0ee0
    void Update(int timeDelta);                         // 0x004a4150
    void Render();                                      // 0x004a41e0
    void HandleInput(float *held, float *pressed);      // 0x004a3420
    void Show(bool show);                               // 0x004a3920
    // Opens it on `human` (`mode`: MODE_OWN, | MODE_OTHER with `other`, the searched body).
    void Open(int mode, C_Human *human, C_Actor *other);   // 0x004a4380
    // The pages from the humans' items (drops a drag).
    void Refresh();                                     // 0x004a26d0 (0x004a49f0 jumps to it)
    // Its frame is on.
    bool IsShown() const;

private:
    friend class C_InventoryScrollBar;

    // A slot of a page (one item; its frame data: 200 + the slot on the left page, 210 + on the
    // right one).
    struct S_Slot {                                     // 0x148 bytes
        C_TextBox name;                                 // "text_*"
        I3D_frame *selection = nullptr;                 // +0x9C "sel*"
        I3D_frame *icon = nullptr;                      // +0xA0
        I3D_material *material = nullptr;               // +0xA4 the icon's own
        C_TextBox amount;                               // +0xA8 a weapon's rounds
        C_Item *item = nullptr;                         // +0x144 the item shown
    };
    // A list of a page: four lines of its container's item groups, scrolled by a bar (frame
    // data: 220 / 230 + the line for the belts, 240 / 250 + the line for the backpacks).
    struct S_List {                                     // 0x3E4 bytes
        static constexpr int LINES = 4;

        C_TextBox caption;                              // "hlstrscaptab*" / "bkpckcaptab*"
        I3D_frame *selection = nullptr;                 // +0x9C "selholsters*" / "selbackpack*"
        I3D_frame *icons[LINES] = {};                   // +0xA0 "item00*".. / "item04*"..
        I3D_material *materials[LINES] = {};            // +0xB0
        C_TextBox amounts[LINES];                       // +0xC0
        C_Item *items[LINES] = {};                      // +0x330 each line's first item
        I3D_frame *capacityBar = nullptr;               // +0x340 "hlcpct0x"
        C_TextBox capacity;                             // +0x344 "text_hlstrscap*" / "text_bkpckcap*"
        int scroll = 0;                                 // +0x3E0 the first line's group
    };
    static constexpr int SLOTS = 6;
    static constexpr int SKILLS = 7;
    static constexpr int SCROLL_BARS = 4;

    // The slots of the left (0) or right (1) page from `human`'s containers.
    void ShowSlots(C_Human *human, int page);           // 0x004a1e20
    // A list from container `container` of `human` (the lines from its scroll index).
    void ShowList(C_Human *human, S_List &list, int container);   // 0x004a2010
    // Scroll bar `id` scrolled to `index`.
    void OnScroll(int id, int index);                   // 0x004a2590
    // Scroll bar `id`'s positions from `container`'s item groups; its list follows its index.
    void SetScrollCount(int id, C_ItemContainer *container);   // 0x004a2640
    // The mouse over the scroll bars.
    void HandleScrollBars(float *held, float *pressed); // 0x004a3280
    // A press on frame `id` starts a drag.
    void StartDrag(int id);                             // 0x004a34d0
    void DragSlot(C_Human *human, int page, int slot);  // 0x004a2a40
    void DragLine(C_Human *human, S_List &list, int container, int line);   // 0x004a2bb0
    // The dragged icon shown under the cursor.
    void ShowDragged();                                 // 0x004a2cc0 (__fastcall)
    void MoveDragged();                                 // 0x004a2d60 (__fastcall)
    // The dragged item dropped where the cursor is.
    void Drop();                                        // 0x004a2de0 (__fastcall)
    void CancelDrag();
    void HideSelections();                              // 0x004a31f0 (__fastcall)
    // The mouse button: pressed, held, released; a second press soon after the first: the item
    // under the cursor sent across.
    void OnPress();                                     // 0x004a3790 (__fastcall)
    void OnHold();                                      // 0x004a3870 (__fastcall)
    void OnRelease();                                   // 0x004a38f0 (__fastcall)
    void OnDoubleClick();                               // 0x004a3830 (__fastcall)
    void SendAcross(int id);                            // 0x004a35d0
    // The description of the item under the cursor once it rests there.
    void UpdateDescription(int timeDelta);              // 0x004a39b0
    void UpdateSkills();                                // 0x004a4680 (__fastcall)
    void RenderSkills();                                // 0x004a49a0 (__fastcall)
    // The cursor is over the inventory.
    bool IsCursorInside();                              // 0x004a4630 (__fastcall)
    C_Human *Other() const;

    I3D_scene *m_scene = nullptr;
    I3D_frame *m_frame = nullptr;                       // +0x04 "inventory"
    I3D_frame *m_pages[2] = {};                         // +0x08 "items00", "items01"
    I3D_texture *m_emptyTexture = nullptr;              // +0x10 an empty place's (X_SQUARE.BMP)
    I3D_frame *m_description = nullptr;                 // +0x14 "description"
    int m_hoverTime = 0;                                // +0x18 the cursor's time on m_hovered (ms)
    I3D_frame *m_hovered = nullptr;                     // +0x1C
    C_TextBox m_descriptionText;                        // +0x20
    I3D_frame *m_dragIcon = nullptr;                    // +0xBC "DraggingFrame"
    I3D_material *m_dragMaterial = nullptr;             // +0xC0
    C_Item *m_dragItem = nullptr;                       // +0xC4
    uint8_t m_unusedC8[8] = {};
    S_vector4 m_dragOffset = {};                        // +0xD0 the icon from the cursor (z 0.1)
    int m_dragContainer = 0;                            // +0xE0 ITEM_CONTAINER_*
    C_Human *m_dragHuman = nullptr;                     // +0xE4
    I3D_frame *m_weightBars[2] = {};                    // +0xE8 "allcpct00/01"
    S_Slot m_slots[SLOTS][2];                           // +0xF0 [slot][page]
    S_List m_belts[2];                                  // +0x1050
    S_List m_backpacks[2];                              // +0x1818
    I3D_frame *m_skills = nullptr;                      // +0x1FE0 "skills"
    C_TextBox m_skillsTitle;                            // +0x1FE4 "text_soldier stats"
    C_TextBox m_skillNames[SKILLS];                     // +0x2080 "text_shooting".."text_lockpick"
    I3D_frame *m_skillBars[SKILLS] = {};                // +0x24C4 "shcpct00".."lpcpct00"
    C_TextBox m_skillValues[SKILLS];                    // +0x24E0 "text_shooting01"..
    C_InventoryScrollBar m_scrollBars[SCROLL_BARS];     // +0x2924 the belts, then the backpacks
    uint32_t m_mode = 0;                                // +0x2B74 MODE_*
    C_Human *m_human = nullptr;                         // +0x2B78
    // (Only humans are searched: the roll menu offers the search on humans alone.)
    C_Actor *m_other = nullptr;                         // +0x2B7C
    C_TextBox m_names[2];                               // +0x2B80 "namexx00/01"
    C_TextBox m_weights[2];                             // +0x2CB8 "text_allcpct00/01"
    C_MapCursor *m_cursor = nullptr;                    // +0x2DF0 the interface's
    bool m_pressed = false;                             // +0x2DF4 the button is down
    int m_doubleClickTime = 0;                          // +0x2DF8 ms left for a double click
    S_vector4 m_pressPos = {};                          // +0x2E00 the cursor at the press
    int m_activeBar = 0;                                // +0x2E10 1..4 (0: none) the bar under the cursor
};

class C_Speedometer {                                   // +0x2E50, 0x10 bytes ("spee": a car's gauges)
public:
    bool Init(I3D_scene *scene);                        // 0x004a8d70
    // The needles: km/h (to 180), rpm (to 9000) and the fuel (0..1).
    void Update(float speed, float rpm, float fuel);    // 0x004a8df0
    I3D_frame *GetFrame() const { return m_frame; }

private:
    I3D_frame *m_frame = nullptr;                       // "spee"
    I3D_frame *m_needles[3] = {};                       // +0x04 "speed01..03": speed, rpm, fuel
};

class C_Reticle {                                       // +0x2E60, 0x20 bytes (the scopes' reticles)
public:
    bool Init(I3D_scene *scene);                        // 0x004a8c10
    void Done();                                        // 0x004a8d50
    // A scope's aim mode (C_ScopeComp::GetAimMode): 0..3 a rifle's reticle, 4 the binoculars, 6
    // none (5: nothing changes).
    void SetMode(int mode);                             // 0x004a8cc0

private:
    I3D_frame *m_mask = nullptr;                        // "sniper" (around the reticles)
    I3D_frame *m_reticles[4] = {};                      // +0x04 "arisaka", "k98", "springfield", "lee_enfield"
    I3D_frame *m_binoculars = nullptr;                  // +0x14 "scope"
    uint8_t m_unused18[8] = {};
};

class C_SignalsPanel {                                  // +0x2E80, 0x80 bytes ("signals": the squad commands menu)
public:
    // An entry of a page: its text and its action (a page, a squad command, a command on the
    // target + 100 (SIGNAL_ACTOR_COMMAND), or SIGNAL_*; -1 / -1: an empty line).
    struct S_Entry {
        int textId;
        int action;
    };
    static constexpr int ENTRIES = 10;

    bool Init(C_Interface *ui, I3D_scene *scene);       // 0x004bf2e0
    void Done();                                        // 0x004bf480
    // Open on `target` (the actor under the crosshair) for `member` (null: the whole squad).
    void Open(bool open, C_Actor *target, C_Human *member);   // 0x004bf4a0
    bool IsOpen() const;                                // 0x004c05f0
    void HandleInput(float *held, float *pressed);      // 0x004c0760
    // Closes it without a living selected soldier or squad; the page follows the squad's group.
    void Update(int timeDelta);                         // 0x004c0f70
    void Render();                                      // 0x004c0880
    // The actor under the crosshair changed (its pages rebuilt).
    void SetTarget(C_Actor *actor);                     // 0x004c0ef0
    // The roll menu's target was deleted (`actor`: Fix, the original took none).
    void OnTargetDeleted(C_Actor *actor);               // 0x004c11c0
    void Relayout();                                    // 0x004c10d0

private:
    // The page's entries from its table, enabled by the situation.
    void BuildPage();                                   // 0x004bffe0 (__fastcall)
    // The target's commands on the actor pages (the tail of BuildPage).
    void BuildTargetPage();                             // 0x004bf670 (not a function in Ghidra)
    // The target has commands (single player).
    bool HasTargetCommands() const;                     // 0x004c10f0 (__fastcall)
    // Entry `index`: a command on the target (SIGNAL_DISABLED_COMMAND: shown disabled).
    void SetEntry(int index, int textId, int command);  // 0x004bf640
    // The text: the title, who it is for, the numbered entries.
    void Refresh();                                     // 0x004c0370 (__fastcall)
    // The chosen entry's action.
    void Activate();                                    // 0x004c0600 (__fastcall)
    void SelectNext();                                  // 0x004c06e0 (__fastcall)
    void SelectPrevious();                              // 0x004c0720 (__fastcall)
    // The chosen squad command sent.
    void Issue();                                       // 0x004c0990 (__fastcall)

    C_Interface *m_ui = nullptr;
    I3D_frame *m_frame = nullptr;                       // +0x04
    I3D_scene *m_scene = nullptr;                       // +0x08
    int m_page = 0;                                     // +0x0C SIGNAL_PAGE_*
    S_Entry m_entries[ENTRIES] = {};                    // +0x10
    int m_selected = -1;                                // +0x60
    C_Actor *m_target = nullptr;                        // +0x64
    C_Human *m_member = nullptr;                        // +0x68
    bool m_noGroup = true;                              // +0x6C no member to signal: the disabled page
    C_TextBox *m_text = nullptr;                        // +0x70
    uint8_t m_unused74[0xC] = {};
};

// The roll menu (+0x2F00; 0x004b0750..0x004b30b0, interface_rollmenu.cpp): the numbered actions
// of the selected soldier on the actor under the crosshair (or his vehicle, or the body he
// carries), opened with the right button. The command map has an instance of its own
// (`commandMap`) with the commanded soldier's actions on the actor clicked.
class C_RollMenu {                                      // 0x90 bytes
public:
    // An entry: its action (ROLL_*, the human commands and below) and its text (-1 / -1: an
    // empty line; a text below -10: the name of the human with network id -10 - text).
    struct S_Entry {
        int action;
        int textId;
    };
    static constexpr int ENTRIES = 10;                  // the last: cancel

    bool Init(C_Interface *ui, I3D_scene *scene, bool commandMap);   // 0x004b0750
    void Done();                                        // 0x004b0900
    // The actor under the crosshair (`distance` to it); the menu follows it.
    void SetTarget(C_Actor *actor, float distance);     // 0x004b0920
    // The selected soldier has actions on `actor` at that distance (or on his vehicle or body).
    bool HasActions(C_Actor *actor, float distance);    // 0x004b0f30
    void Open(bool open);                               // 0x004b27b0
    // Back at its place.
    void Prepare();                                     // 0x004b3060
    void HandleInput(float *held, float *pressed);      // 0x004b2dc0
    void Update(int timeDelta);                         // 0x004b2fc0
    void Render();                                      // 0x004b2fa0
    void Relayout();                                    // 0x004b3010
    bool IsOpen() const { return m_open; }
    C_Actor *GetTarget() const { return m_target; }
    // The chosen entry (-1: none) and an entry's action.
    int GetSelectedIndex() const { return m_selected; }
    int GetEntryAction(int index) const { return m_entries[index].action; }
    // (The command map's.) The map's cursor ("kurzor") is over the menu.
    bool IsCursorOver() const;                          // 0x004b2be0 (__fastcall)
    // Entry `index` chosen (false: not an enabled entry).
    bool Select(int index);                             // 0x004b2ca0
    // The entry under the point `pos` (x, y) chosen.
    void SelectAt(const float *pos);                    // 0x004b2ce0
    void MoveTo(const S_vector &pos);                   // 0x004b3030

private:
    // The text: the target's name and the numbered entries.
    void Refresh();                                     // 0x004b0b20 (__fastcall)
    // The entries built, the first enabled one chosen.
    void BuildEntries();                                // 0x004b0ef0 (__fastcall)
    // The command map's version of HasActions (the commanded soldier on m_target).
    bool HasCommandMapActions() const;                  // 0x004b11f0 (__fastcall)
    void SetEntry(int index, int textId, int action);   // 0x004b13a0
    void BuildActions();                                // 0x004b13c0 (__fastcall)
    void BuildCommandMapActions();                      // 0x004b1ee0 (__fastcall)
    // The chosen entry's action; the menu closes.
    void Activate();                                    // 0x004b2830 (__fastcall)
    void SelectPrevious();                              // 0x004b2f00 (__fastcall)
    void SelectNext();                                  // 0x004b2f50 (__fastcall)
    static bool IsEnabled(const S_Entry &entry);

    float m_distance = 0.0f;                            // +0x00 to m_target (0.2: the vehicle / the body)
    I3D_scene *m_scene = nullptr;                       // +0x04
    C_Interface *m_ui = nullptr;                        // +0x08
    I3D_frame *m_frame = nullptr;                       // +0x0C "rollmenu"
    I3D_frame *m_mapCursor = nullptr;                   // +0x10 "kurzor"
    C_TextBox *m_text = nullptr;                        // +0x14
    int m_openTime = 0;                                 // +0x18 400 at Open, counted down (not read)
    uint8_t m_unused1C[4] = {};
    S_vector4 m_home = {};                              // +0x20 the frame's place at Init
    bool m_open = false;                                // +0x30
    bool m_commandMap = false;                          // +0x31
    S_Entry m_entries[ENTRIES] = {};                    // +0x34
    int m_selected = -1;                                // +0x84
    C_Actor *m_target = nullptr;                        // +0x88
    uint8_t m_unused8C[4] = {};
};

class C_MemberPanel {                                   // +0x10320 [6], 0xC0 bytes ("Soldier00..05": a squad
                                                        // member's photo, bars and icons)
public:
    bool Init(I3D_scene *scene, int index, I3D_texture **stateIcons, I3D_texture **stateIcons2);   // 0x004b30e0
    void Done();                                        // 0x004b3840
    // Shows `human` (the photo, the weapon, the bars from full).
    void SetHuman(C_Human *human);                      // 0x004b3c60
    // The selection frame ("select*").
    void SetSelected(bool selected);                    // 0x004b3e70
    void Update();                                      // 0x004b3ec0
    void Render();                                      // 0x004b4760
    // The texts follow their visuals (the lower panel moved).
    void Relayout();                                    // 0x004b47a0
    // The panel ("Soldier%02d"; its frame data is 100 + its slot, see C_MapCursor::GetHoveredId).
    I3D_frame *GetFrame() const { return m_frame; }
    C_Human *GetHuman() const { return m_human; }

private:
    // The name (rank and last name) as text run 0.
    void UpdateName();                                  // 0x004b3890 (__fastcall)
    // The command icon: the member's state (dead: the blank texture).
    void UpdateStatusIcon();                            // 0x004b47d0 (__fastcall)
    // The mind icon: the member's AI mode.
    void UpdateModeIcon();                              // 0x004b4860 (__fastcall)

    I3D_scene *m_scene = nullptr;
    int m_index = 0;                                    // +0x04
    I3D_frame *m_frame = nullptr;                       // +0x08
    I3D_frame *m_select = nullptr;                      // +0x0C "select*"
    I3D_frame *m_activeMark = nullptr;                  // +0x10 "player0*" (the soldier the player controls)
    I3D_frame *m_photo = nullptr;                       // +0x14 "photo*"
    I3D_frame *m_healthBar = nullptr;                   // +0x18 "healthbar*" (scaled to the health lost)
    I3D_frame *m_healthCritical = nullptr;              // +0x1C "healthcr*" (under 50 %)
    I3D_frame *m_healthIcon = nullptr;                  // +0x20 "health0*"
    I3D_frame *m_staminaBar = nullptr;                  // +0x24 "staminabar*"
    I3D_frame *m_disguiseBar = nullptr;                 // +0x28 "disguisebar*"
    I3D_frame *m_visibility = nullptr;                  // +0x2C "visibility*"
    uint8_t m_unused30[4] = {};
    I3D_frame *m_aiMode = nullptr;                      // +0x34 "AImode*" (an AI-led member; may be missing)
    I3D_frame *m_ear = nullptr;                         // +0x38 "ear*" (within earshot or under orders)
    I3D_frame *m_silencer = nullptr;                    // +0x3C "silence*" (carries a silenced weapon)
    I3D_frame *m_medic = nullptr;                       // +0x40 "medic*" (first aid over 40)
    I3D_frame *m_vip = nullptr;                         // +0x44 "mp_vip*" (multiplayer; may be missing)
    I3D_material *m_commandMaterial = nullptr;          // +0x48 "command*"'s own
    I3D_material *m_mindMaterial = nullptr;             // +0x4C "mind*"'s own
    uint8_t m_unused50[4] = {};
    IImage *m_deathImage = nullptr;                     // +0x54 x_fc_death.bmp
    uint8_t m_unused58[4] = {};
    I3D_material *m_photoMaterial = nullptr;            // +0x5C the photo's own
    I3D_texture **m_stateIcons = nullptr;               // +0x60 the interface's
    I3D_texture **m_modeIcons = nullptr;                // +0x64
    I3D_texture *m_blankTexture = nullptr;              // +0x68 "univerz"'s
    int m_statusIcon = -1;                              // +0x6C
    int m_modeIcon = -1;                                // +0x70
    C_Human *m_human = nullptr;                         // +0x74
    int m_unknown78 = 0;                                // +0x78 (1 from SetHuman; not read)
    float m_health = 0.0f;                              // +0x7C the per cent shown
    int m_stamina = 0;                                  // +0x80
    int m_disguise = 0;                                 // +0x84
    bool m_unknown88 = false;                           // +0x88 (not read)
    bool m_selected = false;                            // +0x89
    int m_rank = 0;                                     // +0x8C the one the name shows
    C_TextBox *m_name = nullptr;                        // +0x90 on the panel: the name, the weapon
    C_TextBox *m_visibilityText = nullptr;              // +0x94
    float m_unknown98 = -1.0f;                          // +0x98 (not read)
    uint8_t m_unused9C[4] = {};
    S_vector4 m_photoColor = {1.0f, 1.0f, 1.0f, 0.0f};  // +0xA0 the human's last one
    bool m_alive = false;                               // +0xB0 updated (until the death is shown)
    uint8_t m_unusedB1[0xF] = {};
};

class C_Crosshair {                                     // +0x107A0, 0x40 bytes ("cross": four parts the soldier's
                                                        // accuracy spreads, the name of the actor pointed at)
public:
    bool Init(I3D_scene *scene);                        // 0x004a9740
    void Done();                                        // 0x004a9f00
    // On (if the game profile allows the crosshair) or off.
    void Show(bool show);                               // 0x004a9f10
    // The actor pointed at: its name and the parts' colour (white; friends blue). Items, doors
    // and the like only show their names when `usable`.
    void SetTarget(C_Actor *actor, bool usable);        // 0x004a9f50
    void Update(int timeDelta);                         // 0x004aa840
    void Render();                                      // 0x004aaab0

private:
    void SetColor(const float *color);                  // 0x004aa760

    I3D_frame *m_frame = nullptr;                       // "cross"
    I3D_frame *m_left = nullptr;                        // +0x04 "cross_L"
    I3D_frame *m_right = nullptr;                       // +0x08 "cross_R"
    I3D_frame *m_top = nullptr;                         // +0x0C "cross_T"
    I3D_frame *m_bottom = nullptr;                      // +0x10 "cross_B"
    float m_spread = 0.0f;                              // +0x14 the parts' distance from the centre
    uint8_t m_unused18[8] = {};
    float m_color[4] = {};                              // +0x20 the parts' emissive (w copied, not compared)
    C_Actor *m_target = nullptr;                        // +0x30
    bool m_usable = false;                              // +0x34
    bool m_shown = false;                               // +0x35
    C_TextBox *m_name = nullptr;                        // +0x38 on "cross_name" (else the cross)
    uint8_t m_unused3C[4] = {};
};

class C_WeaponPanel {                                   // +0x107E0, 0x3C bytes ("tabl02": the hands' and the
                                                        // shoulder's items, their rounds, the grenades)
public:
    bool Init(I3D_scene *scene);                        // 0x004a9010
    void Done();                                        // 0x004a92f0
    void Update(int timeDelta);                         // 0x004a9530
    void Render();                                      // 0x004a9720
    void Relayout();                                    // 0x004a9730
    I3D_frame *GetFrame() const { return m_frame; }

private:
    // The item in the hands / on the shoulder (null: the empty icon); its picture.
    void ShowHandsItem(C_ItemDef *def);                 // 0x004a9320
    void ShowShoulderItem(C_ItemDef *def);              // 0x004a93e0
    // "rounds/ammo" of the hands' (1) or the shoulder's (2) weapon.
    void ShowRounds(int rounds, int ammo, int which);   // 0x004a9420

    I3D_scene *m_scene = nullptr;
    I3D_frame *m_frame = nullptr;                       // +0x04 "tabl02"
    I3D_frame *m_handsIcon = nullptr;                   // +0x08 "i_hands"
    I3D_frame *m_shoulderIcon = nullptr;                // +0x0C "i_shoulder"
    I3D_texture *m_emptyTexture = nullptr;              // +0x10 the icons' "X_SQUARE.BMP" (a reference)
    I3D_material *m_handsMaterial = nullptr;            // +0x14 the icons' own materials
    I3D_material *m_shoulderMaterial = nullptr;         // +0x18
    C_ItemDef *m_handsDef = nullptr;                    // +0x1C shown
    C_ItemDef *m_shoulderDef = nullptr;                 // +0x20
    C_TextBox *m_text = nullptr;                        // +0x24 runs: the name, the two weapons' rounds, the grenades
    int m_handsRounds = -1;                             // +0x28 shown
    int m_shoulderRounds = -1;                          // +0x2C
    int m_handsAmmo = -1;                               // +0x30
    int m_shoulderAmmo = -1;                            // +0x34
    int m_thrownItems = -1;                             // +0x38
};

class C_StancePanel {                                   // +0x1081C, 0x38 bytes ("tabl01": the stance, the pace, the
                                                        // team, the scripts' timer)
public:
    bool Init(I3D_scene *scene);                        // 0x004aaac0
    void Done();                                        // 0x004aad10
    void Update(int timeDelta);                         // 0x004aad30
    void Render();                                      // 0x004aad00
    void Relayout();                                    // 0x004aad20
    I3D_frame *GetFrame() const { return m_frame; }
    // The scripts' timer (ms; shown counting down; saved).
    int &Timer() { return m_timer; }

private:
    I3D_scene *m_scene = nullptr;
    I3D_frame *m_frame = nullptr;                       // +0x04 "tabl01"
    I3D_frame *m_kneel = nullptr;                       // +0x08 "pkneel" (stance flag 2)
    I3D_frame *m_prone = nullptr;                       // +0x0C "play" (stance flag 1)
    I3D_frame *m_walk = nullptr;                        // +0x10 "mwalk" (the paces add up: walk,
    I3D_frame *m_run = nullptr;                         // +0x14 "mrun"     run, sprint)
    I3D_frame *m_sprint = nullptr;                      // +0x18 "msprint"
    I3D_frame *m_axis = nullptr;                        // +0x1C "mpaxis"
    I3D_frame *m_allied = nullptr;                      // +0x20 "mpallied"
    C_TextBox *m_text = nullptr;                        // +0x24 the timer
    uint32_t m_stance = 0;                              // +0x28 C_Human::GetStanceFlags shown
    uint32_t m_state = 0;                               // +0x2C GetStateFlags shown
    int m_team = -1;                                    // +0x30
    int m_timer = 0;                                    // +0x34
};

class C_MapScreen {                                     // +0x108F8, 0x210 bytes (the map, also the respawn screen)
public:
    // Loads missions\<mission>\map into the interface scene (false without it).
    bool Init(I3D_scene *scene, C_MapCursor *cursor);   // 0x004a4ae0
    void Done();                                        // 0x004a4f60
    // (`commandMap`: shown over the command map; the camera's marker.)
    void Show(bool show, bool commandMap);              // 0x004a5630
    void Update(int timeDelta);                         // 0x004a6fa0
    void Render();                                      // 0x004a6fe0
    // A click picks the spawn zone (team games).
    void HandleInput(float *held, float *pressed);      // 0x004a7010
    I3D_frame *GetFrame() const { return m_frame; }     // null before Init

private:
    static constexpr int MARKERS = 32;

    // A squad member's / team player's marker and his chat line on the map.
    struct S_Marker {                                   // 0x0C bytes
        I3D_frame *visual;
        C_TextBox *text;                                // +0x04
        bool shown;                                     // +0x08 the chat line is drawn
    };
    struct S_Zone;                                      // the spawn zones (0x18 bytes)
    struct S_Objective;                                 // multiplayer objectives (0x1C bytes)
    struct S_SpObjective;                               // single player objectives (0x18 bytes)

    void CreateMarkers();                               // 0x004a4fd0 (__fastcall)
    void DestroyMarkers();                              // 0x004a5190 (__fastcall)
    // The enemies' / pick-ups' markers (copies of their templates) to `count`.
    void ResizeEnemyMarkers(size_t count);              // 0x004a51d0
    void ResizeItemMarkers(size_t count);               // 0x004a5400
    void UpdateVip();                                   // 0x004a5690 (__fastcall)
    void UpdateItemMarkers();                           // 0x004a5970 (__fastcall)
    // (The three updates get the frame time, which they do not use.)
    void UpdateSinglePlayer(int timeDelta);             // 0x004a5b50
    void UpdateTeams(int timeDelta);                    // 0x004a65b0
    void UpdateDeathmatch(int timeDelta);               // 0x004a6de0
    void SelectSpawnZone();                             // 0x004a7050 (__fastcall)
    // The zones' sides; the selection marker on the squad soldier's zone.
    void ShowSelectedZone();                            // 0x004a7180 (__fastcall)
    void CreateZones();                                 // 0x004a72c0 (__fastcall)
    void DestroyZones();                                // 0x004a74c0 (__fastcall)
    void CreateObjectives(I3D_scene *scene);            // 0x004a7520
    // (`scene` is only passed on to S_SpObjective::Init, which does not use it.)
    void CreateSpObjectives(I3D_scene *scene);          // 0x004a7680
    void DestroyObjectives();                           // 0x004a7800 (__fastcall)
    void DestroySpObjectives();                         // 0x004a7870 (__fastcall)
    void UpdateObjectives();                            // 0x004a78e0 (__fastcall)
    void RenderObjectives();                            // 0x004a7910 (__fastcall)
    void UpdateSpObjectives();                          // 0x004a7940 (__fastcall)
    // A marker at a world point (x, z scaled; in front of the map).
    void Place(I3D_frame *marker, const S_vector &pos) const;

    I3D_scene *m_scene = nullptr;
    I3D_frame *m_frame = nullptr;                       // +0x04 "center" of the map model
    I3D_frame *m_map = nullptr;                         // +0x08 "map*" (the markers' parent)
    I3D_frame *m_selectMarker = nullptr;                // +0x0C "selectzone" (team games)
    I3D_model *m_model = nullptr;                       // +0x10
    C_MapCursor *m_cursor = nullptr;                    // +0x14
    int m_kind = 0;                                     // +0x18 MAP_KIND_*
    int m_markerCount = 0;                              // +0x1C squad / player markers
    float m_scale = 0.0f;                               // +0x20 map units per metre (map*'s "scale")
    bool m_commandMap = false;                          // +0x24
    float m_depth = 1.0f;                               // +0x28 the map's depth in the camera's range
    I3D_frame *m_activeMarker = nullptr;                // +0x2C "allied_active" (the active soldier)
    S_Marker m_markers[MARKERS] = {};                   // +0x30
    I3D_frame *m_vipMarker = nullptr;                   // +0x1B0 "vip"
    I3D_frame *m_cameraMarker = nullptr;                // +0x1B4 "mapcamera" (over the command map)
    I3D_frame *m_itemTemplate = nullptr;                // +0x1B8 "objective" (the pick-ups)
    I3D_frame *m_enemyTemplate = nullptr;               // +0x1BC "enemy"
    std::vector<I3D_frame *> m_enemyMarkers;            // +0x1C0
    std::vector<I3D_frame *> m_itemMarkers;             // +0x1D0
    std::vector<S_Zone *> m_zones;                      // +0x1E0
    std::vector<S_Objective *> m_objectives;            // +0x1F0
    std::vector<S_SpObjective *> m_spObjectives;        // +0x200
};

class C_ProgressBar {                                   // +0x10B08, 0x18 bytes ("prgbrbk": the active soldier's
                                                        // timed action)
public:
    bool Init(I3D_scene *scene);                        // 0x004aaf20
    void Done();                                        // 0x004ab0f0
    void Update(C_Human *human);                        // 0x004ab110
    void Render();                                      // 0x004ab220
    I3D_frame *GetFrame() const { return m_frame; }

private:
    I3D_frame *m_frame = nullptr;                       // "prgbrbk"
    I3D_frame *m_bar = nullptr;                         // +0x04 "prgbr01" (scaled along z)
    int m_unused08 = 0;
    int m_unused0C = 0;
    int m_textId = -1;                                  // +0x10 the action's name shown
    C_TextBox *m_text = nullptr;                        // +0x14 on "progress_name"
};

class C_InfoSelector;

// The command map (+0x10B20; 0x004b4980..0x004bf2b0, interface_command.cpp): the tactical view in
// which the player gives the squad its orders (single player; the game pauses unless "realtime"
// is lit). The "Border" panel holds the buttons, their id as the frame data: the orders (follow,
// the two waits, delete the chosen order, delete all), the stance, pace and AI mode the orders
// ask for, the options menu, the camera's zoom and the realtime switch. On the map the left
// button makes a soldier the commanded one, picks up an order's marker to move it, or drags the
// map; the right button sends the commanded soldier to the ground clicked, deletes the order
// whose marker is clicked, or opens the roll menu's command map instance on the actor clicked.
// Each member's orders are drawn as a line along their paths; the active and the commanded
// soldier and the actor under the cursor are framed by brackets, the enemies seen by boxes.
class C_CommandPanel {                                  // 0x5A7C0 bytes
public:
    bool Init(I3D_scene *scene, C_Interface *ui, I3D_texture **stateIcons, I3D_texture **actionIcons,
              C_InfoSelector *selector, C_MapCursor *cursor);   // 0x004b4980
    void Done();                                        // 0x004b4d40
    // Opens the map (the game pauses, the tactical camera takes over, the humans the squad has
    // not seen fade out) or closes it.
    void Open(bool open);                               // 0x004b5e40
    bool IsOpen() const;                                // 0x004b6180
    void Update(int timeDelta);                         // 0x004b6420
    // The brackets and boxes (in the game scene's pass).
    void Render();                                      // 0x004b6720
    // The roll menu and the hint texts.
    void RenderMarkers();                               // 0x004b7830
    // The order lines (in the game's space).
    void RenderOverlay();                               // 0x004b7780
    void HandleInput(float *held, float *pressed);      // 0x004b79c0
    // The actor under the cursor.
    C_Actor *GetTarget() const { return m_target; }     // +0x1C
    void ClearTarget() { m_target = nullptr; }
    // The squad's order markers (the order is the marker frame's data): one taken from the
    // spare ones (or a new copy of the marker model), put back, and given the order's icons.
    I3D_frame *AcquireOrderMarker(S_HumanOrder *order); // 0x004b9240
    void ReleaseOrderMarker(I3D_frame *marker);         // 0x004b9080
    void UpdateOrderMarker(S_HumanOrder *order, I3D_frame *marker);   // 0x004b9b10

private:
    static constexpr int BUTTONS = 18;                  // ids 1..18
    static constexpr int HINTS = 8;
    static constexpr int ZOOMS = 5;
    static constexpr int MEMBERS = 6;
    static constexpr int PATH_VERTICES = 2048;
    static constexpr int ROUTE_VERTICES = 512;

    // A button of the panel: its frame and the frame shown while it is not lit ("~name").
    struct S_Button {                                   // 0x10 bytes
        // Lit (its frame shown, the "~" one hidden) or not; `click`: the click sound.
        void Light(bool lit, bool click);               // 0x004a8bd0
        bool IsLit() const;

        I3D_frame *frame = nullptr;                     // its frame data: the id
        int id = 0;                                     // +0x04
        uint8_t unused08[4] = {};
        I3D_frame *unlit = nullptr;                     // +0x0C "~name"
    };
    // A vertex of the order lines (a world point, ARGB).
    struct S_LineVertex {                               // 0x18 bytes
        float x, y, z;
        uint32_t color;                                 // +0x0C
        float u, v;                                     // +0x10 (0)
    };
    // A member's order lines.
    struct S_MemberLines {                              // 0xF008 bytes
        S_LineVertex paths[PATH_VERTICES];              // a line strip along the orders' paths
        int pathSegments;                               // +0xC000
        S_LineVertex routes[ROUTE_VERTICES];            // +0xC004 (never filled: the remains of the
        int routeSegments;                              // +0xF004  unreferenced route code)
    };

    void InitButtons();                                 // 0x004b4de0 (__fastcall)
    void InitHints();                                   // 0x004b5790 (__fastcall)
    void DoneHints();                                   // 0x004b59c0 (__fastcall)
    // The stance, pace and AI mode the buttons show: the chosen order's (its unset ones
    // defaulted) or the commanded soldier's.
    void ReadOrderFlags();                              // 0x004b5a20 (__fastcall)
    // All buttons dark while the roll menu is open, or their usual looks.
    void DarkenButtons(bool dark);                      // 0x004b5ad0
    // The order buttons lit unless the last order (of kind `lastKind`) ends the queue.
    void LightOrderButtons(int lastKind);               // 0x004b5b90
    void LightFlagButtons();                            // 0x004b5c10 (__fastcall)
    // The hint texts back to their titles, then hint `textId` in `group` (8: none).
    void ShowHint(int group, int textId);               // 0x004b61a0
    // The hint of the button under the cursor (`timeDelta` is not used).
    void UpdateHint(int timeDelta);                     // 0x004b6280 (__fastcall)
    // The cursor's shape: the camera keys, the wheel, the screen's edges.
    void UpdateCursorMode(const float *held, const float *pressed);   // 0x004b7e50
    // The tactical camera moved by the cursor's shape.
    void MoveCamera(float *held, float *pressed);       // 0x004b81a0
    // A stance, pace or AI mode button: the chosen order takes it (`index`, the button's, is not
    // used).
    void SetFlag(int id, int index);                    // 0x004b8470
    void PressToolButton(int id, int index);            // 0x004b86d0
    void PressOrderButton(int id, int index);           // 0x004b87b0
    void RemoveSelectedOrder();                         // 0x004b8e00 (__fastcall)
    // A new order of the commanded soldier: before the chosen one (follow: at the end).
    void AddOrder(int kind, const S_vector4 &pos, const S_vector4 &data);   // 0x004b8e30
    // The chosen order takes the buttons' stance, pace and AI mode.
    void ApplyFlags();                                  // 0x004b8ee0 (__fastcall)
    // The commanded soldier's order to do `command` on `actor` (the roll menu's entry `entry`).
    void AddActorOrder(int command, C_Actor *actor, int entry);   // 0x004b8ff0
    void AddPath(int member, const float *color, const S_Vector4Array &path);   // 0x004b9d50
    // The member's paths and markers again when its orders changed.
    void UpdateMember(C_Human *member, int index);      // 0x004b9e80
    // The actor and the point under the cursor (`timeDelta` is not used).
    void PickTarget(int timeDelta);                     // 0x004ba070 (__fastcall)
    void Press(int id);                                 // 0x004ba2d0
    // A right click on an info icon: its order deleted.
    void RemoveOrderAt(int id);                         // 0x004ba360
    void OnLeftPress();                                 // 0x004ba390 (__fastcall)
    // A marker carried: the order moves (a waiting order turns towards the point; the input is
    // not used).
    void OnLeftDrag(float *held, float *pressed);       // 0x004ba660
    void OnLeftRelease();                               // 0x004bad50 (__fastcall)
    void OnRightPress();                                // 0x004bad80 (__fastcall)
    // The roll menu opened on `target` near the cursor, or closed.
    void ShowRollMenu(bool open, C_Actor *target);      // 0x004bb0e0
    void OnRightRelease();                              // 0x004bb210 (__fastcall)
    // Brackets at the corners of the actor's box on the screen (`grow`, added by the port for the
    // original's three copies 0x004bb260, 0x004bc300 and 0x004bd3d0: the box widened by that part
    // of the screen; the commanded soldier's -0.005, the target's 0.005).
    void DrawBrackets(C_Actor *actor, const float *color, float grow);   // 0x004bb260 (grow: added by the port)
    // A box around an enemy (its vehicle's once, by its first occupant).
    void DrawBox(C_Actor *actor, const float *color);   // 0x004be670
    void CycleZoom();                                   // 0x004bf130 (__fastcall)
    // The cursor over the map's part of the panel.
    bool IsOnMap(const S_vector4 &pos) const;           // 0x004bf230
    // The ray from the game's camera through the cursor (`dir`: a unit vector).
    void CursorRay(S_vector &origin, S_vector &dir) const;

    I3D_frame *m_frame = nullptr;                       // +0x00 "Border"
    I3D_scene *m_scene = nullptr;                       // +0x04 the interface's
    I3D_scene *m_gameScene = nullptr;                   // +0x08
    C_Interface *m_ui = nullptr;                        // +0x0C
    int m_unknown10 = 0;                                // +0x10 (cleared, not read)
    C_Human *m_commanded = nullptr;                     // +0x14
    C_Human *m_active = nullptr;                        // +0x18
    C_Actor *m_target = nullptr;                        // +0x1C under the cursor
    S_vector4 m_cursorPoint = {};                       // +0x20 the ground under the cursor
    uint8_t m_unused30[0x10] = {};
    C_InfoSelector *m_selector = nullptr;               // +0x40
    C_MapCursor *m_cursor = nullptr;                    // +0x44
    uint32_t m_pace = 0;                                // +0x48 MOVE_PACE_* (-1: none)
    uint32_t m_mode = 0;                                // +0x4C the AI mode (-1: none)
    uint32_t m_stance = 0;                              // +0x50 (-1: none)
    int m_time = 0;                                     // +0x54 the map's time (the turning cursor)
    int m_wheelTime = 0;                                // +0x58 ms the wheel's cursor stays
    bool m_realtime = false;                            // +0x5C the game runs on
    I3D_texture **m_stateIcons = nullptr;               // +0x60 the interface's
    I3D_texture **m_actionIcons = nullptr;              // +0x64
    I3D_texture *m_stanceIcons[3] = {};                 // +0x68 lie, kneel, stand ("stay")
    I3D_texture *m_paceIcons[4] = {};                   // +0x74 slow, walk, run, sprint
    I3D_texture *m_modeIcons[3] = {};                   // +0x84 passive, defensive, aggressive
    int m_zoom = 1;                                     // +0x90
    I3D_frame *m_zoomFrames[ZOOMS] = {};                // +0x94 "TMcamera1x".."TMcamera16x"
    C_TextBox m_hints[HINTS];                           // +0xA8 on "text_regroup".."text_options"
    C_RollMenu m_rollMenu;                              // +0x590 the command map's
    S_Button m_buttons[BUTTONS];                        // +0x620
    int m_flashTime = 0;                                // +0x740 a pressed button lights again
    S_Button *m_flashButton = nullptr;                  // +0x744
    I3D_model *m_markerModel = nullptr;                 // +0x748 "l_aicomm"
    std::vector<I3D_frame *> m_spareMarkers;            // +0x74C
    S_MemberLines m_lines[MEMBERS];                     // +0x75C by the member's index
    uint8_t m_unused5A78C[4] = {};
    S_vector4 m_dragPoint = {};                         // +0x5A790 the ground under the carried marker's grip
    S_vector4 m_dragMarker = {};                        // +0x5A7A0 the carried marker's place
    bool m_draggingMarker = false;                      // +0x5A7B0
    bool m_refreshPaths = false;                        // +0x5A7B1
    C_Checkpoints *m_checkpoints = nullptr;             // +0x5A7B4 (the unreferenced route code's)
    bool m_leftDown = false;                            // +0x5A7B8
    bool m_rightDown = false;                           // +0x5A7B9
    bool m_middleDown = false;                          // +0x5A7BA
    bool m_draggingMap = false;                         // +0x5A7BB
    bool m_unknown5A7BC = false;                        // (cleared, not read)
    bool m_unknown5A7BD = false;
};

class C_InfoSelector {                                  // +0x6B2E0, 0x40 bytes (a soldier's queue of orders on the
                                                        // command map: "info00..09", "infosel")
public:
    bool Init(I3D_scene *scene, I3D_texture **stateIcons, I3D_texture **actionIcons);   // 0x004ab230
    // The icons of `human`'s orders (again when they changed); the chosen one larger, marked.
    void Update(C_Human *human);                        // 0x004ab2e0
    // The chosen order (its human's queue shown).
    void SelectOrder(S_HumanOrder *order);              // 0x004ab610
    void SelectOrderIndex(int index);                   // 0x004ab700
    S_HumanOrder *GetSelectedOrder() const;             // 0x004ab7e0 (__fastcall)
    int GetSelectedIndex() const;                       // 0x004ab820 (__fastcall; -1: none)

private:
    static constexpr int ICONS = 10;

    // The marker in front of icon `index`.
    void MarkIcon(int index);

    C_Human *m_human = nullptr;
    I3D_frame *m_icons[ICONS] = {};                     // +0x04
    I3D_texture **m_stateIcons = nullptr;               // +0x2C the interface's
    I3D_texture **m_actionIcons = nullptr;              // +0x30
    I3D_frame *m_marker = nullptr;                      // +0x34 "infosel"
    uint8_t m_unused38[8] = {};
};

class C_MapCursor {                                     // +0x6B320, 0x80 bytes (the pointer of the maps and the
                                                        // inventory; the mouse moves it)
public:
    bool Init(I3D_scene *ui, I3D_scene *gameScene);     // 0x004ab860
    // Its shape (CURSOR_*; -1: off).
    void SetMode(int mode);                             // 0x004abaf0
    int GetMode() const { return m_mode; }              // 0x004abb70
    // The mouse's motion (controls 0x34 / 0x35) within the screen or the clip rectangle.
    void HandleInput(float *held, float *pressed);      // 0x004abc50
    // The id of the interface visual under it (the member panels': 100 + slot; 0: none).
    int GetHoveredId();                                 // 0x004abdd0
    // Eases its scale towards the target scale.
    void Update(int timeDelta);                         // 0x004abe60
    const S_vector4 &GetPos() const { return m_pos; }   // 0x004ab990
    // The shape shown (the command map spins the turning cursor).
    I3D_frame *GetFrame() const { return m_frame; }
    // The last motion.
    void GetMove(float *move) const;                    // 0x004ab960
    // Its place in pixels.
    void GetScreenPos(int *x, int *y) const;            // 0x004ab9a0
    // Inside `rect` (x0, y0 .. x1 = [4], y1 = [5]; the interface's coordinates).
    bool IsInRect(const float *rect) const;             // 0x004abc00
    void SetClipRect(const float *rect) { m_clip = rect; }
    void SetTargetScale(float scale) { m_targetScale = scale; }
    // Moved over `point` of the game scene (the command map's; clipped to the rectangle).
    void SetFromWorld(const S_vector4 &point);          // 0x004abf50
    // The mouse's motion (`held` INPUT_CURSOR_X / Y) zeroed where it would leave the screen or
    // the clip rectangle (`pressed` is not used).
    void ClampMotion(float *held, float *pressed) const;   // 0x004ac090

private:
    static constexpr int MODES = 16;

    I3D_frame *m_frame = nullptr;                       // the shape shown
    I3D_scene *m_scene = nullptr;                       // +0x04 the interface's
    I3D_scene *m_gameScene = nullptr;                   // +0x08
    int m_mode = -1;                                    // +0x0C
    S_vector4 m_prevPos = {0.5f, -0.375f, -10.0f, 0.0f};    // +0x10
    S_vector4 m_pos = {0.5f, -0.375f, -10.0f, 0.0f};        // +0x20 x 0..1, y 0..-0.75
    I3D_frame *m_shapes[MODES] = {};                    // +0x30 "kurzor", "k_move", "k_rotate", the
                                                        // camera arrows "t_*", "k_movepos", "k_move" twice
    float m_scale = 1.0f;                               // +0x70
    float m_targetScale = 1.0f;                         // +0x74
    const float *m_clip = nullptr;                      // +0x78 null: the whole screen
    uint8_t m_unused7C[4] = {};
};

class C_WeaponSelector {                                // +0x6B4E4, 0x3C bytes ("select_weapon": a list of the
                                                        // selected soldier's weapons and medkits to take in hand)
public:
    bool Init(I3D_scene *scene);                        // 0x004ac440
    // The list freed.
    void Done();                                        // 0x004acdc0
    void Show(bool show);                               // 0x004ace20
    void HandleInput(float *held, float *pressed);      // 0x004acf00
    void Update(int timeDelta);                         // 0x004ad180
    void Render();                                      // 0x004ad140
    I3D_frame *GetFrame() const { return m_frame; }

private:
    struct S_Entry;

    // A new entry at the end of the list, its text on its shade.
    void AddEntry(C_Item *item, int count, int container);
    // The list of the soldier's hands item (or empty hands), long gun and the belt's weapons
    // and medkits; the first one chosen.
    void Build();                                       // 0x004ac530
    // The entries stacked down from the first one's place; the chosen one larger.
    void Layout();                                      // 0x004ac990
    // The texts: rounds / ammo carried, the items of a kind, a medkit's charge / medkits.
    void UpdateAmounts();                               // 0x004acb70

    I3D_scene *m_scene = nullptr;
    I3D_frame *m_frame = nullptr;                       // +0x04 "select_weapon"
    I3D_frame *m_marker = nullptr;                      // +0x08 "selector_weapon"
    I3D_frame *m_entryTemplate = nullptr;               // +0x0C "select_weapon00"
    I3D_frame *m_shadeTemplate = nullptr;               // +0x10 "drkr1"
    std::vector<S_Entry *> m_entries;                   // +0x18
    C_Human *m_human = nullptr;                         // +0x28 the selected soldier
    int m_selected = 0;                                 // +0x2C
    int m_nextRepeat = -1;                              // +0x30 ms until a held key repeats (-1: released)
    int m_prevRepeat = -1;                              // +0x34
    int m_idleTime = 0;                                 // +0x38 the list closes after 2 s untouched
};

class C_InjuryFlash {                                   // +0x6B520, 0x30 bytes ("injury": one of the
                                                        // GameMovie\AnimHit1..7.bik videos over the screen)
public:
    bool Init(I3D_scene *scene);                        // 0x004ad250
    void Done();                                        // 0x004ad310
    void Start(bool on);                                // 0x004ad380
    void Update(int timeDelta);                         // 0x004ad440
    I3D_frame *GetFrame() const { return m_frame; }

private:
    static constexpr int VIDEOS = 7;

    // A random video playing instead of the current one.
    void PickVideo();                                   // 0x004ad350 (__fastcall)

    I3D_frame *m_frame = nullptr;                       // "injury"
    ISND_stream *m_video = nullptr;                     // +0x04 playing
    ISND_stream *m_videos[VIDEOS] = {};                 // +0x08 (null: failed to open)
    int m_time = 0;                                     // +0x24 ms
    float m_alpha = 0.0f;                               // +0x28
    bool m_rising = false;                              // +0x2C fading in
};

class C_BigMessage {                                    // +0x6B550, 8 bytes ("Htext": a message in the middle)
public:
    bool Init(I3D_scene *scene);                        // 0x004ad4e0
    void Done();                                        // 0x004ad640
    // "<allies>  a : b  <axis>" under the text.
    void ShowRoundResult(bool show, int textId, int axisWins, int alliedWins);   // 0x004ad650
    void ShowCountdown(bool show, int textId, int ms);  // 0x004ad790
    void Show(bool show, int textId);                   // 0x004ad880
    void Render();                                      // 0x004ad8d0
    bool IsShown() const;                               // its frame is on

private:
    I3D_frame *m_frame = nullptr;
    C_TextBox *m_text = nullptr;                        // +0x04
};

class C_PhotoFlash {                                    // +0x6B558, 0xC bytes ("camera": a photo being taken)
public:
    bool Init(I3D_scene *scene);                        // 0x004af4a0
    // The shutter at 1 s left, the screen black 950..800 ms.
    void Update(int timeDelta);                         // 0x004af4f0
    // On for 2 s.
    void Start(bool on);                                // 0x004af600
    I3D_frame *GetFrame() const { return m_frame; }

private:
    I3D_frame *m_frame = nullptr;                       // "camera"
    int m_timeLeft = 0;                                 // +0x04 ms
    bool m_soundPending = false;                        // +0x08
};

class C_ObjectiveIcons {                                // +0x6B564, 0xC4 bytes ("mpobjectives": who holds the 16
                                                        // objectives)
public:
    bool Init(I3D_scene *scene);                        // 0x004ad8f0
    // State 1 allied, 0 axis, 2 neutral, others none.
    void SetIcon(int index, int state);                 // 0x004ad9b0
    I3D_frame *GetFrame() const { return m_frame; }

private:
    static constexpr int OBJECTIVES = 16;

    I3D_frame *m_frame = nullptr;                       // "mpobjectives" (null in other maps)
    I3D_frame *m_icons[OBJECTIVES][3] = {};             // +0x04 "mpallied_obj%02d", "mpaxis_obj", "mpneutral_obj"
};

class C_ScoreTable {                                    // +0x6B628, 0xC bytes ("mp_scoretable": the teams' scores
                                                        // and the clocks of the team games)
public:
    bool Init(I3D_scene *scene);                        // 0x004ae7c0
    void Done();                                        // 0x004aed00
    void Update(int timeDelta);                         // 0x004aed20
    void Render();                                      // 0x004af2b0
    I3D_frame *GetFrame() const { return m_frame; }
    bool IsUsed() const { return m_gameType != -1; }

private:
    I3D_frame *m_frame = nullptr;
    C_TextBox *m_text = nullptr;                        // +0x04
    int m_gameType = -1;                                // +0x08 E_MpGameType (-1: single player, deathmatch)
};

class C_VotingPanel {                                   // +0x6B634, 8 bytes ("voting"; never filled)
public:
    bool Init(I3D_scene *scene);                        // 0x004af2e0
    void Done();                                        // 0x004af450
    void Render();                                      // 0x004af480
    I3D_frame *GetFrame() const { return m_frame; }

private:
    I3D_frame *m_frame = nullptr;
    C_TextBox *m_text = nullptr;                        // +0x04
};

class C_ExplosiveTimer {                                // +0x6B63C, 0x74 bytes ("explosivetimer": the time left of
                                                        // the explosives placed, soonest first)
public:
    bool Init(I3D_scene *scene);                        // 0x004af630
    void Done();                                        // 0x004af8b0
    // Its place (x, y, z; the interface moves it out of the way of the open panels).
    void SetPos(const float *pos);                      // 0x004af900
    void Update(int timeDelta);                         // 0x004af980
    void Render();                                      // 0x004afbd0
    I3D_frame *GetFrame() const { return m_frame; }

private:
    static constexpr int TIMERS = 10;

    I3D_frame *m_frame = nullptr;                       // "explosivetimer"
    I3D_frame *m_timers[TIMERS] = {};                   // +0x04 "timer-explosive%02d"
    C_TextBox *m_texts[TIMERS] = {};                    // +0x2C
    // (+0x54..+0x5C: the original's buffer of (time, index) pairs for its radix sort.)
    I3D_scene *m_scene = nullptr;                       // +0x60
    std::vector<C_Actor *> m_explosives;                // +0x64 the explosive places of the mission
};

class C_CarnageStats {                                  // +0x6B6B0, 8 bytes ("carnage_stats": the carnage mode's
                                                        // enemies left)
public:
    bool Init(I3D_scene *scene);                        // 0x004afc00
    void Done();                                        // 0x004afda0
    void Update(int timeDelta);                         // 0x004afdb0
    void Render();                                      // 0x004afe30
    I3D_frame *GetFrame() const { return m_frame; }

private:
    I3D_frame *m_frame = nullptr;
    C_TextBox *m_text = nullptr;                        // +0x04
};

class C_PauseText {                                     // +0x6B6B8, 0xC bytes (text 0xA46 over "injury" while paused)
public:
    bool Init(I3D_scene *scene);                        // 0x004afe50
    void Done();                                        // 0x004affc0
    void Render();                                      // 0x004afff0
    bool IsShown() const { return m_shown; }
    void SetShown(bool shown) { m_shown = shown; }

private:
    C_TextBox *m_text = nullptr;
    bool m_shown = false;                               // +0x04
    int m_unused08 = 0;
};

class C_CountdownText {                                 // +0x6B6C4, 0xC bytes (a text over "injury" for some ms)
public:
    bool Init(I3D_scene *scene);                        // 0x004b0000
    void Done();                                        // 0x004b0180
    // `ms` -1 / 0: until switched off.
    void Show(bool show, int textId, int ms);           // 0x004b01a0
    void Update(int timeDelta);                         // 0x004b01e0
    void Render();                                      // 0x004b0210
    bool IsShown() const { return m_shown; }

private:
    C_TextBox *m_text = nullptr;
    bool m_shown = false;                               // +0x04
    int m_timeLeft = 0;                                 // +0x08
};

// ---------------------------------------------------------------------------------------------

class alignas(16) C_Interface {                         // C_game +0x58 (0x6B900 bytes)
public:
    static constexpr int MEMBER_PANELS = 6;

    C_Interface();                                      // 0x0049a420
    ~C_Interface();                                     // 0x0049a750
    C_Interface(const C_Interface &) = delete;
    C_Interface &operator=(const C_Interface &) = delete;

    // Mission start / end / restart (client side).
    bool Init();                                        // 0x0049a940
    bool Done();                                        // 0x0049b1b0
    void Reset();                                       // 0x0049f5e0
    bool IsActive() const { return (m_flags & INTERFACE_ACTIVE) != 0; }   // 0x0049b7d0
    void Update(int timeDelta);                         // 0x0049bd50
    // The game's render callback: the panels, and the command map's overlay.
    void Render();                                      // 0x0049b9f0
    void RenderOverlay();                               // 0x0049b9b0
    void HandleInput(float *held, float *pressed);      // 0x0049c3c0
    // Drops the camera / map keys (controls 0x47..0x52) while the roll menu or the signals are
    // open.
    void FilterInput(float *held, float *pressed);      // 0x0049fc10
    // A line of the message log (`color` 0..255 components).
    void AddMessage(const S_vector &color, const char *text);   // 0x0049d2e0
    // A panel takes the keys (the player's soldier gets none).
    bool IsCapturingInput() const;                      // 0x0049d300
    // The big message in the middle of the screen (-1: off), with the teams' scores / a
    // countdown.
    void ShowMessage(int textId);                       // 0x0049de30
    void ShowRoundResult(int textId, int axisWins, int alliedWins);   // 0x0049ddf0
    void ShowCountdown(int textId, int ms);             // 0x0049de10
    // A countdown with text `textId` (spawn protection; -1: off).
    void ShowTimer(int textId, int ms);                 // 0x0049de50
    // The mission information / scores (also while the scores key is held).
    void ShowScores(bool show);                         // 0x0049f020
    // The respawn screen (the map).
    void ShowRespawnScreen(bool show);                  // 0x0049f040
    bool IsRespawnScreenShown() const;                  // 0x0049f090
    // The icon of objective `index` (0-based): state 0 / 1 the team that has it, 2 open.
    void SetObjectiveIcon(int index, int state);        // 0x0049f0c0
    C_Human *GetShownSquadMember() const;               // 0x0049f0e0
    // The squad member panels (slots 0..5) and the radar's blips.
    bool AddSquadMember(C_Human *human, int slot);      // 0x0049b7e0
    bool RemoveSquadMember(C_Human *human, int slot);   // 0x0049b870
    // Marks `enemy` on the radar (`show`) or takes it off.
    void ShowEnemy(C_Human *enemy, bool show);          // 0x0049b980
    void RemoveFromMap(C_Human *human);                 // 0x0049b960
    bool SaveGame(C_chunk &chunk);                      // 0x0049f600
    bool LoadGame(C_chunk &chunk);                      // 0x0049f8d0
    // The command map (single player; while the game's tactical mode allows it).
    bool IsCommandMapOpen() const { return m_commandPanel.IsOpen(); }
    void OpenCommandMap(bool open);                     // 0x0049f410
    // The stance panel's timer (the scripts' TIMER commands).
    void SetTimer(int ms);                              // 0x0049dde0
    // The scripts' hints of the mission information.
    void ClearHints() { m_info.ClearHints(); }
    void AddHint(int textId) { m_info.AddHint(textId); }
    // The photo flash for 2 s (a photo taken).
    void ShowPhotoFlash();                              // 0x0049f5d0
    // Actor events for the HUD (client side; INTERFACE_ACTOR_*).
    void OnActorEvent(int event, C_Actor *actor);       // 0x0049ef70
    // Redraws the inventory screen if it is open.
    void RefreshInventory();                            // 0x0049fbf0
    // 0 the normal screen, 1 / 2 the network game's (spectating, waiting).
    void SetScreen(int screen);                         // 0x0049f250
    // The first member panel shows `human` (null: the selected member).
    void ShowSquadMember(C_Human *human);               // 0x0049f100
    // The local player's damage flash.
    void FlashDamage();                                 // 0x0049f0b0
    // The compass' mark of a sound heard from `dir` (xz, normalized).
    void ShowSoundDirection(const S_vector &dir, int chapter);   // 0x0049de80
    // The crosshair of the player's view mode (C_PlayerController::VIEW_*; the scope: the
    // weapon's reticle).
    void SetViewMode(int viewMode);                     // 0x0049dc60
    // The command map's order markers (C_CommandPanel).
    I3D_frame *AcquireOrderMarker(S_HumanOrder *order) { return m_commandPanel.AcquireOrderMarker(order); }
    void ReleaseOrderMarker(I3D_frame *marker) { m_commandPanel.ReleaseOrderMarker(marker); }
    void UpdateOrderMarker(S_HumanOrder *order, I3D_frame *marker) { m_commandPanel.UpdateOrderMarker(order, marker); }

    // The member panels of `group` (the squad commander's groups; 3 the commanded member, 5
    // none) highlighted, or only `human` of it (the command map and the signals).
    void HighlightMembers(C_Human *human, int group);   // 0x0049d340
    // The inventory screen (single player: the active soldier's, with `container` also its
    // contents; multiplayer: the selected member's, co-op containers asked from the server).
    void ShowInventory(bool show, C_Actor *container);  // 0x0049de90
    // The compass shows while the active soldier carries one (single player, nothing open).
    void UpdateCompassShown();                          // 0x0049f1c0

private:
    bool InitPanels();                                  // 0x0049b280
    void DonePanels();                                  // 0x0049b490
    bool InitDownPanel();                               // 0x0049b5e0
    void ClearPicks();                                  // 0x0049b610
    void LoadStateIcons();                              // 0x0049b620
    void LoadActionIcons();                             // 0x0049b730
    bool RemoveSquadMembers();                          // 0x0049b900
    // Any of the soldier's control keys gives the player's controller back to the active
    // member.
    void ReturnControlOnInput(float *held, float *pressed);   // 0x0049c110
    void SwitchToMember(int index);                     // 0x0049c200
    // The menu key closes the open panels; true when one was.
    bool CloseAll();                                    // 0x0049c2b0
    // The crosshair's visibility (hidden in the scope, seat weapons' own sights, the maps).
    void UpdateCrosshair();                             // 0x0049d530
    // The actor under the crosshair (every 100 ms in single player) and whether it can be used.
    void UpdatePick(int timeDelta);                     // 0x0049d6e0
    // The map cursor's clicks on member `id` (100 + slot): left the control, right the command;
    // a double click on the command map moves its camera to the commanded member.
    void OnMemberClicked(int id);                       // 0x0049dce0
    void OnMemberClicked2(int id);                      // 0x0049dd60
    // The lower panel's next position: up, low, down.
    void CycleDownPanel();                              // 0x0049dfe0
    void RefreshDownPanel();                            // 0x0049e1b0
    // Corner marks around the screen box of the actor picked (the signals) or usable.
    void DrawPickOutline();                             // 0x0049e260

    I3D_scene *m_scene = nullptr;
    I3D_frame *m_camera = nullptr;                      // +0x04
    I3D_model *m_model = nullptr;                       // +0x08 "gameinterface"
    uint32_t m_flags = 0;                               // +0x0C INTERFACE_*
    int m_pickTime = 0;                                 // +0x10 ms toward the next pick (single player)
    int m_pickMisses = 0;                               // +0x14
    uint32_t m_savedFlags = 0;                          // +0x18 the panel's position and the view before the command map
    bool m_infoForced = false;                          // +0x20 ShowScores
    bool m_respawnForced = false;                       // +0x21 ShowRespawnScreen
    I3D_frame *m_downPanel = nullptr;                   // +0x24 "down_panel"
    C_InventoryScreen m_inventory;                      // +0x30
    C_Speedometer m_speedometer;                        // +0x2E50
    C_Reticle m_reticle;                                // +0x2E60
    C_SignalsPanel m_signals;                           // +0x2E80
    C_RollMenu m_rollMenu;                              // +0x2F00
    C_MessageLog m_messages;                            // +0x2F90
    C_MemberPanel m_members[MEMBER_PANELS];             // +0x10320
    C_Crosshair m_crosshair;                            // +0x107A0
    C_WeaponPanel m_weaponPanel;                        // +0x107E0
    C_StancePanel m_stancePanel;                        // +0x1081C
    C_Compass m_compass;                                // +0x10854
    C_MapScreen m_map;                                  // +0x108F8
    C_ProgressBar m_progress;                           // +0x10B08
    C_CommandPanel m_commandPanel;                      // +0x10B20
    C_InfoSelector m_infoSelector;                      // +0x6B2E0
    C_MapCursor m_cursor;                               // +0x6B320
    C_InfoScreen m_info;                                // +0x6B3A0
    C_WeaponSelector m_weaponSelector;                  // +0x6B4E4
    C_InjuryFlash m_injuryFlash;                        // +0x6B520
    C_BigMessage m_bigMessage;                          // +0x6B550
    C_PhotoFlash m_photoFlash;                          // +0x6B558
    C_ObjectiveIcons m_objectives;                      // +0x6B564
    C_ScoreTable m_scoreTable;                          // +0x6B628
    C_VotingPanel m_voting;                             // +0x6B634
    C_ExplosiveTimer m_explosiveTimer;                  // +0x6B63C
    C_CarnageStats m_carnage;                           // +0x6B6B0
    C_PauseText m_pauseText;                            // +0x6B6B8
    C_CountdownText m_countdown;                        // +0x6B6C4
    I3D_texture *m_stateIcons[20] = {};                 // +0x6B6D0 the "playerstate1_%02d" textures
    I3D_texture *m_stateIcons2[40] = {};                // +0x6B720 "playerstate2_%02d"
    I3D_texture *m_actionIcons[39] = {};                // +0x6B7C0 "action%02d"
    C_TextBox m_debugText;                              // +0x6B85C "Camera pos: 0,0,0" on "Border"
    C_Actor *m_pickedActor = nullptr;                   // +0x6B8F8 under the crosshair
    C_Actor *m_usableActor = nullptr;                   // +0x6B8FC near enough to use
};

// The interface's click ("button.wav"; the panels play it).
extern I3D_sound *g_clickSound;                         // 0x0089bc8c
