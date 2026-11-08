// The dialogue subtitles and the quick chat's texts (C_QuickMessages, C_game +0x60; 0x0048eb70..0x004900e0).
// Text<language>\Dialogs.bin and Dialogs_DD.bin: chunk 0x9C4 of 0x9C5 (the version, 1) and 0x9C6 (a dialogue: 0x9C7 its id, 0x9C8 its text).
#include <cstring>

#include "common/chunk.h"
#include "game/game.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/text_box.h"
#include "ls3d/igraph.h"

namespace {

enum : uint16_t {
    CK_DIALOGS = 0x9C4,
    CK_VERSION = 0x9C5,
    CK_DIALOG = 0x9C6,
    CK_DIALOG_ID = 0x9C7,
    CK_DIALOG_TEXT = 0x9C8,
    CK_SAVE = 0x1D22,
};

// The depth of the box, its share of the screen. The byte 0x0086933c, nothing writes it: the other row of heights (0.99, 0.88) is never taken.
const float SUBTITLE_DEPTH = 0.0301146f;

}  // namespace

C_QuickMessages::C_QuickMessages() = default;

C_QuickMessages::~C_QuickMessages()
{
    Done();
}

// (0x0048f420)
static bool LoadDialog(C_chunk &chunk, std::vector<S_QuickDialog *> &dialogs)
{
    uint16_t id = 0;
    if (!chunk.Descend(id))
        return false;
    if (id != CK_DIALOG_ID) {
        chunk.Ascend();
        return false;
    }
    int dialogId = 0;
    chunk.Read(&dialogId, 4);
    chunk.Ascend();
    if (!chunk.Descend(id))
        return false;
    if (id != CK_DIALOG_TEXT) {
        chunk.Ascend();
        return false;
    }
    char buffer[1000] = {};                             // (Fix: terminated when the file's text is not.)
    const int size = chunk.RemainingSize();
    chunk.Read(buffer, size < 999 ? size : 999);
    chunk.Ascend();
    S_QuickDialog *const dialog = new S_QuickDialog();
    dialog->id = dialogId;
    S_QuickDialog::S_Line line;
    line.time = static_cast<int>(std::strlen(buffer)) * 200;
    line.text = buffer;
    dialog->lines.push_back(line);
    dialogs.push_back(dialog);
    return true;
}

// (0x0048f7d0) False: a file missing, of another version, or with a bad dialogue.
static bool LoadDialogs(std::vector<S_QuickDialog *> &dialogs)
{
    static const char *const FILES[] = {"Dialogs.bin", "Dialogs_DD.bin"};
    bool ok = true;
    C_chunk chunk;
    for (const char *const name : FILES) {
        C_str path = g_gameProfile.GetTextDirectory();
        path += name;
        if (!chunk.Open(path.c_str(), false))
            return false;
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        if (id == CK_DIALOGS) {
            while (chunk.HasMore()) {
                if (!chunk.Descend(id))                 // (Fix: the original goes on with the id it had.)
                    break;
                if (id == CK_VERSION) {
                    int version = 0;
                    chunk.Read(&version, 4);
                    chunk.Ascend();
                    ok = ok && version == 1;
                } else if (id == CK_DIALOG) {
                    ok = LoadDialog(chunk, dialogs) && ok;
                    chunk.Ascend();
                }
                // (Kept: another chunk is not left: what is in it is read as chunks of this one.)
            }
        }
        chunk.Ascend();
        chunk.Close();
    }
    return ok;
}

void C_QuickMessages::Init(const char * /*missionName*/)
{
    if (!LoadDialogs(m_dialogs))
        return;
    m_box = new C_TextBox();
    m_style = g_gameProfile.UsesUnicodeFont() ? 0x1A : 8;
    m_ready = true;
}

void C_QuickMessages::Done()
{
    Reset();
    delete m_box;
    m_box = nullptr;
    m_ready = false;
    while (!m_dialogs.empty()) {
        delete m_dialogs.back();
        m_dialogs.pop_back();
    }
}

void C_QuickMessages::Reset()
{
    m_current = -1;
    m_timeLeft = 0;
    if (m_box)
        m_box->Clear();
}

void C_QuickMessages::ShowSubtitles(bool on)
{
    if (m_box && on) {
        IGraph *const graph = GetIGraph();
        const float width = static_cast<float>(graph->ScrnX());
        const float height = static_cast<float>(graph->ScrnY());
        m_layer = 0;
        float rect[7];
        rect[0] = width * (1.0f / 15.0f);
        rect[2] = width - rect[0];
        rect[1] = height * 0.85f;
        rect[3] = height * 0.74f;
        rect[4] = rect[2] - rect[0];
        rect[5] = rect[1] - rect[3];
        rect[6] = SUBTITLE_DEPTH;
        m_box->SetRect(rect);
    }
    m_showing = on;
    m_current = -1;
}

int C_QuickMessages::FindDialog(int id) const
{
    for (size_t i = 0; i < m_dialogs.size(); ++i) {
        if (m_dialogs[i]->id == id)
            return static_cast<int>(i);
    }
    return -1;
}

// The dialogue's first line taken (false: it has none); the time of it in m_timeLeft.
bool C_QuickMessages::StartDialog(int index)
{
    S_QuickDialog &dialog = *m_dialogs[index];
    dialog.line = 0;
    if (dialog.lines.empty())
        return false;
    m_timeLeft = dialog.lines[0].time;
    m_text = dialog.lines[0].text;
    return true;
}

void C_QuickMessages::SetSubtitle(int text)
{
    if (!m_ready)
        return;
    if (!m_showing)
        ShowSubtitles(true);
    const int index = FindDialog(text);
    if (index < 0 || !StartDialog(index)) {
        m_box->Clear();
        m_current = -1;
        return;
    }
    if (m_timeLeft < 1001)
        m_timeLeft = 1000;
    m_box->SetText(m_text, m_style, 0);
    m_current = index;
}

void C_QuickMessages::ShowDubbing(int dubbing, int length)
{
    if (!m_ready)
        return;
    const int index = FindDialog(dubbing);
    if (index < 0 || !StartDialog(index) || std::strstr(m_text.c_str(), "NOSUB")) {
        m_box->Clear();
        m_current = -1;
        return;
    }
    if (length != -1)
        m_timeLeft = static_cast<int>(static_cast<float>(length) * 1.2f);
    if (m_timeLeft < 1501)
        m_timeLeft = 1500;
    m_box->SetText(m_text, m_style, 0);
    m_current = index;
}

void C_QuickMessages::Update(int timeDelta)
{
    if (!m_ready || !m_showing || m_current < 0)
        return;
    m_timeLeft -= timeDelta;
    if (m_timeLeft > 0)
        return;
    S_QuickDialog &dialog = *m_dialogs[m_current];
    const size_t next = static_cast<size_t>(++dialog.line);
    if (next < dialog.lines.size()) {
        m_timeLeft = dialog.lines[next].time;
        m_text = dialog.lines[next].text;
        m_box->SetText(m_text, m_style, 0);
        return;
    }
    Reset();
}

void C_QuickMessages::Render()
{
    if (!m_ready || !m_showing || m_current < 0 || !m_box || !g_gameProfile.m_player->GetOptions()->subtitles)
        return;
    IGraph *const graph = GetIGraph();
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    const int state = graph->GetRenderState(0);
    graph->SetRenderState(0, 1);
#endif
    m_box->Render();
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    graph->SetRenderState(0, state);
#endif
}

bool C_QuickMessages::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CK_SAVE);
    chunk.Write(&m_ready, 1);
    chunk.Write(&m_showing, 1);
    chunk.Write(&m_layer, 1);
    chunk.Write(&m_current, 4);
    chunk.Write(&m_timeLeft, 4);
    chunk.Ascend();
    return true;
}

bool C_QuickMessages::LoadGame(C_chunk &chunk)
{
    while (chunk.HasMore()) {
        uint16_t id = 0;
        if (!chunk.Descend(id))                         // (Fix: the original goes on with the id it had.)
            break;
        if (id != CK_SAVE) {
            chunk.Ascend();
            continue;
        }
        chunk.Read(&m_ready, 1);
        chunk.Read(&m_showing, 1);
        chunk.Read(&m_layer, 1);
        chunk.Read(&m_current, 4);
        chunk.Read(&m_timeLeft, 4);
        chunk.Ascend();
        // Kept: this clears the dialogue just read, before the original's 0x00490040 would take it up again, so a saved subtitle does not come back.
        ShowSubtitles(m_showing);
    }
    return true;
}

const char *C_QuickMessages::GetText(int id) const
{
    if (id > 89999999) {
        const int index = FindDialog(id);
        if (index >= 0) {
            const S_QuickDialog &dialog = *m_dialogs[index];
            return dialog.lines.empty() ? "NO TEXT WITH THIS ID !!!" : dialog.lines[0].text.c_str();
        }
    }
    return nullptr;                                     // (The word 0x0089bc54, which nothing sets: SayQuickMessage says nothing.)
}
