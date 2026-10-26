// Speech of the humans: the sentence sets of Tables\speech.set and the sound codes of their
// samples (original source 0x005af7a0..0x005b0e10).
#include "game/human.h"

#include <cstdlib>
#include <new>

#include "common/chunk.h"
#include "game/game.h"

namespace {

// Chunks of Tables\speech.set: CHUNK_SPEECH_SETS holds one chunk per set (any id; the n-th is
// set n), each with the name and the sentences; a sentence chunk's id is the sentence, with one
// chunk per sample.
enum : uint16_t {
    CHUNK_SPEECH_SETS = 1000,
    CHUNK_SET_NAME_SIZE = 0,
    CHUNK_SET_NAME = 1,
    CHUNK_SET_SENTENCES = 2,
};

const char *const SPEECH_SET_FILE = "Tables\\speech.set";

// Speech sound codes start at 90 00 0000.
constexpr uint32_t SPEECH_CODE_BASE = 90000000;
constexpr int SPEECH_CODE_SET_BASE = 90;

int RandomPosition(int from, int size)
{
    return std::rand() % (size - from + 2) + from;
}

}  // namespace

// ----------------------------------------------------------------------- S_SpeechSentence

void S_SpeechSentence::SetDelay(int delay)
{
    nextTime = GetGame()->GetMissionTime() + delay;
}

bool S_SpeechSentence::IsWaiting() const
{
    return GetGame()->GetMissionTime() < nextTime;
}

bool S_SpeechSentence::CanSay() const
{
    return !IsWaiting() && HasSamples();
}

bool S_SpeechSentence::HasSamples() const
{
    return enabled && !samples.empty();
}

void S_SpeechSentence::AddSample(int sample)
{
    const int size = static_cast<int>(samples.size());
    const int pos = RandomPosition(static_cast<int>(cursor), size);
    if (pos >= 0 && pos <= size - 1)
        samples.insert(samples.begin() + pos, sample);
    else
        samples.push_back(sample);
    // (The cursor keeps its index.)
}

int S_SpeechSentence::Take()
{
    const int sample = samples.front();
    samples.erase(samples.begin());
    // The cursor stays on its sample; from the front it wraps to the end.
    if (cursor == 0)
        cursor = samples.size();
    else
        --cursor;
    const int size = static_cast<int>(samples.size());
    const int pos = cursor == 0 ? std::rand() % (size + 1) + 1 : RandomPosition(static_cast<int>(cursor), size);
    if (pos >= 0 && pos <= size - 1)
        samples.insert(samples.begin() + pos, sample);
    else
        samples.push_back(sample);
    return sample;
}

// ---------------------------------------------------------------------------- C_SpeechSet

C_SpeechSet::~C_SpeechSet()
{
    m_refCount = 0;
    ::operator delete(m_unknown00);
}

int C_SpeechSet::Say(int sentence, int delay)
{
    if (sentence < 0 || sentence >= SENTENCE_COUNT)
        return -1;
    S_SpeechSentence &entry = m_sentences[sentence];
    if (!entry.CanSay())
        return -1;
    if (delay > 0)
        entry.SetDelay(delay);
    return entry.Take();
}

int C_SpeechSet::SayNow(int sentence)
{
    if (sentence < 0 || sentence >= SENTENCE_COUNT)
        return -1;
    S_SpeechSentence &entry = m_sentences[sentence];
    if (!entry.HasSamples())
        return -1;
    return entry.Take();
}

void C_SpeechSet::SetDelay(int sentence, int delay)
{
    if (sentence >= 0 && sentence < SENTENCE_COUNT && m_sentences[sentence].enabled)
        m_sentences[sentence].SetDelay(delay);
}

// --------------------------------------------------------------------------- C_SpeechSets

C_SpeechSet *C_SpeechSets::Acquire(int id)
{
    for (C_SpeechSet *set : m_sets) {
        if (set->m_id == id) {
            ++set->m_refCount;
            return set;
        }
    }

    C_chunk file;
    if (!file.Open(SPEECH_SET_FILE, false))
        return nullptr;
    uint16_t chunkId = 0;
    file.Descend(chunkId);
    if (chunkId != CHUNK_SPEECH_SETS) {
        file.Close();
        return nullptr;
    }

    C_SpeechSet *set = nullptr;
    int index = -1;
    do {
        // Skip to the id-th set.
        bool found = false;
        while (!file.AtEnd() && file.Descend(chunkId)) {
            if (++index == id) {
                found = true;
                break;
            }
            file.Ascend();
        }
        if (!found)
            break;
        while (!file.AtEnd() && file.Descend(chunkId)) {
            if (chunkId == CHUNK_SET_NAME_SIZE) {
                int nameSize = 0;
                file.Read(nameSize);
                file.Ascend();
            } else if (chunkId == CHUNK_SET_NAME) {
                // Fix: the original reads the name (nameSize bytes, unchecked) into a stack buffer
                // it never uses; it is skipped here.
                file.Ascend();
            } else if (chunkId == CHUNK_SET_SENTENCES) {
                set = new C_SpeechSet();
                set->m_id = index;
                while (!file.AtEnd()) {
                    uint16_t sentence = 0;
                    file.Descend(sentence);
                    if (sentence < C_SpeechSet::SENTENCE_COUNT) {
                        S_SpeechSentence &entry = set->m_sentences[sentence];
                        while (!file.AtEnd()) {
                            uint16_t sampleChunk;
                            file.Descend(sampleChunk);
                            entry.enabled = true;
                            int sample = 0;
                            file.Read(sample);
                            file.Ascend();
                            entry.AddSample(sample);
                        }
                    }
                    file.Ascend();
                }
                file.Ascend();
            }
            // (Other chunks are not ascended: their content is read as the next chunks.)
        }
        file.Ascend();
    } while (!set);

    if (set) {
        m_sets.push_back(set);
        ++set->m_refCount;
    }
    file.Ascend();
    file.Close();
    return set;
}

void C_SpeechSets::Release(C_SpeechSet *set)
{
    // Fix: the original dereferences a null set (it is not in the list).
    if (!set)
        return;
    for (size_t i = 0; i < m_sets.size(); ++i) {
        if (m_sets[i] != set)
            continue;
        if (--set->m_refCount > 0)
            return;
        m_sets[i] = m_sets.back();
        m_sets.pop_back();
        delete set;
        return;
    }
    // A set the list does not know.
    if (--set->m_refCount < 1)
        delete set;
}

void C_SpeechSets::Clear()
{
    while (!m_sets.empty()) {
        delete m_sets.back();
        m_sets.pop_back();
    }
}

void C_SpeechSets::ResetDelays()
{
    for (C_SpeechSet *set : m_sets) {
        for (S_SpeechSentence &sentence : set->m_sentences)
            sentence.nextTime = 0;
    }
}

// ------------------------------------------------------------------------- sound codes
// (Computed through doubles in the original, scaling by 1e-6; truncation errors included.)

uint32_t SpeechCode_Base(uint32_t code)
{
    return code % 10000 + SPEECH_CODE_BASE;
}

int SpeechCode_GetSet(uint32_t code)
{
    return -SPEECH_CODE_SET_BASE - static_cast<int>(static_cast<double>(code) * -1e-6);
}

int SpeechCode_GetVariant(uint32_t code)
{
    const double scaled = static_cast<double>(code) * 1e-6;
    return static_cast<int>((scaled - static_cast<int>(scaled)) * 100.0f);
}

void SpeechCode_SetSet(uint32_t *code, int set)
{
    const uint32_t old = *code;
    *code = static_cast<uint32_t>((SpeechCode_GetVariant(old) + set * 100) * 10000) + SpeechCode_Base(old);
}

void SpeechCode_SetVariant(uint32_t *code, int variant)
{
    const uint32_t old = *code;
    *code = static_cast<uint32_t>((variant + SpeechCode_GetSet(old) * 100) * 10000) + SpeechCode_Base(old);
}
