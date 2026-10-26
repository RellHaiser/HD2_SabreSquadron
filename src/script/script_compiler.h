// C_ScriptCompiler — translates mission script source (*.scr) into a C_Script.
//
// The language is statement based; keywords and names are case-insensitive:
//
//   #include "common.scr"                 (at the start of a line, see Preprocess())
//   Integer count = 5;                    declarations: Integer, String, Frame
//   String text;                          (Bool / Enum parse but get no storage)
//   Frame door;
//   count = count + 1;                    assignment of an expression
//   ScriptStartPoint:                     where the script starts / restarts
//   Label loop:  GoTo loop;  Gosub sub;  Return;
//   If (count > 3) { ... } Else { ... }
//   Whenever name (expr) { ... }          tested every 500 ms until it fires once;
//   SetWhenever(name, 1);                 re-armed / disabled with SetWhenever,
//   SetWheneverTime(name, 1000);          period changed with SetWheneverTime
//   OnSignal(3) { ... }  OnCutscene(1) { ... }  OnCutsceneDone(1) { ... }
//   OnDeath() { ... }    (also OnUse, OnHit, OnAlarm, OnAlarmDone, OnGlobalAlarm, OnExit)
//   Block { ... }                         body runs without yielding
//   FRM_SetOn(door, 0);                   any command of g_scriptCommandDefs
//   // comment to the end of the line
//
// Command arguments are variable names or literals (numbers, true/false, "strings").
// The output is the flat command list described in script.h; jump targets that are
// not known yet are encoded as -1 - (label / variable index) and patched later.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common/c_str.h"

class C_Script;

// A named symbol of the script being compiled: a variable or a named Whenever.
struct S_ScriptVariable {                                   // 12 bytes
    C_str name;
    int type = 0;                                           // E_ScriptType
    int index = -1;                                         // into the table of that type, -1 = not defined yet
};

// A GoTo / Gosub target.
struct S_ScriptLabel {                                      // 8 bytes
    C_str name;
    int index = -1;                                         // command index, -1 = used but not defined yet
};

class C_ScriptCompiler {                                    // 0x444 bytes
public:
    C_ScriptCompiler() = default;                           // 0x0042cb80
    ~C_ScriptCompiler();                                    // 0x0042bfa0
    C_ScriptCompiler(const C_ScriptCompiler &) = delete;
    C_ScriptCompiler &operator=(const C_ScriptCompiler &) = delete;

    // Compiles the script file at `path` into `script` (which is cleared first).
    // On success the script's file name becomes the name part of `path`.
    bool Compile(const char *path, C_Script *script);      // 0x0042c850

    // Script being compiled, null outside Compile() (used by the expression parser).
    C_Script *GetScript() const { return m_script; }
    // Looks a symbol up by name, latest declaration first; `type` / `index`
    // (both optional) receive its type and table index.
    bool FindVariable(const char *name, int *type, int *index) const;   // 0x0042ceb0
    const S_ScriptVariable *GetVariable(const char *name) const;        // 0x0042ce30
    // Line breaks read so far (position of the last error after a failed Compile()).
    int GetLine() const { return m_line; }

private:
    void Reset();                                           // 0x0042cf90
    // (The original takes the source buffer, g_scriptSource 0x008693e8.)
    void Preprocess();                                      // 0x0042c580
    // Inserts the contents of an include file at `pos` of the source (the original is a
    // __stdcall (fileName, insertion pointer) working on the global buffers).
    bool InsertInclude(std::size_t pos, const char *fileName);   // 0x0042c4b0

    // --- lexer -------------------------------------------------------------------
    // Reads the next token into m_token; false once the end of the source is
    // reached (m_token may still hold the last token then).
    bool NextToken();                                       // 0x0042cc00
    void SkipWhitespace();                                  // 0x0042cdd0
    // Reads the next token and compares it with `text`.
    bool Expect(const char *text);                          // 0x0042cf60
    // Turns the operator in m_token into "==", "<=", ">=", "!=" or "<>" when the
    // next token completes it (inlined in 0x0042d170).
    void JoinOperator();

    // --- statements ----------------------------------------------------------------
    bool ParseStatement();                                  // 0x0042c0b0
    bool ParseAssignment();                                 // 0x0042e160
    bool ParseDeclaration(int keyword);                     // 0x0042ecd0
    bool ParseLabel();                                      // 0x0042f0a0
    // GoTo / Gosub.
    bool ParseJump(int opcode);                             // 0x0042f2e0, 0x0042fd10
    bool ParseIf();                                         // 0x0042e2a0
    bool ParseElse();                                       // 0x0042e3d0
    bool ParseReturn();                                     // 0x0042e380
    bool ParseBlockKeyword();                               // 0x0042e340
    bool ParseStartPoint();                                 // 0x0042e410
    bool ParseWhenever();                                   // 0x0042e7c0
    // SetWhenever / SetWheneverTime.
    bool ParseSetWhenever(int opcode);                      // 0x0042f530, 0x0042f920
    // OnSignal / OnCutscene / OnCutsceneDone (n).
    bool ParseNumberedHandler(int opcode);                  // 0x0042e460, 0x0042e550, 0x0042e640
    // OnDeath() etc.; `entry` receives the command index of the handler's block.
    bool ParseHandler(int opcode, int *entry);              // 0x0042e730
    // '{' (open) or '}'.
    bool ParseBlock(bool open);                             // 0x0042d740
    bool ParseCommand(int opcode);                          // 0x0042ec20
    // "(arg, ...);" of a command; `args` receives the variable indices.
    bool ParseArguments(int opcode, int *args);             // 0x0042dd40
    // Expression up to ';' (consumed) or an unmatched token such as ')' (left for
    // the caller); added to the script's expression table.
    bool ParseExpression();                                 // 0x0042d170

    // Table index of the variable `name` of type `type`, -1 if there is none.
    int FindTypedVariable(const char *name, int type) const;   // 0x0042d890
    // Adds the literal `token` for argument `argIndex` of `opcode` to the script's
    // number / string table; returns its index or -1 if it is not a valid literal.
    int ParseLiteral(int opcode, int argIndex, const char *token);   // 0x0042d920
    // Reads the "(n)" of a numbered handler; false if it is not a number.
    bool ParseHandlerNumber(int *number);

    // Appends a command; returns its index (-1 when the script keeps no program,
    // i.e. on clients).
    int Emit(int opcode, const int *args = nullptr);
    int CommandCount() const;
    int ExpressionCount() const;
    // Index into m_labels / m_variables, latest first; -1 if missing.
    int FindLabel(const char *name) const;
    int FindVariableEntry(const char *name) const;
    // Checks run after the last statement (labels defined, blocks closed...).
    bool CheckProgram() const;

    C_Script *m_script = nullptr;                           // +0x000 script being compiled
    const char *m_cursor = nullptr;                         // +0x004 read position, null at the end
    char m_token[0x400] = {};                               // +0x008 current token
    int m_line = 0;                                         // +0x408 line breaks read
    int m_lineBreaksBeforeToken = 0;                        // +0x40C ... directly before the current token
    std::vector<S_ScriptVariable> m_variables;              // +0x410
    std::vector<S_ScriptLabel> m_labels;                    // +0x420
    std::vector<int> m_openBlocks;                          // +0x430 command index of each unclosed '{'
    std::string m_source;                                   // static buffer 0x008693e8 in the original
};

extern C_ScriptCompiler g_scriptCompiler;                   // 0x008993f0

// Text of a string literal (`length` characters without the quotes) as the original
// stored it: run through printf with no arguments, so "%%" becomes "%" and a lone
// '%' is dropped. Scripts rely on this ("%%turnright" names MODELS\%turnright.5ds).
C_str ScriptStringLiteral(const char *text, std::size_t length);
