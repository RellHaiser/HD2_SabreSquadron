#include "script/script_compiler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/game.h"
#include "ls3d/rw_data.h"
#include "script/script.h"
#include "script/script_expression.h"
#include "script/script_opcodes.h"
#include "script/script_tables.h"

// Constructed by the static initializer 0x0042bf60 (destructor registered with atexit).
C_ScriptCompiler g_scriptCompiler;

namespace {

// Largest file the compiler reads, in bytes; the rest of a longer file is ignored.
constexpr int SCRIPT_MAX_FILE_SIZE = 0xFFFE;
// Guards against include files that include themselves (the original overran its
// 64 KB source buffer instead).
constexpr int SCRIPT_MAX_INCLUDES = 64;

// Keyword tokens of the operators "=" .. "<="; they cannot start a statement.
constexpr int KEYWORD_FIRST_OPERATOR = 0x107;
constexpr int KEYWORD_LAST_OPERATOR = 0x110;

// Letters and '_' (and digits after the first character) (0x0042ded0).
bool IsNameChar(char c, bool allowDigits)
{
    if (allowDigits && c >= '0' && c <= '9')
        return true;
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Digits and '.' (0x0042df10).
bool IsNumberChar(char c)
{
    return c == '.' || (c >= '0' && c <= '9');
}

// Reads a game file (from the data archives or the disk) into `out`.
bool ReadScriptFile(const char *path, std::string &out)
{
    const int file = dtaOpen(path, 0);
    if (file == -1)
        return false;
    out.resize(SCRIPT_MAX_FILE_SIZE);
    const int size = dtaRead(file, &out[0], SCRIPT_MAX_FILE_SIZE);
    out.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
    dtaClose(file);
    return true;
}

}  // namespace

// The original destructor (0x0042bfa0) runs Reset() and frees the tables; only
// the tables are left to free here since m_script is null outside Compile().
C_ScriptCompiler::~C_ScriptCompiler() = default;

bool C_ScriptCompiler::Compile(const char *path, C_Script *script)
{
    if (!script || !ReadScriptFile(path, m_source))
        return false;

    m_script = script;
    Reset();
    Preprocess();
    m_cursor = m_source.c_str();

    bool ok = true;
    do {
        if (!ParseStatement()) {
            // Failing only because the source ended is fine.
            if (m_token[0] != '\0')
                ok = false;
            break;
        }
    } while (m_cursor);

    if (!CheckProgram())
        ok = false;

    m_script = nullptr;
    if (ok) {
        const char *name = std::strrchr(path, '\\');
        script->m_fileName = name ? name + 1 : path;
    }
    return ok;
}

bool C_ScriptCompiler::CheckProgram() const
{
    bool ok = true;

    // Every label that was jumped to has been defined.
    for (const S_ScriptLabel &label : m_labels) {
        if (label.index < 0)
            ok = false;
    }

    // Every Whenever named by SetWhenever / SetWheneverTime exists. Forward
    // references are only resolved for SetWhenever (see ParseWhenever()).
    const std::vector<S_ScriptCommand *> &commands = m_script->m_commands;
    for (const S_ScriptCommand *cmd : commands) {
        if (cmd && (cmd->opcode == OP_SetWhenever || cmd->opcode == OP_SetWheneverTime) && cmd->Arg(0) < 0)
            ok = false;
    }

    if (!m_openBlocks.empty())
        ok = false;

    // Else sits between '}' and '{'; event handlers are followed by their block
    // (OnHit and OnGlobalAlarm are not checked).
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const S_ScriptCommand *cmd = commands[i];
        if (!cmd)
            continue;
        const bool blockFollows = i + 1 < commands.size() && commands[i + 1]->opcode == OP_BlockBegin;
        switch (cmd->opcode) {
        case OP_Else:
            if (!blockFollows || i == 0 || commands[i - 1]->opcode != OP_BlockEnd)
                ok = false;
            break;
        case OP_OnSignal:
        case OP_OnDeath:
        case OP_OnUse:
        case OP_OnAlarm:
        case OP_OnAlarmDone:
        case OP_OnExit:
        case OP_OnCutscene:
        case OP_OnCutsceneDone:
            if (!blockFollows)
                ok = false;
            break;
        default:
            break;
        }
    }
    return ok;
}

void C_ScriptCompiler::Reset()
{
    if (m_script)
        m_script->Clear();
    m_variables.clear();
    m_labels.clear();
    m_openBlocks.clear();
    m_line = 0;
    m_lineBreaksBeforeToken = 0;
}

// Replaces each `#include "file"` directive at the start of a line with the
// contents of the file, looked up in Scripts\<mission>\ first and then in
// Scripts\include\.
//
// The original blanked the directive but took the file name from its own token
// reader, which starts at the beginning of the source. A directive was therefore
// only honoured with nothing but other directives before it, and in practice only
// the first one worked, since inserting text left that reader behind. Scripts keep
// their include at the top, where both read the same file name.
void C_ScriptCompiler::Preprocess()
{
    static const char DIRECTIVE[] = "#include";
    const std::size_t directiveLength = sizeof(DIRECTIVE) - 1;

    int includes = 0;
    std::size_t pos = 0;
    while ((pos = m_source.find(DIRECTIVE, pos)) != std::string::npos) {
        // Only whitespace may precede the directive on its line, so one behind
        // "//" or inside a string literal is left alone.
        std::size_t lineStart = pos;
        while (lineStart > 0 && (m_source[lineStart - 1] == ' ' || m_source[lineStart - 1] == '\t'))
            --lineStart;
        if (lineStart > 0 && m_source[lineStart - 1] != '\r' && m_source[lineStart - 1] != '\n') {
            pos += directiveLength;
            continue;
        }

        // The quoted file name follows on the same line.
        std::size_t end = pos + directiveLength;
        while (end < m_source.size() && (m_source[end] == ' ' || m_source[end] == '\t'))
            ++end;
        std::string fileName;
        if (end < m_source.size() && m_source[end] == '"') {
            const std::size_t close = m_source.find_first_of("\"\r\n", end + 1);
            if (close != std::string::npos && m_source[close] == '"') {
                fileName = m_source.substr(end + 1, close - end - 1);
                end = close + 1;
            }
        }
        if (fileName.empty()) {
            // Not a valid directive: only the keyword is removed.
            m_source.replace(pos, directiveLength, directiveLength, ' ');
            continue;
        }
        m_source.erase(pos, end - pos);
        // Included text is scanned too, so nested includes work.
        if (++includes <= SCRIPT_MAX_INCLUDES)
            InsertInclude(pos, fileName.c_str());
    }
}

bool C_ScriptCompiler::InsertInclude(std::size_t pos, const char *fileName)
{
    char path[0x104];
    std::string text;
    std::snprintf(path, sizeof(path), "Scripts\\%s\\%s", GetGame()->GetMissionName(), fileName);
    if (!ReadScriptFile(path, text)) {
        std::snprintf(path, sizeof(path), "Scripts\\include\\%s", fileName);
        if (!ReadScriptFile(path, text))
            return false;
    }
    // Text after a NUL byte would never be read.
    text.resize(std::strlen(text.c_str()));
    m_source.insert(pos, text);
    return true;
}

// ---------------------------------------------------------------------------
// lexer

bool C_ScriptCompiler::NextToken()
{
    m_token[0] = '\0';
    if (!m_cursor || *m_cursor == '\0')
        return false;
    SkipWhitespace();
    if (!m_cursor)
        return false;

    std::size_t length = 0;
    const auto append = [this, &length](char c) {
        if (length < sizeof(m_token) - 1)
            m_token[length++] = c;
    };

    const char first = *m_cursor;
    if (IsNameChar(first, false)) {
        while (IsNameChar(*m_cursor, true))
            append(*m_cursor++);
    } else if (IsNumberChar(first)) {
        while (IsNumberChar(*m_cursor))
            append(*m_cursor++);
    } else if (first == '/' && m_cursor[1] == '/') {
        // A comment is one token reaching to the end of the line.
        while (*m_cursor != '\r' && *m_cursor != '\n' && *m_cursor != '\0')
            append(*m_cursor++);
    } else if (first == '"') {
        // String literal with its quotes; an unterminated one ends with the line.
        do {
            append(*m_cursor++);
        } while (*m_cursor != '"' && *m_cursor != '\r' && *m_cursor != '\n' && *m_cursor != '\0');
        if (*m_cursor == '"')
            append(*m_cursor++);
    } else {
        // Any other character is a token by itself (whitespace after it is left).
        m_token[0] = first;
        m_token[1] = '\0';
        ++m_cursor;
        return true;
    }
    m_token[length] = '\0';
    SkipWhitespace();
    return m_cursor != nullptr;
}

void C_ScriptCompiler::SkipWhitespace()
{
    m_lineBreaksBeforeToken = 0;
    for (;; ++m_cursor) {
        const char c = *m_cursor;
        if (c == '\r') {
            ++m_line;
            ++m_lineBreaksBeforeToken;
        } else if (c != ' ' && c != '\n' && c != '\t') {
            if (c == '\0')
                m_cursor = nullptr;
            return;
        }
    }
}

bool C_ScriptCompiler::Expect(const char *text)
{
    NextToken();
    return _stricmp(m_token, text) == 0;
}

void C_ScriptCompiler::JoinOperator()
{
    const char first = m_token[0];
    if (first == '\0' || m_token[1] != '\0' || !std::strchr("=<>!", first))
        return;

    // Rewinding also rewinds the line counter (the original counted those lines twice).
    const char *const cursor = m_cursor;
    const int line = m_line;
    NextToken();
    const char second = m_token[1] == '\0' ? m_token[0] : '\0';
    if (second == '=' || (first == '<' && second == '>')) {
        m_token[0] = first;
        m_token[1] = second;
        m_token[2] = '\0';
        return;
    }
    m_cursor = cursor;
    m_line = line;
    m_token[0] = first;
    m_token[1] = '\0';
}

// ---------------------------------------------------------------------------
// symbols

bool C_ScriptCompiler::FindVariable(const char *name, int *type, int *index) const
{
    if (type)
        *type = 0;
    const int entry = FindVariableEntry(name);
    if (entry < 0)
        return false;
    if (type)
        *type = m_variables[entry].type;
    if (index)
        *index = m_variables[entry].index;
    return true;
}

const S_ScriptVariable *C_ScriptCompiler::GetVariable(const char *name) const
{
    const int entry = FindVariableEntry(name);
    return entry >= 0 ? &m_variables[entry] : nullptr;
}

int C_ScriptCompiler::FindVariableEntry(const char *name) const
{
    for (std::size_t i = m_variables.size(); i-- > 0;) {
        if (m_variables[i].name.EqualsNoCase(name))
            return static_cast<int>(i);
    }
    return -1;
}

int C_ScriptCompiler::FindTypedVariable(const char *name, int type) const
{
    for (std::size_t i = m_variables.size(); i-- > 0;) {
        const S_ScriptVariable &var = m_variables[i];
        if (var.type == type && var.name.EqualsNoCase(name))
            return var.index;
    }
    return -1;
}

int C_ScriptCompiler::FindLabel(const char *name) const
{
    for (std::size_t i = m_labels.size(); i-- > 0;) {
        if (m_labels[i].name.EqualsNoCase(name))
            return static_cast<int>(i);
    }
    return -1;
}

// Original behaviour: ParseLiteral copied a literal with sprintf(buffer, literal) and
// the expression parser with C_fstr(literal). MSVC's printf prints the character after
// a '%' that starts no conversion ("%%" -> "%", "%B" -> "B") and drops a '%' at the
// end ("%%nudazed1%" -> "%nudazed1", shipped). Real conversions ("%d") read missing
// arguments in the original; they do not occur in shipped scripts and are treated
// like any other character here.
C_str ScriptStringLiteral(const char *text, std::size_t length)
{
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] == '%' && ++i == length)
            break;
        out += text[i];
    }
    return C_str(out.c_str(), out.size());
}

int C_ScriptCompiler::ParseLiteral(int opcode, int argIndex, const char *token)
{
    const int type = GetScriptArgType(opcode, argIndex);

    if (type == SCRIPT_TYPE_INTEGER) {
        // "-" arrives as a token of its own.
        const bool negative = std::strcmp(token, "-") == 0;
        if (negative) {
            NextToken();
            token = m_token;
        }
        char *end = nullptr;
        int value = static_cast<int>(std::strtod(token, &end));
        if (end < token + std::strlen(token)) {
            if (!_stricmp(token, "true"))
                value = 1;
            else if (!_stricmp(token, "false"))
                value = 0;
            else
                return -1;
        }
        if (negative)
            value = -value;

        // Constants are shared; literals themselves are added as plain numbers,
        // so in practice every literal gets its own entry.
        const std::vector<S_ScriptNumber> &numbers = m_script->m_numbers;
        for (std::size_t i = numbers.size(); i-- > 0;) {
            if (numbers[i].value == value && numbers[i].isConstant)
                return static_cast<int>(i);
        }
        return m_script->AddNumber(value, false);
    }

    if (type == SCRIPT_TYPE_STRING && token[0] == '"') {
        const std::size_t length = std::strlen(token);
        if (token[length - 1] == '"' && length > 1) {
            // Formatted like the original's sprintf before the case-insensitive
            // pooling below.
            const C_str text = ScriptStringLiteral(token + 1, length - 2);
            const std::vector<C_str> &strings = m_script->m_strings;
            for (std::size_t i = strings.size(); i-- > 0;) {
                if (strings[i].EqualsNoCase(text))
                    return static_cast<int>(i);
            }
            return m_script->AddString(text);
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// statements

int C_ScriptCompiler::Emit(int opcode, const int *args)
{
    auto *cmd = new S_ScriptCommand;
    cmd->opcode = opcode;
    cmd->SetArgs(args);
    const int index = m_script->AddCommand(cmd);
    if (index < 0) {
        cmd->FreeArgs();
        delete cmd;
    }
    return index;
}

int C_ScriptCompiler::CommandCount() const
{
    return m_script->CommandCount();
}

int C_ScriptCompiler::ExpressionCount() const
{
    return static_cast<int>(m_script->m_expressions.size());
}

bool C_ScriptCompiler::ParseStatement()
{
    if (!NextToken() && m_token[0] == '\0')
        return false;
    if (m_token[0] == '/' && m_token[1] == '/')
        return true;

    const int keyword = FindScriptKeyword(m_token);
    switch (keyword) {
    case 0:
        return ParseAssignment();
    case OP_Integer:
    case OP_Bool:
    case OP_String:
    case OP_Enum:
    case OP_Frame:
        return ParseDeclaration(keyword);
    case OP_Label:
        return ParseLabel();
    case OP_GoTo:
    case OP_Gosub:
        return ParseJump(keyword);
    case OP_If:
        return ParseIf();
    case OP_Else:
        return ParseElse();
    case OP_Whenever:
        return ParseWhenever();
    case OP_Return:
        return ParseReturn();
    case OP_OnSignal:
    case OP_OnCutscene:
    case OP_OnCutsceneDone:
        return ParseNumberedHandler(keyword);
    case OP_ScriptStartPoint:
        return ParseStartPoint();
    case OP_OnDeath:
        return ParseHandler(keyword, &m_script->m_onDeath);
    case OP_OnUse:
        return ParseHandler(keyword, &m_script->m_onUse);
    case OP_OnAlarm:
        return ParseHandler(keyword, &m_script->m_onAlarm);
    case OP_OnHit:
        return ParseHandler(keyword, &m_script->m_onHit);
    case OP_OnGlobalAlarm:
        return ParseHandler(keyword, &m_script->m_onGlobalAlarm);
    case OP_OnExit:
        return ParseHandler(keyword, &m_script->m_onExit);
    case OP_OnAlarmDone:
        return ParseHandler(keyword, &m_script->m_onAlarmDone);
    case OP_SetWhenever:
    case OP_SetWheneverTime:
        return ParseSetWhenever(keyword);
    case OP_BlockBegin:
        return ParseBlock(true);
    case OP_BlockEnd:
        return ParseBlock(false);
    case OP_Block:
        return ParseBlockKeyword();
    default:
        if (keyword >= KEYWORD_FIRST_OPERATOR && keyword <= KEYWORD_LAST_OPERATOR)
            return false;
        return ParseCommand(keyword);
    }
}

// name = expression;
bool C_ScriptCompiler::ParseAssignment()
{
    int type = 0;
    int index = 0;
    if (!FindVariable(m_token, &type, &index) || !Expect("=") || !ParseExpression())
        return false;
    const int args[3] = {ExpressionCount() - 1, type, index};
    Emit(OP_Evaluate, args);
    return true;
}

// Integer name [= expression];  String name [= expression];  Frame name;
bool C_ScriptCompiler::ParseDeclaration(int keyword)
{
    NextToken();
    if (FindVariable(m_token, nullptr, nullptr))
        return false;                                       // declared twice

    S_ScriptVariable var;
    var.name = m_token;
    switch (keyword) {
    case OP_Integer:
        var.type = SCRIPT_TYPE_INTEGER;
        var.index = m_script->AddNumber(0, false);
        break;
    case OP_String:
        var.type = SCRIPT_TYPE_STRING;
        var.index = m_script->AddString(C_str(""));
        break;
    case OP_Frame:
        var.type = SCRIPT_TYPE_FRAME;
        m_script->m_frames.push_back(nullptr);
        var.index = static_cast<int>(m_script->m_frames.size()) - 1;
        break;
    default:                                                // Bool, Enum: no storage
        break;
    }
    m_variables.push_back(var);

    NextToken();
    if (!std::strcmp(m_token, "=") && (keyword == OP_Integer || keyword == OP_String)) {
        if (!ParseExpression())
            return false;
        const int args[3] = {ExpressionCount() - 1, var.type, var.index};
        Emit(OP_Evaluate, args);
        // m_token now holds the expression's terminator.
    }
    return !std::strcmp(m_token, ";");
}

// Label name:
bool C_ScriptCompiler::ParseLabel()
{
    NextToken();
    const int here = CommandCount();
    const int label = FindLabel(m_token);
    if (label < 0) {
        m_labels.push_back(S_ScriptLabel{m_token, here});
    } else {
        if (m_labels[label].index != -1)
            return false;                                   // defined twice
        // Resolve the jumps that were waiting for this label.
        for (S_ScriptCommand *cmd : m_script->m_commands) {
            if (cmd && (cmd->opcode == OP_GoTo || cmd->opcode == OP_Gosub) && cmd->args[0] == -1 - label) {
                cmd->args[0] = here;
                m_labels[label].index = here;
            }
        }
    }
    return Expect(":");
}

// GoTo name;  Gosub name;
bool C_ScriptCompiler::ParseJump(int opcode)
{
    NextToken();
    const int label = FindLabel(m_token);
    int target;
    if (label < 0) {
        // First mention of a label defined further down.
        target = -1 - static_cast<int>(m_labels.size());
        m_labels.push_back(S_ScriptLabel{m_token, -1});
    } else if (m_labels[label].index == -1) {
        target = -1 - label;
    } else {
        // (The original also appended a duplicate of the label here, which
        // changed nothing.)
        target = m_labels[label].index;
    }
    Emit(opcode, &target);
    return Expect(";");
}

// If (expression) { ... }
bool C_ScriptCompiler::ParseIf()
{
    Expect("(");                                            // not checked, as in the original
    if (!ParseExpression())
        return false;
    const int expression = ExpressionCount() - 1;
    Emit(OP_If, &expression);
    if (!Expect(")"))
        return false;

    // A block must follow; it is left to the next statement.
    const char *const cursor = m_cursor;
    const int line = m_line;
    if (!Expect("{"))
        return false;
    m_cursor = cursor;
    m_line = line;
    return true;
}

bool C_ScriptCompiler::ParseElse()
{
    Emit(OP_Else);
    return true;
}

bool C_ScriptCompiler::ParseReturn()
{
    Emit(OP_Return);
    return Expect(";");
}

// Block { ... }
bool C_ScriptCompiler::ParseBlockKeyword()
{
    Emit(OP_Block);
    return true;
}

// ScriptStartPoint:
bool C_ScriptCompiler::ParseStartPoint()
{
    if (!Expect(":") || m_script->m_startPoint != -1)
        return false;
    m_script->m_startPoint = CommandCount();
    return true;
}

// Whenever name (expression) { ... }
bool C_ScriptCompiler::ParseWhenever()
{
    NextToken();
    const C_str name = m_token;
    int type = 0;
    int index = -1;
    if (FindVariable(name, &type, &index) && type == SCRIPT_TYPE_WHENEVER && index >= 0)
        return false;                                       // name already used by a Whenever

    Expect("(");                                            // not checked, as in the original
    if (!ParseExpression())
        return false;
    const int expression = ExpressionCount() - 1;
    Emit(OP_Whenever, &expression);

    auto *whenever = new S_ScriptWhenever;
    whenever->expression = expression;
    whenever->label = CommandCount();
    m_script->m_whenevers.push_back(whenever);
    const int wheneverIndex = static_cast<int>(m_script->m_whenevers.size()) - 1;
    m_variables.push_back(S_ScriptVariable{name, SCRIPT_TYPE_WHENEVER, wheneverIndex});

    // Resolve SetWhenever commands that named this Whenever before it was defined
    // (SetWheneverTime ones stay unresolved and fail the compilation). The original
    // copied both arguments to the stack, replaced the first and passed both back to
    // SetArgs, so the second argument stays intact (0x0042ea45-0x0042eb70).
    for (S_ScriptCommand *cmd : m_script->m_commands) {
        if (!cmd || cmd->opcode != OP_SetWhenever || cmd->args[0] >= 0)
            continue;
        const std::size_t entry = static_cast<std::size_t>(-1 - cmd->args[0]);
        if (entry < m_variables.size() && m_variables[entry].name.EqualsNoCase(name))
            cmd->args[0] = wheneverIndex;
    }
    return Expect(")");
}

// SetWhenever(name, on);  SetWheneverTime(name, period);
bool C_ScriptCompiler::ParseSetWhenever(int opcode)
{
    if (!Expect("("))
        return false;
    NextToken();
    const C_str name = m_token;
    const int entry = FindVariableEntry(name);
    if (!Expect(","))
        return false;

    int args[2];
    if (entry < 0) {
        // Whenever defined further down: refer to a placeholder symbol.
        m_variables.push_back(S_ScriptVariable{name, SCRIPT_TYPE_WHENEVER, -1});
        args[0] = -static_cast<int>(m_variables.size());
    } else {
        const S_ScriptVariable &var = m_variables[entry];
        if (var.type != SCRIPT_TYPE_WHENEVER)
            return false;
        args[0] = var.index != -1 ? var.index : -1 - entry;
    }

    NextToken();
    args[1] = ParseLiteral(opcode, 1, m_token);
    if (args[1] == -1)
        return false;
    Emit(opcode, args);
    return Expect(")") && Expect(";");
}

bool C_ScriptCompiler::ParseHandlerNumber(int *number)
{
    Expect("(");                                            // not checked, as in the original
    NextToken();
    char *end = nullptr;
    *number = static_cast<int>(std::strtod(m_token, &end));
    if (end < m_token + std::strlen(m_token))
        return false;
    return Expect(")");
}

// OnSignal(n) { ... }  OnCutscene(n) { ... }  OnCutsceneDone(n) { ... }
bool C_ScriptCompiler::ParseNumberedHandler(int opcode)
{
    int number = 0;
    if (!ParseHandlerNumber(&number))
        return false;
    Emit(opcode, &number);
    const int entry = CommandCount();
    // A second handler for the same number is ignored.
    if (opcode == OP_OnSignal)
        m_script->AddSignalHandler(number, entry);
    else if (opcode == OP_OnCutscene)
        m_script->AddCutsceneHandler(number, entry);
    else
        m_script->AddCutsceneDoneHandler(number, entry);
    return true;
}

// OnDeath() { ... } etc.
bool C_ScriptCompiler::ParseHandler(int opcode, int *entry)
{
    if (!Expect("(") || !Expect(")"))
        return false;
    Emit(opcode);
    *entry = CommandCount();
    return true;
}

bool C_ScriptCompiler::ParseBlock(bool open)
{
    if (open) {
        m_openBlocks.push_back(Emit(OP_BlockBegin));
        return true;
    }
    if (m_openBlocks.empty())
        return false;
    const int begin = m_openBlocks.back();
    m_openBlocks.pop_back();
    if (begin >= 0) {
        // The '{' records the number of commands in its body.
        S_ScriptCommand *cmd = m_script->m_commands[begin];
        cmd->FreeArgs();
        cmd->args = new int[1]{CommandCount() - begin - 1};
    }
    Emit(OP_BlockEnd);
    return true;
}

bool C_ScriptCompiler::ParseCommand(int opcode)
{
    int args[SCRIPT_MAX_ARGS] = {};
    if (!ParseArguments(opcode, args))
        return false;
    Emit(opcode, args);
    return true;
}

bool C_ScriptCompiler::ParseArguments(int opcode, int *args)
{
    if (!Expect("("))
        return false;
    const int count = GetScriptArgCount(opcode);
    for (int i = 0; i < count; ++i) {
        NextToken();
        int index = FindTypedVariable(m_token, GetScriptArgType(opcode, i));
        if (index == -1)
            index = ParseLiteral(opcode, i, m_token);
        if (index == -1)
            return false;
        args[i] = index;
        if (i == count - 1)
            break;
        NextToken();
        if (std::strcmp(m_token, ",") != 0)
            return false;
    }
    return Expect(")") && Expect(";");
}

bool C_ScriptCompiler::ParseExpression()
{
    auto *expr = new C_ScriptExpression;
    while (NextToken()) {
        JoinOperator();
        // Commas only separate function arguments, which are positional.
        if (!_stricmp(m_token, ",") || expr->AddToken(m_token))
            continue;

        // This token ends the expression: ';' is consumed, anything else (the
        // closing ')' of If / Whenever) is given back to the caller.
        if (_stricmp(m_token, ";") != 0 && m_cursor)
            --m_cursor;
        if (!expr->Build())
            break;
        const std::size_t count = m_script->m_expressions.size();
        m_script->AddExpression(expr);
        if (m_script->m_expressions.size() == count)
            delete expr;                                    // not kept (clients have no program)
        return true;
    }
    delete expr;
    return false;
}
