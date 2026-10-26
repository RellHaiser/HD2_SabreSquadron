#include "script/script.h"

#include <windows.h>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/human.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/physics.h"
#include "ls3d/i3d.h"
#include "script/script_expression.h"
#include "script/script_tables.h"

namespace {

// Save-game chunk ids of a script instance.
enum : uint16_t {
    CHUNK_IP = 1000,
    CHUNK_WHENEVER_TIMER = 0x4B0,
    CHUNK_DELAY = 0x514,
    CHUNK_PENDING_SIGNALS = 0x578,
    CHUNK_DISABLED_SIGNALS = 0x5DC,
    CHUNK_RUNNING = 0x640,
    CHUNK_SIGNALS_DISABLED = 0x6A4,
    CHUNK_ALARMS_DISABLED = 0x708,
    CHUNK_WHENEVERS_DISABLED = 0x76C,
    CHUNK_HUD_HIDDEN = 0x7D0,
    CHUNK_HUD_PANEL_HIDDEN = 0x834,
    CHUNK_FINISHED = 0x898,
    CHUNK_NUMBERS = 0x8FC,
    CHUNK_FRAMES = 0x960,
    CHUNK_FRAME_NAME = 0x992,
    CHUNK_CALL_STACK = 0x9C4,
    CHUNK_WAIT_ANIMATION = 0xA28,
    CHUNK_WHENEVERS = 0xA8C,
    CHUNK_HUMAN_STATE = 0xAF0,
    CHUNK_MELEE_TRACKER_STATE = 0xB54,
};

// Whenever conditions are tested in steps of this many ms.
constexpr int WHENEVER_STEP = 500;
constexpr int MAX_CALL_DEPTH = 100;
constexpr int MAX_SIGNALS = 32;

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }

int64_t MicrosecondsNow()
{
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return counter.QuadPart * 1000000 / frequency.QuadPart;
}

S_ScriptHandler *FindHandler(const std::vector<S_ScriptHandler *> &handlers, int id)
{
    for (size_t i = handlers.size(); i-- > 0;) {
        if (handlers[i] && handlers[i]->id == id)
            return handlers[i];
    }
    return nullptr;
}

bool AddHandler(std::vector<S_ScriptHandler *> &handlers, int id, int label)
{
    if (!IsServer() || FindHandler(handlers, id))
        return false;
    handlers.push_back(new S_ScriptHandler{id, label});
    return true;
}

// Frame lookup used when restoring frame variables: the scene first, then the
// models of the mission's items (inlined in 0x00414e30).
I3D_frame *FindFrameForLoad(const char *name)
{
    if (I3D_frame *frame = GetGame()->GetScene()->FindFrame(name, FRAME_MASK_ALL))
        return frame;
    for (int i = g_itemsModule.GetPlacementCount(); i-- > 0;) {
        C_Item *item = g_itemsModule.GetPlacement(i).item;
        if (item && item->GetFrame(0) && std::strcmp(item->GetFrame(0)->GetName(), name) == 0)
            return item->GetFrame(0);
    }
    const std::vector<C_Item *> &respawning = g_itemsModule.GetRespawningItems();
    for (size_t i = respawning.size(); i-- > 0;) {
        C_Item *item = respawning[i];
        if (item->GetFrame(0) && std::strcmp(item->GetFrame(0)->GetName(), name) == 0)
            return item->GetFrame(0);
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// S_ScriptCommand

void S_ScriptCommand::SetArgs(const int *values)
{
    const int count = GetScriptArgCount(opcode);
    if (!values || count == 0)
        return;
    if (!args)
        args = new int[count];
    for (int i = 0; i < count; ++i)
        args[i] = values[i];
}

void S_ScriptCommand::FreeArgs()
{
    delete[] args;
    args = nullptr;
}

// ---------------------------------------------------------------------------
// construction

C_Script::C_Script(I3D_frame *frame, bool global)
    : m_global(global), m_frame(frame)
{
    if (!global && frame) {
        if (!frame->GetData()) {
            S_FrameData data = {};
            data.script = this;
            data.physicsObject = g_noPhysicsObject;
            SetFrameData(frame, data);
        } else {
            frame->GetData()->script = this;
        }
        frame->AddRef();
    }
    m_name = "";
}

C_Script::~C_Script()
{
    Clear();
    if (!m_global) {
        if (m_frame) {
            if (S_FrameData *data = m_frame->GetData())
                data->script = nullptr;
            m_frame->Release();
        }
        m_frame = nullptr;
    }
}

void C_Script::Clear()
{
    if (!IsServer())
        return;
    m_pendingSignals = 0;
    m_disabledSignals = 0;
    m_numbers.clear();
    m_strings.clear();
    m_callStack.clear();

    for (S_ScriptHandler *h : m_signalHandlers)
        delete h;
    m_signalHandlers.clear();
    for (S_ScriptHandler *h : m_cutsceneHandlers)
        delete h;
    m_cutsceneHandlers.clear();
    for (S_ScriptHandler *h : m_cutsceneDoneHandlers)
        delete h;
    m_cutsceneDoneHandlers.clear();
    for (S_ScriptWhenever *w : m_whenevers)
        delete w;
    m_whenevers.clear();
    for (C_ScriptExpression *e : m_expressions)
        delete e;
    m_expressions.clear();
    for (S_ScriptCommand *c : m_commands) {
        if (c) {
            c->FreeArgs();
            delete c;
        }
    }
    m_commands.clear();
    for (I3D_frame *f : m_frames) {
        if (f)
            f->Release();
    }
    m_frames.clear();
}

// ---------------------------------------------------------------------------
// execution

void C_Script::Advance()
{
    if (!IsServer())
        return;
    // Scripts hold still until the mission's first frame, except during a restart.
    if (!g_scriptModuleRestarting && !GetGame()->HasMissionStarted())
        return;
    m_running = true;
    if (++m_ip >= CommandCount())
        m_running = false;
}

bool C_Script::SkipBlock()
{
    // The block starts with '{' carrying the body length; continue after its '}'.
    const int open = ++m_ip;
    m_ip = open + m_commands[open]->Arg(0) + 2;
    if (m_ip < CommandCount())
        return true;
    m_running = false;
    m_ip = CommandCount() - 1;
    return false;
}

void C_Script::ExecuteBlock()
{
    if (!IsServer())
        return;
    const int open = ++m_ip;
    const S_ScriptCommand *begin = m_commands[open];
    if (!begin || begin->opcode != OP_BlockBegin || !begin->args)
        return;
    const int length = begin->Arg(0);

    while (m_ip < CommandCount()) {
        const S_ScriptCommand *cmd = m_commands[m_ip];
        if (m_ip >= open + length + 2 || m_ip < open) {
            m_running = true;
            return;
        }
        if (cmd->opcode == OP_BlockBegin || cmd->opcode == OP_BlockEnd) {
            ++m_ip;
            continue;
        }
        const int ip = m_ip;
        ExecuteCommand(*cmd);
        if (m_ip == ip)
            ++m_ip;
    }
}

void C_Script::Tick(int timeDelta)
{
    if (!IsServer())
        return;

    // FRM_SetAnimationDelayed: wait for the animation on stage 0 to end.
    if (m_waitAnimLabel != -1) {
        if (m_waitAnimLabel == m_ip) {
            I3D_animation_set *anim = m_waitAnimModel->GetAnimation(0);
            const int time = m_waitAnimModel->GetAnimTime(0) + timeDelta;
            if (time < anim->GetEndTime())
                goto waiting;
            // Stop right before the end so the last pose stays.
            const int now = m_waitAnimModel->GetAnimTime(0);
            m_waitAnimModel->Tick(m_waitAnimModel->GetAnimation(0)->GetEndTime() - now - 1);
            Advance();
        } else {
            m_waitAnimModel = nullptr;
        }
        m_waitAnimLabel = -1;
    }
waiting:
    if (m_finished)
        return;

    const int64_t start = MicrosecondsNow();

    if (!m_wheneversDisabled) {
        // Test the Whenever conditions in fixed steps of the accumulated time.
        while (m_wheneverTimer > WHENEVER_STEP) {
            for (size_t i = m_whenevers.size(); i-- > 0;) {
                S_ScriptWhenever *w = m_whenevers[i];
                if (!w || !w->enabled)
                    continue;
                w->elapsed += WHENEVER_STEP;
                if (w->elapsed <= w->period)
                    continue;
                w->elapsed -= w->period;
                if (!m_expressions[w->expression]->Evaluate(this))
                    continue;
                m_running = true;
                m_ip = w->label;
                w->enabled = false;
                m_delay = 0;
                break;
            }
            m_wheneverTimer -= WHENEVER_STEP;
        }
        m_wheneverTimer += timeDelta;
    }

    if (m_delay != 0) {
        m_delay -= timeDelta;
        if (m_delay > 0)
            goto done;
        m_delay = 0;
        m_running = true;
        ++m_ip;
    }

    if (m_running && static_cast<unsigned>(m_ip) < static_cast<unsigned>(CommandCount())) {
        m_running = false;
        const S_ScriptCommand *cmd = m_commands[m_ip];
        if (!cmd)
            goto done;
        switch (cmd->opcode) {
        case OP_Delay:
            m_delay = NumberVar(cmd->Arg(0));
            break;
        case OP_GoTo:
            JumpTo(cmd->Arg(0));        // the argument is a command index, not a variable
            break;
        // Handler and Whenever bodies are only entered through their events.
        case OP_Whenever:
        case OP_OnSignal:
        case OP_OnDeath:
        case OP_OnUse:
        case OP_OnAlarm:
        case OP_OnHit:
        case OP_OnGlobalAlarm:
        case OP_OnExit:
        case OP_OnAlarmDone:
        case OP_OnCutsceneDone:
        case OP_OnCutscene: {
            const int open = m_ip + 1;
            m_ip = m_commands[open]->Arg(0) + 2 + open;
            if (m_ip >= CommandCount()) {
                m_running = false;
                m_ip = CommandCount() - 1;
            } else {
                m_running = true;
            }
            break;
        }
        case OP_Gosub:
            if (m_callStack.size() < MAX_CALL_DEPTH) {
                m_callStack.push_back(m_ip + 1);
                JumpTo(cmd->Arg(0));
            }
            break;
        case OP_Return:
            if (m_callStack.empty()) {
                Stop();
                break;
            }
            m_ip = m_callStack.back();
            m_callStack.pop_back();
            m_running = true;
            break;
        case OP_Block:
            ExecuteBlock();
            break;
        default:
            ExecuteCommand(*cmd);
            break;
        }
    }

done:
    m_lastTickTime = static_cast<int>(MicrosecondsNow() - start);
}

void C_Script::Stop()
{
    if (!IsServer())
        return;
    m_running = false;
    m_finished = true;
    // An AI human whose script ends goes back to normal behaviour.
    C_Human *human = GetFrameActorAs<C_Human>(m_frame, ACTOR_HUMAN);
    if (human && human->GetControllerType() == HUMAN_CONTROLLER_AI) {
        human->SetAIOptimized(0);
        human->Suspend(false);
    }
}

void C_Script::Restart()
{
    if (!IsServer())
        return;
    m_waitAnimLabel = -1;
    m_waitAnimModel = nullptr;
    m_ip = m_startPoint != -1 ? m_startPoint : 0;
    m_running = true;
    m_wheneversDisabled = false;
    m_alarmsDisabled = false;
    m_signalsDisabled = false;
    m_hudPanelHiddenByScript = false;
    m_hudHiddenByScript = false;
    m_inCutsceneHandler = false;
}

// ---------------------------------------------------------------------------
// events

void C_Script::SendSignal(int signal)
{
    if (m_signalsDisabled || !IsServer())
        return;
    // Signals do not interrupt the OnDeath and OnAlarm handlers.
    if (m_running && (m_ip == m_onDeath || m_ip == m_onAlarm))
        return;
    const uint32_t bit = 1u << (signal & 31);
    if (m_disabledSignals & bit)
        return;

    if (const S_ScriptHandler *handler = FindHandler(m_signalHandlers, signal)) {
        // An AI human only reacts while its AI is in control.
        C_Actor *actor = GetFrameActor(m_frame);
        if (actor && actor->GetType() == ACTOR_HUMAN &&
            static_cast<C_Human *>(actor)->GetControllerType() != HUMAN_CONTROLLER_AI)
            return;
        m_running = true;
        m_delay = 0;
        m_ip = handler->label;
        return;
    }
    if (signal < MAX_SIGNALS)
        m_pendingSignals |= bit;
}

void C_Script::OnDeath()
{
    if (!IsServer())
        return;
    if (m_onDeath == -1) {
        Stop();
        return;
    }
    m_ip = m_onDeath;
    m_delay = 0;
    m_running = true;
    m_finished = false;
}

void C_Script::OnUse(int /*unused*/)
{
    if (!IsServer())
        return;
    if (m_running && (m_ip == m_onDeath || m_ip == m_onAlarm))
        return;
    if (m_onUse != -1)
        JumpTo(m_onUse);
}

void C_Script::OnHit()
{
    if (!IsServer())
        return;
    if (m_running && (m_ip == m_onDeath || m_ip == m_onAlarm))
        return;
    if (m_onHit != -1)
        JumpTo(m_onHit);
}

void NotifyFrameHit(I3D_frame *frame)
{
    if (frame && frame->GetData() && frame->GetData()->script)
        frame->GetData()->script->OnHit();
}

void C_Script::OnAlarm()
{
    if (!IsServer())
        return;
    if (!m_finished && m_running && m_ip == m_onDeath)
        return;
    if (m_alarmsDisabled || m_onAlarm == -1)
        return;
    m_ip = m_onAlarm;
    m_delay = 0;
    m_running = true;
}

bool C_Script::OnAlarmDone()
{
    if (!IsServer())
        return false;
    if (m_running && m_ip == m_onDeath)
        return false;
    if (m_onAlarmDone == -1)
        return false;
    m_ip = m_onAlarmDone;
    m_delay = 0;
    m_running = true;
    m_finished = false;
    return true;
}

void C_Script::OnGlobalAlarm()
{
    if (IsServer() && m_onGlobalAlarm != -1)
        JumpTo(m_onGlobalAlarm);
}

void C_Script::OnExit()
{
    m_running = true;
    m_ip = m_onExit;
    if (m_onExit != -1) {
        m_ip = m_onExit - 1;
        ExecuteBlock();
    }
}

bool C_Script::OnCutscene(int cutscene)
{
    if (!IsServer())
        return false;
    const S_ScriptHandler *handler = FindHandler(m_cutsceneHandlers, cutscene);
    if (!handler)
        return false;
    m_running = true;
    m_delay = 0;
    m_finished = false;
    m_ip = handler->label;
    m_inCutsceneHandler = true;
    return true;
}

bool C_Script::OnCutsceneDone(int cutscene)
{
    if (!IsServer())
        return false;
    const S_ScriptHandler *handler = FindHandler(m_cutsceneDoneHandlers, cutscene);
    if (!handler)
        return false;
    m_running = true;
    m_delay = 0;
    m_inCutsceneHandler = false;
    m_finished = false;
    m_ip = handler->label;
    return true;
}

void C_Script::OnActionDone(int label)
{
    if (m_ip == label)
        Advance();
}

// ---------------------------------------------------------------------------
// building

int C_Script::AddCommand(S_ScriptCommand *cmd)
{
    if (!IsServer())
        return -1;
    m_commands.push_back(cmd);
    return CommandCount() - 1;
}

void C_Script::AddExpression(C_ScriptExpression *expr)
{
    if (!IsServer())
        return;
    m_expressions.push_back(expr);
    expr->FreeTokens();
}

bool C_Script::AddSignalHandler(int signal, int label)
{
    return AddHandler(m_signalHandlers, signal, label);
}

bool C_Script::AddCutsceneHandler(int cutscene, int label)
{
    return AddHandler(m_cutsceneHandlers, cutscene, label);
}

bool C_Script::AddCutsceneDoneHandler(int cutscene, int label)
{
    return AddHandler(m_cutsceneDoneHandlers, cutscene, label);
}

void C_Script::SetFrameVar(int index, I3D_frame *frame, bool addRef)
{
    I3D_frame *&var = m_frames[index];
    if (var)
        var->Release();
    var = frame;
    if (frame && addRef)
        frame->AddRef();
}

// ---------------------------------------------------------------------------
// saved games

bool C_Script::SaveGame(C_chunk &chunk) const
{
    if (!IsServer())
        return false;

    chunk.WriteChunk(CHUNK_IP, m_ip);
    chunk.WriteChunk(CHUNK_WHENEVER_TIMER, m_wheneverTimer);
    chunk.WriteChunk(CHUNK_DELAY, m_delay);
    chunk.WriteChunk(CHUNK_PENDING_SIGNALS, m_pendingSignals);
    chunk.WriteChunk(CHUNK_DISABLED_SIGNALS, m_disabledSignals);
    chunk.WriteChunk(CHUNK_RUNNING, m_running);
    chunk.WriteChunk(CHUNK_SIGNALS_DISABLED, m_signalsDisabled);
    chunk.WriteChunk(CHUNK_ALARMS_DISABLED, m_alarmsDisabled);
    chunk.WriteChunk(CHUNK_WHENEVERS_DISABLED, m_wheneversDisabled);
    chunk.WriteChunk(CHUNK_HUD_HIDDEN, m_hudHiddenByScript);
    chunk.WriteChunk(CHUNK_HUD_PANEL_HIDDEN, m_hudPanelHiddenByScript);
    chunk.WriteChunk(CHUNK_FINISHED, m_finished);

    // Literals are part of the compiled script; only variables are saved.
    chunk.Begin(CHUNK_NUMBERS);
    for (const S_ScriptNumber &n : m_numbers) {
        if (!n.isConstant)
            chunk.Write(n.value);
    }
    chunk.Ascend();

    chunk.Begin(CHUNK_FRAMES);
    for (const I3D_frame *frame : m_frames) {
        const char *name = frame ? frame->GetName() : "";
        chunk.WriteChunk(CHUNK_FRAME_NAME, name, static_cast<int>(std::strlen(name) + 1));
    }
    chunk.Ascend();

    chunk.Begin(CHUNK_CALL_STACK);
    chunk.Write(static_cast<int>(m_callStack.size()));
    for (int label : m_callStack)
        chunk.Write(label);
    chunk.Ascend();

    if (m_waitAnimLabel != -1) {
        chunk.Begin(CHUNK_WAIT_ANIMATION);
        chunk.Write(m_waitAnimLabel);
        chunk.WriteSizedString(m_waitAnimModel->GetName());
        chunk.Ascend();
    }

    chunk.Begin(CHUNK_WHENEVERS);
    for (const S_ScriptWhenever *w : m_whenevers) {
        chunk.Write(w->period);
        chunk.Write(w->elapsed);
        chunk.Write(w->enabled);
    }
    chunk.Ascend();

    if (GetKind() == SCRIPT_KIND_HUMAN)
        chunk.WriteChunk(CHUNK_HUMAN_STATE, m_actorFlag);
    if (GetKind() == SCRIPT_KIND_MELEE_TRACKER)
        chunk.WriteChunk(CHUNK_MELEE_TRACKER_STATE, m_actorFlag);
    return true;
}

bool C_Script::LoadGame(C_chunk &chunk)
{
    if (!IsServer())
        return false;

    // The original builds an error text for broken files but never reports it.
    for (I3D_frame *&frame : m_frames) {
        if (frame) {
            frame->Release();
            frame = nullptr;
        }
    }
    m_callStack.clear();

    while (chunk.IsOpen() && chunk.RemainingSize() > 0) {
        uint16_t id;
        if (!chunk.Descend(id))
            return true;                                // "chunk error."

        switch (id) {
        case CHUNK_IP: chunk.Read(m_ip); break;
        case CHUNK_WHENEVER_TIMER: chunk.Read(m_wheneverTimer); break;
        case CHUNK_DELAY: chunk.Read(m_delay); break;
        case CHUNK_PENDING_SIGNALS: chunk.Read(m_pendingSignals); break;
        case CHUNK_DISABLED_SIGNALS: chunk.Read(m_disabledSignals); break;
        case CHUNK_RUNNING: chunk.Read(m_running); break;
        case CHUNK_SIGNALS_DISABLED: chunk.Read(m_signalsDisabled); break;
        case CHUNK_ALARMS_DISABLED: chunk.Read(m_alarmsDisabled); break;
        case CHUNK_WHENEVERS_DISABLED: chunk.Read(m_wheneversDisabled); break;
        case CHUNK_HUD_HIDDEN: chunk.Read(m_hudHiddenByScript); break;
        case CHUNK_HUD_PANEL_HIDDEN: chunk.Read(m_hudPanelHiddenByScript); break;
        case CHUNK_FINISHED: chunk.Read(m_finished); break;

        case CHUNK_NUMBERS:
            for (S_ScriptNumber &n : m_numbers) {
                if (!n.isConstant)
                    chunk.Read(n.value);
            }
            break;

        case CHUNK_FRAMES:
            for (I3D_frame *&frame : m_frames) {
                uint16_t sub;
                if (!chunk.Descend(sub))
                    continue;                           // "Broken chunk '%d'."
                char name[1000];
                chunk.ReadString(name, sizeof(name));
                chunk.Ascend();
                if (!name[0])
                    continue;
                frame = FindFrameForLoad(name);         // "Frame not found : '%s'." if null
                if (frame)
                    frame->AddRef();
            }
            break;

        case CHUNK_CALL_STACK: {
            int count = 0;
            chunk.Read(count);
            for (int i = 0; i < count; ++i) {
                int label = 0;
                chunk.Read(label);
                m_callStack.push_back(label);
            }
            break;
        }

        case CHUNK_WAIT_ANIMATION: {
            chunk.Read(m_waitAnimLabel);
            char name[0x400];
            chunk.ReadSizedString(name, sizeof(name));
            m_waitAnimModel = static_cast<I3D_model *>(GetGame()->GetScene()->FindFrame(name, FRAME_MASK_ALL));
            if (!m_waitAnimModel)
                m_waitAnimLabel = -1;
            break;
        }

        case CHUNK_WHENEVERS:
            for (S_ScriptWhenever *w : m_whenevers) {
                chunk.Read(w->period);
                chunk.Read(w->elapsed);
                chunk.Read(w->enabled);
            }
            break;

        case CHUNK_HUMAN_STATE:
            if (GetKind() == SCRIPT_KIND_HUMAN)
                chunk.Read(m_actorFlag);
            break;
        case CHUNK_MELEE_TRACKER_STATE:
            if (GetKind() == SCRIPT_KIND_MELEE_TRACKER)
                chunk.Read(m_actorFlag);
            break;

        default:
            return false;                               // "Unknown chunk '%d'."
        }
        chunk.Ascend();
    }
    return true;
}
