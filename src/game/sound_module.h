// C_SoundModule — the "Sound" module: sound effects, music and AI hearing.
//
// Effects come from the sound table (chapter / group, see sound_table.h) or
// straight from a file (S_SoundRequest). Each playing effect occupies a slot;
// finished slots are recycled. Four channels limit how many effects of one
// kind play at the same time (a new one stops the oldest). Sound files are
// loaded through g_soundCache; every table sample also keeps a master copy to
// open new sounds from and a pool of finished sounds for reuse.
//
// Indoors, sound is damped by the scene's sectors and portals: a closed portal
// (shut door) halves it, and outdoor sounds reach a sector in proportion to how
// open the sector is to the outside.
//
// On the server the module also tells AI "listeners" which sounds they can hear.
// Two music streams are kept: the mission's looping background music and a
// second track (PlayMusic) that plays over it while the background is turned
// down.
#pragma once

#include <cstdint>
#include <vector>

#include "game/module.h"
#include "ls3d/math.h"

class C_Actor;
class C_Human;
class I3D_frame;
class I3D_scene;
class I3D_sector;
class I3D_sound;
class ISND_stream;
struct S_SoundGroup;
struct S_SoundSample;

// S_SoundRequest::flags
enum : uint32_t {
    SOUND_SILENT_TO_AI = 0x01,      // AI listeners are not told about it
    SOUND_AI_ONLY = 0x02,           // only AI listeners notice it; nothing is played
    SOUND_KEEP_SLOT = 0x04,         // the slot stays reserved when the sound ends (until Stop)
    SOUND_LOOP = 0x08,
    SOUND_FADE_IN = 0x10,           // starts silent and rises to its volume within a second
    SOUND_AI_OCCLUSION = 0x20,      // AI hearing is damped by the sectors / portals in between
    SOUND_AMBIENT = 0x40,           // heard everywhere at the same volume (non-positional)
    SOUND_ANY_RANGE = 0x80,         // played however far the listener is
};

constexpr int SOUND_CHANNELS = 4;
constexpr int SOUND_CHANNEL_NONE = 4;

// Parameters of C_SoundModule::Play().
struct S_SoundRequest {                                 // 0x40 bytes, defaults from 0x004374d0
    S_vector pos = {0, 0, 0};                           // +0x00 world position, or relative to attachTo
    float posW = 0.0f;                                  // +0x0C
    uint32_t flags = 0;                                 // +0x10 SOUND_*
    I3D_frame *attachTo = nullptr;                      // +0x14 the sound moves with this frame
    C_Actor *source = nullptr;                          // +0x18 who makes the sound (AI hearing)
    int chapter = 0;                                    // +0x1C sound table chapter (E_SoundChapter)
    int group = 0;                                      // +0x20 group of the chapter
    const char *fileName = nullptr;                     // +0x24 file to play instead of the table
    float nearRange = 0.0f;                             // +0x28 ranges for fileName (m)
    float farRange = 0.0f;                              // +0x2C
    int channel = SOUND_CHANNEL_NONE;                   // +0x30 0..3 or SOUND_CHANNEL_NONE
    float nearScale = 1.0f;                             // +0x34 scales the sample's ranges
    float farScale = 1.0f;                              // +0x38
    float volume = 1.0f;                                // +0x3C
};

// What an AI listener is told about a sound it can hear.
struct S_HeardSound {                                   // 0x20 bytes
    S_vector pos;                                       // +0x00 where the sound was made
    float posW;
    I3D_frame *listener;                                // +0x10 the listener's frame
    float intensity;                                    // +0x14 1 inside the near range, 0 at the far range
    int chapter;                                        // +0x18 kind of sound (E_SoundChapter)
    C_Actor *source;                                    // +0x1C may be null
};
using T_SoundHeardProc = void(__stdcall *)(const S_HeardSound *sound);

// Sound properties of a sector (its user data while a mission runs).
struct S_SectorAcoustics {                              // 12 bytes
    I3D_sector *sector;
    float openArea;                                     // m^2 of open portals to the outside (max 40)
    float outdoorFactor;                                // share of outdoor sound that gets in (0.1 .. 1)
};

// How many portals apart the sectors are, kept in the sectors' user data (where the acoustics
// live while a mission runs). None of this is called in the game.
struct S_SectorHopEntry {                               // 2 bytes
    S_SectorHopEntry() {}                               // 0x00431f20 (DS 0x00457670)
    ~S_SectorHopEntry() {}                              // 0x00431f40 (DS 0x004576a0)
    uint8_t hops = 0xFF;                                // +0x00 0xFF: not counted yet
    uint8_t unknown1 = 0x80;                            // +0x01
};
struct S_SectorHops {                                   // 12 bytes
    S_SectorHops(I3D_sector *sector, int index);        // 0x00431dd0 (DS 0x004574b0)
    I3D_sector *sector;                                 // +0x00
    int index;                                          // +0x04 in the table, and the entries' count
    S_SectorHopEntry *entries;                          // +0x08 one per sector before this one
};
// Distance from `a` to `b`; `factor` falls with the hops between the sectors, by 0.1 a hop
// when `gradual`, else by 0.25.
float SectorHopDistance(float &factor, const S_vector &a, const S_vector &b, I3D_sector *sectorA,
                        I3D_sector *sectorB, bool gradual);   // 0x00431120 (DS 0x00456430)
// Hops from `from` to `to` (at most 10), counted once and then remembered.
uint8_t CountSectorHops(I3D_sector *from, I3D_sector *to, std::vector<I3D_sector *> &path,
                        uint32_t maxPath);              // 0x004312b0 (DS 0x00456630)
void BuildSectorHopTable(std::vector<S_SectorHops *> &table, I3D_scene *scene);   // 0x004314e0 (DS 0x004568f0)
uint32_t __stdcall AddSectorHopEntry(I3D_frame *frame, void *table);   // 0x00431630 (DS 0x00456aa0)

// A sound frame of the scene whose volume follows the listener's sector.
struct S_SoundFrame {                                   // 0x14 bytes
    S_SoundFrame(I3D_sound *sound, bool trackSector);   // 0x00432420
    ~S_SoundFrame();                                    // 0x00432480 (DS 0x00457d80)
    S_SoundFrame(const S_SoundFrame &) = delete;
    S_SoundFrame &operator=(const S_SoundFrame &) = delete;
    // Sets the sector damping and applies it to the sound's volume.
    void SetSectorFactor(float factor);                 // 0x00432490

    I3D_sound *sound;                                   // +0x00 holds a reference
    bool trackSector;                                   // +0x04 the sound moves between sectors
    I3D_sector *sector;                                 // +0x08
    float baseVolume;                                   // +0x0C the sound's own volume
    float sectorFactor = 1.0f;                          // +0x10
};

class C_SoundModule : public C_Module {                 // "Sound", vtable 0x0080f7b8, 0x104 bytes
public:
    C_SoundModule(int id, const char *name);            // 0x004336d0 (DS 0x00459490)
    ~C_SoundModule() override;                          // 0x004335d0 (DS 0x00459340)

    void Update(int timeDelta) override;                // 0x004368d0
    void LoadMissionData(bool server) override;         // 0x004338d0
    void FreeMissionData(bool server) override;         // 0x004338f0
    void StartMission(bool server) override;            // 0x00433840 (DS 0x00459680)
    void StopMission(bool server) override;             // 0x004338a0
    void RestartMission(bool server) override;          // 0x00433910 (DS 0x00459790)
    bool SaveGame(C_chunk &chunk) override;             // 0x00433950 (DS 0x004597d0)
    bool LoadGame(C_chunk &chunk) override;             // 0x00433b10 (DS 0x004599f0)

    // --- AI hearing (server) -------------------------------------------------------
    // `callback` is called for every sound `frame` can hear; returns the listener
    // index (-1 on clients).
    int AddListener(I3D_frame *frame, T_SoundHeardProc callback);   // vtable +0x44 0x00433ed0
    bool RemoveListener(int index);                                 // vtable +0x40 0x00433e80

    // --- effects ----------------------------------------------------------------
    // Starts a sound; returns its slot or -1.
    int Play(const S_SoundRequest &request);                        // vtable +0x54 0x00433fa0 (DS 0x00459fd0)
    // Replays a table sample into the sound object of a recycled slot (see the
    // definition: free slots never keep a sound, so in practice this fails).
    int PlayTableSound(void *owner, const S_SoundRequest &request);   // vtable +0x58 0x00435240 (DS 0x0045a010)
    // Stops the sound in `*slot` and sets `*slot` to -1.
    void Stop(int *slot);                                           // vtable +0x5C 0x00437340 (DS 0x0045b2c0)
    I3D_sound *GetSound(int slot) const;                            // vtable +0x78 0x004374a0
    // Stops every sound; `releaseTableSounds` also frees the samples' loaded
    // copies, `keepMusic` leaves the background music running.
    void StopAll(bool releaseTableSounds, bool keepMusic);          // vtable +0x48 0x004361f0 (DS 0x0045ade0)
    // At most `limit` sounds of `channel` play at once.
    void SetChannelLimit(int channel, int limit);                   // vtable +0x90 0x004324c0
    S_SoundGroup *GetGroup(int chapter, int group) const;           // vtable +0x8C 0x004326d0
    int GetDubbingSampleSize(int id);                               // vtable +0x94 0x004326c0 (DS 0x00458060) (ms)

    // Plays dubbing number `dubbing` (a speech code's base, an animation note's "!a b")
    // at `owner` or `frame` in a free speech slot (PlaySpeechSlot: the sample and the lip
    // sync; `showSubtitle` also its quick message); 0 ends the frame's speech. The
    // second form also lets AI listeners near the owner hear it
    // (SOUND_SILENT_TO_AI / SOUND_AI_ONLY / SOUND_AI_OCCLUSION in `flags`).
    bool PlayDubbing(C_Human *owner, I3D_frame *frame, int dubbing, float nearRange, float farRange,
                     bool showSubtitle);                            // vtable +0x50 0x00436470 (DS 0x0045b090)
    bool PlayDubbingWithNoise(C_Human *owner, I3D_frame *frame, int dubbing, float nearRange,
                              float farRange, bool showSubtitle, uint32_t flags);   // vtable +0x4C 0x00436520 (DS 0x0045b0b0)

    // Registers a sound frame of the scene (damped by the listener's sector).
    S_SoundFrame *RegisterSoundFrame(I3D_sound *sound, bool trackSector);   // vtable +0x88 0x00432710
    // Shakes the listener's velocity (wobbling pitch through the Doppler effect).
    void SetDopplerWobble(bool on);                                 // vtable +0x98 0x00436840

    // --- music -------------------------------------------------------------------
    // Starts background track `track` (random file of music group `track`), -1 stops.
    void SetBackgroundMusic(int track, int position);               // vtable +0x60 0x004370f0
    void FadeBackgroundMusic(float volume, int time);               // vtable +0x64 0x00436e90
    // Plays track `track` once over the (turned down) background music; -1 stops it.
    void PlayMusic(int track);                                      // vtable +0x68 0x00436f60
    // Fades the music out in fadeTime/2 ms, then starts `track` and fades it in.
    void CrossfadeMusic(int track, int fadeTime, int position);     // vtable +0x6C 0x00436f10 (DS 0x0045b1f0)
    int GetBackgroundMusic() const { return m_musicTrack; }         // vtable +0x70 0x00433800 (DS 0x00459620)
    int GetBackgroundMusicPosition() const;                         // vtable +0x74 0x00433810 (DS 0x00459640) (ms)
    float SetMusicVolume(float volume);                             // vtable +0x7C 0x00433510 (DS 0x00459270)
    float GetMusicVolume() const { return m_musicVolume; }          // vtable +0x80 0x00433830 (DS 0x00459660)
    float SetMasterVolume(float volume);                            // vtable +0x84 0x00433550 (DS 0x00459290)

private:
    struct S_SoundSlot {                                // 0x40 bytes
        S_vector4 pos;                                  // +0x00 world position
        bool active = false;                            // +0x10
        I3D_sound *sound = nullptr;                     // +0x14 holds a reference
        int chapter = 0;                                // +0x18
        float farRange = 0.0f;                          // +0x1C
        float nearRange = 0.0f;                         // +0x20
        float volume = 0.0f;                            // +0x24
        float unknown28 = 0.0f;                         // +0x28 S_SoundSample::unknown18
        C_Actor *source = nullptr;                      // +0x2C
        I3D_frame *attachTo = nullptr;                  // +0x30
        uint32_t flags = 0;                             // +0x34 SOUND_*
        S_SoundSample *sample = nullptr;                // +0x38 table sample, null for files
    };
    struct S_Listener {                                 // 12 bytes
        I3D_frame *frame;
        C_Human *human;                                 // frame's human, if it is one
        T_SoundHeardProc callback;                      // null = free entry
    };
    // Free slot (a new one if needed), -1 if none could be made.
    int AllocSlot();
    void ReleaseSlotSound(S_SoundSlot *slot);           // 0x004361b0
    void NotifyListeners(int slot, bool occluded);      // 0x00435b00
    bool NotifyListenersForRequest(const S_SoundRequest &request);   // 0x00435dc0 (DS 0x0045a8a0)
    void InitSectorAcoustics();                         // 0x004328d0
    void FreeSectorAcoustics();                         // 0x00432860 (DS 0x00458280)
    static uint32_t __stdcall AddSectorAcoustics(I3D_frame *sector, void *module);   // 0x00432900
    bool IsMusicPlaying() const;                        // 0x004323e0
    bool IsOverlayPlaying() const;                      // 0x00432400
    void FadeOverlay(float volume, int time);           // 0x00436ed0
    // (Re)opens `stream` with "sounds\<fileName>" and plays it; null name only stops.
    static bool OpenStream(ISND_stream *&stream, const char *fileName, float volume, bool loop);   // 0x00437210

    std::vector<S_Listener> m_listeners;                // +0x0C
    std::vector<S_SoundSlot *> m_slots;                 // +0x1C (free 0x00437680 (DS 0x0045b6c0))
    std::vector<int> m_freeSlots;                       // +0x2C (free 0x004376a0 (DS 0x0045b6f0))
    float m_masterVolume = 0.0f;                        // +0x3C
    bool m_masterVolumeApplied = false;                 // +0x40
    float m_musicVolume = 1.0f;                         // +0x44
    ISND_stream *m_music = nullptr;                     // +0x48 background music
    int m_musicTrack = -1;                              // +0x4C
    float m_musicFadeTarget = 1.0f;                     // +0x50
    float m_musicFade = 1.0f;                           // +0x54
    float m_musicFadeSpeed = 1.0f;                      // +0x58 per ms
    float m_overlayFadeTarget = 1.0f;                   // +0x5C
    float m_overlayFade = 1.0f;                         // +0x60
    float m_overlayFadeSpeed = 0.0f;                    // +0x64 per ms (signed)
    ISND_stream *m_overlay = nullptr;                   // +0x68 PlayMusic track
    bool m_crossfadePending = false;                    // +0x6C
    int m_crossfadeTrack = 0;                           // +0x70
    int m_crossfadeTime = 0;                            // +0x74
    int m_crossfadePosition = 0;                        // +0x78
    std::vector<S_SectorAcoustics *> m_sectorAcoustics; // +0x7C
    std::vector<S_SoundFrame *> m_soundFrames;          // +0x8C
    I3D_sector *m_listenerSector = nullptr;             // +0x9C
    int m_fadeInTime = 0;                               // +0xA0 ms of silence left after a mission start
    int m_channelLimit[SOUND_CHANNELS] = {};            // +0xA4
    int m_channelNext[SOUND_CHANNELS] = {};             // +0xB4
    std::vector<int> m_channelSlots[SOUND_CHANNELS];    // +0xC4 slot of each place, -1 = free
};

extern C_SoundModule g_soundModule;                     // 0x00899838
C_SoundModule *GetSoundModule();                        // 0x004336c0
