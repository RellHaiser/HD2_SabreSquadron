// C_ScriptModule — owns the running scripts of a mission ("Script" module).
//
// At mission start it reads missions\<mission>\Scripts.dta (or mpscripts.dta),
// a list of (frame name, script file) pairs, and compiles a C_Script for each
// frame found in the scene. It also keeps track of everything scripts change in
// the scene so a restart or saved game can restore it: frames moved or
// re-linked, rotations in progress, animated models, sound frames, cutscene
// duplicates. And it runs cutscenes (letterboxed camera, scripts' OnCutscene).
//
// script_module_save.cpp: its saved games, a joining client's state, the clients' messages.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/module.h"
#include "ls3d/math.h"

class C_BitStream;
class C_chunk;
class C_Script;
class I3D_camera;
class I3D_frame;
class I3D_model;
class I3D_sound;

// What C_ScriptModule::RememberFrame() records about a frame.
enum : uint32_t {
    SCRIPT_FRAME_POSITION = 0x01,
    SCRIPT_FRAME_ROTATION = 0x02,
    SCRIPT_FRAME_LINK = SCRIPT_FRAME_POSITION | SCRIPT_FRAME_ROTATION,
    SCRIPT_FRAME_LINKED = 0x04,     // parent
    SCRIPT_FRAME_ANIMATION = 0x08,
    SCRIPT_FRAME_SOUND = 0x10,
    SCRIPT_FRAME_ON = 0x20,         // on / off state
};

// Script network messages (server -> clients).
enum E_ScriptNetMessage {
    SCRIPT_MSG_FRAME_SET_ON = 0x5F,
    SCRIPT_MSG_MORPH_SPEECH = 0x6C,
    SCRIPT_MSG_SET_LIGHTMAP = 0x7E,
    SCRIPT_MSG_CREATE_PARTICLE = 0x7F,
    SCRIPT_MSG_SET_GROUP_LIGHTMAP = 0x83,
    SCRIPT_MSG_WEATHER_ON = 0xB8,
    SCRIPT_MSG_WEATHER_COUNT = 0xB9,
    SCRIPT_MSG_WEATHER_SPEED = 0xBA,
    SCRIPT_MSG_THUNDERSTORM = 0xBC,
    SCRIPT_MSG_WEATHER_SIZE = 0xBD,
    SCRIPT_MSG_WEATHER_COLOR_IN = 0xBE,
    SCRIPT_MSG_WEATHER_COLOR_OUT = 0xBF,
    SCRIPT_MSG_WEATHER_MODE = 0xC1,
    SCRIPT_MSG_WEATHER_SPACE = 0xC2,
    SCRIPT_MSG_WEATHER_DIR = 0xC3,
    SCRIPT_MSG_SUBTITLES_ON = 0xFA,
    SCRIPT_MSG_SUBTITLES_TEXT = 0xFB,
};

// Starts a script message of the given type on the script channel; null when
// there is nobody to send to.
C_BitStream *BeginScriptNetMessage(int type, bool reliable);   // 0x00424540
void SendScriptNetMessage(C_BitStream *msg);                   // 0x00424580

// State of a frame before scripts touched it (restored on mission stop / restart).
struct S_RememberedFrame {                                     // 0x50 bytes, 16-aligned
    S_vector4 soundVelocity;            // +0x00 (sound frames)
    S_vector4 pos;                      // +0x10 local position
    S_quat rot;                         // +0x20
    float soundVolume = 0.0f;           // +0x30
    I3D_frame *frame = nullptr;         // +0x34 (holds a reference)
    I3D_frame *parent = nullptr;        // +0x38 (holds a reference)
    uint32_t what = 0;                  // +0x3C SCRIPT_FRAME_*
    bool on = false;                    // +0x40

    void Restore();                     // 0x00423bf0
};

// FRM_RotateX/Y/Z in progress.
struct S_FrameRotation {                                       // 0x20 bytes, 16-aligned
    uint8_t unknown00[0x10];
    I3D_frame *frame = nullptr;         // +0x10 (holds a reference)
    int axis = 0;                       // +0x14 0 = x, 1 = y, 2 = z
    float angleLeft = 0.0f;             // +0x18 radians, signed
    float speed = 0.0f;                 // +0x1C radians per second

    S_FrameRotation(I3D_frame *frame, int axis, int angleDeg, int speedDeg);   // 0x00428cc0
    ~S_FrameRotation();                 // 0x00428d00 (DS 0x0044c960)
    // Rotates for `timeDelta` ms; false once the angle is reached.
    bool Update(int timeDelta);         // 0x00427200
};

class C_ScriptModule : public C_Module {                       // "Script", vtable 0x0080f750
public:
    C_ScriptModule(int id, const char *name);                  // 0x004245c0
    ~C_ScriptModule() override;                                // 0x00424770

    void Tick(int timeDelta) override;                         // 0x00428490
    void Update(int timeDelta) override;                       // 0x00428b30 (DS 0x0044c700)
    void FreeMissionData(bool server) override;                // 0x004285d0
    void StartMission(bool server) override;                   // 0x004270d0 (DS 0x0044a580)
    void StopMission(bool server) override;                    // 0x00427b80
    void RestartMission(bool server) override;                 // 0x00424900 (DS 0x004473e0)
    bool OnMessage(C_BitStream *msg) override;                 // 0x00424590
    bool SaveGame(C_chunk &chunk) override;                    // 0x00424970
    bool LoadGame(C_chunk &chunk) override;                    // 0x004257b0 (DS 0x00448610)
    void WriteState(C_BitStream *msg) override;                // 0x0042aff0 (DS 0x0044f740)
    void ReadState(C_BitStream *msg) override;                 // 0x0042b330

    // Runs `scriptName` on `frame`. With an empty name, or when the frame already
    // has a script, the existing script is removed instead (OnExit runs).
    bool AssignScript(I3D_frame *frame, const char *scriptName);   // 0x00427730

    void RememberFrame(I3D_frame *frame, uint32_t what);          // 0x00423670
    // Restores and forgets a remembered frame.
    void RestoreFrame(I3D_frame *frame);                           // 0x00423b40
    void RegisterSoundFrame(I3D_frame *frame);                     // 0x00423980
    bool RegisterAnimatedModel(I3D_frame *model);                  // 0x00424260
    bool UnregisterAnimatedModel(I3D_frame *model);                // 0x00424310
    bool RegisterDuplicate(I3D_frame *frame);                      // 0x00424420
    bool UnregisterDuplicate(I3D_frame *frame);                    // 0x00424490
    // FRM_RotateX/Y/Z: rotates `frame` by `angle` degrees at `speed` degrees/s.
    void RotateFrame(I3D_frame *frame, int axis, int angle, int speed);   // 0x004274a0
    void RunCutscene(int cutscene);                                // 0x00423c70
    void EndCutscene();                                            // 0x00423eb0
    // Frames whose sounds are switched off while the game is paused.
    void AddPauseSound(const char *frameName);                     // 0x00428d10
    void SetPauseSoundsPaused(bool paused);                        // 0x00428f00
    void BroadcastGlobalAlarm();                                   // 0x00428b00

    bool IsCutsceneRunning() const { return m_cutsceneRunning; }   // 0x0041d5d0 (DS 0x0043dfd0)
    // The cutscene running (RunCutscene's; -1: the scripts' own camera).
    int GetCutscene() const { return m_cutscene; }
    void SetFakeCutscene(bool on) { m_fakeCutscene = on; }         // 0x0041d5c0
    bool IsFakeCutscene() const { return m_fakeCutscene; }         // (inlined: the decorators' update)
    int *GetSaveGameValues() { return m_saveGameValues; }
    // The 256 values in a saved game (C_GameProfile's chunk 0x3EF; 0x400 bytes, no header).
    bool WriteSaveGameValues(C_chunk &chunk);                      // 0x00427010
    bool ReadSaveGameValues(C_chunk &chunk);                       // 0x00427050

private:
    struct S_ScriptAssignment {                                    // 8 bytes (its destructor: 0x00428a40 (DS 0x0044c5c0))
        C_str frameName;
        C_str scriptName;
    };

    void LoadScriptList(bool multiplayer);                         // 0x00428600 (DS 0x0044c070)
    void ClearScriptList();                                        // 0x00428a70
    void BackupSaveGameValues();                                   // 0x00428c80
    void RestoreSaveGameValues();                                  // 0x00428ca0
    void RestoreAnimatedModel(I3D_model *model);
    void SetLetterboxViewport();
    bool LoadRotation(C_chunk &chunk);                             // (from LoadGame)
    void LoadRememberedFrames(C_chunk &chunk);
    bool LoadScript(C_chunk &chunk);
    // (0x0042b650, __fastcall: type and message in registers, no module.)
    void HandleClientMessage(int type, C_BitStream *msg);
    void HandleFrameSetOn(C_BitStream *msg);

    int m_inputDelay = 0;                                          // +0x0C ms until player input returns
    bool m_started = false;                                        // +0x10
    bool m_commandMapWasOpen = false;                          // +0x11
    std::vector<C_Script *> m_scripts;                             // +0x14
    int m_saveGameValues[256] = {};                                // +0x24 (= 0x00868b3c)
    int m_saveGameValuesAtStart[256] = {};                         // +0x424
    bool m_cutsceneRunning = false;                                // +0x824
    bool m_fakeCutscene = false;                                   // +0x825
    I3D_camera *m_cutsceneCamera = nullptr;                        // +0x828 camera before the cutscene
    float m_savedAspectRatio = 0.0f;                               // +0x82C
    float m_savedFOV = 0.0f;                                       // +0x830
    int m_cutscene = 0;                                            // +0x834
    std::vector<S_ScriptAssignment> m_assignments;                 // +0x838 Scripts.dta
    std::vector<S_FrameRotation *> m_rotations;                    // +0x848
    std::vector<I3D_frame *> m_pauseSounds;                        // +0x858
    std::vector<I3D_frame *> m_duplicates;                         // +0x868 CSC_DuplicateHuman copies
    std::vector<I3D_frame *> m_animatedModels;                     // +0x878
    std::vector<bool> m_animatedInCutscene;                        // +0x888
    std::vector<S_RememberedFrame *> m_rememberedFrames;           // +0x898
    std::vector<I3D_frame *> m_soundFrames;                        // +0x8A8
};

extern C_ScriptModule g_scriptModule;                              // 0x00868b18

// Bits of a script message's type (enough for 275 types).
extern int g_scriptMessageTypeBits;                                // 0x00838874
// Sounds whose start was deferred to the next tick: {start position, frame}.
struct S_PendingSound {
    int start;
    I3D_sound *sound;
};
extern std::vector<S_PendingSound *> g_pendingSounds;              // 0x008693d0
