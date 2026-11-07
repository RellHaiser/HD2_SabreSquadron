// C_ScriptModule's saved games, its state for a joining client and its network messages (part 11a; the rest of the module is
// script_module.cpp). See script_module.h.
#include <cstdio>
#include <cstring>
#include <new>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actor.h"
#include "game/animation_actor.h"
#include "game/game.h"
#include "game/geffects.h"
#include "game/human.h"
#include "game/human_movement.h"
#include "game/lightmap_groups.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"
#include "script/script_module.h"

#include <windows.h>

namespace {

enum : uint16_t {
    CHUNK_SAVE_GAME_VALUES = 0x1004,
    CHUNK_PAUSE_SOUND = 0x1036,
    CHUNK_ROTATION = 0x13EC,
    CHUNK_ROTATION_FRAME = 0x1450,
    CHUNK_ROTATION_QUAT = 0x14B4,                       // s, x, y, z
    CHUNK_ROTATION_AXIS = 0x1518,
    CHUNK_ROTATION_ANGLE = 0x157C,                      // radians left
    CHUNK_ROTATION_SPEED = 0x15E0,                      // radians per second
    CHUNK_WEATHER = 0x16A8,
    CHUNK_REMEMBERED_FRAMES = 0x170C,
    CHUNK_SCRIPT = 0x44C,
    CHUNK_SCRIPT_FRAME = 0x834,
    CHUNK_SCRIPT_NAME = 0xC1C,
    CHUNK_SCRIPT_STATE = 0x1644,
    CHUNK_FAKE_CUTSCENE = 0x47E,
};

constexpr int MODEL_STAGES = 7;                         // what the module saves of a model's 8 stages
constexpr float RAD_TO_DEG = 57.2958f;                  // 0x0080f798
constexpr uint32_t FIND_ANY_WITH_MODELS = 0xCFFFF;
constexpr uint32_t VISUAL_LMAP = 0x50414D4C;            // 'LMAP'
// The weather values of a saved game (10 of I3D_scene::SetWeather's), in the file's order.
constexpr int SAVED_WEATHER[] = {WEATHER_ON,     WEATHER_COUNT,     WEATHER_SPEED, WEATHER_LENGTH, WEATHER_COLOR_IN,
                                 WEATHER_COLOR_OUT, WEATHER_DIR_X, WEATHER_DIR_Y, WEATHER_DIR_Z, WEATHER_MODE};
constexpr int SAVED_WEATHER_COUNT = sizeof(SAVED_WEATHER) / sizeof(SAVED_WEATHER[0]);
constexpr int NAME_SIZE = 0x400;

uint32_t FloatBits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

const char *FileNameOf(const char *path)
{
    const char *slash = std::strrchr(path, '\\');
    return slash ? slash + 1 : path;
}

// In the scene, else one of the driver's.
I3D_frame *FindFrameAnywhere(const char *name)
{
    I3D_scene *scene = GetGame()->GetScene();
    I3D_frame *frame = scene->FindFrame(name, FIND_ANY_WITH_MODELS);
    return frame ? frame : I3DGetDriver()->FindFrame(scene, name);
}

// The sounds started when the mission goes on (script_module.cpp's list).
void AddPendingSound(I3D_sound *sound, int position)
{
    auto *pending = new S_PendingSound{position, sound};
    sound->AddRef();
    g_pendingSounds.push_back(pending);
}

// The animations of a model: stage, time, file name, looping, still running.
void SaveModelAnimations(C_chunk &chunk, I3D_model *model)
{
    int count = 0;
    for (int stage = 0; stage < MODEL_STAGES; ++stage) {
        if (model->GetAnimation(stage))
            ++count;
    }
    chunk.Write(count);
    for (int stage = 0; stage < MODEL_STAGES; ++stage) {
        const I3D_animation_set *anim = model->GetAnimation(stage);
        if (!anim)
            continue;
        chunk.Write(stage);
        chunk.Write(model->GetAnimTime(stage));
        char path[NAME_SIZE];
        const char *file = anim->GetFileName();
        std::strcpy(path, file ? file : "");
        chunk.WriteSizedString(FileNameOf(path));
        const bool loop = model->GetAnimation(stage)->GetPlayMode() == I3DANIM_LOOP;
        chunk.Write(loop);
        const bool active = model->IsAnimActive(stage);
        chunk.Write(active);
    }
}

void LoadModelAnimations(C_chunk &chunk, I3D_model *model, C_ScriptModule *module)
{
    for (int stage = 0; stage < MODEL_STAGES; ++stage)
        model->SetAnimation(nullptr, stage, 1.0f, 0);
    int count = 0;
    chunk.Read(count);
    for (; count != 0; --count) {
        int stage = 0;
        chunk.Read(stage);
        int time = 0;
        chunk.Read(time);
        char name[NAME_SIZE];
        chunk.ReadSizedString(name, sizeof name);
        char loop = 0, active = 0;
        chunk.Read(loop);
        chunk.Read(active);
        if (model->GetType() != FRAME_MODEL)
            continue;
        if (std::strcmp(name, model->GetFileName()) == 0) {
            // The model's own animation.
            model->SetAnimTime(stage, time);
            model->Tick(1);
        } else {
            I3D_animation_set *anim = nullptr;
            if (g_animationCache.Get(name, &anim) != I3D_OK)
                continue;
            anim->SetPlayMode(loop ? I3DANIM_LOOP : I3DANIM_PLAY_ONCE);
            model->SetAnimation(anim, stage, 1.0f, 0);
            anim->Release();
            model->SetAnimTime(stage, time);
            model->Tick(1);
            module->RegisterAnimatedModel(model);
            model->Update();
        }
        if (!active)
            model->StopAnimation(stage);
    }
}

}  // namespace

// ---- saved games -------------------------------------------------------------------------------------------------------

bool C_ScriptModule::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_SAVE_GAME_VALUES);
    WriteSaveGameValues(chunk);
    chunk.Ascend();

    for (size_t i = m_pauseSounds.size(); i-- > 0;) {
        chunk.Begin(CHUNK_PAUSE_SOUND);
        chunk.WriteSizedString(m_pauseSounds[i]->GetName());
        chunk.Ascend();
    }

    for (size_t i = m_rotations.size(); i-- > 0;) {
        const S_FrameRotation *rotation = m_rotations[i];
        chunk.Begin(CHUNK_ROTATION);
        const char *name = rotation->frame->GetName();
        chunk.WriteChunk(CHUNK_ROTATION_FRAME, name, static_cast<int>(std::strlen(name)) + 1);
        const S_quat &rot = rotation->frame->GetRot();
        const float quat[4] = {rot.s, rot.v.x, rot.v.y, rot.v.z};
        chunk.WriteChunk(CHUNK_ROTATION_QUAT, quat, sizeof quat);
        chunk.WriteChunk(CHUNK_ROTATION_AXIS, rotation->axis);
        chunk.WriteChunk(CHUNK_ROTATION_ANGLE, rotation->angleLeft);
        chunk.WriteChunk(CHUNK_ROTATION_SPEED, rotation->speed);
        chunk.Ascend();
    }

    chunk.Begin(CHUNK_WEATHER);
    I3D_scene *scene = GetGame()->GetScene();
    uint32_t weather[SAVED_WEATHER_COUNT];
    for (int i = 0; i < SAVED_WEATHER_COUNT; ++i)
        weather[i] = scene->GetWeather(SAVED_WEATHER[i]);
    chunk.Write(weather, sizeof weather);
    chunk.Ascend();

    chunk.Begin(CHUNK_REMEMBERED_FRAMES);
    // Frames gone from the scene (only this list holds them, or their name finds nothing) are put back and forgotten.
    for (size_t i = m_rememberedFrames.size(); i-- > 0;) {
        S_RememberedFrame *remembered = m_rememberedFrames[i];
        if (remembered->frame->GetRefCount() != 1 && scene->FindFrame(remembered->frame->GetName(), FIND_ANY_WITH_MODELS))
            continue;
        remembered->Restore();
        remembered->~S_RememberedFrame();
        _aligned_free(remembered);
        m_rememberedFrames[i] = m_rememberedFrames.back();
        m_rememberedFrames.pop_back();
    }
    const int frameCount = static_cast<int>(m_rememberedFrames.size());
    chunk.Write(frameCount);
    for (size_t i = m_rememberedFrames.size(); i-- > 0;) {
        const S_RememberedFrame *remembered = m_rememberedFrames[i];
        I3D_frame *frame = remembered->frame;
        chunk.WriteSizedString(frame->GetName());
        chunk.Write(remembered->what);
        const bool on = frame->IsOn();
        chunk.Write(on);
        if (remembered->what & SCRIPT_FRAME_ON)
            chunk.Write(on);
        if (remembered->what & SCRIPT_FRAME_SOUND) {
            I3D_sound *sound = static_cast<I3D_sound *>(frame);
            const S_vector4 velocity = sound->GetVelocityInline();
            chunk.Write(&velocity, 12);
            const float volume = sound->GetVolumeInline();
            chunk.Write(volume);
            const int position = sound->GetPosition();
            chunk.Write(position);
        }
        if (remembered->what & SCRIPT_FRAME_POSITION) {
            const S_vector &pos = frame->GetPos();
            chunk.Write(&pos, 12);
        }
        if (remembered->what & SCRIPT_FRAME_ROTATION) {
            const S_quat rot = frame->GetRot();
            chunk.Write(&rot, sizeof rot);
        }
        if (remembered->what & SCRIPT_FRAME_LINKED)
            chunk.WriteSizedString(frame->GetParent()->GetName());
        if ((remembered->what & SCRIPT_FRAME_ANIMATION) && frame->GetType() == FRAME_MODEL)
            SaveModelAnimations(chunk, static_cast<I3D_model *>(frame));
    }
    chunk.Ascend();

    for (size_t i = m_scripts.size(); i-- > 0;) {
        C_Script *script = m_scripts[i];
        if (!script)
            continue;
        chunk.Begin(CHUNK_SCRIPT);
        const char *frameName = script->GetFrame()->GetName();
        chunk.WriteChunk(CHUNK_SCRIPT_FRAME, frameName, static_cast<int>(std::strlen(frameName)) + 1);
        chunk.WriteStringChunk(CHUNK_SCRIPT_NAME, script->GetFileName());
        chunk.Begin(CHUNK_SCRIPT_STATE);
        script->SaveGame(chunk);
        chunk.Ascend();
        chunk.Ascend();
    }

    chunk.WriteChunk(CHUNK_FAKE_CUTSCENE, m_fakeCutscene);
    return true;
}

// One rotation in progress; false: its frame is gone.
bool C_ScriptModule::LoadRotation(C_chunk &chunk)
{
    C_str frameName("");
    int axis = 0;
    float angle = 0.0f, speed = 0.0f;
    S_quat rot;
    uint16_t id;
    while (chunk.HasMore()) {
        // (Fix: at a failed read the original goes on with the last chunk's id.)
        if (!chunk.Descend(id))
            break;
        switch (id) {
        case CHUNK_ROTATION_FRAME: {
            char name[1000];
            chunk.ReadString(name, sizeof name);
            frameName = name;
            break;
        }
        case CHUNK_ROTATION_QUAT: {
            float quat[4];
            chunk.Read(quat, sizeof quat);
            rot.v = S_vector(quat[1], quat[2], quat[3]);
            rot.s = quat[0];
            break;
        }
        case CHUNK_ROTATION_AXIS:
            chunk.Read(axis);
            break;
        case CHUNK_ROTATION_ANGLE:
            chunk.Read(angle);
            break;
        case CHUNK_ROTATION_SPEED:
            chunk.Read(speed);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    I3D_frame *frame = GetGame()->GetScene()->FindFrame(frameName, FIND_ANY_WITH_MODELS);
    if (!frame)
        return false;
    frame->SetRot(rot);
    RotateFrame(frame, axis, static_cast<int>(angle * RAD_TO_DEG), static_cast<int>(speed * RAD_TO_DEG));
    return true;
}

void C_ScriptModule::LoadRememberedFrames(C_chunk &chunk)
{
    int count = 0;
    chunk.Read(count);
    uint32_t what = 0;
    for (int i = 0; i < count; ++i) {
        char name[NAME_SIZE];
        chunk.ReadSizedString(name, sizeof name);
        chunk.Read(what);
        bool on = true;
        chunk.Read(on);
        I3D_frame *frame = GetGame()->GetScene()->FindFrame(name, FIND_ANY_WITH_MODELS);
        // (The rest of the chunk is left unread: the caller's ascend skips it.)
        if (!frame)
            return;
        RememberFrame(frame, what);
        frame->SetOn(on);
        if (what & SCRIPT_FRAME_ON) {
            bool on2 = true;
            chunk.Read(on2);
            frame->SetOn(on2);
            frame->Update();
        }
        if (what & SCRIPT_FRAME_SOUND) {
            S_vector velocity;
            chunk.Read(&velocity, 12);
            float volume = 0.0f;
            chunk.Read(volume);
            int position = 0;
            chunk.Read(position);
            I3D_sound *sound = static_cast<I3D_sound *>(frame);
            sound->SetVelocity(velocity);
            sound->SetVolume(volume);
            sound->Play(position);
            if (sound->IsOn()) {
                sound->SetOn(false);
                AddPendingSound(sound, position);
            }
        }
        if (what & SCRIPT_FRAME_POSITION) {
            S_vector pos;
            chunk.Read(&pos, 12);
            frame->SetPosInline(pos);
            frame->Update();
        }
        if (what & SCRIPT_FRAME_ROTATION) {
            S_quat rot;
            chunk.Read(&rot, sizeof rot);
            frame->SetRot(rot);
            frame->Update();
        }
        if (what & SCRIPT_FRAME_LINKED) {
            char parentName[NAME_SIZE];
            chunk.ReadSizedString(parentName, sizeof parentName);
            if (I3D_frame *parent = GetGame()->GetScene()->FindFrame(parentName, FIND_ANY_WITH_MODELS))
                frame->LinkTo(parent, 0);
            frame->Update();
        }
        if (what & SCRIPT_FRAME_ANIMATION)
            LoadModelAnimations(chunk, static_cast<I3D_model *>(frame), this);
    }
}

// One script of a saved game; false: it cannot go on from there.
bool C_ScriptModule::LoadScript(C_chunk &chunk)
{
    uint16_t id = 0;
    chunk.Descend(id);
    if (id != CHUNK_SCRIPT_FRAME)
        return false;
    char frameName[1000];
    chunk.ReadString(frameName, sizeof frameName);
    chunk.Ascend();
    I3D_frame *frame = FindFrameAnywhere(frameName);
    if (!frame)
        return true;                                    // (skipped: its frame is gone)
    chunk.Descend(id);
    if (id != CHUNK_SCRIPT_NAME)
        return false;
    char scriptName[1000];
    chunk.ReadString(scriptName, sizeof scriptName);
    chunk.Ascend();
    AssignScript(frame, scriptName);
    C_Script *script = frame->GetData() ? frame->GetData()->script : nullptr;
    if (!script)
        return false;
    chunk.Descend(id);
    if (id != CHUNK_SCRIPT_STATE)
        return false;
    const bool loaded = script->LoadGame(chunk);
    chunk.Ascend();
    return loaded;
}

bool C_ScriptModule::LoadGame(C_chunk &chunk)
{
    for (size_t i = m_scripts.size(); i-- > 0;) {
        if (C_Script *script = m_scripts[i]) {
            script->OnExit();
            delete m_scripts[i];
            m_scripts.pop_back();
        }
    }
    m_scripts.clear();
    for (size_t i = m_rememberedFrames.size(); i-- > 0;) {
        if (S_RememberedFrame *remembered = m_rememberedFrames[i]) {
            remembered->Restore();
            remembered->~S_RememberedFrame();
            _aligned_free(remembered);
        }
    }
    m_rememberedFrames.clear();

    uint16_t id;
    while (chunk.HasMore()) {
        // (Fix: at a failed read the original goes on with the last chunk's id.)
        if (!chunk.Descend(id))
            return false;
        switch (id) {
        case CHUNK_SAVE_GAME_VALUES:
            ReadSaveGameValues(chunk);
            break;
        case CHUNK_PAUSE_SOUND: {
            char name[NAME_SIZE];
            chunk.ReadSizedString(name, sizeof name);
            AddPauseSound(name);
            break;
        }
        case CHUNK_ROTATION:
            if (!LoadRotation(chunk))
                return false;
            break;
        case CHUNK_WEATHER: {
            uint32_t weather[SAVED_WEATHER_COUNT] = {};
            chunk.Read(weather, sizeof weather);
            I3D_scene *scene = GetGame()->GetScene();
            for (int i = 0; i < SAVED_WEATHER_COUNT; ++i)
                scene->SetWeather(SAVED_WEATHER[i], weather[i]);
            if (weather[0] != 0 && weather[SAVED_WEATHER_COUNT - 1] == 0)
                GetGEffects()->SetRain(true);
            break;
        }
        case CHUNK_REMEMBERED_FRAMES:
            LoadRememberedFrames(chunk);
            break;
        case CHUNK_SCRIPT:
            if (!LoadScript(chunk))
                return false;
            break;
        case CHUNK_FAKE_CUTSCENE:
            chunk.Read(m_fakeCutscene);
            break;
        default:
            return false;
        }
        chunk.Ascend();
    }
    return true;
}

// ---- a joining client's state ------------------------------------------------------------------------------------------

void C_ScriptModule::WriteState(C_BitStream *msg)
{
    if (!(g_gameRole & GAME_ROLE_SERVER) || !msg)
        return;
    I3D_scene *scene = GetGame()->GetScene();
    if (!scene)
        return;
    const uint32_t weatherOn = scene->GetWeather(WEATHER_ON);
    msg->WriteBits(weatherOn != 0, 1);
    if (weatherOn != 0) {
        for (int param = 1; param < WEATHER_MODE; ++param)
            msg->WriteBits(scene->GetWeather(param), 32);
        msg->WriteBits(scene->GetWeather(WEATHER_MODE), 1);
    }
    OutputDebugStringA(C_fstr("ScriptModule : send state for %d sounds ", static_cast<int>(m_soundFrames.size())));
    msg->WriteBits(static_cast<uint32_t>(m_soundFrames.size()), 16);
    for (size_t i = m_soundFrames.size(); i-- > 0;) {
        I3D_frame *frame = m_soundFrames[i];
        OutputDebugStringA(C_fstr("ScriptModule : send state for %s(%s)\n", frame->GetName(), frame->IsOn() ? "true" : "false"));
        msg->WriteString(frame->GetName());
        msg->WriteBits(frame->IsOn(), 1);
    }
}

void C_ScriptModule::ReadState(C_BitStream *msg)
{
    if (!(g_gameRole & GAME_ROLE_CLIENT) || !msg)
        return;
    const uint32_t weatherOn = msg->ReadBits(1);
    I3D_scene *scene = GetGame()->GetScene();
    if (!scene)
        return;
    scene->SetWeather(WEATHER_ON, weatherOn != 0);
    if (weatherOn != 0) {
        for (int param = 1; param < WEATHER_MODE; ++param)
            scene->SetWeather(param, msg->ReadBits(32));
        scene->SetWeather(WEATHER_MODE, msg->ReadBits(1));
    }
    int count = static_cast<int16_t>(msg->ReadBits(16));
    OutputDebugStringA(C_fstr("ScriptModule : recieve state for %d sounds \n", count));
    for (; count != 0; --count) {
        char name[0x100];
        msg->ReadString(name, 0xFF);
        const uint32_t on = msg->ReadBits(1);
        OutputDebugStringA(C_fstr("ScriptModule : receive state for %s(%s)\n", name, on == 1 ? "true" : "false"));
        if (I3D_frame *frame = GetGame()->GetScene()->FindFrame(name, 0xFFFF))
            frame->SetOn(on == 1);
    }
}

// ---- the script messages (the clients') --------------------------------------------------------------------------------

bool C_ScriptModule::OnMessage(C_BitStream *msg)
{
    if (!msg)
        return false;
    HandleClientMessage(static_cast<int>(msg->ReadBits(g_scriptMessageTypeBits)), msg);
    return true;
}

// A frame switched on or off by a script (OP_FRM_SetOn's message).
void C_ScriptModule::HandleFrameSetOn(C_BitStream *msg)
{
    char name[0x100];
    msg->ReadString(name, 0xFE);
    const bool on = msg->ReadBits(1) != 0;
    if (!GetGame()->GetScene())
        return;
    I3D_frame *frame = FindFrameAnywhere(name);
    if (!frame)
        return;
    const int type = frame->GetType();
    RememberFrame(frame, type == FRAME_SOUND ? SCRIPT_FRAME_ON | SCRIPT_FRAME_SOUND : SCRIPT_FRAME_ON);
    if (type == FRAME_SOUND) {
        I3D_sound *sound = static_cast<I3D_sound *>(frame);
        if (on)
            sound->Play(0);
        sound->SetActive(on, false);
        frame->Update();
        return;
    }
    if (type != FRAME_MODEL) {
        frame->SetOn(on);
        frame->Update();
        return;
    }
    C_Actor *actor = GetFrameActor(frame);
    if (actor && actor->GetType() == ACTOR_HUMAN) {
        C_HumanMovement *movement = static_cast<C_Human *>(actor)->GetMovement();
        if (on) {
            movement->UpdateModelNames();
            if (I3D_frame *shadow = movement->GetShadowFrame())
                shadow->SetOn(true);
        } else {
            movement->RemoveShadow();
        }
    } else if (on) {
        GetGEffects()->CreateShadow(frame);
    } else if (I3D_frame *shadow = GetGEffects()->GetShadow(frame)) {
        shadow->SetOn(false);
        shadow->Update();
        shadow->Release();
    }
    if (actor) {
        const int actorType = actor->GetType();
        if (actorType == ACTOR_ANIMATION) {
            static_cast<C_AnimationActor *>(actor)->SetCollisionsOn(on);
        } else if (actorType == ACTOR_JUNKERS88 || actorType == ACTOR_AIR_ATTACK) {
            GetGEffects()->SetHierarchyOn(frame, on, 0x2088);
            frame->Update();
            return;
        }
    }
    frame->SetOn(on);
    if (on && actor && actor->GetType() == ACTOR_HUMAN) {
        C_Team *team = g_teamManager.GetPlayerTeam();
        if (team->GetActivePlayer() == actor)
            team->UpdateCameraTarget();
    }
    frame->Update();
}

void C_ScriptModule::HandleClientMessage(int type, C_BitStream *msg)
{
    if (!(g_gameRole & GAME_ROLE_CLIENT) || !msg)
        return;
    char name[0x100];
    C_game *const game = GetGame();
    switch (type) {
    case SCRIPT_MSG_FRAME_SET_ON:
        HandleFrameSetOn(msg);
        break;
    case SCRIPT_MSG_MORPH_SPEECH: {
        msg->ReadString(name, 0xFF);
        const int dubbing = static_cast<int>(msg->ReadBits(32));
        const uint32_t nearRange = msg->ReadBits(8);
        const uint32_t farRange = msg->ReadBits(8);
        if (I3D_frame *frame = game->GetScene()->FindFrame(name, 0xFFFF))
            C_HumanMovement::PlayDubbing(dubbing, frame, nullptr, static_cast<float>(nearRange), static_cast<float>(farRange));
        break;
    }
    case SCRIPT_MSG_SET_LIGHTMAP: {
        msg->ReadString(name, 0xFE);
        const int lightmap = static_cast<int>(msg->ReadBits(8));
        if (!game->GetScene())
            break;
        I3D_frame *frame = FindFrameAnywhere(name);
        if (frame && frame->GetVisualType() == VISUAL_LMAP)
            GetLightMapGroups()->SetFrameLightmap(frame, lightmap);
        break;
    }
    case SCRIPT_MSG_CREATE_PARTICLE: {
        const int effect = static_cast<int>(msg->ReadBits(8));
        msg->ReadString(name, 0xFF);
        if (I3D_frame *frame = game->GetScene()->FindFrame(name, 0xFFFF))
            g_particlesModule.Create(effect, frame->GetMatrix(), frame, true, false);
        break;
    }
    case SCRIPT_MSG_SET_GROUP_LIGHTMAP: {
        msg->ReadString(name, 0xFE);
        const int lightmap = static_cast<int>(msg->ReadBits(8));
        C_LightMapGroupsModule *groups = GetLightMapGroups();
        if (const S_LightmapGroup *group = groups ? groups->FindGroup(name) : nullptr)
            groups->SetGroupLightmap(group, lightmap);
        break;
    }
    case SCRIPT_MSG_WEATHER_ON: {
        const uint32_t on = msg->ReadBits(1);
        if (game->GetScene())
            game->GetScene()->SetWeather(WEATHER_ON, on != 0);
        GetGEffects()->SetRain(on != 0);
        break;
    }
    case SCRIPT_MSG_WEATHER_COUNT:
    case SCRIPT_MSG_WEATHER_SPEED:
    case SCRIPT_MSG_WEATHER_COLOR_IN:
    case SCRIPT_MSG_WEATHER_COLOR_OUT: {
        const uint32_t value = msg->ReadBits(32);
        const int param = type == SCRIPT_MSG_WEATHER_COUNT   ? WEATHER_COUNT
                          : type == SCRIPT_MSG_WEATHER_SPEED ? WEATHER_SPEED
                          : type == SCRIPT_MSG_WEATHER_COLOR_IN ? WEATHER_COLOR_IN
                                                                : WEATHER_COLOR_OUT;
        if (game->GetScene())
            game->GetScene()->SetWeather(param, value);
        break;
    }
    case SCRIPT_MSG_THUNDERSTORM:
        GetGEffects()->StartMpLightning(static_cast<int16_t>(msg->ReadBits(16)));
        break;
    case SCRIPT_MSG_WEATHER_SIZE: {
        const uint32_t length = msg->ReadBits(32);
        const uint32_t width = msg->ReadBits(32);
        if (game->GetScene()) {
            game->GetScene()->SetWeather(WEATHER_LENGTH, length);
            game->GetScene()->SetWeather(WEATHER_WIDTH, width);
        }
        break;
    }
    case SCRIPT_MSG_WEATHER_MODE: {
        const uint32_t mode = msg->ReadBits(1);
        if (game->GetScene())
            game->GetScene()->SetWeather(WEATHER_MODE, mode);
        GetGEffects()->SetRain(mode == 0);
        break;
    }
    case SCRIPT_MSG_WEATHER_SPACE: {
        const uint32_t distance = msg->ReadBits(32);
        const uint32_t height = msg->ReadBits(32);
        if (game->GetScene()) {
            game->GetScene()->SetWeather(WEATHER_DISTANCE, FloatBits(static_cast<float>(distance)));
            game->GetScene()->SetWeather(WEATHER_HEIGHT, FloatBits(static_cast<float>(height)));
        }
        break;
    }
    case SCRIPT_MSG_WEATHER_DIR: {
        const uint32_t x = msg->ReadBits(32);
        const uint32_t y = msg->ReadBits(32);
        const uint32_t z = msg->ReadBits(32);
        if (game->GetScene()) {
            game->GetScene()->SetWeather(WEATHER_DIR_X, x);
            game->GetScene()->SetWeather(WEATHER_DIR_Y, y);
            game->GetScene()->SetWeather(WEATHER_DIR_Z, z);
        }
        break;
    }
    case SCRIPT_MSG_SUBTITLES_ON:
        game->GetQuickMessages()->ShowSubtitles(msg->ReadBits(1) != 0);
        break;
    case SCRIPT_MSG_SUBTITLES_TEXT:
        game->GetQuickMessages()->SetSubtitle(static_cast<int>(msg->ReadBits(32)));
        break;
    default:
        break;
    }
}
