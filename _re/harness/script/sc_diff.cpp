// Script part 1: the mission script compiler (script/script_compiler.cpp, with script_expression.cpp's parser and the
// script's building members) against the original executable's, over every game script and mutated copies of them.
// Both compile the same file (the dta functions read H:\HD2_Unpacked, or a mutated copy in memory); the compiled
// scripts (commands, literals, variables, whenevers, handlers, expression trees) and the compiler's symbols are written
// out as text and compared line by line; before them, the string literals' printf formatting against the original's
// sprintf, and a few crafted files. MinGW; build_sc.bat; runs sc_diff.exe ("sc_diff: N checks, 0 failures").
// Env: HD2_SC_DATA or HD2_UNPACKED (the unpacked data, default H:\HD2_Unpacked), HD2_SC_MUTANTS (mutated copies per
// file, default 12; quick 2), HD2_SC_QUICK, HD2_SC_ONLY (a substring of the script paths to run), HD2_SC_DUMP (print
// both dumps of the failures), HD2_SC_SEED, HD2_SC_SAVE_FAILS (a folder: each failing mutant saved there, and the one
// being compiled as current.scr), HD2_SC_REPLAY (one saved mutant compiled and dumped), HD2_SC_BISECT (a script path:
// the first line after which the two differ), HD2_SC_FIRST_CHANCE (access violations reported where they happen).
#define HARNESS_NAME "sc_diff"
#include "sc_shared.inc"

namespace H {

// ---- the mutations: a copy of the file with a few tokens changed ----
const char *const kVocabulary[] = {
    "{", "}", "(", ")", ";", ",", ":", "=", "==", "<", ">", "<=", ">=", "!=", "<>", "!", "+", "-", "*", "/", "&&",
    "||", "and", "or", "not", "If", "Else", "Whenever", "Label", "GoTo", "Gosub", "Return", "Block", "Integer",
    "String", "Frame", "Bool", "Enum", "ScriptStartPoint", "OnSignal", "OnCutscene", "OnCutsceneDone", "OnDeath",
    "OnUse", "OnHit", "OnAlarm", "OnAlarmDone", "OnGlobalAlarm", "OnExit", "SetWhenever", "SetWheneverTime", "0",
    "1", "-1", "12", "3.5", "1.", "..", "99999999999", "\"\"", "\"a%%b\"", "\"text\"", "\"%\"", "\"%B\"", "\"%5B\"",
    "%", "true", "false", "TRUE", "x", "count", "_PlayerInRange", "_RandomInt", "_SignalReceived", "Delay",
    "EndScript", "FRM_SetOn", "\"unterminated", "//", "// comment", "\r\n", "\t", " ", "@", "$",
    // Whole statements.
    "ScriptStartPoint:", "ScriptStartPoint :", "Label sc_l:", "GoTo sc_l;", "Gosub sc_l;", "Return;", "Block {",
    "SetWhenever(sc_w, 1);", "SetWheneverTime(sc_w, 300);", "Whenever sc_w (1 == 1)", "Integer sc_i = 3 * 4 + 2;",
    "String sc_s = \"a\";", "Frame sc_f;", "OnSignal(3)", "OnCutscene(2) {", "OnDeath() {", "Else {", "If (sc_i > -1) {",
};
// (Not in the vocabulary or the edits: "#include" and its lines (the port's include handling is its own, see
// script_compiler.cpp), and real printf conversions in a literal or name ("%d": the original reads a missing argument).)
std::vector<std::pair<size_t, size_t>> Tokens(const std::string &t)
{
    // Rough token spans: names, numbers, strings, single characters; none on a line with "#include".
    std::vector<std::pair<size_t, size_t>> spans;
    std::vector<std::pair<size_t, size_t>> includeLines;
    for (size_t at = t.find("#include"); at != std::string::npos; at = t.find("#include", at + 1)) {
        const size_t from = t.find_last_of("\r\n", at);
        includeLines.push_back({from == std::string::npos ? 0 : from + 1, std::min(t.find_first_of("\r\n", at), t.size())});
    }
    size_t i = 0;
    while (i < t.size()) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (std::isspace(c)) {
            ++i;
            continue;
        }
        size_t j = i + 1;
        if (std::isalnum(c) || c == '_' || c == '.') {
            while (j < t.size() && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '_' || t[j] == '.'))
                ++j;
        } else if (c == '"') {
            while (j < t.size() && t[j] != '"' && t[j] != '\r' && t[j] != '\n')
                ++j;
            if (j < t.size() && t[j] == '"')
                ++j;
        }
        bool onInclude = false;
        for (const auto &line : includeLines)
            onInclude |= i >= line.first && i <= line.second;
        if (!onInclude)
            spans.push_back({i, j - i});
        i = j;
    }
    return spans;
}
std::string MutateOnce(const std::string &text);
// (No line of 1000 characters or more: the original copies a token into its 0x400-byte buffer unbounded and
// overwrites the compiler's tables; the longest shipped line has 526.)
std::string Mutate(const std::string &text)
{
    for (;;) {
        const std::string t = MutateOnce(text);
        size_t longest = 0, start = 0;
        for (size_t i = 0; i <= t.size(); ++i)
            if (i == t.size() || t[i] == '\n' || t[i] == '\r') {
                longest = std::max(longest, i - start);
                start = i + 1;
            }
        if (longest < 1000)
            return t;
    }
}
std::string MutateOnce(const std::string &text)
{
    std::string t = text;
    const int edits = P(0.6) ? 1 : I(2, 4);
    for (int e = 0; e < edits; ++e) {
        const auto spans = Tokens(t);
        if (spans.empty())
            break;
        const auto sp = spans[size_t(I(0, int(spans.size()) - 1))];
        const std::string word = kVocabulary[I(0, int(sizeof kVocabulary / sizeof *kVocabulary) - 1)];
        switch (I(0, 5)) {
        case 0:                                         // delete a token
            t.erase(sp.first, sp.second);
            break;
        case 1:                                         // replace it
            t.replace(sp.first, sp.second, word);
            break;
        case 2:                                         // insert a word before it
            t.insert(sp.first, word + " ");
            break;
        case 3:                                         // duplicate it
            t.insert(sp.first, t.substr(sp.first, sp.second) + " ");
            break;
        case 4:                                         // cut the file there
            t.resize(sp.first + (P(0.5) ? sp.second : 0));
            break;
        default: {                                      // swap it with another token
            const auto sp2 = spans[size_t(I(0, int(spans.size()) - 1))];
            if (sp2.first > sp.first + sp.second) {
                const std::string a = t.substr(sp.first, sp.second), b = t.substr(sp2.first, sp2.second);
                t.replace(sp2.first, sp2.second, a);
                t.replace(sp.first, sp.second, b);
            }
            break;
        }
        }
    }
    return t;
}

}  // namespace H

#include "sc_stubs.inc"

int main()
{
    using namespace H;
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("sc_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    if (const char *d = std::getenv("HD2_UNPACKED"))
        g_data = d;
    if (const char *d = std::getenv("HD2_SC_DATA"))
        g_data = d;
    g_dump = std::getenv("HD2_SC_DUMP") != nullptr;
    if (const char *s = std::getenv("HD2_SC_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    int mutants = std::getenv("HD2_SC_QUICK") ? 2 : 12;
    if (const char *m = std::getenv("HD2_SC_MUTANTS"))
        mutants = std::atoi(m);
    const char *only = std::getenv("HD2_SC_ONLY");
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    // The string literals: ScriptStringLiteral against the original's sprintf (no arguments) on every byte after a '%'
    // and on random strings of the characters its state machine knows (no conversion that reads an argument, no '*').
    {
        g_case = "literals";
        using T_Sprintf = int(__cdecl *)(char *, const char *, ...);
        auto one = [](const std::string &format) {
            char out[256] = {};
            OriginalAt<T_Sprintf>(0x007e6334)(out, format.c_str());
            const C_str port = ScriptStringLiteral(format.c_str(), format.size());
            ++checks;
            if (std::string(port.c_str(), port.Size()) != out) {
                ++failures;
                if (failures <= 40)
                    std::printf("FAIL literal \"%s\": orig \"%s\" port \"%s\"\n", format.c_str(), out, port.c_str());
            }
        };
        for (int x = 1; x < 256; ++x)
            if (!std::strchr("cCdeEfgGinopsSuxXZ*", x))
                one(std::string("a%") + char(x) + "b");
        // (Flags carried into the next '%' and combined, a size at the start.)
        for (const char *s : {"%0B%5B", "%-05B", "%0-5B", "%I64B", "%I6B", "%0%5B", "%05B%3B", "%-3B%03B", "%h5B"})
            one(s);
        static const char alphabet[] = "%%%%-+ #0123456789.hlwLFNIB6 4abAD_qrtvyzjkm:,\t\x80\xe9";
        const int literals = std::getenv("HD2_SC_QUICK") ? 20000 : 100000;
        for (int n = 0; n < literals; ++n) {
            std::string s;
            int digits = 0;
            for (int k = I(0, 12); k > 0; --k) {
                const char c = alphabet[I(0, int(sizeof alphabet) - 2)];
                // (At most two digits in a row: a width stays under the 256-byte buffer.)
                digits = c >= '0' && c <= '9' ? digits + 1 : 0;
                if (digits <= 2)
                    s += c;
            }
            one(s);
        }
        g_case = "";
    }

    const char *saveFails = std::getenv("HD2_SC_SAVE_FAILS");
    if (const char *replay = std::getenv("HD2_SC_REPLAY")) {
        // A saved failing mutant: its first line the script's path, the rest its text.
        std::string all;
        FILE *in = std::fopen(replay, "rb");
        if (!in) {
            std::printf("sc_diff: no %s\n", replay);
            return 1;
        }
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
            all.append(buf, n);
        std::fclose(in);
        const size_t eol = all.find('\n');
        const std::string path = all.substr(0, eol);
        SetRole(GAME_ROLE_LOCAL);
        SetMission(path.substr(8, path.find('\\', 8) - 8));
        g_overrides[Lower(path)] = all.substr(eol + 1);
        g_current = path + " (replay)";
        g_dump = 1;
        Compare(g_current, CompileBoth(path));
        return 0;
    }
    if (const char *seq = std::getenv("HD2_SC_SEQ")) {
        // A file of statements, one per line: each compiled in turn after the random expressions' declarations.
        SetRole(GAME_ROLE_LOCAL);
        SetMission("AFRICA1");
        std::ifstream in(seq);
        std::string line;
        const std::string path = "Scripts\\AFRICA1\\sc_x.scr";
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            g_overrides[Lower(path)] = "Integer a = 2;\r\nInteger b;\r\nString s;\r\nFrame f;\r\n" + line + "\r\n";
            g_current = line;
            Compare(line, CompileBoth(path));
        }
        std::printf("sc_diff: sequence done, %d failures\n", failures);
        return 0;
    }
    if (const char *b = std::getenv("HD2_SC_BISECT")) {
        // The file cut after each line: the first cut whose dumps differ, and the line it ends with.
        const std::string path = b;
        SetRole(GAME_ROLE_LOCAL);
        SetMission(path.substr(8, path.find('\\', 8) - 8));
        std::string text;
        FileText(path, text);
        for (size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', at + 1)) {
            g_overrides[Lower(path)] = text.substr(0, at + 1);
            const S_Result r = CompileBoth(path);
            if (r.orig != r.port) {
                const size_t from = text.rfind('\n', at - 1);
                std::printf("first difference at byte %u: %s\n", unsigned(at),
                            text.substr(from == std::string::npos ? 0 : from + 1, at - from).c_str());
                Compare(path, r);
                return 0;
            }
        }
        std::printf("no difference in any cut\n");
        return 0;
    }
    const std::vector<S_File> files = Corpus();
    if (files.empty()) {
        std::printf("sc_diff: no scripts under %s\\SCRIPTS\n", g_data.c_str());
        return 1;
    }
    // Crafted cases: a missing file, an include found only in Scripts\include, a missing include, start points.
    {
        SetRole(GAME_ROLE_LOCAL);
        SetMission("AFRICA1");
        const struct {
            const char *path, *text;
        } crafted[] = {
            {"Scripts\\AFRICA1\\sc_none.scr", nullptr},
            {"Scripts\\AFRICA1\\sc_inc.scr", "#include \"sc_only.scr\"\r\nInteger a = b + 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_inc2.scr", "#include \"sc_missing.scr\"\r\nInteger a;\r\n"},
            {"Scripts\\AFRICA1\\sc_start.scr", "Integer a;\r\nScriptStartPoint:\r\na = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_start2.scr", "Integer a;\r\nScriptStartPoint:\r\na = 1;\r\nScriptStartPoint:\r\n"},
            {"Scripts\\AFRICA1\\sc_start3.scr", "ScriptStartPoint;\r\n"},
            {"Scripts\\AFRICA1\\sc_empty.scr", ""},
            // Include directives both read alike: indented, spaced, no quotes, unterminated, an empty name, a lone
            // quote at the line's end, a later "#include" inside a comment.
            {"Scripts\\AFRICA1\\sc_i1.scr", " #include \"sc_only.scr\"\r\nInteger a = b + 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i2.scr", "\t \t#include \t \"sc_only.scr\"\r\nInteger a = b + 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i3.scr", "#include sc_only.scr\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i4.scr", "#include \"sc_only.scr\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i5.scr", "#include \"\"\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i6.scr", "#include \"\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i7.scr", "#include \"sc_only.scr\"\r\nInteger x; // #include \"sc_only.scr\"\r\n"},
            {"Scripts\\AFRICA1\\sc_i8.scr", "Integer a;\r\nFRM_SetOn(\"x#include\", 1);\r\n"},
            {"Scripts\\AFRICA1\\sc_i9.scr", "#include\"sc_only.scr\"\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i10.scr", "#include  \"sc_only.scr\"\r\nInteger a = b;\r\n"},
            // (Not "#include" at the very end of the file: the original's search restarts there for ever.)
            // Else: first, not after a '}', last; handlers followed by no block.
            {"Scripts\\AFRICA1\\sc_e1.scr", "Else { }\r\n"},
            {"Scripts\\AFRICA1\\sc_e2.scr", "Integer a;\r\nElse { }\r\n"},
            {"Scripts\\AFRICA1\\sc_e3.scr", "If (1) { }\r\nElse\r\n"},
            {"Scripts\\AFRICA1\\sc_e4.scr", "If (1) { }\r\nElse\r\nInteger a;\r\n"},
            // A label defined twice; "#include" right after the file's first character; a control character.
            {"Scripts\\AFRICA1\\sc_l1.scr", "Label l:\r\nLabel l:\r\nGoTo l;\r\n"},
            {"Scripts\\AFRICA1\\sc_i11.scr", "x#include \"sc_only.scr\"\r\nInteger a = b;\r\n"},
            {"Scripts\\AFRICA1\\sc_c1.scr", "Integer a;\x01\r\nInteger b;\r\n"},
            {"Scripts\\AFRICA1\\sc_i12.scr", "#include \""},
            {"Scripts\\AFRICA1\\sc_i13.scr", "#include \"sc_only.scr"},
            {"Scripts\\AFRICA1\\sc_s1.scr", "Integer a;\r\nString s = \"abc"},
        };
        g_overrides[Lower("Scripts\\include\\sc_only.scr")] = "Integer b = 2;\r\n";
        for (const auto &c : crafted) {
            if (c.text)
                g_overrides[Lower(c.path)] = c.text;
            g_current = c.path;
            Compare(c.path, CompileBoth(c.path));
            g_overrides.erase(Lower(c.path));
        }
        // A NUL byte in the file and in an include (the text after it never read).
        {
            const std::string withNul("Integer a = 1;\r\n\0Integer b;\r\n", 29);
            g_overrides[Lower("Scripts\\AFRICA1\\sc_n1.scr")] = withNul;
            g_current = "Scripts\\AFRICA1\\sc_n1.scr";
            Compare(g_current, CompileBoth(g_current));
            g_overrides[Lower("Scripts\\include\\sc_nul.scr")] = std::string("Integer b = 2;\r\n\0Integer c;\r\n", 29);
            g_overrides[Lower("Scripts\\AFRICA1\\sc_n2.scr")] = "#include \"sc_nul.scr\"\r\nInteger a = b;\r\nc = 1;\r\n";
            g_current = "Scripts\\AFRICA1\\sc_n2.scr";
            Compare(g_current, CompileBoth(g_current));
        }
        for (const char *handler : {"OnDeath()", "OnUse()", "OnHit()", "OnAlarm()", "OnAlarmDone()", "OnGlobalAlarm()",
                                    "OnExit()", "OnSignal(1)", "OnCutscene(1)", "OnCutsceneDone(1)", "Whenever w (1)"}) {
            for (const char *after : {"\r\nInteger a;\r\n", "\r\n", " { }\r\n", " { }\r\nElse { }\r\n"}) {
                const std::string path = "Scripts\\AFRICA1\\sc_h.scr";
                g_overrides[Lower(path)] = std::string(handler) + after;
                g_current = path + " " + handler + after;
                Compare(g_current, CompileBoth(path));
            }
        }
        // Compiled into a script that already holds another (the second compilation clears it first).
        g_overrides[Lower("Scripts\\AFRICA1\\sc_a.scr")] =
            "Integer a = 3;\r\nString s = \"x\";\r\nFrame f;\r\nOnSignal(2) { }\r\nOnCutscene(4) { }\r\nOnCutsceneDone(5) { }\r\n"
            "Whenever w (a > 1) { }\r\nOnDeath() { }\r\nLabel l:\r\nGoTo l;\r\n";
        g_overrides[Lower("Scripts\\AFRICA1\\sc_b.scr")] = "Integer b = 1;\r\nOnSignal(2) { }\r\nOnUse() { }\r\n";
        g_current = "Scripts\\AFRICA1\\sc_b.scr (after sc_a)";
        Compare(g_current, CompileBoth("Scripts\\AFRICA1\\sc_b.scr", "Scripts\\AFRICA1\\sc_a.scr"));
        g_overrides.clear();
    }
    // Random expressions from a grammar (in an If, an assignment, a Whenever): terms of each type and literals, every
    // operator (mixed precedence: the grouping, and the original's use after free), parentheses, NOT, a sign on a
    // literal, the built-in functions with right and wrong arguments. (Well formed: at a level with two operators more
    // than operands the original's list runs out of its bounds, and its damage shows in later cases; the mutated
    // scripts above bring the malformed ones.)
    {
        static const char *const operators[] = {"+", "-", "*", "/", "AND", "OR", "and", "==", "!=", "<>", "<", ">", "<=", ">="};
        static const char *const terms[] = {
            "a", "b", "0", "1", "7", "12", "3.9", "\"t\"", "\"%%\"", "s", "f", "_RandomInt(5)", "_SignalReceived(a)",
            "_GetNumLiveEnemies()", "_EnemyInRange(f, 10)", "_EnemyInRange(f, -2)", "_GetObjectiveStatus(1, 2, 3, 4)",
            "_IsLanguageVersion(\"cz\")", "_FrameInRange(a)", "_SPGetGameDifficulty()", "_RandomInt(s)",
        };
        std::function<std::string(int)> expression = [&](int depth) {
            std::string e;
            bool signed_ = false;                       // (one sign per level: one operator more than operands)
            for (int n = I(1, 4), t = 0; t < n; ++t) {
                if (t)
                    e += std::string(" ") + operators[I(0, int(sizeof operators / sizeof *operators) - 1)] + " ";
                if (P(0.1))
                    e += P(0.5) ? "NOT " : "! ";
                const int kind = I(0, 9);
                if (kind == 0 && depth < 3)
                    e += "(" + expression(depth + 1) + ")";
                else if (kind == 1 && !signed_) {
                    signed_ = true;
                    e += std::string("- ") + (P(0.5) ? "1" : "4");
                } else
                    e += terms[I(0, int(sizeof terms / sizeof *terms) - 1)];
            }
            return e;
        };
        const int count = std::getenv("HD2_SC_QUICK") ? 6000 : 30000;
        const std::string path = "Scripts\\AFRICA1\\sc_x.scr";
        for (int n = 0; n < count; ++n) {
            const std::string expr = expression(0);
            std::string text = "Integer a = 2;\r\nInteger b;\r\nString s;\r\nFrame f;\r\n";
            switch (I(0, 2)) {
            case 0: text += "If (" + expr + ") { b = 1; }\r\n"; break;
            case 1: text += "b = " + expr + ";\r\n"; break;
            default: text += "Whenever w (" + expr + ") { }\r\n"; break;
            }
            g_overrides[Lower(path)] = text;
            g_current = path + " [" + text.substr(text.find("Frame f;") + 10) + "]";
            if (!Compare(g_current, CompileBoth(path)) && saveFails) {
                if (FILE *out = std::fopen((std::string(saveFails) + F("\\%04d.scr", failures)).c_str(), "wb")) {
                    std::fprintf(out, "%s\n%s", path.c_str(), text.c_str());
                    std::fclose(out);
                }
            }
        }
        g_overrides.clear();
    }
    int compiled = 0, failed = 0;
    for (const S_File &f : files) {
        if (only && Lower(f.path).find(Lower(only)) == std::string::npos)
            continue;
        SetMission(f.mission);
        g_current = f.path;
        // The file as shipped. (Only on a server: on a client the original's AddCommand answers -1 and its '}' reads the
        // command before the list, so no client ever compiles one.)
        SetRole(GAME_ROLE_LOCAL);
        const S_Result r = CompileBoth(f.path);
        Compare(f.path, r);
        (r.port[0] == "result 1" ? compiled : failed)++;
        // Mutated copies.
        std::string text;
        if (!FileText(f.path, text))
            continue;
        for (int m = 0; m < mutants; ++m) {
            const std::string key = Lower(f.path);
            g_overrides[key] = Mutate(text);
            g_current = f.path + F(" (mutant %d)", m);
            if (saveFails) {
                // (The one being compiled, for a crash.)
                if (FILE *out = std::fopen((std::string(saveFails) + "\\current.scr").c_str(), "wb")) {
                    std::fprintf(out, "%s\n", f.path.c_str());
                    std::fwrite(g_overrides[key].data(), 1, g_overrides[key].size(), out);
                    std::fclose(out);
                }
            }
            if (!Compare(g_current, CompileBoth(f.path)) && saveFails) {
                // (Kept for HD2_SC_REPLAY: the mutant's text, named by its failure's number.)
                const std::string name = std::string(saveFails) + F("\\%04d.scr", failures);
                if (FILE *out = std::fopen(name.c_str(), "wb")) {
                    std::fprintf(out, "%s\n", f.path.c_str());
                    std::fwrite(g_overrides[key].data(), 1, g_overrides[key].size(), out);
                    std::fclose(out);
                }
            }
            g_overrides.erase(key);
        }
    }
    std::printf("sc_diff: %d scripts (%d compile, %d fail in the port), %d expression nodes the original freed, %d cases it wrote past a block in (not compared), %d checks, "
                "%d failures\n", compiled + failed, compiled, failed, g_freedNodes, g_overruns, checks, failures);
    return failures != 0;
}
