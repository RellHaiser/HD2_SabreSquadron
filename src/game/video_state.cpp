// The video player (game/video_state.h, part 9g-iv).
#include "game/video_state.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/config_file.h"
#include "common/os_files.h"
#include "game/campaign.h"
#include "game/profile.h"
#include "game/sound_module.h"
#include "game/text_box.h"
#include "game/text_style.h"
#include "game/video_text_box.h"
#include "ls3d/igraph.h"

// (Fix: the original's started at 0, so the first movie's stop released style 0.)
int g_creditsStyles[CREDITS_STYLES] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
int g_creditsIntro;

namespace {

// The credits' music (track 37); the music's fades (ms); the movies' volumes under a briefing and others.
constexpr int CREDITS_MUSIC = 37, MUSIC_FADE = 2000;
constexpr float BRIEFING_MUSIC_VOLUME = 0.05f, MOVIE_MUSIC_VOLUME = 0.011f;
// The keys (DirectInput's codes).
constexpr int KEY_ESCAPE = 1, KEY_SPACE = 0x39;
// "Press space to continue ..."; the subtitles' styles.
constexpr int TEXT_PRESS_SPACE = 0x7F8;
constexpr uint8_t SUBTITLE_STYLE = 8, SUBTITLE_UNICODE_STYLE = 0x1A;
// The stream's open flags (a movie).
constexpr uint32_t STREAM_VIDEO = 0x400;
// The credits' end: their last line gone (ms); D_Outro's fade (ms) and its end.
constexpr uint32_t CREDITS_END_WAIT = 3000, OUTRO_END = 1200;
// The box's depth.
constexpr float BOX_DEPTH = 0.764f;

const char *const CREDITS_INTROS[2] = {"IntroAlps.bik", "IntroBurma.bik"};   // 0x008415a0

// The credits' styles (0x008412c8): title, name, small, EL, ES, the Czech title, name and small, the
// small columns (Czech, then not) at a quarter, half and three quarters of the box.
S_TextStyleDesc Style(int font, float size, float x, bool joins, uint32_t flags)
{
    S_TextStyleDesc desc = {};
    desc.font = font;
    desc.color = 0xFFFFFFFF;
    desc.size = size;
    desc.width = 0.0f;
    desc.x = x;
    desc.y = -1.0f;
    desc.align = 2;
    desc.valign = 1;
    desc.joins = joins ? 1 : 0;
    desc.flags30 = flags;
    return desc;
}
const S_TextStyleDesc CREDITS_STYLE_DESCS[CREDITS_STYLES] = {
    Style(2, 0.06f, -1.0f, false, 0),  Style(3, 0.04f, -1.0f, false, 0),  Style(3, 0.022f, -1.0f, false, 0),
    Style(3, 0.04f, -1.0f, false, 0),  Style(3, 0.025f, -1.0f, false, 0), Style(5, 0.06f, -1.0f, false, 0),
    Style(6, 0.04f, -1.0f, false, 0),  Style(6, 0.022f, -1.0f, false, 0), Style(6, 0.022f, 0.25f, true, 0x18),
    Style(6, 0.022f, 0.5f, true, 0x10), Style(6, 0.022f, 0.75f, true, 0x10), Style(3, 0.022f, 0.25f, true, 0x18),
    Style(3, 0.022f, 0.5f, true, 0x10), Style(3, 0.022f, 0.75f, true, 0x10),
};
// The credits' tags' styles (indices into g_creditsStyles).
enum : int {
    STYLE_TITLE = 0, STYLE_NAME = 1, STYLE_SMALL = 2, STYLE_EL = 3, STYLE_ES = 4, STYLE_TITLE_CZ = 5, STYLE_NAME_CZ = 6,
    STYLE_SMALL_CZ = 7, STYLE_COLUMNS_CZ = 8, STYLE_COLUMNS = 11,
};

const char *NodeFile(const C_CampaignVideo *video) { return video->GetFileName(); }

}   // namespace

// ---------------------------------------------------------------------------
// C_VideoState

bool C_VideoState::Start(void *arg)
{
    // (The original's copy protection is left out.)
    m_video = static_cast<C_CampaignVideo *>(arg);
    m_textStyle = g_gameProfile.UsesUnicodeFont() ? SUBTITLE_UNICODE_STYLE : SUBTITLE_STYLE;
    const char *file = NodeFile(m_video);
    m_credits = std::strstr(file, "credits") == file;
    m_outro2 = std::strstr(file, "Outro2") == file;
    m_dataDiskOutro = std::strstr(file, "D_Outro") == file;
    if (m_credits || m_dataDiskOutro)
        m_scrolling = true;
    C_SoundModule *sound = GetSoundModule();
    IGraph *graph = GetIGraph();
    if (m_credits) {
        m_musicTrack = sound->GetBackgroundMusic();
        m_musicPosition = sound->GetBackgroundMusicPosition();
        if (m_musicTrack != CREDITS_MUSIC)
            sound->CrossfadeMusic(CREDITS_MUSIC, MUSIC_FADE, 0);
        const int width = graph->ScrnX(), height = graph->ScrnY();
        m_time = 0;
        MakeCreditsBox(width, height);
        LoadScript(NodeFile(m_video));
    } else {
        m_voicePlayer = ISndGetDriver()->CreateSource();
        m_stream = graph->CreateStream();
        OpenStream(NodeFile(m_video));
        if (m_outro2) {
            // (Over the main menu: its music back afterwards.)
            if (GetTypeId() == GAME_STATE_TYPE_VIDEO) {
                C_GameState *under = static_cast<C_MovieState *>(this)->m_returnState;
                if (under && under->GetTypeId() == GAME_STATE_TYPE_MAIN_MENU) {
                    m_musicTrack = sound->GetBackgroundMusic();
                    m_musicPosition = sound->GetBackgroundMusicPosition();
                }
            }
            sound->CrossfadeMusic(CREDITS_MUSIC, MUSIC_FADE, 0);
        } else {
            sound->FadeBackgroundMusic(m_video->IsBriefing() ? BRIEFING_MUSIC_VOLUME : MOVIE_MUSIC_VOLUME, MUSIC_FADE);
        }
        if (m_stream->IsPlaying()) {
            const int width = graph->ScrnX(), height = graph->ScrnY();
            FitVideo(width, height);
            if (m_scrolling) {
                MakeCreditsBox(width, height);
                LoadScript(NodeFile(m_video));
            } else {
                LoadScript(NodeFile(m_video));
                if (!m_subtitles.empty())
                    MakeSubtitleBox(width, height);
            }
            m_stream->SetVolume(g_gameProfile.GetDetailSettings()->soundVolume * 0.01f * 1.1f);
            m_stream->Play();
            // (The original also kept the start's time, in ms, at 0x008ae67c: read nowhere.)
        }
    }
    m_state = GAME_STATE_RUNNING;
    return true;
}

bool C_VideoState::Stop()
{
    IGraph *graph = GetIGraph();
    for (int i = 0; i < 3; ++i) {
        graph->Clear(0, 1.0f, 3);
        graph->Flip();
    }
    if (!m_video->IsBriefing())
        g_gameProfile.GetPlayerProfile()->SetMovieSeen(NodeFile(m_video), true);
    if (m_musicTrack == -1 || m_musicPosition == -1)
        GetSoundModule()->FadeBackgroundMusic(1.0f, MUSIC_FADE);
    else
        GetSoundModule()->CrossfadeMusic(m_musicTrack, MUSIC_FADE, m_musicPosition);
    if (m_stream) {
        m_stream->Close();
        m_stream->Release();
        m_stream = nullptr;
    }
    if (m_voicePlayer) {
        m_voicePlayer->Stop();
        m_voicePlayer->Release();
        m_voicePlayer = nullptr;
    }
    m_subtitles.clear();
    while (!m_voices.empty()) {
        m_voices.back().sound->Release();
        m_voices.pop_back();
    }
    for (int &style : g_creditsStyles) {
        ReleaseTextStyle(style);
        style = -1;
    }
    delete m_box;
    m_box = nullptr;
    return true;
}

bool C_VideoState::HandleInput(float *, float *)
{
    const int key = GetIGraph()->GetAsciiKey();
    if (key == KEY_ESCAPE || (m_ending && key == KEY_SPACE)) {
        if (m_stream)
            m_stream->Stop();
        GetIGraph()->Clear(0, 1.0f, 3);
        GetIGraph()->Flip();
        m_state = GAME_STATE_ENDED;
    }
    return true;
}

void C_VideoState::Update(int timeDelta)
{
    GetSoundModule()->Update(timeDelta);
    if (!m_ending && m_stream) {
        const int state = m_stream->GetState();
        bool stopped = state == -1;
        if (state == ISNDSTREAM_STATE_FINISHED) {
            if (m_video->IsBriefing()) {
                // The briefing waits for its key.
                if (!m_box)
                    return;
                m_box->Clear();
                m_box->SetTextId(TEXT_PRESS_SPACE, m_textStyle, 0);
                m_ending = true;
                return;
            }
            if (m_dataDiskOutro)
                m_ending = true;
            else
                stopped = true;
        }
        if (stopped) {
            m_stream->Stop();
            GetIGraph()->Clear(0, 1.0f, 3);
            GetIGraph()->Flip();
            m_state = GAME_STATE_ENDED;
        }
        m_stream->GetPosition(&m_time);
    }
    const uint32_t time = static_cast<uint32_t>(m_time);
    if (m_box) {
        if (m_scrolling) {
            m_scroll = static_cast<float>(timeDelta) * m_scrollSpeed + m_scroll;
            m_box->ScrollTo(static_cast<int>(m_scroll));
            bool done = false;
            bool counting = false;
            if (m_credits && static_cast<C_VideoTextBox *>(m_box)->Finished()) {
                m_fadeTime += static_cast<uint32_t>(timeDelta);
                done = m_fadeTime >= CREDITS_END_WAIT;
                counting = true;
            } else if (m_dataDiskOutro && m_ending) {
                m_fadeTime += static_cast<uint32_t>(timeDelta);
                float faded = static_cast<float>(m_fadeTime) * 0.001f;
                if (faded > 1.0f)
                    faded = 1.0f;
                const uint32_t shade = static_cast<uint32_t>(static_cast<int>(faded * 255.0f)) & 0xFF;
                m_box->SetColour(static_cast<uint32_t>(-static_cast<int>(shade + 1)) << 24 | 0xFFFFFF);
                done = m_fadeTime >= OUTRO_END;
                counting = true;
            }
            if (counting && done)
                m_state = GAME_STATE_ENDED;
        } else if (!m_ending) {
            if (m_subtitle != -1 &&
                static_cast<uint32_t>(m_subtitles[static_cast<size_t>(m_subtitle)].end * 100) < time) {
                m_box->Clear();
                m_subtitle = -1;
            }
            if (m_subtitle == -1) {
                // (Every subtitle of the time set in turn: the last one shown.)
                for (size_t i = 0; i < m_subtitles.size(); ++i) {
                    const S_Subtitle &subtitle = m_subtitles[i];
                    if (static_cast<uint32_t>(subtitle.start * 100) < time && time < static_cast<uint32_t>(subtitle.end * 100)) {
                        m_box->SetText(subtitle.text, m_textStyle, 0);
                        m_subtitle = static_cast<int>(i);
                    }
                }
            }
        }
    }
    if (m_voicePlayer && m_nextVoice < m_voices.size() &&
        time > static_cast<uint32_t>(m_voices[m_nextVoice].time * 100)) {
        ISndGetDriver()->SetVolume(g_gameProfile.GetDetailSettings()->soundVolume * 0.01f);
        m_voicePlayer->SetSound(m_voices[m_nextVoice].sound);
        m_voicePlayer->SetVolume(1.0f);
        m_voicePlayer->Play(0, 0);
        ++m_nextVoice;
    }
}

void C_VideoState::Render()
{
    IGraph *graph = GetIGraph();
    const int old = graph->GetRenderState(0);
    graph->Clear(0, 1.0f, 3);
    if (m_stream) {
        graph->BeginScene();
        m_stream->Draw(false);
        graph->EndScene();
    }
    if (m_box) {
        graph->SetRenderState(0, 1);
        graph->BeginScene();
        m_box->Render();
        graph->EndScene();
    }
    graph->SetRenderState(0, old);
}

void C_VideoState::OpenStream(const char *file)
{
    if (m_credits) {
        // (Start opens no stream for the credits: not reached.)
        g_creditsIntro %= 2;
        m_stream->Open(C_fstr("GameMovie\\%s", CREDITS_INTROS[g_creditsIntro < 0 ? -g_creditsIntro : g_creditsIntro]).c_str(),
                       STREAM_VIDEO);
        ++g_creditsIntro;
        return;
    }
    m_stream->Open(C_fstr("GameMovie\\%s", file).c_str(), STREAM_VIDEO);
}

void C_VideoState::FitVideo(int width, int height)
{
    S_showRect size = {};
    m_stream->GetVideoSize(&size);
    S_showRect rect = {};
    const float videoWidth = static_cast<float>(size.width), videoHeight = static_cast<float>(size.height);
    if (static_cast<float>(height) / static_cast<float>(width) < videoHeight / videoWidth) {
        // Taller than the screen: its height, centred across.
        rect.y = 5;
        rect.height = height - 10;
        rect.width = static_cast<int>(static_cast<float>(height - 10) / videoHeight * videoWidth);
        rect.x = (width - rect.width) / 2;
    } else {
        rect.x = 5;
        rect.width = width - 10;
        rect.height = static_cast<int>(static_cast<float>(width - 10) / videoWidth * videoHeight);
        rect.y = (height - rect.height) / 2;
    }
    m_stream->SetRect(&rect);
}

void C_VideoState::MakeSubtitleBox(int width, int height)
{
    // (The fractions are doubles, the products the rectangle's floats.)
    const double w = width, h = height;
    float rect[7];
    rect[0] = static_cast<float>(w * 0.125);
    rect[1] = static_cast<float>(h * 0.98);
    rect[2] = static_cast<float>(w - w * 0.125);
    rect[3] = static_cast<float>(h * 0.82);
    rect[4] = rect[2] - rect[0];
    rect[5] = rect[1] - rect[3];
    rect[6] = BOX_DEPTH;
    m_box = new C_TextBox();
    m_box->SetRect(rect);
}

void C_VideoState::MakeCreditsBox(int width, int height)
{
    const double wd = width, hd = height;
    float rect[7];
    rect[0] = static_cast<float>(wd * 0.02);
    rect[1] = static_cast<float>(hd * 0.98);
    rect[2] = static_cast<float>(wd * 0.98);
    rect[3] = static_cast<float>(hd * 0.02);
    rect[4] = rect[2] - rect[0];
    rect[5] = rect[1] - rect[3];
    rect[6] = BOX_DEPTH;
    m_box = new C_VideoTextBox();
    m_box->SetRect(rect);
    const float w = static_cast<float>(width);
    m_scrollSpeed = w * 0.0009765625f * 50.0f * 0.001f;
    m_scroll = -(w * 0.0009765625f * static_cast<float>(height + 100));
    m_box->ScrollTo(static_cast<int>(m_scroll));
    for (int i = 0; i < CREDITS_STYLES; ++i)
        g_creditsStyles[i] = CreateTextStyle(CREDITS_STYLE_DESCS[i]);
}

void C_VideoState::LoadScript(const char *file)
{
    const C_str language = g_gameProfile.GetLanguage();
    std::string name = file;
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos)
        name.resize(dot);
    const C_fstr path("%s\\text\\%s\\subtitles\\%s.txt", GetGameDirectory(), language.c_str(), name.c_str());
    if (FileExists(path.c_str()))
        m_script.Load(path.c_str());
    if (!m_credits && !m_dataDiskOutro)
        ParseSubtitles();
    else
        ParseCredits();
}

void C_VideoState::ParseSubtitles()
{
    const bool subtitles = g_gameProfile.GetPlayerProfile()->GetOptions()->subtitles;
    for (const S_GameText &entry : m_script.GetTexts()) {
        std::string line = entry.text.c_str();
        char *text = &line[0];
        if (text[0] == 'S') {
            S_VideoVoice voice;
            voice.sound = ISndGetDriver()->CreateSound();
            // (Fix: the original released the missing sound.)
            if (!voice.sound)
                continue;
            char *open = std::strchr(text, '<');
            char *close = std::strchr(text, '>');
            if (!open || !close || open == close) {
                // (The original leaked the sound.)
                voice.sound->Release();
                continue;
            }
            *close = '\0';
            voice.time = std::atoi(open + 1);
            const char *rest = close + 1;
            if (voice.sound->Open((C_str("Sounds\\") += rest).c_str(), 0) != 0)
                voice.sound->Open((C_str("sounds_DATADISK\\") += rest).c_str(), 0);
            m_voices.push_back(voice);
        } else if (subtitles) {
            char *open = std::strchr(text, '<');
            char *comma = std::strchr(text, ',');
            char *close = std::strchr(text, '>');
            if (!open || !comma || !close || comma == open || comma == close)
                continue;
            *comma = '\0';
            *close = '\0';
            S_Subtitle subtitle;
            subtitle.start = std::atoi(open + 1);
            subtitle.end = std::atoi(comma + 1);
            subtitle.text = close + 1;
            m_subtitles.push_back(subtitle);
        }
    }
    m_script.Clear();
}

void C_VideoState::ParseCredits()
{
    m_box->Clear();
    const auto add = [this](const char *text, int style) {
        m_box->AddText(C_fstr("%s", text), static_cast<uint8_t>(g_creditsStyles[style]));
    };
    // Three columns (<C_1> .. <C_2> .. <C_3> ..; else the whole line in `whole`).
    const auto columns = [&](char *text, int first, int whole) {
        char *c1 = std::strstr(text, "<C_1>");
        char *c2 = std::strstr(text, "<C_2>");
        char *c3 = std::strstr(text, "<C_3>");
        if (!c1 || !c2 || !c3) {
            add(text, whole);
            return;
        }
        *c2 = '\0';
        *c3 = '\0';
        add(c1 + 5, first);
        add(c2 + 5, first + 1);
        add(c3 + 5, first + 2);
    };
    for (const S_GameText &entry : m_script.GetTexts()) {
        std::string line = entry.text.c_str();
        char *open = std::strchr(&line[0], '<');
        char *close = std::strchr(&line[0], '>');
        if (!open || !close || close - open <= 1)
            continue;
        *close = '\0';
        const char *tag = open + 1;
        char *text = close + 1;
        if (std::strcmp(tag, "TI") == 0)
            add(text, STYLE_TITLE);
        else if (std::strcmp(tag, "NA") == 0)
            add(text, STYLE_NAME);
        else if (std::strcmp(tag, "SM") == 0)
            columns(text, STYLE_COLUMNS, STYLE_SMALL);
        else if (std::strcmp(tag, "TICZ") == 0)
            add(text, STYLE_TITLE_CZ);
        else if (std::strcmp(tag, "NACZ") == 0)
            add(text, STYLE_NAME_CZ);
        else if (std::strcmp(tag, "SMCZ") == 0)
            columns(text, STYLE_COLUMNS_CZ, STYLE_SMALL_CZ);
        else if (std::strcmp(tag, "EL") == 0)
            add(text, STYLE_EL);
        else if (std::strcmp(tag, "ES") == 0)
            add(text, STYLE_ES);
        else if (std::strcmp(tag, "SPEED") == 0) {
            int speed = 0;
            if (std::sscanf(text, "%d", &speed) > 0)
                m_scrollSpeed = static_cast<float>(GetIGraph()->ScrnX()) * 0.0009765625f * static_cast<float>(speed) * 0.001f;
        }
    }
    m_script.Clear();
}

// ---------------------------------------------------------------------------
// C_MovieState

bool C_MovieState::Play(const char *fileName, int sequence)
{
    m_sequence = sequence;
    auto *video = new C_CampaignVideo();
    video->m_fileName = fileName;
    video->m_title = fileName;
    video->m_briefing = false;
    Start(video);
    SetState(GAME_STATE_RUNNING);
    return false;
}

bool C_MovieState::Stop()
{
    C_VideoState::Stop();
    delete m_video;
    m_video = nullptr;
    return true;
}

bool C_MovieState::HandleInput(float *held, float *pressed)
{
    const bool result = C_VideoState::HandleInput(held, pressed);
    if (m_state == GAME_STATE_ENDED)
        m_state = GAME_STATE_MOVIE_FINISHED;
    return result;
}

void C_MovieState::Update(int timeDelta)
{
    C_VideoState::Update(timeDelta);
    if (m_state == GAME_STATE_ENDED)
        m_state = GAME_STATE_MOVIE_FINISHED;
}
