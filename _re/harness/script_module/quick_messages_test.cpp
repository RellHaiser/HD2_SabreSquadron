// The dialogue subtitles (part 13b: game/quick_messages.cpp). MinGW; build_quick_messages.bat; run quick_messages_test.exe ("quick messages: N checks, 0 failures").
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/chunk.h"
#include "game/game.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/text_box.h"
#include "ls3d/igraph.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)
bool Near(float a, float b, float within = 1e-3f) { return std::fabs(a - b) <= within; }

// ---- files by name ----
std::map<std::string, std::string> g_files;
std::string *g_open;
int g_at;
std::string g_written;
}  // namespace
int RW_DATA_CALL dtaOpen(const char *name, bool)
{
    auto it = g_files.find(name);
    if (it == g_files.end())
        return -1;
    g_open = &it->second;
    g_at = 0;
    return 1;
}
int RW_DATA_CALL dtaOpenWrite(const char *, int) { g_written.clear(); g_open = &g_written; g_at = 0; return 1; }
bool RW_DATA_CALL dtaClose(int) { return true; }
int RW_DATA_CALL dtaSeek(int, int offset, int origin)
{
    if (origin == 0) g_at = offset;
    else if (origin == 1) g_at += offset;
    else g_at = static_cast<int>(g_open->size()) + offset;
    return g_at;
}
int RW_DATA_CALL dtaRead(int, void *data, int size)
{
    int got = static_cast<int>(g_open->size()) - g_at;
    if (got > size) got = size;
    if (got < 0) got = 0;
    std::memcpy(data, g_open->data() + g_at, got);
    g_at += got;
    return got;
}
int RW_DATA_CALL dtaWrite(int, const void *data, int size)
{
    if (static_cast<size_t>(g_at) + size > g_open->size())
        g_open->resize(g_at + size);
    std::memcpy(&(*g_open)[g_at], data, size);
    g_at += size;
    return size;
}

// ---- the box: what was asked of it ----
namespace {
int g_boxes, g_boxesGone, g_clears, g_setTexts, g_rects, g_renders;
std::string g_lastText;
int g_lastStyle, g_lastIndex;
float g_lastRect[7];
int g_stateDuringRender = -1;
}  // namespace
C_TextBox::C_TextBox() { ++g_boxes; }
C_TextBox::~C_TextBox() { ++g_boxesGone; }
void C_TextBox::UpdateScrollRange() {}
bool C_TextBox::ScrollTo(int) { return true; }
void C_TextBox::Clear() { ++g_clears; g_lastText = "<cleared>"; }
bool C_TextBox::SetText(const C_str &text, uint8_t style, int index)
{
    ++g_setTexts;
    g_lastText = text.c_str();
    g_lastStyle = style;
    g_lastIndex = index;
    return true;
}
void C_TextBox::SetRect(const float *rect)
{
    ++g_rects;
    std::memcpy(g_lastRect, rect, sizeof g_lastRect);
}

// ---- the graph ----
namespace {
void *g_graphVt[128];
void *g_graphObject[1] = {g_graphVt};
int g_width = 800, g_height = 600, g_renderState = 7;
int __stdcall F_ScrnX(void *) { return g_width; }
int __stdcall F_ScrnY(void *) { return g_height; }
int __stdcall F_GetRenderState(void *, int state) { return state == 0 ? g_renderState : -1; }
void __stdcall F_SetRenderState(void *, int state, int value)
{
    if (state == 0)
        g_renderState = value;
}
}  // namespace
void C_TextBox::Render()
{
    ++g_renders;
    g_stateDuringRender = g_renderState;
}
IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(g_graphObject); }

// ---- the profile ----
C_str C_GameProfile::GetTextDirectory() { return C_str("Text\\"); }

namespace {
void U16(std::string &out, uint16_t value) { out.append(reinterpret_cast<const char *>(&value), 2); }
void I32(std::string &out, int value) { out.append(reinterpret_cast<const char *>(&value), 4); }
std::string Chunk(uint16_t id, const std::string &body)
{
    std::string out;
    U16(out, id);
    I32(out, static_cast<int>(body.size()) + 6);
    return out + body;
}
std::string IntBody(int value)
{
    std::string out;
    I32(out, value);
    return out;
}
std::string Dialog(int id, const std::string &text)
{
    std::string withNul = text;
    withNul.push_back('\0');
    return Chunk(0x9C6, Chunk(0x9C7, IntBody(id)) + Chunk(0x9C8, withNul));
}
void Files(const std::string &first, const std::string &second)
{
    g_files.clear();
    g_files["Text\\Dialogs.bin"] = first;
    g_files["Text\\Dialogs_DD.bin"] = second;
}
std::string File(const std::string &dialogs, int version = 1) { return Chunk(0x9C4, Chunk(0x9C5, IntBody(version)) + dialogs); }

void StandardFiles()
{
    // (Lengths: time is 200 a character.)
    Files(File(Dialog(100, "abc") + Dialog(200, "abcdefghijkl") + Dialog(90000001, "chat one")),
          File(Dialog(300, "dubbed text here") + Dialog(301, "x NOSUB y") + Dialog(302, "ab")));
}

void TestLoad()
{
    StandardFiles();
    {
        C_QuickMessages q;
        CHECK(!q.m_ready && !q.m_showing && q.m_box == nullptr && q.m_current == -1 && q.m_timeLeft == -1 && q.m_layer == 0 && q.m_style == 0);
        const int before = g_boxes;
        q.Init("x");
        CHECK(q.m_ready && q.m_box != nullptr && g_boxes == before + 1 && q.m_style == 8);
        CHECK(q.m_dialogs.size() == 6);
        // From both files, in order; the time by the length.
        CHECK(q.m_dialogs[0]->id == 100 && q.m_dialogs[1]->id == 200 && q.m_dialogs[2]->id == 90000001 && q.m_dialogs[3]->id == 300 && q.m_dialogs[5]->id == 302);
        CHECK(q.m_dialogs[1]->lines.size() == 1 && q.m_dialogs[1]->lines[0].time == 2400 && std::strcmp(q.m_dialogs[1]->lines[0].text.c_str(), "abcdefghijkl") == 0);
        CHECK(q.m_dialogs[0]->line == -1);
        const int gone = g_boxesGone;
        q.Done();
        CHECK(q.m_box == nullptr && !q.m_ready && q.m_dialogs.empty() && g_boxesGone == gone + 1);
        q.Done();
        CHECK(g_boxesGone == gone + 1);
    }
    // The other style with the unicode font.
    g_gameProfile.m_unicodeFont = true;
    {
        C_QuickMessages q;
        q.Init("x");
        CHECK(q.m_style == 0x1A);
    }
    g_gameProfile.m_unicodeFont = false;
    // Either file missing: nothing shown at all.
    const auto fails = [](const std::string &why) {
        C_QuickMessages q;
        const int before = g_boxes;
        q.Init("x");
        CHECK(!q.m_ready && q.m_box == nullptr && g_boxes == before);
        (void)why;
    };
    g_files.clear();
    fails("none");
    StandardFiles();
    g_files.erase("Text\\Dialogs.bin");
    fails("first");
    StandardFiles();
    g_files.erase("Text\\Dialogs_DD.bin");
    fails("second");
    // Another version, in either; a bad dialogue, in either: the first's problem is not forgotten.
    Files(File(Dialog(1, "a"), 2), File(Dialog(2, "b")));
    fails("version one");
    Files(File(Dialog(1, "a")), File(Dialog(2, "b"), 2));
    fails("version two");
    Files(File(Chunk(0x9C6, Chunk(0x9C8, std::string("a\0", 2)))), File(Dialog(2, "b")));
    fails("no id");
    Files(File(Dialog(2, "b")), File(Chunk(0x9C6, Chunk(0x9C7, IntBody(1)) + Chunk(0x99, "zz"))));
    fails("no text");
    Files(File(Chunk(0x9C6, Chunk(0x9C7, IntBody(1)))), File(Dialog(2, "b")));
    fails("id only");
    // Another root: no dialogues, not a failure.
    Files(Chunk(0x123, Dialog(1, "a")), File(Dialog(2, "b")));
    {
        C_QuickMessages q;
        q.Init("x");
        CHECK(q.m_ready && q.m_dialogs.size() == 1 && q.m_dialogs[0]->id == 2);
    }
    // A long text: kept to 999 bytes.
    Files(File(Chunk(0x9C6, Chunk(0x9C7, IntBody(5)) + Chunk(0x9C8, std::string(1500, 'y')))), File(""));
    {
        C_QuickMessages q;
        q.Init("x");
        CHECK(q.m_dialogs[0]->lines[0].time == 999 * 200 && std::strlen(q.m_dialogs[0]->lines[0].text.c_str()) == 999);
    }
    // After a long dialogue's chunk, the next is read from where it should be.
    Files(File(Chunk(0x9C6, Chunk(0x9C7, IntBody(5)) + Chunk(0x9C8, std::string(1500, 'y'))) + Dialog(6, "z")), File(""));
    {
        C_QuickMessages q;
        q.Init("x");
        CHECK(q.m_dialogs.size() == 2 && q.m_dialogs[1]->id == 6);
    }
}

void TestShow()
{
    StandardFiles();
    C_QuickMessages q;
    // Not ready: nothing asked of a box it has not got.
    q.SetSubtitle(100);
    q.ShowDubbing(100, -1);
    q.Update(10);
    q.Render();
    q.Reset();
    q.ShowSubtitles(true);
    CHECK(q.m_showing && q.m_current == -1 && g_lastText != "abc");
    q.ShowSubtitles(false);
    q.Init("x");

    // The rectangle.
    g_width = 900;
    g_height = 1000;
    q.m_layer = 5;
    q.m_timeLeft = 77;
    q.m_current = 3;
    q.ShowSubtitles(true);
    CHECK(q.m_layer == 0 && q.m_showing && q.m_current == -1 && q.m_timeLeft == 77);
    CHECK(Near(g_lastRect[0], 60.0f) && Near(g_lastRect[2], 840.0f) && Near(g_lastRect[1], 850.0f) && Near(g_lastRect[3], 740.0f));
    CHECK(Near(g_lastRect[4], 780.0f) && Near(g_lastRect[5], 110.0f) && Near(g_lastRect[6], 0.0301146f, 1e-6f));
    const int rects = g_rects;
    q.ShowSubtitles(false);
    CHECK(!q.m_showing && g_rects == rects);
    g_width = 800;
    g_height = 600;

    // A subtitle: turns them on; its text in the box, at least a second.
    q.SetSubtitle(100);
    CHECK(q.m_showing && g_rects == rects + 1);
    CHECK(g_lastText == "abc" && g_lastStyle == 8 && g_lastIndex == 0 && q.m_current == 0 && q.m_timeLeft == 1000);
    CHECK(q.m_dialogs[0]->line == 0 && std::strcmp(q.m_text.c_str(), "abc") == 0);
    q.SetSubtitle(200);
    CHECK(q.m_current == 1 && q.m_timeLeft == 2400 && g_rects == rects + 1);
    // 1001 ms stays so, 1000 too.
    q.m_dialogs[0]->lines[0].time = 1001;
    q.SetSubtitle(100);
    CHECK(q.m_timeLeft == 1001);
    q.m_dialogs[0]->lines[0].time = 600;
    // Another id: cleared.
    const int clears = g_clears;
    q.SetSubtitle(999);
    CHECK(g_clears == clears + 1 && q.m_current == -1);
    // A dialogue with no lines: cleared.
    q.m_dialogs[5]->lines.clear();
    q.SetSubtitle(302);
    CHECK(g_clears == clears + 2 && q.m_current == -1);

    // Dubbing: does not turn them on; 1.5 s at least; by the sound's length and a fifth if there is one.
    q.ShowSubtitles(false);
    q.ShowDubbing(100, -1);
    CHECK(!q.m_showing && q.m_current == 0 && q.m_timeLeft == 1500 && g_lastText == "abc");
    q.ShowDubbing(300, -1);
    CHECK(q.m_timeLeft == 3200 && q.m_current == 3);
    q.ShowDubbing(300, 2000);
    CHECK(q.m_timeLeft == 2400);
    q.ShowDubbing(300, 100);
    CHECK(q.m_timeLeft == 1500);
    q.ShowDubbing(300, 1251);
    CHECK(q.m_timeLeft == 1501);
    // Marked NOSUB, or unknown: cleared.
    const int clears2 = g_clears;
    q.ShowDubbing(301, 2000);
    CHECK(g_clears == clears2 + 1 && q.m_current == -1);
    q.ShowDubbing(999, 2000);
    CHECK(g_clears == clears2 + 2);
    q.ShowDubbing(302, 2000);
    CHECK(g_clears == clears2 + 3);
    q.m_dialogs[2]->lines.push_back(q.m_dialogs[1]->lines[0]);
}

void TestUpdate()
{
    StandardFiles();
    C_QuickMessages q;
    q.Init("x");
    q.SetSubtitle(200);
    CHECK(q.m_timeLeft == 2400);
    q.Update(400);
    CHECK(q.m_timeLeft == 2000 && q.m_current == 1);
    // Running out: the next line, if it has one.
    S_QuickDialog *const dialog = q.m_dialogs[1];
    dialog->lines.push_back(q.m_dialogs[3]->lines[0]);
    q.Update(2000);
    CHECK(g_lastText == "dubbed text here" && q.m_timeLeft == 3200 && dialog->line == 1 && q.m_current == 1);
    CHECK(g_lastIndex == 0);
    // Not yet at 1 ms left.
    q.m_timeLeft = 2;
    q.Update(1);
    CHECK(q.m_timeLeft == 1 && dialog->line == 1);
    // The last ended: all cleared.
    const int clears = g_clears;
    q.Update(1);
    CHECK(q.m_current == -1 && q.m_timeLeft == 0 && g_clears == clears + 1 && dialog->line == 2);
    q.Update(1);
    CHECK(q.m_timeLeft == 0);
    // Not shown: the time stands.
    q.SetSubtitle(200);
    dialog->lines.pop_back();
    q.m_showing = false;
    const int left = q.m_timeLeft;
    q.Update(100);
    CHECK(q.m_timeLeft == left);
    q.m_showing = true;
    q.m_ready = false;
    q.Update(100);
    CHECK(q.m_timeLeft == left);
    q.m_ready = true;
    // The first dialogue runs down like the others.
    q.SetSubtitle(100);
    q.Update(10);
    CHECK(q.m_current == 0 && q.m_timeLeft == 990);
    // Reset.
    const int clears2 = g_clears;
    q.Reset();
    CHECK(q.m_current == -1 && q.m_timeLeft == 0 && g_clears == clears2 + 1);
}

void TestRender()
{
    StandardFiles();
    C_QuickMessages q;
    q.Init("x");
    q.SetSubtitle(100);
    g_gameProfile.m_player->m_options.subtitles = true;
    g_renderState = 7;
    const int before = g_renders;
    q.Render();
    CHECK(g_renders == before + 1 && g_stateDuringRender == 1 && g_renderState == 7);
    // Each of the conditions.
    g_gameProfile.m_player->m_options.subtitles = false;
    q.Render();
    CHECK(g_renders == before + 1);
    g_gameProfile.m_player->m_options.subtitles = true;
    q.m_showing = false;
    q.Render();
    CHECK(g_renders == before + 1);
    q.m_showing = true;
    const int current = q.m_current;
    q.m_current = -1;
    q.Render();
    CHECK(g_renders == before + 1);
    q.m_current = current;
    q.m_ready = false;
    q.Render();
    CHECK(g_renders == before + 1);
    q.m_ready = true;
    C_TextBox *const box = q.m_box;
    q.m_box = nullptr;
    q.Render();
    CHECK(g_renders == before + 1);
    q.m_box = box;
    q.m_current = 0;
    q.Render();
    CHECK(g_renders == before + 2);
}

void TestGetText()
{
    StandardFiles();
    C_QuickMessages q;
    q.Init("x");
    CHECK(std::strcmp(q.GetText(90000001), "chat one") == 0);
    // Below the quick ids: not looked for.
    CHECK(q.GetText(100) == nullptr);
    CHECK(q.GetText(89999999) == nullptr);
    CHECK(q.GetText(90000002) == nullptr);
    q.m_dialogs[2]->lines.clear();
    CHECK(std::strcmp(q.GetText(90000001), "NO TEXT WITH THIS ID !!!") == 0);
    // Exactly the first quick id.
    q.m_dialogs[0]->id = 90000000;
    CHECK(std::strcmp(q.GetText(90000000), "abc") == 0);
}

void TestSave()
{
    StandardFiles();
    C_QuickMessages q;
    q.Init("x");
    q.m_layer = 9;
    q.m_showing = true;
    q.m_current = 4;
    q.m_timeLeft = 12345;
    C_chunk out;
    CHECK(out.Open("save", true));
    out.Begin(0x77);
    CHECK(q.SaveGame(out));
    out.Ascend();
    out.Close();
    // The bytes.
    const std::string body = g_written.substr(6);
    CHECK(static_cast<unsigned char>(body[0]) == 0x22 && static_cast<unsigned char>(body[1]) == 0x1D);
    int size = 0;
    std::memcpy(&size, body.data() + 2, 4);
    CHECK(size == 6 + 3 + 8);
    CHECK(body[6] == 1 && body[7] == 1 && body[8] == 9);
    int current = 0, left = 0;
    std::memcpy(&current, body.data() + 9, 4);
    std::memcpy(&left, body.data() + 13, 4);
    CHECK(current == 4 && left == 12345);

    // Read back, with others around it.
    std::string file = Chunk(0x77, Chunk(0x55, "zzzz") + body.substr(0, size) + Chunk(0x56, "y"));
    g_files["save"] = file;
    C_QuickMessages back;
    back.Init("x");
    C_chunk in;
    CHECK(in.Open("save", false));
    uint16_t id = 0;
    CHECK(in.Descend(id) && id == 0x77);
    const int rects = g_rects;
    CHECK(back.LoadGame(in));
    CHECK(back.m_ready && back.m_showing && back.m_layer == 0 && back.m_timeLeft == 12345);
    // Kept: the rectangle made anew, what was being shown not.
    CHECK(g_rects == rects + 1 && back.m_current == -1);
    in.Ascend();
    in.Close();
    // Hidden in the save: hidden, no rectangle.
    q.m_showing = false;
    CHECK(out.Open("save", true));
    q.SaveGame(out);
    out.Close();
    g_files["save"] = Chunk(0x77, g_written);
    C_QuickMessages hidden;
    hidden.Init("x");
    hidden.m_layer = 3;
    C_chunk in2;
    in2.Open("save", false);
    in2.Descend(id);
    const int rects2 = g_rects;
    hidden.LoadGame(in2);
    in2.Close();
    CHECK(!hidden.m_showing && g_rects == rects2 && hidden.m_layer == 9);
}
}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (void *&slot : g_graphVt) slot = nullptr;
    g_graphVt[0x18 / 4] = reinterpret_cast<void *>(F_ScrnX);
    g_graphVt[0x1C / 4] = reinterpret_cast<void *>(F_ScrnY);
    g_graphVt[0x74 / 4] = reinterpret_cast<void *>(F_SetRenderState);
    g_graphVt[0x78 / 4] = reinterpret_cast<void *>(F_GetRenderState);
    static unsigned char playerBytes[0x4000];
    g_gameProfile.m_player = reinterpret_cast<C_PlayerProfile *>(playerBytes);
    TestLoad();
    TestShow();
    TestUpdate();
    TestRender();
    TestGetText();
    TestSave();
    std::printf("quick messages: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
