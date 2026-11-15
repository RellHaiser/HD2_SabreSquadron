// Part 28 of the human harness (HUMAN_SPEECHSETS builds, build_speechsets.bat; included by human_test.cpp after
// locomotion3_test.inc): the speech sets (game/human_speech.cpp: Tables\speech.set, the sentences' shuffled samples,
// the sound codes) against the original's (0x005af7d0..0x005b0510).
//
// The original's objects are built in its own layout from the port's (a sentence 0x1C bytes: its MSVC6 vector's first,
// last and end +0x04..+0x0C, the cursor an iterator +0x10, the next time +0x14, enabled +0x18; a set 0xF24 bytes: id
// +0x04, the sentences +0x08, the reference count +0xF20; the sets' list first and last +0x04 / +0x08, its C_chunk
// +0x10) and both are compared as text after each call. Both sides draw from the same rand() (the original's _rand
// redirected, both seeded alike); the original's new and delete use the test's heap; its file calls the fake engine's.
namespace {

#define SP __attribute__((thiscall))

int __cdecl SS_Rand() { return std::rand(); }
void *__cdecl SS_New(size_t size) { return std::malloc(size); }
void __cdecl SS_Delete(void *p) { std::free(p); }

const int SENTENCES = C_SpeechSet::SENTENCE_COUNT;
const size_t O_SENTENCE = 0x1C, O_SET = 0xF24, O_SETS = 0x424;

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }
uint32_t RdW(const uint8_t *p) { uint32_t w; std::memcpy(&w, p, 4); return w; }
void WrW(uint8_t *p, uint32_t w) { std::memcpy(p, &w, 4); }
template <class T> T *AtW(const uint8_t *p) { return reinterpret_cast<T *>(static_cast<uintptr_t>(RdW(p))); }

// ---- the sentences as text: samples, cursor index, next time, enabled
std::string SentenceText(const std::vector<int> &samples, size_t cursor, uint32_t next, bool enabled)
{
    std::string t = std::to_string(cursor) + "/" + std::to_string(next) + "/" + std::to_string(enabled) + "[";
    for (int s : samples)
        t += std::to_string(s) + ",";
    return t + "]";
}
std::string PortSentence(const S_SpeechSentence &s) { return SentenceText(s.samples, s.cursor, s.nextTime, s.enabled); }
std::string OrigSentence(const uint8_t *o)
{
    const int *first = AtW<int>(o + 4), *last = AtW<int>(o + 8), *cursor = AtW<int>(o + 0x10);
    std::vector<int> samples(first, last);
    const size_t index = first ? size_t(cursor - first) : 0;
    return SentenceText(samples, index, RdW(o + 0x14), o[0x18] != 0);
}
// The original's copy of a port sentence (its vector's room from the test's heap, sometimes more than it holds).
void ToOrigSentence(const S_SpeechSentence &s, uint8_t *o)
{
    std::memset(o, 0, O_SENTENCE);
    if (!s.samples.empty() || P(0.3f)) {
        const size_t room = s.samples.size() + size_t(P(0.5f) ? I(0, 4) : 0);
        int *first = static_cast<int *>(std::malloc((room ? room : 1) * 4));
        std::copy(s.samples.begin(), s.samples.end(), first);
        WrW(o + 4, Word(first));
        WrW(o + 8, Word(first + s.samples.size()));
        WrW(o + 0xC, Word(first + room));
        WrW(o + 0x10, Word(first + s.cursor));
    }
    WrW(o + 0x14, s.nextTime);
    o[0x18] = s.enabled;
}
void FreeOrigSentence(uint8_t *o) { std::free(AtW<void>(o + 4)); }

void RandomSentence(S_SpeechSentence &s)
{
    s.samples.clear();
    const int n = P(0.3f) ? 0 : I(1, 9);
    for (int i = 0; i < n; ++i)
        s.samples.push_back(P(0.8f) ? int(90000000 + I(0, 9999999)) : I(-5, 5));
    s.cursor = size_t(I(0, n));
    s.nextTime = P(0.3f) ? 0u : uint32_t(I(0, 20000));
    s.enabled = P(0.8f);
}

// ---- the sets
std::string PortSetText(const C_SpeechSet *s)
{
    if (!s)
        return "null";
    std::string t = "set " + std::to_string(s->m_id) + " refs " + std::to_string(s->m_refCount) + ":";
    for (int i = 0; i < SENTENCES; ++i) {
        const S_SpeechSentence &e = s->m_sentences[i];
        if (e.enabled || !e.samples.empty() || e.nextTime || e.cursor)
            t += " " + std::to_string(i) + "=" + PortSentence(e);
    }
    return t;
}
std::string OrigSetText(const uint8_t *s)
{
    if (!s)
        return "null";
    std::string t = "set " + std::to_string(int(RdW(s + 4))) + " refs " + std::to_string(int(RdW(s + 0xF20))) + ":";
    for (int i = 0; i < SENTENCES; ++i) {
        const uint8_t *e = s + 8 + O_SENTENCE * i;
        if (e[0x18] || RdW(e + 4) != RdW(e + 8) || RdW(e + 0x14) || (RdW(e + 0x10) != RdW(e + 4)))
            t += " " + std::to_string(i) + "=" + OrigSentence(e);
    }
    return t;
}
uint8_t *NewOrigSet(const C_SpeechSet &p)
{
    auto *o = static_cast<uint8_t *>(std::malloc(O_SET));
    std::memset(o, 0, O_SET);
    WrW(o + 4, uint32_t(p.m_id));
    for (int i = 0; i < SENTENCES; ++i)
        ToOrigSentence(p.m_sentences[i], o + 8 + O_SENTENCE * i);
    WrW(o + 0xF20, uint32_t(p.m_refCount));
    return o;
}
void FreeOrigSet(uint8_t *o)
{
    for (int i = 0; i < SENTENCES; ++i)
        FreeOrigSentence(o + 8 + O_SENTENCE * i);
    std::free(o);
}

// ---- 1: the sound codes (fastcall in the original), exhaustively over the speech codes' range
typedef int(__fastcall *T_CodeGet)(uint32_t);
typedef void(__fastcall *T_CodeSet)(uint32_t *, int);
void TestSpeechCodes()
{
    std::printf("speech: the sound codes\n");
    const auto origSet = OriginalAt<T_CodeGet>(0x005b0460), origVariant = OriginalAt<T_CodeGet>(0x005b0490);
    const auto origBase = OriginalAt<T_CodeGet>(0x005b0440);
    long differ[3] = {}, shown = 0;
    const auto one = [&](uint32_t code) {
        const int ps = SpeechCode_GetSet(code), os = origSet(code);
        const int pv = SpeechCode_GetVariant(code), ov = origVariant(code);
        const uint32_t pb = SpeechCode_Base(code), ob = uint32_t(origBase(code));
        differ[0] += ps != os;
        differ[1] += pv != ov;
        differ[2] += pb != ob;
        if ((ps != os || pv != ov || pb != ob) && shown++ < 8)
            std::printf("  code %u: set %d / %d, variant %d / %d, base %u / %u\n", code, ps, os, pv, ov, pb, ob);
    };
    // (Every code in the suite; mutation runs, SPEECH_CODES_STRIDE, every n-th and those beside a step of 10000.)
#ifdef SPEECH_CODES_STRIDE
    for (uint32_t code = 90000000; code < 100000000; code += SPEECH_CODES_STRIDE)
        one(code);
    for (uint32_t step = 90000000; step < 100000000; step += 10000)
        for (uint32_t code = step - 2; code <= step + 2; ++code)
            one(code);
#else
    for (uint32_t code = 90000000; code < 100000000; ++code)
        one(code);
#endif
    for (int i = 0; i < 2000000 / QUICK; ++i)
        one(uint32_t(std::rand()) * 65599u + uint32_t(std::rand()));
    for (uint32_t code : {0u, 1u, 99999999u, 100000000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu})
        one(code);
    std::printf("  codes differing: set %ld, variant %ld, base %ld\n", differ[0], differ[1], differ[2]);
    CHECK(differ[0] == 0);
    CHECK(differ[1] == 0);
    CHECK(differ[2] == 0);
    // SetSet / SetVariant on speech codes, sets and variants in and out of their ranges.
    long setDiffer = 0;
    for (int i = 0; i < 400000 / QUICK; ++i) {
        const uint32_t code = P(0.9f) ? uint32_t(90000000 + I(0, 9999999)) : uint32_t(std::rand()) * 65599u;
        const int value = P(0.9f) ? I(0, 99) : I(-120, 300);
        uint32_t p = code, o = code;
        if (i % 2) {
            SpeechCode_SetSet(&p, value % 10);
            OriginalAt<T_CodeSet>(0x005b04d0)(&o, value % 10);
        } else {
            SpeechCode_SetVariant(&p, value);
            OriginalAt<T_CodeSet>(0x005b0510)(&o, value);
        }
        if (p != o && setDiffer++ < 8)
            std::printf("  %s(%u, %d): %u / %u\n", i % 2 ? "SetSet" : "SetVariant", code, i % 2 ? value % 10 : value, p, o);
        CHECK(p == o);
    }
}

// ---- 2: the sentences: AddSample directly, Say / SayNow / SetDelay on a set
void TestSpeechSentences()
{
    std::printf("speech: the sentences\n");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        S_SpeechSentence ps;
        RandomSentence(ps);
        alignas(4) uint8_t os[O_SENTENCE];
        ToOrigSentence(ps, os);
        const int sample = I(0, 1000);
        const unsigned seed = unsigned(I(1, 1 << 30));
        std::srand(seed);
        ps.AddSample(sample);
        std::srand(seed);
        typedef void(SP * T)(void *, int);
        OriginalAt<T>(0x005b0570)(os, sample);
        const std::string p = PortSentence(ps), o = OrigSentence(os);
        if (p != o && i < 2000)
            std::printf("  AddSample %d: port %s / original %s\n", sample, p.c_str(), o.c_str());
        CHECK(p == o);
        FreeOrigSentence(os);
    }
    // Sets with random sentences (most empty), the calls on any sentence (out of range too) at random mission times.
    C_game *game = GetGame();
    for (int i = 0; i < 4000 / QUICK; ++i) {
        auto *pset = new C_SpeechSet();
        pset->m_id = I(0, 9);
        pset->m_refCount = I(0, 3);
        for (int k = 0; k < SENTENCES; ++k)
            if (P(0.2f) || k < 4)
                RandomSentence(pset->m_sentences[k]);
        uint8_t *oset = NewOrigSet(*pset);
        for (int n = 0; n < 40; ++n) {
            game->m_missionTime = uint32_t(I(0, 30000));
            const int sentence = P(0.9f) ? (P(0.5f) ? I(0, 3) : I(0, SENTENCES - 1)) : I(-3, SENTENCES + 3);
            // (Now and then the very time the sentence may be said again; delays of 1 and less.)
            if (sentence >= 0 && sentence < SENTENCES && P(0.2f))
                game->m_missionTime = pset->m_sentences[sentence].nextTime;
            const int delay = P(0.3f) ? I(-100, 1) : I(1, 9000);
            const int kind = I(0, 2);
            const unsigned seed = unsigned(I(1, 1 << 30));
            int pr = 0, orr = 0;
            std::srand(seed);
            if (kind == 0)
                pr = pset->Say(sentence, delay);
            else if (kind == 1)
                pr = pset->SayNow(sentence);
            else
                pset->SetDelay(sentence, delay);
            std::srand(seed);
            if (kind == 0) {
                typedef int(SP * T)(void *, int, int);
                orr = OriginalAt<T>(0x005af8e0)(oset, sentence, delay);
            } else if (kind == 1) {
                typedef int(SP * T)(void *, int);
                orr = OriginalAt<T>(0x005afa60)(oset, sentence);
            } else {
                typedef void(SP * T)(void *, int, int);
                OriginalAt<T>(0x005afd10)(oset, sentence, delay);
            }
            static const char *const KINDS[] = {"Say", "SayNow", "SetDelay"};
            const std::string p = PortSetText(pset), o = OrigSetText(oset);
            if ((p != o || pr != orr) && i < 50)
                std::printf("  %s(%d, %d) at %u: %d / %d\n    port %s\n    original %s\n", KINDS[kind], sentence, delay,
                            game->m_missionTime, pr, orr, p.c_str(), o.c_str());
            CHECK(pr == orr);
            CHECK(p == o);
        }
        delete pset;
        FreeOrigSet(oset);
    }
}

// ---- 3: the sets loaded from Tables\speech.set (the game's and the test's), released and cleared
// The sets' list as text (its sets in order) on each side.
std::string PortListText(const C_SpeechSets &s)
{
    std::string t;
    for (const C_SpeechSet *set : s.m_sets)
        t += PortSetText(set) + "\n";
    return t;
}
std::string OrigListText(const uint8_t *s)
{
    std::string t;
    for (uint8_t **p = AtW<uint8_t *>(s + 4), **e = AtW<uint8_t *>(s + 8); p && p != e; ++p)
        t += OrigSetText(*p) + "\n";
    return t;
}
// A file of sets: some with a name, some without sentences, sentence ids out of range, empty sentences.
void WriteSpeechFile(const std::string &path, int sets)
{
    C_chunk c;
    c.Open(path.c_str(), true);
    c.Begin(P(0.05f) ? 999 : 1000);
    for (int s = 0; s < sets; ++s) {
        bool written = false;                           // (per set: Acquire reads one set's samples)
        c.Begin(uint16_t(I(0, 50)));
        const auto name = [&] {
            const int size = I(1, 24);
            c.Begin(0);
            c.Write(&size, 4);
            c.Ascend();
            c.Begin(1);
            for (int k = 0; k < size; ++k) {
                const char ch = char('a' + k % 26);
                c.Write(&ch, 1);
            }
            c.Ascend();
        };
        const auto sentences = [&] {
            c.Begin(2);
            const int n = I(0, 12);
            for (int k = 0; k < n; ++k) {
                const int sentence = P(0.9f) ? I(0, SENTENCES - 1) : I(SENTENCES, SENTENCES + 20);
                c.Begin(uint16_t(sentence));
                const int samples = I(0, 5);
                for (int m = 0; m < samples; ++m) {
                    c.Begin(uint16_t(I(0, 9)));
                    const int code = 90000000 + I(0, 9999999);
                    // (Now and then a sample chunk short of its 4 bytes: not the set's first one read, whose sample
                    // the original leaves undefined; out-of-range sentences' are not read.)
                    c.Write(&code, written && P(0.1f) ? I(0, 3) : 4);
                    written = written || sentence < SENTENCES;
                    c.Ascend();
                }
                c.Ascend();
            }
            c.Ascend();
        };
        // (The name, the sentences, either missing, now and then the sentences first.)
        const bool named = P(0.7f), spoken = P(0.9f);
        if (spoken && P(0.3f)) {
            sentences();
            if (named)
                name();
        } else {
            if (named)
                name();
            if (spoken)
                sentences();
        }
        c.Ascend();
    }
    c.Ascend();
    c.Close();
}
void TestSpeechFiles()
{
    std::printf("speech: the sets of Tables\\speech.set\n");
    // A folder of the test's with a Tables folder: the game's file, then the test's.
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(sizeof cwd, cwd);
    const std::string dir = TempFile("speech_sets");
    CreateDirectoryA(dir.c_str(), nullptr);
    CreateDirectoryA((dir + "\\Tables").c_str(), nullptr);
    SetCurrentDirectoryA(dir.c_str());
    const std::string file = "Tables\\speech.set";
    for (int i = 0; i < 1200 / QUICK; ++i) {
        std::remove(file.c_str());
        const bool game = i % 4 == 0;
        if (game)
            CopyFileA("H:\\HD2_Unpacked\\Tables\\speech.set", file.c_str(), FALSE);
        else if (i % 20 != 3)                           // (and now and then no file)
            WriteSpeechFile(file, I(0, 8));
        C_SpeechSets psets;
        alignas(16) uint8_t osets[O_SETS] = {};
        std::vector<C_SpeechSet *> portGot;
        std::vector<uint8_t *> origGot;
        for (int n = 0; n < 12; ++n) {
            const int kind = I(0, 9);
            const unsigned seed = unsigned(I(1, 1 << 30));
            if (kind < 6) {
                const int id = P(0.8f) ? I(0, game ? 12 : 8) : I(-2, 40);
                // (The file as it was for the original's call too, whatever the port's did to it.)
                const std::vector<uint8_t> bytes = FileBytes(file);
                const bool exists = GetFileAttributesA(file.c_str()) != INVALID_FILE_ATTRIBUTES;
                std::srand(seed);
                C_SpeechSet *p = psets.Acquire(id);
                std::remove(file.c_str());
                if (exists) {
                    FILE *f = std::fopen(file.c_str(), "wb");
                    if (!bytes.empty())
                        std::fwrite(bytes.data(), 1, bytes.size(), f);
                    std::fclose(f);
                }
                std::srand(seed);
                typedef uint8_t *(SP * T)(void *, int);
                uint8_t *o = OriginalAt<T>(0x005afe00)(osets, id);
                CHECK((p == nullptr) == (o == nullptr));
                if (p && o) {
                    portGot.push_back(p);
                    origGot.push_back(o);
                }
            } else if (kind < 8 && !portGot.empty()) {
                // A set given out (the same one on both sides), released.
                const size_t k = size_t(I(0, int(portGot.size()) - 1));
                C_SpeechSet *const released = portGot[k];
                psets.Release(released);
                typedef void(SP * T)(void *, void *);
                OriginalAt<T>(0x005b0390)(osets, origGot[k]);
                // (One reference given back; the set deleted when none is left: not used again then.)
                portGot.erase(portGot.begin() + long(k));
                origGot.erase(origGot.begin() + long(k));
                bool still = false;
                for (C_SpeechSet *s : psets.m_sets)
                    still = still || s == released;
                for (size_t j = portGot.size(); !still && j-- > 0;)
                    if (portGot[j] == released) {
                        portGot.erase(portGot.begin() + long(j));
                        origGot.erase(origGot.begin() + long(j));
                    }
            } else if (kind == 8) {
                // A set the list does not hold (its references: deleted at none left).
                auto *pstray = new C_SpeechSet();
                pstray->m_id = I(0, 9);
                pstray->m_refCount = I(-1, 2);
                RandomSentence(pstray->m_sentences[I(0, SENTENCES - 1)]);
                uint8_t *ostray = NewOrigSet(*pstray);
                const int refs = pstray->m_refCount;
                const std::string before = PortSetText(pstray);
                psets.Release(pstray);
                typedef void(SP * T)(void *, void *);
                OriginalAt<T>(0x005b0390)(osets, ostray);
                if (refs > 1) {
                    // (Still there: the reference given back on both sides.)
                    CHECK(PortSetText(pstray) == OrigSetText(ostray));
                    CHECK(PortSetText(pstray) != before);
                    delete pstray;
                    FreeOrigSet(ostray);
                }
                // (Otherwise both deleted it: the original's destructor frees its sentences' vectors, the test's heap.)
            } else {
                psets.Clear();
                typedef void(SP * T)(void *);
                OriginalAt<T>(0x005afd60)(osets);
                portGot.clear();
                origGot.clear();
            }
            // ResetDelays (inline in the original's C_game::RestartMission, no function to compare with): every
            // sentence of every set may be said again, nothing else changes.
            if (P(0.1f) && !psets.m_sets.empty()) {
                std::string expected;
                for (C_SpeechSet *set : psets.m_sets) {
                    for (int k = 0; k < SENTENCES; ++k)
                        set->m_sentences[k].nextTime = 0;
                    expected += PortSetText(set);
                    for (int k = 0; k < SENTENCES; ++k)
                        set->m_sentences[k].nextTime = uint32_t(I(1, 9000));
                }
                psets.ResetDelays();
                std::string got;
                for (C_SpeechSet *set : psets.m_sets)
                    got += PortSetText(set);
                CHECK(got == expected);
                for (uint8_t **p = AtW<uint8_t *>(osets + 4), **e = AtW<uint8_t *>(osets + 8); p && p != e; ++p)
                    for (int k = 0; k < SENTENCES; ++k)
                        WrW(*p + 8 + O_SENTENCE * k + 0x14, 0);
            }
            const std::string p = PortListText(psets), o = OrigListText(osets);
            static int shownLists = 0;
            if (p != o && shownLists++ < 4)
                std::printf("  file %d call %d (kind %d):\n  port\n%s  original\n%s", i, n, kind, p.c_str(), o.c_str());
            CHECK(p == o);
        }
        psets.Clear();
        typedef void(SP * T)(void *);
        OriginalAt<T>(0x005afd60)(osets);
        std::free(AtW<void>(osets + 4));
    }
    std::remove(file.c_str());
    SetCurrentDirectoryA(cwd);
    RemoveDirectoryA((dir + "\\Tables").c_str());
    RemoveDirectoryA(dir.c_str());
}

void TestSpeechSets()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x007e6393, reinterpret_cast<const void *>(SS_Rand)},
        {0x007e566f, reinterpret_cast<const void *>(SS_New)},
        {0x007e2120, reinterpret_cast<const void *>(SS_Delete)},
        {0x007e53a4, reinterpret_cast<const void *>(dtaOpen)},
        {0x007e53aa, reinterpret_cast<const void *>(dtaOpenWrite)},
        {0x007e53b0, reinterpret_cast<const void *>(dtaClose)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    TestSpeechCodes();
    TestSpeechSentences();
    TestSpeechFiles();
}
}  // namespace
