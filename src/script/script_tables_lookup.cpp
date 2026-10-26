// Queries on the static script language tables (script_tables.cpp).
#include <cstring>

#include "script/script_tables.h"

namespace {

const S_ScriptCommandDef *FindCommandDef(int opcode)
{
    for (const S_ScriptCommandDef *def = g_scriptCommandDefs; def->opcode; ++def) {
        if (def->opcode == opcode)
            return def;
    }
    return nullptr;
}

int CountArgs(const int (&types)[SCRIPT_MAX_ARGS])
{
    int count = 0;
    while (count < SCRIPT_MAX_ARGS && types[count] != SCRIPT_TYPE_NONE)
        ++count;
    return count;
}

int g_keywordCount = -1;                                    // 0x008993e8

} // namespace

int GetScriptArgCount(int opcode)
{
    const S_ScriptCommandDef *def = FindCommandDef(opcode);
    // Unknown opcodes use the terminator entry, which has no arguments.
    return def ? CountArgs(def->argTypes) : 0;
}

int GetScriptArgType(int opcode, int index)
{
    const S_ScriptCommandDef *def = FindCommandDef(opcode);
    return def ? def->argTypes[index] : SCRIPT_TYPE_NONE;
}

const S_ScriptFunctionDef *FindScriptFunction(const char *name)
{
    for (const S_ScriptFunctionDef *def = g_scriptFunctionDefs; def->name; ++def) {
        if (!_stricmp(name, def->name))
            return def;
    }
    return nullptr;
}

int GetScriptFunctionArgCount(int functionId)
{
    for (const S_ScriptFunctionDef *def = g_scriptFunctionDefs; def->name; ++def) {
        if (def->id == functionId)
            return CountArgs(def->argTypes);
    }
    return 0;
}

int FindScriptKeyword(const char *name)
{
    if (g_keywordCount < 0) {                               // counted by 0x0042cb80
        g_keywordCount = 0;
        while (g_scriptKeywords[g_keywordCount].name)
            ++g_keywordCount;
    }
    // Searched from the end, so the operators win over same-named entries.
    for (int i = g_keywordCount; i-- > 0;) {
        if (!_stricmp(name, g_scriptKeywords[i].name))
            return g_scriptKeywords[i].token;
    }
    return 0;
}
