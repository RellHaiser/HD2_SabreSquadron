// The campaign tree (see campaign.h).
//
// (The original's chunk loops keep the last chunk id when a header cannot be read; here a
// failed header reads as no chunk. Only damaged files differ.)
#include "game/campaign.h"

#include <cstdio>
#include <cstring>

#include "common/chunk.h"
#include "game/pending.h"
#include "game/profile.h"

namespace {

// Chunks of the complete format (Load / Save).
enum : uint16_t {
    CHUNK_NODE_CURSOR = 4,                              // progress: a container's cursor
    CHUNK_NODE_CHILDREN = 5,
    CHUNK_CHILD_VIDEO = 0x0A,
    CHUNK_VIDEO_FILE = 0x0B,
    CHUNK_VIDEO_TITLE = 0x0C,
    CHUNK_VIDEO_BRIEFING = 0x0D,
    CHUNK_CHILD_SCREEN = 0x14,
    CHUNK_SCREEN_KIND = 0x15,
    CHUNK_SCREEN_BACK_ACROSS = 0x16,
    CHUNK_SCREEN_UNKNOWN15 = 0x17,
    CHUNK_SCREEN_ITEM = 0x18,
    CHUNK_SCREEN_UNKNOWN28 = 0x19,
    CHUNK_CHILD_PLAY = 0x1E,
    CHUNK_MISSION_OBJECTIVE = 0x28,
    CHUNK_OBJECTIVE_ID = 0x29,
    CHUNK_OBJECTIVE_STATUS = 0x2A,
    CHUNK_OBJECTIVE_BONUS = 0x2B,
    CHUNK_OBJECTIVE_REWARDED = 0x2C,                    // progress
    CHUNK_CHILD_MISSION = 0x32,
    CHUNK_MISSION_ID = 0x33,
    CHUNK_MISSION_UNKNOWN2C = 0x34,
    CHUNK_MISSION_IMAGE = 0x35,
    CHUNK_MISSION_NAME = 0x36,
    CHUNK_MISSION_TEXTS = 0x37,
    CHUNK_CHILD_OPERATION = 0x3C,
    CHUNK_OPERATION_ID = 0x3D,
    CHUNK_OPERATION_NAME = 0x3E,
    CHUNK_ROOT_ID = 0x47,
};

// Chunks of the definition format (LoadDefinition and the parsers).
enum : uint16_t {
    CHUNK_DEF_FILE_ROOT = 1,
    CHUNK_DEF_OPERATION = 2,
    CHUNK_DEF_MISSION = 3,
    CHUNK_DEF_FILE_VERSION = 3,                         // (in a file, before the root)
    CHUNK_DEF_OBJECTIVE = 4,
    CHUNK_DEF_VIDEO = 5,
    CHUNK_DEF_SCREEN = 6,
    CHUNK_DEF_PLAY = 7,
    CHUNK_DEF_STRING = 1000,
    CHUNK_DEF_SCREEN_ITEM = 10,
    CHUNK_DEF_SCREEN_UNKNOWN28 = 11,
};

// The campaign's progress in the profile's saved games.
enum : uint16_t {
    CHUNK_CAMPAIGN_CURRENT = 0x50,
    CHUNK_CAMPAIGN_VERSION = 0x51,
    CHUNK_CAMPAIGN_SINGLE_NUMBER = 0x52,
    CHUNK_CAMPAIGN_SINGLE_MISSION = 0x53,
    CHUNK_CAMPAIGN_FILES = 0x54,
    CHUNK_CAMPAIGN_FILE = 1,
};

// The id the multiplayer map's mission gets.
constexpr int MP_MISSION_ID = 2002;
// The mission without an equipment screen in a single mission.
constexpr int MISSION_WITHOUT_EQUIPMENT = 10;

const char *const CAMPAIGN_DIRECTORY = "GameData";

// A string chunk with its terminator.
void WriteStringChunk(C_chunk &chunk, uint16_t id, const C_str &text)
{
    chunk.WriteStringChunk(id, text);
}

// A container's progress chunks (the root, operations, missions) are read until the parent
// chunk ends; the others are skipped.
template <typename T> bool ReadValue(C_chunk &chunk, T &value)
{
    return chunk.Read(&value, sizeof(T));
}

} // namespace

void ReadChunkString(C_chunk &chunk, C_str &out)
{
    char buffer[1000];
    const int remaining = chunk.RemainingSize();
    const int size = remaining < 0 ? 0 : remaining < 1000 ? remaining : 999;
    chunk.Read(buffer, size);
    buffer[size] = '\0';
    out = buffer;
    chunk.Ascend();
}

// ---------------------------------------------------------------------------
// C_CampaignNode

C_CampaignNode::C_CampaignNode(const C_CampaignNode &other)   // 0x006b9e80
    : m_type(other.m_type), m_id(other.m_id), m_text(other.m_text)
{
}

C_CampaignNode::~C_CampaignNode() = default;            // 0x0066f010

// ---------------------------------------------------------------------------
// Leaves

C_CampaignVideo::~C_CampaignVideo() = default;          // 0x0066ef70

bool C_CampaignVideo::LoadDefinition(C_chunk &chunk)    // 0x006ba850
{
    ReadValue(chunk, m_briefing);
    if (!chunk.IsOpen())
        return true;
    // The file name, then the title (a chunk of another id is skipped).
    uint16_t id = 0xFFFF;
    if (chunk.Descend(id) && id == CHUNK_DEF_STRING)
        ReadChunkString(chunk, m_fileName);
    else
        chunk.Ascend();
    if (chunk.IsOpen())
        chunk.Descend(id);
    if (id == CHUNK_DEF_STRING) {
        ReadChunkString(chunk, m_title);
        return true;
    }
    chunk.Ascend();
    return true;
}

bool C_CampaignVideo::Load(C_chunk &chunk)              // 0x006bce50
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        switch (id) {
        case CHUNK_VIDEO_FILE:
            ReadChunkString(chunk, m_fileName);
            break;
        case CHUNK_VIDEO_TITLE:
            ReadChunkString(chunk, m_title);
            break;
        case CHUNK_VIDEO_BRIEFING:
            ReadValue(chunk, m_briefing);
            chunk.Ascend();
            break;
        default:
            chunk.Ascend();
            break;
        }
    }
    return true;
}

bool C_CampaignVideo::Save(C_chunk &chunk)              // 0x006bcc30
{
    chunk.WriteChunk(CHUNK_VIDEO_BRIEFING, m_briefing);
    WriteStringChunk(chunk, CHUNK_VIDEO_FILE, m_fileName);
    WriteStringChunk(chunk, CHUNK_VIDEO_TITLE, m_title);
    return true;
}

C_CampaignScreen::~C_CampaignScreen() = default;        // 0x006b9ef0

bool C_CampaignScreen::LoadDefinition(C_chunk &chunk)   // 0x006bab50
{
    bool ok = ReadValue(chunk, m_kind) && ReadValue(chunk, m_unknown15) && ReadValue(chunk, m_backAcross);
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        if (id == CHUNK_DEF_SCREEN_ITEM) {
            // (A short chunk still adds an item.)
            S_CampaignItem item = {};
            chunk.Read(&item, sizeof(item));
            m_items.push_back(item);
        } else if (id == CHUNK_DEF_SCREEN_UNKNOWN28) {
            int value = 0;
            ReadValue(chunk, value);
            m_diaryTexts.push_back(value);
        }
        chunk.Ascend();
    }
    return ok;
}

bool C_CampaignScreen::Load(C_chunk &chunk)             // 0x006bd330
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        switch (id) {
        case CHUNK_SCREEN_KIND:
            ReadValue(chunk, m_kind);
            break;
        case CHUNK_SCREEN_BACK_ACROSS:
            ReadValue(chunk, m_backAcross);
            break;
        case CHUNK_SCREEN_UNKNOWN15:
            ReadValue(chunk, m_unknown15);
            break;
        case CHUNK_SCREEN_ITEM: {
            S_CampaignItem item = {};
            chunk.Read(&item, sizeof(item));
            m_items.push_back(item);
            break;
        }
        case CHUNK_SCREEN_UNKNOWN28: {
            int value = 0;
            ReadValue(chunk, value);
            m_diaryTexts.push_back(value);
            break;
        }
        default:
            break;
        }
        chunk.Ascend();
    }
    return true;
}

bool C_CampaignScreen::Save(C_chunk &chunk)             // 0x006bd160
{
    chunk.WriteChunk(CHUNK_SCREEN_KIND, m_kind);
    chunk.WriteChunk(CHUNK_SCREEN_UNKNOWN15, m_unknown15);
    chunk.WriteChunk(CHUNK_SCREEN_BACK_ACROSS, m_backAcross);
    for (const S_CampaignItem &item : m_items)
        chunk.WriteChunk(CHUNK_SCREEN_ITEM, item);
    for (int value : m_diaryTexts)
        chunk.WriteChunk(CHUNK_SCREEN_UNKNOWN28, value);
    return true;
}

C_CampaignPlay::~C_CampaignPlay() = default;            // 0x006b9ff0

S_MissionObjective::~S_MissionObjective() = default;    // 0x006b5610

bool S_MissionObjective::LoadDefinition(C_chunk &chunk) // 0x006bae20
{
    if (!chunk.IsOpen())
        return false;
    ReadValue(chunk, m_id);
    return ReadValue(chunk, status);
}

bool S_MissionObjective::Load(C_chunk &chunk)           // 0x006bd810
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        if (id == CHUNK_OBJECTIVE_ID)
            ReadValue(chunk, m_id);
        else if (id == CHUNK_OBJECTIVE_STATUS)
            ReadValue(chunk, status);
        else if (id == CHUNK_OBJECTIVE_BONUS)
            ReadValue(chunk, bonus);
        chunk.Ascend();
    }
    return true;
}

bool S_MissionObjective::Save(C_chunk &chunk)           // 0x006bd620
{
    // (The status is saved as inactive: the campaign files start without states.)
    status = OBJECTIVE_INACTIVE;
    chunk.WriteChunk(CHUNK_OBJECTIVE_ID, m_id);
    chunk.WriteChunk(CHUNK_OBJECTIVE_STATUS, status);
    chunk.WriteChunk(CHUNK_OBJECTIVE_BONUS, bonus);
    return true;
}

bool S_MissionObjective::LoadProgress(C_chunk &chunk)   // 0x006beff0
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        if (id == CHUNK_OBJECTIVE_STATUS)
            ReadValue(chunk, status);
        else if (id == CHUNK_OBJECTIVE_REWARDED)
            ReadValue(chunk, rewarded);
        chunk.Ascend();
    }
    return true;
}

bool S_MissionObjective::SaveProgress(C_chunk &chunk)   // 0x006bee00
{
    chunk.WriteChunk(CHUNK_OBJECTIVE_STATUS, status);
    chunk.WriteChunk(CHUNK_OBJECTIVE_REWARDED, rewarded);
    return true;
}

S_MpObjective::~S_MpObjective() = default;              // 0x006b6000

// ---------------------------------------------------------------------------
// C_CampaignContainer

C_CampaignContainer::~C_CampaignContainer()             // 0x006b57f0
{
    C_CampaignContainer::Clear();
}

void C_CampaignContainer::Clear()                       // 0x006b58c0
{
    while (!m_children.empty()) {
        delete m_children.back();
        m_children.pop_back();
    }
}

bool C_CampaignContainer::Next()                        // 0x006b85a0
{
    if (m_cursor < 0) {
        ++m_cursor;
        return true;
    }
    if (m_cursor < GetChildCount()) {
        if (m_children[m_cursor]->Next())
            return true;
        ++m_cursor;
        if (m_cursor < GetChildCount())
            return true;
    }
    return false;
}

bool C_CampaignContainer::Prev()                        // 0x006b8600
{
    if (m_cursor < 0)
        return false;
    if (m_cursor == GetChildCount()) {
        --m_cursor;
        m_children[m_cursor]->Prev();
        return true;
    }
    if (!m_children[m_cursor]->Prev()) {
        --m_cursor;
        if (m_cursor >= 0)
            m_children[m_cursor]->Prev();
    }
    return true;
}

C_CampaignNode *C_CampaignContainer::GetCurrent()       // 0x006b8670
{
    if (m_cursor >= 0 && m_cursor < GetChildCount())
        return m_children[m_cursor]->GetCurrent();
    return this;
}

int C_CampaignContainer::GetCursorState() const         // 0x006b86a0
{
    if (m_cursor < 0)
        return 0;
    return m_cursor < GetChildCount() ? 1 : 2;
}

C_CampaignNode *C_CampaignContainer::GetChild(int index) const   // 0x006b51c0
{
    if (index < 0 || index >= GetChildCount())
        return nullptr;
    return m_children[index];
}

int C_CampaignContainer::CountChildren(E_CampaignNodeType type) const   // 0x006b8710 / 0x006b8800
{
    int count = 0;
    for (const C_CampaignNode *child : m_children) {
        if (child->GetType() == type)
            ++count;
    }
    return count;
}

C_CampaignNode *C_CampaignContainer::GetChildOfType(E_CampaignNodeType type, int index, bool select)   // 0x006b8740 / 0x006b8830
{
    int found = 0;
    for (int i = 0; i < GetChildCount(); ++i) {
        if (m_children[i]->GetType() != type)
            continue;
        if (found == index) {
            if (select) {
                m_children[i]->Reset();
                m_cursor = i;
            }
            return m_children[i];
        }
        ++found;
    }
    return nullptr;
}

int C_CampaignContainer::GetCurrentIndexOfType(E_CampaignNodeType type) const   // 0x006b87a0 / 0x006b8890
{
    if (m_cursor < 0 || m_cursor >= GetChildCount())
        return -1;
    for (int i = 0; i < CountChildren(type); ++i) {
        if (const_cast<C_CampaignContainer *>(this)->GetChildOfType(type, i, false) == m_children[m_cursor])
            return i;
    }
    return -1;
}

bool C_CampaignContainer::LoadChildren(C_chunk &chunk)  // 0x006bc8c0
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        C_CampaignNode *child = nullptr;
        switch (id) {
        case CHUNK_CHILD_VIDEO:
            child = new C_CampaignVideo();
            break;
        case CHUNK_CHILD_SCREEN:
            child = new C_CampaignScreen();
            break;
        case CHUNK_CHILD_PLAY:
            child = new C_CampaignPlay();
            break;
        case CHUNK_CHILD_MISSION:
            child = new C_MissionDef();
            break;
        case CHUNK_CHILD_OPERATION:
            child = new C_CampaignOperation();
            break;
        default:
            break;
        }
        if (child) {
            child->Load(chunk);
            AddChild(child);
        }
        chunk.Ascend();
    }
    return true;
}

bool C_CampaignContainer::SaveChildren(C_chunk &chunk)  // 0x006bc5e0
{
    for (C_CampaignNode *child : m_children) {
        uint16_t id;
        switch (child->GetType()) {
        case CAMPAIGN_NODE_OPERATION: id = CHUNK_CHILD_OPERATION; break;
        case CAMPAIGN_NODE_MISSION: id = CHUNK_CHILD_MISSION; break;
        case CAMPAIGN_NODE_VIDEO: id = CHUNK_CHILD_VIDEO; break;
        case CAMPAIGN_NODE_SCREEN: id = CHUNK_CHILD_SCREEN; break;
        case CAMPAIGN_NODE_PLAY: id = CHUNK_CHILD_PLAY; break;
        default: continue;
        }
        chunk.Begin(id);
        child->Save(chunk);
        chunk.Ascend();
    }
    return true;
}

bool C_CampaignContainer::LoadChildrenProgress(C_chunk &chunk)   // 0x006beb60
{
    // (The last chunk id read stays when a header cannot be read.)
    uint16_t id = 0xFFFF;
    for (C_CampaignNode *child : m_children) {
        uint16_t expected;
        if (child->GetType() == CAMPAIGN_NODE_OPERATION)
            expected = CHUNK_CHILD_OPERATION;
        else if (child->GetType() == CAMPAIGN_NODE_MISSION)
            expected = CHUNK_CHILD_MISSION;
        else
            continue;
        if (chunk.RemainingSize() <= 0)
            return false;
        chunk.Descend(id);
        if (id == expected)
            child->LoadProgress(chunk);
        chunk.Ascend();
    }
    return true;
}

bool C_CampaignContainer::SaveChildrenProgress(C_chunk &chunk)   // 0x006be990
{
    for (C_CampaignNode *child : m_children) {
        uint16_t id;
        if (child->GetType() == CAMPAIGN_NODE_OPERATION)
            id = CHUNK_CHILD_OPERATION;
        else if (child->GetType() == CAMPAIGN_NODE_MISSION)
            id = CHUNK_CHILD_MISSION;
        else
            continue;
        chunk.Begin(id);
        child->SaveProgress(chunk);
        chunk.Ascend();
    }
    return true;
}

bool C_CampaignContainer::LoadCursorProgress(C_chunk &chunk)
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        if (id == CHUNK_NODE_CURSOR)
            ReadValue(chunk, m_cursor);
        else if (id == CHUNK_NODE_CHILDREN)
            LoadChildrenProgress(chunk);
        chunk.Ascend();
    }
    return true;
}

void C_CampaignContainer::SaveCursorProgress(C_chunk &chunk)
{
    chunk.WriteChunk(CHUNK_NODE_CURSOR, m_cursor);
    chunk.Begin(CHUNK_NODE_CHILDREN);
    SaveChildrenProgress(chunk);
    chunk.Ascend();
}

// ---------------------------------------------------------------------------
// C_CampaignRoot / C_CampaignOperation

C_CampaignRoot::~C_CampaignRoot() = default;            // 0x006bbc00

bool C_CampaignRoot::LoadDefinition(C_chunk &chunk)     // 0x006bb3c0
{
    // (Three ints that are not kept, then the id.)
    int unused = 0;
    if (!ReadValue(chunk, unused) || !ReadValue(chunk, unused) || !ReadValue(chunk, unused))
        return false;
    return ReadValue(chunk, m_id);
}

bool C_CampaignRoot::Load(C_chunk &chunk)               // 0x006be6f0
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        if (id == CHUNK_NODE_CHILDREN)
            LoadChildren(chunk);
        else if (id == CHUNK_ROOT_ID)
            ReadValue(chunk, m_id);
        chunk.Ascend();
    }
    return true;
}

bool C_CampaignRoot::Save(C_chunk &chunk)               // 0x006be590
{
    chunk.WriteChunk(CHUNK_ROOT_ID, m_id);
    chunk.Begin(CHUNK_NODE_CHILDREN);
    SaveChildren(chunk);
    chunk.Ascend();
    return true;
}

bool C_CampaignRoot::LoadProgress(C_chunk &chunk)       // 0x006bfc90
{
    return LoadCursorProgress(chunk);
}

bool C_CampaignRoot::SaveProgress(C_chunk &chunk)       // 0x006bfb30
{
    SaveCursorProgress(chunk);
    return true;
}

C_CampaignOperation::~C_CampaignOperation() = default;  // 0x006bbf80

bool C_CampaignOperation::LoadDefinition(C_chunk &chunk)   // 0x006bb1b0
{
    const bool ok = ReadValue(chunk, m_id);
    if (!chunk.IsOpen())
        return ok;
    uint16_t id = 0xFFFF;
    if (chunk.Descend(id) && id == CHUNK_DEF_STRING) {
        ReadChunkString(chunk, m_name);
        return ok;
    }
    chunk.Ascend();
    return ok;
}

bool C_CampaignOperation::Load(C_chunk &chunk)          // 0x006be280
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        if (id == CHUNK_NODE_CHILDREN) {
            LoadChildren(chunk);
        } else if (id == CHUNK_OPERATION_ID) {
            ReadValue(chunk, m_id);
        } else if (id == CHUNK_OPERATION_NAME) {
            ReadChunkString(chunk, m_name);
            continue;
        }
        chunk.Ascend();
    }
    return true;
}

bool C_CampaignOperation::Save(C_chunk &chunk)          // 0x006be090
{
    chunk.WriteChunk(CHUNK_OPERATION_ID, m_id);
    WriteStringChunk(chunk, CHUNK_OPERATION_NAME, m_name);
    chunk.Begin(CHUNK_NODE_CHILDREN);
    SaveChildren(chunk);
    chunk.Ascend();
    return true;
}

bool C_CampaignOperation::LoadProgress(C_chunk &chunk)  // 0x006bf890
{
    return LoadCursorProgress(chunk);
}

bool C_CampaignOperation::SaveProgress(C_chunk &chunk)  // 0x006bf730
{
    SaveCursorProgress(chunk);
    return true;
}

// ---------------------------------------------------------------------------
// C_MissionDef

C_MissionDef::C_MissionDef(E_CampaignNodeType type)     // 0x006b5160
    : C_CampaignContainer(type)
{
}

C_MissionDef::~C_MissionDef()                           // 0x006b5650
{
    for (size_t i = m_objectives.size(); i-- > 0;)
        delete m_objectives[i];
}

S_MissionObjective *C_MissionDef::GetObjective(int index) const
{
    if (index < 0 || index >= GetObjectiveCount())
        return nullptr;
    return m_objectives[index];
}

void C_MissionDef::DeactivateObjectives()               // 0x006b86e0
{
    for (S_MissionObjective *objective : m_objectives)
        objective->status = OBJECTIVE_INACTIVE;
}

void C_MissionDef::Reset()                              // 0x006b86d0
{
    m_cursor = -1;
    DeactivateObjectives();
}

void C_MissionDef::Clear()                              // 0x006b51f0
{
    for (size_t i = m_objectives.size(); i-- > 0;)
        delete m_objectives[i];
    m_objectives.clear();
    C_CampaignContainer::Clear();
}

void C_MissionDef::CreateObjective()                    // 0x006b5440
{
    m_objectives.push_back(new S_MissionObjective());
}

void C_MissionDef::AddObjective(S_MissionObjective *objective)   // 0x006b5290
{
    m_objectives.push_back(objective);
}

bool C_MissionDef::LoadDefinition(C_chunk &chunk)       // 0x006baf00
{
    // The id, four ints that are not kept, the flag.
    ReadValue(chunk, m_id);
    int unused = 0;
    for (int i = 0; i < 4; ++i)
        ReadValue(chunk, unused);
    ReadValue(chunk, m_unknown2C);
    // The image, then the name (a chunk of another id is skipped).
    uint16_t id = 0xFFFF;
    if (chunk.Descend(id) && id == CHUNK_DEF_STRING)
        ReadChunkString(chunk, m_image);
    else
        chunk.Ascend();
    chunk.Descend(id);
    if (id == CHUNK_DEF_STRING) {
        ReadChunkString(chunk, m_name);
        return true;
    }
    chunk.Ascend();
    return true;
}

bool C_MissionDef::LoadChildrenDefinition(C_chunk *chunk, C_MissionDef *mission)   // 0x006bc320
{
    if (!chunk || !mission)
        return false;
    while (chunk->RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk->Descend(id);
        switch (id) {
        case CHUNK_DEF_OBJECTIVE: {
            auto *objective = new S_MissionObjective();
            objective->LoadDefinition(*chunk);
            mission->AddObjective(objective);
            break;
        }
        case CHUNK_DEF_VIDEO: {
            auto *video = new C_CampaignVideo();
            video->LoadDefinition(*chunk);
            mission->AddChild(video);
            break;
        }
        case CHUNK_DEF_SCREEN: {
            auto *screen = new C_CampaignScreen();
            screen->LoadDefinition(*chunk);
            mission->AddChild(screen);
            break;
        }
        case CHUNK_DEF_PLAY: {
            auto *play = new C_CampaignPlay();
            play->LoadDefinition(*chunk);
            mission->AddChild(play);
            break;
        }
        default:
            break;
        }
        chunk->Ascend();
    }
    return true;
}

bool C_MissionDef::Load(C_chunk &chunk)                 // 0x006bdcb0
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        switch (id) {
        case CHUNK_NODE_CHILDREN:
            LoadChildren(chunk);
            break;
        case CHUNK_MISSION_OBJECTIVE: {
            auto *objective = new S_MissionObjective();
            objective->Load(chunk);
            chunk.Ascend();
            AddObjective(objective);
            continue;
        }
        case CHUNK_MISSION_ID:
            ReadValue(chunk, m_id);
            break;
        case CHUNK_MISSION_UNKNOWN2C:
            ReadValue(chunk, m_unknown2C);
            break;
        case CHUNK_MISSION_IMAGE:
            ReadChunkString(chunk, m_image);
            continue;
        case CHUNK_MISSION_NAME:
            ReadChunkString(chunk, m_name);
            continue;
        case CHUNK_MISSION_TEXTS:
            chunk.Read(m_texts, sizeof(m_texts));
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    return true;
}

bool C_MissionDef::Save(C_chunk &chunk)                 // 0x006bda60
{
    chunk.WriteChunk(CHUNK_MISSION_ID, m_id);
    chunk.WriteChunk(CHUNK_MISSION_UNKNOWN2C, m_unknown2C);
    WriteStringChunk(chunk, CHUNK_MISSION_IMAGE, m_image);
    WriteStringChunk(chunk, CHUNK_MISSION_NAME, m_name);
    chunk.WriteChunk(CHUNK_MISSION_TEXTS, m_texts, sizeof(m_texts));
    for (S_MissionObjective *objective : m_objectives) {
        chunk.Begin(CHUNK_MISSION_OBJECTIVE);
        objective->Save(chunk);
        chunk.Ascend();
    }
    chunk.Begin(CHUNK_NODE_CHILDREN);
    SaveChildren(chunk);
    chunk.Ascend();
    return true;
}

bool C_MissionDef::LoadProgress(C_chunk &chunk)         // 0x006bf440
{
    int objective = 0;
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        if (id == CHUNK_NODE_CURSOR) {
            ReadValue(chunk, m_cursor);
        } else if (id == CHUNK_NODE_CHILDREN) {
            LoadChildrenProgress(chunk);
        } else if (id == CHUNK_MISSION_OBJECTIVE) {
            if (S_MissionObjective *target = GetObjective(objective))
                target->LoadProgress(chunk);
            ++objective;
        }
        chunk.Ascend();
    }
    return true;
}

bool C_MissionDef::SaveProgress(C_chunk &chunk)         // 0x006bf1e0
{
    for (S_MissionObjective *objective : m_objectives) {
        chunk.Begin(CHUNK_MISSION_OBJECTIVE);
        objective->SaveProgress(chunk);
        chunk.Ascend();
    }
    SaveCursorProgress(chunk);
    return true;
}

// ---------------------------------------------------------------------------
// C_MpMissionDef

C_MpMissionDef::C_MpMissionDef()                        // 0x006b5940
    : C_MissionDef(CAMPAIGN_NODE_MP_MISSION)
{
}

C_MpMissionDef::~C_MpMissionDef() = default;            // 0x006b5a10

S_MpObjective *C_MpMissionDef::GetMpObjective(int index) const
{
    return static_cast<S_MpObjective *>(GetObjective(index));
}

void C_MpMissionDef::Clear()                            // 0x006b5bb0
{
    for (size_t i = m_objectives.size(); i-- > 0;)
        delete m_objectives[i];
    m_objectives.clear();
    m_items.clear();
    C_CampaignContainer::Clear();
}

void C_MpMissionDef::CreateObjective()                  // 0x006b5e30
{
    m_objectives.push_back(new S_MpObjective());
}

void C_MpMissionDef::AddMpObjective(S_MissionObjective *objective)   // 0x006b5c80
{
    m_objectives.push_back(objective);
}

// ---------------------------------------------------------------------------
// C_Campaign

C_Campaign::~C_Campaign()                               // 0x006b6420
{
    for (size_t i = m_campaigns.size(); i-- > 0;)
        delete m_campaigns[i];
    m_campaigns.clear();
    m_singleMission.Clear();
}

void C_Campaign::Clear()                                // 0x006b8c60
{
    for (C_CampaignRoot *campaign : m_campaigns)
        delete campaign;
    m_campaigns.clear();
    m_unknownF0 = false;
}

bool C_Campaign::LoadFiles()                            // 0x006bb860
{
    C_chunk chunk;
    for (int index = 0;; ++index) {
        char path[256];
        std::snprintf(path, sizeof(path), "%s\\Gamedata%02d.gdt", CAMPAIGN_DIRECTORY, index);
        // (A file that is still open makes the next Open fail: the loading ends there.)
        if (!chunk.Open(path, false))
            break;
        auto *root = new C_CampaignRoot();
        uint16_t id = 0xFFFF;
        bool current = false;
        bool ok = chunk.Descend(id) && id == CHUNK_DEF_FILE_VERSION;
        if (ok) {
            int version = 0;
            chunk.Read(version);
            chunk.Ascend();
            current = version == m_version;
            ok = chunk.Descend(id) && id == CHUNK_DEF_FILE_ROOT;
        }
        if (!ok) {
            // Fix: the original leaked the root of a file it did not recognize.
            delete root;
            continue;
        }
        if (current) {
            root->Load(chunk);
        } else {
            root->LoadDefinition(chunk);
            LoadRootDefinition(&chunk, root);
        }
        m_campaigns.push_back(root);
        chunk.Ascend();
        chunk.Close();
    }
    chunk.Close();
    return !m_campaigns.empty();
}

bool C_Campaign::LoadRootDefinition(C_chunk *chunk, C_CampaignRoot *root)   // 0x006bbcd0
{
    if (!chunk || !root)
        return false;
    while (chunk->RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk->Descend(id);
        if (id == CHUNK_DEF_OPERATION) {
            auto *operation = new C_CampaignOperation();
            operation->LoadDefinition(*chunk);
            LoadOperationDefinition(chunk, operation);
            root->AddChild(operation);
        } else if (id == CHUNK_DEF_VIDEO) {
            auto *video = new C_CampaignVideo();
            video->LoadDefinition(*chunk);
            root->AddChild(video);
        } else if (id == CHUNK_DEF_SCREEN) {
            auto *screen = new C_CampaignScreen();
            screen->LoadDefinition(*chunk);
            root->AddChild(screen);
        }
        chunk->Ascend();
    }
    return true;
}

bool C_Campaign::LoadOperationDefinition(C_chunk *chunk, C_CampaignOperation *operation)   // 0x006bc060
{
    if (!chunk || !operation)
        return false;
    while (chunk->RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk->Descend(id);
        if (id == CHUNK_DEF_MISSION) {
            auto *mission = new C_MissionDef();
            mission->LoadDefinition(*chunk);
            C_MissionDef::LoadChildrenDefinition(chunk, mission);
            operation->AddChild(mission);
        } else if (id == CHUNK_DEF_VIDEO) {
            auto *video = new C_CampaignVideo();
            video->LoadDefinition(*chunk);
            operation->AddChild(video);
        } else if (id == CHUNK_DEF_SCREEN) {
            auto *screen = new C_CampaignScreen();
            screen->LoadDefinition(*chunk);
            operation->AddChild(screen);
        }
        chunk->Ascend();
    }
    return true;
}

C_CampaignContainer *C_Campaign::GetFlowNode()
{
    if (g_playMode->singleMission == 1)
        return &m_singleMission;
    return m_campaigns[m_current];
}

bool C_Campaign::AdvanceToMission()                     // 0x006b8d40
{
    if (g_playMode->singleMission == 1) {
        m_singleMission.Reset();
        return true;
    }
    if (m_current >= GetCount())
        return false;
    C_CampaignRoot *root = m_campaigns[m_current];
    if (!root)
        return false;
    for (;;) {
        root->Next();
        switch (root->GetCurrent()->GetType()) {
        case CAMPAIGN_NODE_END:
        case CAMPAIGN_NODE_ROOT:
            return false;
        case CAMPAIGN_NODE_OPERATION:
        case CAMPAIGN_NODE_MISSION:
            return true;
        default:
            break;
        }
    }
}

C_CampaignNode *C_Campaign::NextStep()                  // 0x006b8dc0
{
    if (m_current >= GetCount())
        return nullptr;
    C_CampaignContainer *flow = GetFlowNode();
    for (;;) {
        flow->Next();
        C_CampaignNode *step = flow->GetCurrent();
        switch (step->GetType()) {
        case CAMPAIGN_NODE_OPERATION:
        case CAMPAIGN_NODE_MISSION:
            break;
        case CAMPAIGN_NODE_VIDEO:
        case CAMPAIGN_NODE_SCREEN:
        case CAMPAIGN_NODE_PLAY:
            return step;
        default:
            return nullptr;
        }
    }
}

C_CampaignNode *C_Campaign::PreviousScreen()            // 0x006b8e40
{
    if (m_current >= GetCount())
        return nullptr;
    C_CampaignContainer *flow = GetFlowNode();
    C_CampaignNode *current = flow->GetCurrent();
    if (current->GetType() != CAMPAIGN_NODE_SCREEN)
        return nullptr;
    const bool backAcross = static_cast<C_CampaignScreen *>(current)->GoesBackAcross();
    // (Where the search stops without a screen, the cursor stays moved back.)
    for (;;) {
        flow->Prev();
        C_CampaignNode *step = flow->GetCurrent();
        switch (step->GetType()) {
        case CAMPAIGN_NODE_OPERATION:
        case CAMPAIGN_NODE_MISSION:
            if (!backAcross)
                return nullptr;
            break;
        case CAMPAIGN_NODE_OBJECTIVE:
        case CAMPAIGN_NODE_VIDEO:
        case CAMPAIGN_NODE_PLAY:
            break;
        case CAMPAIGN_NODE_SCREEN:
            return step;
        default:
            return nullptr;
        }
    }
}

C_MissionDef *C_Campaign::GetCurrentMission()           // 0x006b94b0
{
    if (g_playMode->singleMission == 1)
        return &m_singleMission;
    if (g_mpSettings->sessionType != 0)
        return &m_mpMission;
    if (m_current >= GetCount())
        return nullptr;
    C_CampaignRoot *root = m_campaigns[m_current];
    C_CampaignNode *node = root->GetChild(root->m_cursor);
    if (!node || node->GetType() != CAMPAIGN_NODE_OPERATION)
        return nullptr;
    auto *operation = static_cast<C_CampaignOperation *>(node);
    node = operation->GetChild(operation->m_cursor);
    if (!node || node->GetType() != CAMPAIGN_NODE_MISSION)
        return nullptr;
    return static_cast<C_MissionDef *>(node);
}

bool C_Campaign::FindMission(int campaign, int missionNumber, int *operation, int *mission)   // 0x006b8980
{
    if (campaign < 0 || campaign >= GetCount())
        return false;
    *operation = 0;
    *mission = 0;
    C_CampaignRoot *root = m_campaigns[campaign];
    if (missionNumber < 0)
        return false;
    for (int i = 0; i < root->CountChildren(CAMPAIGN_NODE_OPERATION); ++i) {
        C_CampaignNode *node = root->GetChildOfType(CAMPAIGN_NODE_OPERATION, i, false);
        if (!node)
            return false;
        const int count = static_cast<C_CampaignContainer *>(node)->CountChildren(CAMPAIGN_NODE_MISSION);
        if (missionNumber < count) {
            *operation = i;
            *mission = missionNumber;
            return true;
        }
        missionNumber -= count;
    }
    return false;
}

bool C_Campaign::GetMissionNumberOf(int campaign, int *number, int operation, int mission)   // 0x006b88f0
{
    if (campaign < 0 || campaign >= GetCount())
        return false;
    *number = 0;
    C_CampaignRoot *root = m_campaigns[campaign];
    for (int i = 0; i < root->CountChildren(CAMPAIGN_NODE_OPERATION); ++i) {
        C_CampaignNode *node = root->GetChildOfType(CAMPAIGN_NODE_OPERATION, i, false);
        if (!node)
            return false;
        const int count = static_cast<C_CampaignContainer *>(node)->CountChildren(CAMPAIGN_NODE_MISSION);
        if (operation == i) {
            if (mission < count) {
                *number += mission;
                return true;
            }
            return false;
        }
        *number += count;
    }
    return false;
}

int C_Campaign::GetMissionNumber(int campaign)          // 0x006b91e0
{
    if (campaign < 0 || campaign >= GetCount())
        return -1;
    if (g_playMode->singleMission == 1)
        return m_singleMissionNumber;
    C_CampaignRoot *root = m_campaigns[campaign];
    C_CampaignNode *node = root->GetChild(root->m_cursor);
    if (!node || node->GetType() != CAMPAIGN_NODE_OPERATION)
        return -1;
    const int operation = root->GetCurrentIndexOfType(CAMPAIGN_NODE_OPERATION);
    const int mission = static_cast<C_CampaignOperation *>(node)->GetCurrentIndexOfType(CAMPAIGN_NODE_MISSION);
    if (mission < 0)
        return -1;
    int number = 0;
    if (!GetMissionNumberOf(campaign, &number, operation, mission))
        return -1;
    return number;
}

int C_Campaign::GetOperationIndex(int campaign)         // 0x006b92b0
{
    if (campaign < 0 || campaign >= GetCount())
        return -1;
    if (g_playMode->singleMission == 1) {
        int operation = -1;
        int mission = 0;
        FindMission(campaign, m_singleMissionNumber, &operation, &mission);
        return operation;
    }
    return m_campaigns[campaign]->GetCurrentIndexOfType(CAMPAIGN_NODE_OPERATION);
}

int C_Campaign::CountOperations(int campaign)           // 0x006b9320
{
    if (campaign < 0 || campaign >= GetCount())
        return -1;
    return m_campaigns[campaign]->CountChildren(CAMPAIGN_NODE_OPERATION);
}

int C_Campaign::CountMissions(int campaign)             // 0x006b9350
{
    if (campaign < 0 || campaign >= GetCount())
        return -1;
    C_CampaignRoot *root = m_campaigns[campaign];
    int count = 0;
    for (int i = 0; i < root->GetChildCount(); ++i) {
        C_CampaignNode *child = root->GetChild(i);
        if (child->GetType() == CAMPAIGN_NODE_OPERATION)
            count += static_cast<C_CampaignContainer *>(child)->CountChildren(CAMPAIGN_NODE_MISSION);
    }
    return count;
}

int C_Campaign::CountOperationMissions(int campaign, int operation)   // 0x006b93c0
{
    if (campaign < 0 || campaign >= GetCount())
        return -1;
    C_CampaignNode *node = m_campaigns[campaign]->GetChildOfType(CAMPAIGN_NODE_OPERATION, operation, false);
    return node ? static_cast<C_CampaignContainer *>(node)->CountChildren(CAMPAIGN_NODE_MISSION) : -1;
}

int C_Campaign::GetOperationId(int campaign, int operation)   // 0x006b8f60
{
    if (campaign < 0 || campaign >= GetCount())
        return -1;
    C_CampaignNode *node = m_campaigns[campaign]->GetChildOfType(CAMPAIGN_NODE_OPERATION, operation, false);
    return node ? node->GetId() : -1;
}

const C_str &C_Campaign::GetOperationName(int campaign, int operation)   // 0x006b8fa0
{
    static const C_str none;                            // 0x008aea50
    if (campaign < 0 || campaign >= GetCount())
        return none;
    C_CampaignNode *node = m_campaigns[campaign]->GetChildOfType(CAMPAIGN_NODE_OPERATION, operation, false);
    return node ? static_cast<C_CampaignOperation *>(node)->GetName() : none;
}

int C_Campaign::GetMissionId(int campaign, int missionNumber)   // 0x006b9040
{
    int operation = 0;
    int mission = 0;
    if (campaign < 0 || campaign >= GetCount() || !FindMission(campaign, missionNumber, &operation, &mission))
        return -1;
    C_CampaignNode *node = m_campaigns[campaign]->GetChildOfType(CAMPAIGN_NODE_OPERATION, operation, false);
    if (!node)
        return -1;
    node = static_cast<C_CampaignContainer *>(node)->GetChildOfType(CAMPAIGN_NODE_MISSION, mission, false);
    return node ? node->GetId() : -1;
}

const C_str &C_Campaign::GetMissionImage(int campaign, int missionNumber)   // 0x006b90e0
{
    static const C_str none;                            // 0x008aea48
    int operation = 0;
    int mission = 0;
    if (campaign < 0 || campaign >= GetCount() || !FindMission(campaign, missionNumber, &operation, &mission))
        return none;
    C_CampaignNode *node = m_campaigns[campaign]->GetChildOfType(CAMPAIGN_NODE_OPERATION, operation, false);
    if (!node)
        return none;
    node = static_cast<C_CampaignContainer *>(node)->GetChildOfType(CAMPAIGN_NODE_MISSION, mission, false);
    return node ? static_cast<C_MissionDef *>(node)->GetImage() : none;
}

int C_Campaign::GetMissionText(int campaign, int missionNumber, int index)   // 0x006b9420
{
    int operation = 0;
    int mission = 0;
    if (campaign < 0 || campaign >= GetCount() || !FindMission(campaign, missionNumber, &operation, &mission))
        return -1;
    C_CampaignNode *node = m_campaigns[campaign]->GetChildOfType(CAMPAIGN_NODE_OPERATION, operation, false);
    if (!node)
        return -1;
    node = static_cast<C_CampaignContainer *>(node)->GetChildOfType(CAMPAIGN_NODE_MISSION, mission, false);
    if (!node || index < 0 || index >= C_MissionDef::TEXT_COUNT)
        return -1;
    return static_cast<C_MissionDef *>(node)->GetText(index);
}

int C_Campaign::GetObjectiveStatus(int campaign, int operation, int mission, int objective)   // 0x006b8ed0
{
    // Fix: campaign 0 read the pointer before the vector.
    if (campaign < 1 || campaign > GetCount())
        return -1;
    C_CampaignRoot *root = m_campaigns[campaign - 1];
    if (!root)
        return -1;
    C_CampaignNode *node = root->GetChildOfType(CAMPAIGN_NODE_OPERATION, operation - 1, false);
    if (!node)
        return -1;
    node = static_cast<C_CampaignContainer *>(node)->GetChildOfType(CAMPAIGN_NODE_MISSION, mission - 1, false);
    if (!node)
        return -1;
    const S_MissionObjective *target = static_cast<C_MissionDef *>(node)->GetObjective(objective);
    return target ? target->status : OBJECTIVE_INACTIVE;
}

bool C_Campaign::CollectEquipment(C_CampaignScreen *screen, C_CampaignOperation *operation, int mission)   // 0x006b96e0
{
    for (; mission >= 0; --mission) {
        C_CampaignNode *node = operation->GetChildOfType(CAMPAIGN_NODE_MISSION, mission, false);
        if (!node)
            return false;
        auto *source = static_cast<C_MissionDef *>(node);
        for (int i = 0; i < source->GetChildCount(); ++i) {
            C_CampaignNode *child = source->GetChild(i);
            if (!child || child->GetType() != CAMPAIGN_NODE_SCREEN)
                continue;
            const std::vector<S_CampaignItem> &items = static_cast<C_CampaignScreen *>(child)->GetItems();
            screen->m_items.insert(screen->m_items.end(), items.begin(), items.end());
        }
    }
    return true;
}

bool C_Campaign::SetupSingleMission(int campaign, int missionNumber)   // 0x006b99a0
{
    if (campaign < 0 || campaign >= GetCount())
        return false;
    m_singleMission.Clear();
    int operationIndex = 0;
    int missionIndex = 0;
    if (!FindMission(campaign, missionNumber, &operationIndex, &missionIndex))
        return false;
    m_singleMissionNumber = missionNumber;
    C_CampaignNode *node = m_campaigns[campaign]->GetChildOfType(CAMPAIGN_NODE_OPERATION, operationIndex, false);
    if (!node)
        return false;
    auto *operation = static_cast<C_CampaignOperation *>(node);
    node = operation->GetChildOfType(CAMPAIGN_NODE_MISSION, missionIndex, false);
    if (!node)
        return false;
    auto *source = static_cast<C_MissionDef *>(node);

    m_singleMission.m_name = source->m_name;
    m_singleMission.m_image = source->m_image;
    m_singleMission.m_unknown2C = source->m_unknown2C;
    m_singleMission.m_id = source->m_id;
    for (int i = 0; i < C_MissionDef::TEXT_COUNT; ++i)
        m_singleMission.m_texts[i] = source->m_texts[i];
    for (const S_MissionObjective *objective : source->m_objectives)
        m_singleMission.AddObjective(new S_MissionObjective(*objective));
    // The briefings.
    for (int i = 0; i < source->GetChildCount(); ++i) {
        C_CampaignNode *child = source->GetChild(i);
        if (!child || child->GetType() != CAMPAIGN_NODE_VIDEO || !static_cast<C_CampaignVideo *>(child)->m_briefing)
            continue;
        m_singleMission.AddChild(new C_CampaignVideo(*static_cast<C_CampaignVideo *>(child)));
    }
    // The screens and the mission: screen 2, the equipment, the mission, screen 5, the end.
    auto *screen = new C_CampaignScreen();
    screen->m_kind = 2;
    m_singleMission.AddChild(screen);
    if (missionNumber != MISSION_WITHOUT_EQUIPMENT) {
        screen = new C_CampaignScreen();
        screen->m_kind = C_CampaignScreen::KIND_EQUIPMENT;
        screen->m_backAcross = true;
        m_singleMission.AddChild(screen);
    }
    // (Mission 10's equipment goes to its first screen.)
    CollectEquipment(screen, operation, missionIndex);
    m_singleMission.AddChild(new C_CampaignPlay());
    screen = new C_CampaignScreen();
    screen->m_kind = 5;
    m_singleMission.AddChild(screen);
    m_singleMission.AddChild(new C_CampaignNode());
    m_singleMission.Reset();
    // (The original always returns false.)
    return false;
}

bool C_Campaign::SetupMultiplayerMission()              // 0x006b8a20
{
    C_MpMap *map = g_mpSettings->GetCurrentMap();
    m_mpMission.Clear();
    m_mpMission.m_id = MP_MISSION_ID;
    m_mpMission.m_mapName = map->GetName();
    m_mpMission.m_name = map->GetDirectory();
    for (const S_MpMapObjective &entry : map->GetObjectives()) {
        auto *objective = new S_MpObjective();
        objective->alliedText = entry.alliedText;
        objective->axisText = entry.axisText;
        objective->points = entry.points;
        objective->type = entry.type;
        objective->team = entry.team;
        objective->vipUniform = entry.vipUniform;
        m_mpMission.AddMpObjective(objective);
    }
    for (const S_MpMapItem &entry : map->GetItems()) {
        // (The fourth int of each item is stack garbage in the original.)
        m_mpMission.m_items.push_back(S_CampaignItem{entry.itemId, entry.count, entry.ammo, false});
    }
    m_mpMission.m_aircraftAttackAxis = map->aircraftAxis;
    m_mpMission.m_aircraftAttackAllied = map->aircraftAllied;
    m_mpMission.m_balanceAxis = map->balancedAxis;
    m_mpMission.m_balanceAllied = map->balancedAllied;
    m_mpMission.m_unknown60 = map->frontline;
    m_mpMission.m_briefingText = map->description;
    m_mpMission.m_presets = map->GetPresets();
    m_mpMission.Reset();
    return true;
}

bool C_Campaign::LoadGame(C_chunk &chunk)               // 0x006bff30
{
    m_singleMissionNumber = -1;
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        switch (id) {
        case CHUNK_CAMPAIGN_CURRENT:
            ReadValue(chunk, m_current);
            break;
        case CHUNK_CAMPAIGN_VERSION:
            ReadValue(chunk, m_version);
            break;
        case CHUNK_CAMPAIGN_SINGLE_NUMBER:
            ReadValue(chunk, m_singleMissionNumber);
            break;
        case CHUNK_CAMPAIGN_SINGLE_MISSION:
            // (The single mission is set up again; its saved progress is not read.)
            if (m_singleMissionNumber >= 0)
                SetupSingleMission(m_current, m_singleMissionNumber);
            break;
        case CHUNK_CAMPAIGN_FILES:
            for (C_CampaignRoot *campaign : m_campaigns) {
                if (chunk.RemainingSize() == 0)
                    break;
                uint16_t fileId = 0xFFFF;
                chunk.Descend(fileId);
                if (fileId != CHUNK_CAMPAIGN_FILE)
                    return false;
                campaign->LoadProgress(chunk);
                chunk.Ascend();
            }
            break;
        default:
            // An older layout: the first campaign's progress alone.
            // Fix: the original read the first campaign also when there was none.
            if (id != CHUNK_CAMPAIGN_FILE || chunk.RemainingSize() == 0 || m_campaigns.empty())
                return false;
            m_campaigns[0]->LoadProgress(chunk);
            break;
        }
        chunk.Ascend();
    }
    return true;
}

bool C_Campaign::SaveGame(C_chunk &chunk)               // 0x006c02f0
{
    chunk.WriteChunk(CHUNK_CAMPAIGN_CURRENT, m_current);
    chunk.WriteChunk(CHUNK_CAMPAIGN_VERSION, m_version);
    chunk.WriteChunk(CHUNK_CAMPAIGN_SINGLE_NUMBER, m_singleMissionNumber);
    chunk.Begin(CHUNK_CAMPAIGN_SINGLE_MISSION);
    m_singleMission.SaveProgress(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_CAMPAIGN_FILES);
    for (C_CampaignRoot *campaign : m_campaigns) {
        chunk.Begin(CHUNK_CAMPAIGN_FILE);
        campaign->SaveProgress(chunk);
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}
