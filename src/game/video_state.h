// The video player (C_VideoState, vtable 0x00815bfc, 0x7C bytes: a campaign video node; C_MovieState,
// vtable 0x00815bc8, 0x84 bytes: the logos, the intro, the game's and the menu's movies): a Bink movie
// over the screen (GameMovie\..) with its subtitles and voices, or the credits scrolled up the screen
// over the music; both from the language's text\<language>\subtitles\<movie>.txt. Part 9g-iv.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/game_state.h"
#include "game/game_texts.h"

class C_CampaignVideo;
class C_TextBox;
class ISND_sound;
class ISND_source;
class ISND_stream;

// A subtitle: shown from `start` to `end` (tenths of a second of the movie).
struct S_Subtitle {                                     // 0x0C bytes
    int start = 0;                                      // +0x00
    int end = 0;                                        // +0x04
    C_str text;                                         // +0x08
};
// A voice played from `time` (tenths of a second of the movie).
struct S_VideoVoice {                                   // 0x08 bytes
    int time = 0;                                       // +0x00
    ISND_sound *sound = nullptr;                        // +0x04
};

// The credits' text styles (made with the credits' box, released by Stop; -1: none).
constexpr int CREDITS_STYLES = 14;
extern int g_creditsStyles[CREDITS_STYLES];             // 0x008ae644
// The credits' intro movie played next (IntroAlps / IntroBurma by turns; not reached from Start).
extern int g_creditsIntro;                              // 0x008ae680

class C_VideoState : public C_GameState {
public:
    C_VideoState() = default;                           // 0x006c3330 (also inline)
    // (Its tables freed; the subtitles and voices are Stop's.)
    ~C_VideoState() override = default;                 // 0x006c11a0 (0x006c1180)
    C_VideoState(const C_VideoState &) = delete;
    C_VideoState &operator=(const C_VideoState &) = delete;

    // `arg` the C_CampaignVideo. The credits ("credits*"): the music saved, track 37 crossfaded to
    // (unless playing), their box and script; a movie: its voices' player, the stream opened
    // (GameMovie\..; "Outro2*": the main menu's music saved, track 37; else the music faded to 5 %
    // for a briefing, 1.1 %), opened: fitted to the screen, "D_Outro*" scrolled like the credits, else
    // the subtitles (their box when there are some), its volume the options' sound volume (110 %),
    // played. Running, true.
    bool Start(void *arg) override;                     // 0x04 0x00688330
    // The screen cleared three times; not a briefing: the movie seen; the music back (or faded in);
    // the stream, the voices' player, the subtitles, the voices, the credits' styles, the box gone.
    bool Stop() override;                               // 0x08 0x00688750
    // Escape (Space once the end is reached): the stream stopped, the screen cleared, ended. True.
    bool HandleInput(float *held, float *pressed) override;   // 0x0C 0x00688ea0
    // The sound module's update; the stream's end (a briefing: "Press space to continue ...",
    // waited for; "D_Outro": its fade; else ended), its position; the credits scrolled (their last
    // line gone: 3 s more; "D_Outro" faded out over 1 s, ended after 1.2 s) or the subtitle shown;
    // the next voice played once reached.
    void Update(int timeDelta) override;                // 0x14 0x00688f00
    // The screen cleared; the movie's picture, the box (render state 0 on meanwhile).
    void Render() override;                             // 0x18 0x00689240
    int GetTypeId() const override { return GAME_STATE_TYPE_CAMPAIGN_VIDEO; }   // 0x1C 0x006c1170

protected:
    // The stream opened: GameMovie\<file> (the credits': their intro movies by turns).
    void OpenStream(const char *file);                  // 0x006892e0
    // The movie's rectangle: its picture's shape kept, 5 pixels in from the screen's nearer edges.
    void FitVideo(int width, int height);               // 0x00688da0
    // The subtitles' box (the screen's lower part, 12.5 % in at each side).
    void MakeSubtitleBox(int width, int height);        // 0x00688920
    // The credits' box (the screen less 2 % at each edge, scrolled from under it, 50 / 1024 of the
    // screen's width a second) and their 14 styles.
    void MakeCreditsBox(int width, int height);         // 0x00688ab0
    // The script text\<language>\subtitles\<file less its extension>.txt (when there is one) read into
    // the subtitles and voices, or the credits.
    void LoadScript(const char *file);                  // 0x006893e0
    // The script's lines: "S<time>file" a voice (Sounds\.., else sounds_DATADISK\..), "<start,end>text"
    // a subtitle (the options' subtitles on). The script emptied.
    void ParseSubtitles();                              // 0x00689c60
    // The script's lines into the box by their tags (<TI> title, <NA> name, <SM> small (three columns
    // with <C_1> <C_2> <C_3>), <EL>, <ES>, the Czech ones <TICZ> <NACZ> <SMCZ>; <SPEED> the scroll's).
    // The script emptied.
    void ParseCredits();                                // 0x00689630

    int m_musicTrack = -1;                              // +0x0C the music before (Stop restores it)
    int m_musicPosition = -1;                           // +0x10
    float m_scroll = 0.0f;                              // +0x14 the credits' box's scroll (pixels)
    float m_scrollSpeed = 0.0f;                         // +0x18 pixels a ms
    bool m_credits = false;                             // +0x1C
    bool m_outro2 = false;                              // +0x1D
    bool m_dataDiskOutro = false;                       // +0x1E "D_Outro*"
    bool m_ending = false;                              // +0x1F the end reached (Space ends it too)
    ISND_stream *m_stream = nullptr;                    // +0x20
    ISND_source *m_voicePlayer = nullptr;               // +0x24
    C_GameTexts m_script;                               // +0x28
    C_TextBox *m_box = nullptr;                         // +0x38 the subtitles' or the credits' (C_VideoTextBox)
    bool m_scrolling = false;                           // +0x3C the credits' box ("credits", "D_Outro")
    uint32_t m_fadeTime = 0;                            // +0x40 ms since the credits' end
    std::vector<S_Subtitle> m_subtitles;                // +0x44
    std::vector<S_VideoVoice> m_voices;                 // +0x54
    uint32_t m_nextVoice = 0;                           // +0x64
    int m_subtitle = -1;                                // +0x68 the one shown
    int m_time = 0;                                     // +0x6C the stream's position (ms)
    C_CampaignVideo *m_video = nullptr;                 // +0x70 Start's
    uint8_t m_textStyle = 0;                            // +0x78 8, the unicode font's 0x1A
};

class C_MovieState : public C_VideoState {
public:
    C_MovieState() = default;                           // (inline)
    ~C_MovieState() override = default;                 // 0x006c1100 (0x006c10e0)

    // Plays `fileName` (a video node of its own); `sequence` says what follows it (C_GameApp: 0 back to
    // the state it was opened over, 1 .. 3 the next logo / the intro, 4 the main menu, 5 the first logo,
    // 6 the application ends). Running; returns false.
    bool Play(const char *fileName, int sequence);      // 0x0066ede0
    // The base's, the node deleted.
    bool Stop() override;                               // 0x08 0x0066f030
    // The base's; ended: GAME_STATE_MOVIE_FINISHED.
    bool HandleInput(float *held, float *pressed) override;   // 0x0C 0x0066f050
    void Update(int timeDelta) override;                // 0x14 0x0066f080
    void Render() override { C_VideoState::Render(); }  // 0x18 0x0066f0a0
    int GetTypeId() const override { return GAME_STATE_TYPE_VIDEO; }   // 0x1C 0x006c10d0

    int GetSequence() const { return m_sequence; }
    C_GameState *GetReturnState() const { return m_returnState; }
    void SetReturnState(C_GameState *state) { m_returnState = state; }

private:
    friend class C_VideoState;
    int m_sequence = 0;                                 // +0x7C
    C_GameState *m_returnState = nullptr;               // +0x80
};
