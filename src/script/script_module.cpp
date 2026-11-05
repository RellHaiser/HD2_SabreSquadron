#include "script/script_module.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/game.h"
#include "game/interface.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/bitstream.h"
#include "script/actor_scripts.h"
#include "script/script.h"
#include "script/script_compiler.h"

C_ScriptModule g_scriptModule(MODULE_SCRIPT, "Script");          // 0x00868b18, constructed by 0x00428f60

namespace {

// Saved-game chunk ids used by the (tier 1) save / load functions.
enum : uint16_t {
    CHUNK_SAVE_GAME_VALUES = 0x1004,
    CHUNK_PAUSE_SOUND = 0x1036,
    CHUNK_ROTATION = 0x13EC,
    CHUNK_WEATHER = 0x16A8,
    CHUNK_REMEMBERED_FRAMES = 0x170C,
    CHUNK_SCRIPT = 0x44C,
    CHUNK_FAKE_CUTSCENE = 0x47E,
};

// Bits per script message type (enough for 275 types).
int g_scriptMessageTypeBits = 9;                                 // 0x00838874

// Sounds whose start was deferred to the next tick: {start position, frame}.
struct S_PendingSound {
    int start;
    I3D_sound *sound;
};
std::vector<S_PendingSound *> g_pendingSounds;                   // 0x008693d0

// Frames of other models linked into a model being reloaded (StopMission).
struct S_Relink {
    C_str parentName;
    I3D_frame *frame;
};
std::vector<S_Relink *> g_relinks;                               // 0x00867b08

char g_scriptPath[0x100];                                        // 0x00867b18

void StartPendingSound(S_PendingSound *pending)                  // 0x004281a0
{
    pending->sound->SetOn(true);
    pending->sound->Play(pending->start);
    pending->sound->Release();
}

// I3D_frame::EnumFrames callback: unlinks frames of other models attached to `model`.
uint32_t __stdcall CollectForeignChildren(I3D_frame *frame, void *context)  // 0x004281d0
{
    I3D_frame *model = static_cast<I3D_frame *>(context);
    I3D_frame *parent = frame->GetParent();
    if (model == frame->GetOwnerModel() || (parent->GetOwnerModel() != model && parent != model))
        return 0;
    g_relinks.push_back(new S_Relink{C_str(parent->GetShortName()), frame});
    frame->LinkTo(nullptr, 0);
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// network

C_BitStream *BeginScriptNetMessage(int type, bool reliable)
{
    C_BitStream *msg = g_netGame.CreateMessage(NET_CHANNEL_SCRIPT, !reliable);
    if (msg)
        msg->WriteBits(type, g_scriptMessageTypeBits);
    return msg;
}

void SendScriptNetMessage(C_BitStream *msg)
{
    g_netGame.SendNetMessage(msg);
}

// ---------------------------------------------------------------------------
// remembered frames

void S_RememberedFrame::Restore()
{
    if (!frame)
        return;
    frame->SetOn(on);
    if (parent) {
        frame->LinkTo(parent, 0);
        parent->Release();
    }
    frame->Update();
    if (frame->GetType() == FRAME_SOUND) {
        I3D_sound *sound = static_cast<I3D_sound *>(frame);
        sound->SetVelocity(S_vector(soundVelocity.x, soundVelocity.y, soundVelocity.z));
        sound->SetVolume(soundVolume);
    }
    frame->Update();
    frame->Release();
}

void C_ScriptModule::RememberFrame(I3D_frame *frame, uint32_t what)
{
    for (size_t i = m_rememberedFrames.size(); i-- > 0;) {
        if (m_rememberedFrames[i]->frame == frame) {
            m_rememberedFrames[i]->what |= what;
            return;
        }
    }
    auto *r = new (_aligned_malloc(sizeof(S_RememberedFrame), 16)) S_RememberedFrame;
    r->frame = frame;
    frame->AddRef();
    r->parent = frame->GetParent();
    r->rot = frame->GetRot();
    r->on = frame->IsOn();
    if (r->parent)
        r->parent->AddRef();
    const S_vector &pos = frame->GetPos();
    r->pos = {pos.x, pos.y, pos.z, frame->GetPosW()};
    if (frame->GetType() == FRAME_SOUND) {
        const I3D_sound *sound = static_cast<I3D_sound *>(frame);
        r->soundVelocity = sound->GetVelocityInline();
        r->soundVolume = sound->GetVolumeInline();
    }
    r->what = what;
    m_rememberedFrames.push_back(r);
}

void C_ScriptModule::RestoreFrame(I3D_frame *frame)
{
    for (size_t i = m_rememberedFrames.size(); i-- > 0;) {
        S_RememberedFrame *r = m_rememberedFrames[i];
        if (r->frame != frame)
            continue;
        r->Restore();
        r->~S_RememberedFrame();
        _aligned_free(r);
        m_rememberedFrames[i] = m_rememberedFrames.back();
        m_rememberedFrames.pop_back();
        return;
    }
}

void C_ScriptModule::RegisterSoundFrame(I3D_frame *frame)
{
    for (size_t i = m_soundFrames.size(); i-- > 0;) {
        if (m_soundFrames[i] == frame)
            return;
    }
    m_soundFrames.push_back(frame);
}

bool C_ScriptModule::RegisterAnimatedModel(I3D_frame *model)
{
    for (size_t i = m_animatedModels.size(); i-- > 0;) {
        if (m_animatedModels[i] == model)
            return false;
    }
    AttachModelUpdateHook(model);
    GetGame()->AddAnimatedModel(model);
    m_animatedModels.push_back(model);
    m_animatedInCutscene.push_back(m_cutsceneRunning);
    model->AddRef();
    return true;
}

bool C_ScriptModule::UnregisterAnimatedModel(I3D_frame *model)
{
    for (size_t i = m_animatedModels.size(); i-- > 0;) {
        if (m_animatedModels[i] != model)
            continue;
        DetachModelUpdateHook(model);
        GetGame()->RemoveAnimatedModel(model);
        m_animatedModels[i]->Release();
        m_animatedModels[i] = m_animatedModels.back();
        m_animatedModels.pop_back();
        m_animatedInCutscene[i] = m_animatedInCutscene.back();
        m_animatedInCutscene.pop_back();
        return true;
    }
    return false;
}

bool C_ScriptModule::RegisterDuplicate(I3D_frame *frame)
{
    for (size_t i = m_duplicates.size(); i-- > 0;) {
        if (m_duplicates[i] == frame)
            return false;
    }
    m_duplicates.push_back(frame);
    frame->AddRef();
    return true;
}

bool C_ScriptModule::UnregisterDuplicate(I3D_frame *frame)
{
    for (size_t i = m_duplicates.size(); i-- > 0;) {
        if (m_duplicates[i] != frame)
            continue;
        GetGame()->RemoveAnimatedModel(frame);
        m_duplicates[i]->Release();
        m_duplicates[i] = m_duplicates.back();
        m_duplicates.pop_back();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// rotations

S_FrameRotation::S_FrameRotation(I3D_frame *frame_, int axis_, int angleDeg, int speedDeg)
    : frame(frame_), axis(axis_), angleLeft(angleDeg * 0.017453292f), speed(speedDeg * 0.017453292f)
{
    std::memset(unknown00, 0, sizeof(unknown00));
    frame->AddRef();
}

S_FrameRotation::~S_FrameRotation()
{
    frame->Release();
}

bool S_FrameRotation::Update(int timeDelta)
{
    frame->GetRot();                                    // make sure the quaternion is current

    // Step towards the remaining angle without overshooting it.
    float step = timeDelta * 0.001f * std::fabs(speed);
    const bool negative = angleLeft < 0.0f;
    angleLeft -= negative ? -step : step;
    const bool done = negative ? angleLeft > 0.0f : angleLeft < 0.0f;
    if (done)
        step += negative ? -angleLeft : angleLeft;
    const float angle = negative ? -step : step;

    S_matrix rotation = axis == 1 ? RotateYMatrix(angle) : axis == 2 ? RotateZMatrix(angle) : RotateXMatrix(angle);
    rotation *= frame->GetLocalMatrix();
    frame->SetLocalMatrix(rotation);
    frame->Update();
    return !done;
}

void C_ScriptModule::RotateFrame(I3D_frame *frame, int axis, int angle, int speed)
{
    for (size_t i = m_rotations.size(); i-- > 0;) {
        S_FrameRotation *r = m_rotations[i];
        if (r->frame == frame) {
            r->axis = axis;
            r->angleLeft = angle * 0.017453292f;
            r->speed = speed * 0.017453292f;
            return;
        }
    }
    auto *r = new (_aligned_malloc(sizeof(S_FrameRotation), 16)) S_FrameRotation(frame, axis, angle, speed);
    RememberFrame(frame, SCRIPT_FRAME_ROTATION);
    m_rotations.push_back(r);
}

// ---------------------------------------------------------------------------
// lifetime

C_ScriptModule::C_ScriptModule(int id, const char *name)
    : C_Module(id, name)
{
    // The message type field is just wide enough for every opcode.
    g_scriptMessageTypeBits = 9;                        // ceil(log2(275))
}

C_ScriptModule::~C_ScriptModule() = default;

void C_ScriptModule::LoadScriptList(bool multiplayer)
{
    ClearScriptList();
    const C_fstr path("missions\\%s\\%s", GetGame()->GetMissionName(),
                      multiplayer ? "mpscripts.dta" : "Scripts.dta");
    C_chunk chunk;
    if (!chunk.Open(path, false))
        return;
    uint16_t id;
    chunk.Descend(id);
    while (chunk.IsOpen() && chunk.RemainingSize() > 0) {
        // Each entry: a chunk with the frame name, then one with the script name.
        if (!chunk.Descend(id))
            break;
        m_assignments.push_back(S_ScriptAssignment{C_str(""), C_str("")});
        char frameName[1000];
        chunk.ReadString(frameName, sizeof(frameName));
        m_assignments.back().frameName = frameName;
        chunk.Ascend();
        if (chunk.Descend(id)) {
            char scriptName[1000];
            chunk.ReadString(scriptName, sizeof(scriptName));
            m_assignments.back().scriptName = scriptName;
            chunk.Ascend();
        }
        // Entries for frames that are not in the scene are dropped.
        if (!I3DGetDriver()->FindFrame(GetGame()->GetScene(), m_assignments.back().frameName))
            m_assignments.pop_back();
    }
    chunk.Ascend();
    chunk.Close();
}

void C_ScriptModule::ClearScriptList()
{
    m_assignments.clear();
}

void C_ScriptModule::BackupSaveGameValues()
{
    std::memcpy(m_saveGameValuesAtStart, m_saveGameValues, sizeof(m_saveGameValues));
}

void C_ScriptModule::RestoreSaveGameValues()
{
    std::memcpy(m_saveGameValues, m_saveGameValuesAtStart, sizeof(m_saveGameValues));
}

// Both are inlined copies of C_chunk::Write / Read.
bool C_ScriptModule::WriteSaveGameValues(C_chunk &chunk)
{
    return chunk.Write(m_saveGameValues, sizeof(m_saveGameValues));
}

bool C_ScriptModule::ReadSaveGameValues(C_chunk &chunk)
{
    return chunk.Read(m_saveGameValues, sizeof(m_saveGameValues));
}

void C_ScriptModule::StartMission(bool server)
{
    if (!server || m_started)
        return;
    m_fakeCutscene = false;
    LoadScriptList((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL);
    for (size_t i = m_assignments.size(); i-- > 0;) {
        I3D_frame *frame = I3DGetDriver()->FindFrame(GetGame()->GetScene(), m_assignments[i].frameName);
        if (frame)
            AssignScript(frame, m_assignments[i].scriptName);
    }
    ClearScriptList();
    m_started = true;
    m_inputDelay = 0;
    m_cutsceneRunning = false;
    m_fakeCutscene = false;
    m_cutsceneCamera = nullptr;
    m_savedAspectRatio = 0.0f;
    m_savedFOV = 0.0f;
    m_cutscene = 0;
    // Values carried between missions start empty for a single mission.
    if (g_playMode->singleMission == 1)
        std::memset(m_saveGameValues, 0, sizeof(m_saveGameValues));
    else
        BackupSaveGameValues();
    m_commandMapWasOpen = false;
}

void C_ScriptModule::RestoreAnimatedModel(I3D_model *model)
{
    for (S_Relink *r : g_relinks)
        delete r;
    g_relinks.clear();
    for (int stage = 7; stage-- > 0;)
        model->SetAnimation(nullptr, stage, 1.0f, 0);
    // Detach frames of other models, reload the model, then re-attach them.
    model->EnumFrames(CollectForeignChildren, model, 0xFFFF, 0);
    char fileName[0x400];
    std::strcpy(fileName, model->GetFileName());
    g_modelCache.OpenInto(model, fileName);
    GetGEffects()->CreateShadow(model);
    model->Update();
    for (size_t i = g_relinks.size(); i-- > 0;) {
        S_Relink *r = g_relinks[i];
        I3D_frame *parent = model->FindChildFrame(r->parentName, 0xFFFF);
        if (!parent)
            parent = model;
        r->frame->LinkTo(parent, 0);
        r->frame->Update();
        parent->Update();
        for (size_t j = m_rememberedFrames.size(); j-- > 0;) {
            S_RememberedFrame *rf = m_rememberedFrames[j];
            if (rf->frame != r->frame)
                continue;
            if (rf->parent) {
                rf->parent->Release();
                rf->parent = parent;
                parent->AddRef();
            }
            break;
        }
    }
    for (S_Relink *r : g_relinks)
        delete r;
    g_relinks.clear();
}

void C_ScriptModule::StopMission(bool server)
{
    if (!m_started || !server)
        return;

    m_soundFrames.clear();
    m_fakeCutscene = false;
    SetPauseSoundsPaused(false);
    m_pauseSounds.clear();
    for (size_t i = g_pendingSounds.size(); i-- > 0;) {
        if (g_pendingSounds[i]) {
            StartPendingSound(g_pendingSounds[i]);
            delete g_pendingSounds[i];
        }
    }
    g_pendingSounds.clear();

    // Put back what the scripts changed.
    for (size_t i = m_rememberedFrames.size(); i-- > 0;) {
        S_RememberedFrame *r = m_rememberedFrames[i];
        if (r->what & SCRIPT_FRAME_ON)
            r->frame->SetOn(r->on);
        if (r->what & SCRIPT_FRAME_LINKED) {
            r->frame->LinkTo(r->parent, I3DLINK_KEEP_WORLD);
            r->frame->SetPosInline(S_vector(r->pos.x, r->pos.y, r->pos.z));
            if (r->parent) {
                r->parent->Release();
                r->parent = nullptr;
            }
        }
        if (r->what & SCRIPT_FRAME_ROTATION)
            r->frame->SetRot(r->rot);
        if (r->what & SCRIPT_FRAME_ANIMATION)
            RestoreAnimatedModel(static_cast<I3D_model *>(r->frame));
    }
    for (S_FrameRotation *r : m_rotations) {
        r->~S_FrameRotation();
        _aligned_free(r);
    }
    m_rotations.clear();
    for (S_RememberedFrame *r : m_rememberedFrames) {
        r->Restore();
        r->~S_RememberedFrame();
        _aligned_free(r);
    }
    m_rememberedFrames.clear();

    for (size_t i = m_scripts.size(); i-- > 0;) {
        if (C_Script *script = m_scripts[i]) {
            script->OnExit();
            delete script;
        }
    }
    m_scripts.clear();
    m_started = false;

    for (size_t i = m_duplicates.size(); i-- > 0;) {
        GetGame()->RemoveAnimatedModel(m_duplicates[i]);
        m_duplicates[i]->Release();
    }
    m_duplicates.clear();
    for (size_t i = m_animatedModels.size(); i-- > 0;) {
        DetachModelUpdateHook(m_animatedModels[i]);
        GetGame()->RemoveAnimatedModel(m_animatedModels[i]);
        m_animatedModels[i]->Release();
    }
    m_animatedModels.clear();
    m_animatedInCutscene.clear();

    if (m_cutsceneRunning)
        EndCutscene();
    GetGEffects()->Fade(0, FADE_IN, nullptr);
}

void C_ScriptModule::RestartMission(bool /*server*/)
{
    g_scriptModuleRestarting = true;
    if (g_gameRole & GAME_ROLE_CLIENT)
        StopMission(false);
    if (g_gameRole & GAME_ROLE_SERVER) {
        StopMission(true);
        RestoreSaveGameValues();
        if (g_gameRole & GAME_ROLE_SERVER)
            StartMission(true);
    }
    if (g_gameRole & GAME_ROLE_CLIENT)
        StartMission(false);
    g_scriptModuleRestarting = false;
}

void C_ScriptModule::FreeMissionData(bool server)
{
    if (m_started)
        StopMission(server);
    if (server)
        ClearScriptList();
}

// ---------------------------------------------------------------------------
// per frame

void C_ScriptModule::Tick(int timeDelta)
{
    for (size_t i = g_pendingSounds.size(); i-- > 0;) {
        if (g_pendingSounds[i]) {
            StartPendingSound(g_pendingSounds[i]);
            delete g_pendingSounds[i];
        }
    }
    g_pendingSounds.clear();

    // Player input comes back a little after a cutscene.
    if (m_inputDelay > 0 && (m_inputDelay -= timeDelta) < 1) {
        m_inputDelay = 0;
        GetGame()->SetPlayerInputEnabled(true);
    }

    if (!m_started)
        return;
    for (size_t i = m_scripts.size(); i-- > 0;) {
        C_Script *script = m_scripts[i];
        // During a cutscene only the scripts in their OnCutscene handler run.
        if (script && (!m_cutsceneRunning || script->IsInCutsceneHandler()))
            script->Tick(timeDelta);
    }
}

void C_ScriptModule::Update(int timeDelta)
{
    for (size_t i = m_rotations.size(); i-- > 0;) {
        if (m_rotations[i]->Update(timeDelta))
            continue;
        m_rotations[i]->~S_FrameRotation();
        _aligned_free(m_rotations[i]);
        m_rotations[i] = m_rotations.back();
        m_rotations.pop_back();
    }
    if (m_cutsceneRunning)
        SetLetterboxViewport();
}

void C_ScriptModule::SetLetterboxViewport()
{
    // 16:9 band in the middle of the screen.
    const int width = GetIGraph()->ScrnX();
    const int height = GetIGraph()->ScrnY();
    const float bandHeight = width * 0.0625f * 9.0f;
    const float top = (height - bandHeight) * 0.5f;
    GetGame()->GetScene()->SetViewport(0, static_cast<int>(top), width, static_cast<int>(height - top));
}

// ---------------------------------------------------------------------------
// scripts

bool C_ScriptModule::AssignScript(I3D_frame *frame, const char *scriptName)
{
    S_FrameData *data = frame->GetData();
    const bool hasScript = data && data->script;

    if (!scriptName || !scriptName[0] || hasScript) {
        // Remove the frame's current script.
        for (size_t i = m_scripts.size(); i-- > 0;) {
            if (m_scripts[i]->GetFrame() != frame)
                continue;
            m_scripts[i]->OnExit();
            delete m_scripts[i];
            m_scripts[i] = m_scripts.back();
            m_scripts.pop_back();
            // A new name for a frame that had a script is not started.
            return !hasScript || !scriptName || !scriptName[0];
        }
        return false;
    }

    C_Actor *actor = data ? data->actor : nullptr;
    C_Script *script = nullptr;
    C_Human *aiHuman = nullptr;
    C_MeleeTracker *meleeTracker = nullptr;
    if (actor && actor->GetType() == ACTOR_HUMAN) {
        C_Human *human = static_cast<C_Human *>(actor);
        script = new C_HumanScript(frame, human);
        if (human->GetRole() == HUMAN_ROLE_PLAYER || human->GetRole() == HUMAN_ROLE_NPC_TEAM)
            human->SetScripted(true);
        if (human->GetController() && human->GetControllerType() == HUMAN_CONTROLLER_AI)
            aiHuman = human;
    } else if (actor && actor->GetType() == ACTOR_MELEE_TRACKER) {
        meleeTracker = static_cast<C_MeleeTracker *>(actor);
        script = new C_MeleeTrackerScript(frame, meleeTracker);
    } else {
        script = new C_Script(frame, false);
    }

    std::snprintf(g_scriptPath, sizeof(g_scriptPath), "Scripts\\%s\\%s", GetGame()->GetMissionName(), scriptName);
    if (!std::strstr(g_scriptPath, ".scr") && !std::strstr(g_scriptPath, ".SCR"))
        std::strcat(g_scriptPath, ".scr");

    if (g_scriptCompiler.Compile(g_scriptPath, script)) {
        m_scripts.push_back(script);
        script->Restart();
        if (aiHuman) {
            aiHuman->GetScriptedController()->SetScript(script);
            return true;
        }
        if (meleeTracker && meleeTracker->GetAI())
            meleeTracker->GetAI()->SetScript(script);
        return true;
    }

    delete script;
    if (aiHuman)
        aiHuman->GetScriptedController()->SetScript(nullptr);
    else if (meleeTracker && meleeTracker->GetAI())
        meleeTracker->GetAI()->SetScript(nullptr);
    return false;
}

void C_ScriptModule::BroadcastGlobalAlarm()
{
    for (size_t i = m_scripts.size(); i-- > 0;) {
        if (m_scripts[i])
            m_scripts[i]->OnGlobalAlarm();
    }
}

void C_ScriptModule::AddPauseSound(const char *frameName)
{
    I3D_frame *frame = GetGame()->GetScene()->FindFrame(frameName, 0xFFFF);
    if (!frame)
        return;
    for (size_t i = m_pauseSounds.size(); i-- > 0;) {
        if (m_pauseSounds[i] == frame)
            return;
    }
    m_pauseSounds.push_back(frame);
}

void C_ScriptModule::SetPauseSoundsPaused(bool paused)
{
    for (size_t i = m_pauseSounds.size(); i-- > 0;) {
        m_pauseSounds[i]->SetOn(!paused);
        m_pauseSounds[i]->Update();
    }
}

// ---------------------------------------------------------------------------
// cutscenes

void C_ScriptModule::RunCutscene(int cutscene)
{
    m_cutsceneRunning = true;
    C_Interface *ui = GetGame()->GetInterface();
    m_commandMapWasOpen = ui->IsCommandMapOpen();
    ui->OpenCommandMap(false);

    I3D_scene *scene = GetGame()->GetScene();
    m_cutsceneCamera = scene->GetActiveCamera();
    if (m_cutsceneCamera)
        m_cutsceneCamera->AddRef();
    m_cutscene = cutscene;

    // Actors without a cutscene handler stop (except the player).
    for (size_t i = m_scripts.size(); i-- > 0;) {
        C_Script *script = m_scripts[i];
        if (!script || script->OnCutscene(m_cutscene))
            continue;
        C_Human *human = GetFrameActorAs<C_Human>(script->GetFrame(), ACTOR_HUMAN);
        if (human && human->GetRole() != HUMAN_ROLE_PLAYER)
            g_actorsModule.SetActorActive(human, false);
    }
    // (A copy-protection check of the original was here.)

    GetGame()->SetPlayerInputEnabled(false);
    SetLetterboxViewport();
    I3D_camera *camera = scene->GetActiveCamera();
    m_savedAspectRatio = camera->GetAspectRatio();
    m_savedFOV = camera->GetFOV();
    camera->SetAspectRatio(16.0f / 9.0f);
    camera->SetFOV(GetGame()->GetCutsceneFOV());
    ResetSpeechSlots();
}

void C_ScriptModule::EndCutscene()
{
    if (!m_cutsceneRunning)
        return;
    m_cutsceneRunning = false;

    m_cutsceneCamera->SetAspectRatio(m_savedAspectRatio);
    m_cutsceneCamera->SetFOV(m_savedFOV);
    if (GetGame())
        GetGame()->GetScene()->SetActiveCamera(m_cutsceneCamera);
    m_cutsceneCamera->Release();
    m_cutsceneCamera = nullptr;

    bool showHud = false, showHudPanel = false;
    for (size_t i = m_scripts.size(); i-- > 0;) {
        C_Script *script = m_scripts[i];
        if (!script)
            continue;
        if (!script->OnCutsceneDone(m_cutscene)) {
            C_Human *human = GetFrameActorAs<C_Human>(script->GetFrame(), ACTOR_HUMAN);
            if (human && human->GetRole() != HUMAN_ROLE_PLAYER && !human->IsAISuspended())
                g_actorsModule.SetActorActive(human, true);
        }
        if (script->TakeHudHidden())
            showHud = true;
        if (script->TakeHudPanelHidden())
            showHudPanel = true;
    }
    if (showHud)
        GetGame()->ShowFirstPersonView(true);
    if (showHudPanel)
        GetGame()->ShowBinocularsView(true);

    if (GetGame()) {
        m_inputDelay = 500;
        GetGame()->GetScene()->SetViewport(0, 0, GetIGraph()->ScrnX(), GetIGraph()->ScrnY());
        GetGame()->GetScene()->GetActiveCamera()->SetAspectRatio(4.0f / 3.0f);
    }
    GetGEffects()->Fade(0, FADE_OUT, nullptr);

    // Cutscene copies go away...
    for (size_t i = m_duplicates.size(); i-- > 0;) {
        I3D_frame *copy = m_duplicates[i];
        GetGame()->RemoveAnimatedModel(copy);
        RestoreFrame(copy);
        copy->Release();
    }
    m_duplicates.clear();
    // ...and so do the animations started during the cutscene.
    for (size_t i = m_animatedModels.size(); i-- > 0;) {
        if (!m_animatedInCutscene[i])
            continue;
        I3D_frame *model = m_animatedModels[i];
        DetachModelUpdateHook(model);
        GetGame()->RemoveAnimatedModel(model);
        g_scriptModule.RestoreFrame(model);
        model->Release();
        m_animatedModels[i] = m_animatedModels.back();
        m_animatedModels.pop_back();
        m_animatedInCutscene[i] = m_animatedInCutscene.back();
        m_animatedInCutscene.pop_back();
    }
    ResetSpeechSlots();
    GetGame()->GetInterface()->OpenCommandMap(m_commandMapWasOpen);
    m_commandMapWasOpen = false;
}
