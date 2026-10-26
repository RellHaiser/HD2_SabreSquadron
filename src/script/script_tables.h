// Static description of the mission script language: command signatures, the
// built-in condition functions usable in expressions and the keyword list.
#pragma once

#include <cstdint>

// Value types of script variables, command arguments and expression operands.
enum E_ScriptType {
    SCRIPT_TYPE_NONE = 0,
    SCRIPT_TYPE_INTEGER = 2,
    SCRIPT_TYPE_STRING = 4,
    SCRIPT_TYPE_FRAME = 5,
    SCRIPT_TYPE_EXPRESSION = 7,      // argument is an index into the expression table
    SCRIPT_TYPE_WHENEVER = 8,        // named Whenever block
};

constexpr int SCRIPT_MAX_ARGS = 4;

struct S_ScriptCommandDef {
    int opcode;
    int argTypes[SCRIPT_MAX_ARGS];           // E_ScriptType, 0 terminates the list
    const char *argNames[SCRIPT_MAX_ARGS];   // shown by the in-game editor
};

struct S_ScriptFunctionDef {
    const char *name;                        // e.g. "_PlayerInRange"
    int id;                                  // E_ScriptFunction
    int argTypes[SCRIPT_MAX_ARGS];
    const char *argNames[SCRIPT_MAX_ARGS];
    const char *result;                      // description of the return value
};

struct S_ScriptKeyword {
    const char *name;
    int token;                               // opcode, or operator token (see E_ScriptToken)
    uint32_t color;                          // editor syntax colour (0xRRGGBB)
};

extern const S_ScriptCommandDef g_scriptCommandDefs[];      // 0x008338a8
extern const S_ScriptFunctionDef g_scriptFunctionDefs[];    // 0x00832d60
extern const S_ScriptKeyword g_scriptKeywords[];            // 0x00835b68

// Number of arguments of a command (0x00414680) and the type of one of them (0x004146d0).
int GetScriptArgCount(int opcode);
int GetScriptArgType(int opcode, int index);
// Built-in function lookup by name (0x0042e0e0) and argument count (0x0042e080).
const S_ScriptFunctionDef *FindScriptFunction(const char *name);
int GetScriptFunctionArgCount(int functionId);
// Token id of a keyword, 0 if `name` is not a keyword (0x0042de80).
int FindScriptKeyword(const char *name);
