// C_ScriptExpression — compiled condition / arithmetic expression of a script.
//
// The compiler feeds the expression's tokens one by one (AddToken), then Build()
// turns them into a tree: a node either is a leaf (a variable operand or a call
// of a built-in function) or holds child nodes joined by binary operators that
// are applied strictly left to right. Build() groups mixed-precedence operator
// runs into sub-nodes so that * / bind tighter than + -, which bind tighter than
// comparisons, which bind tighter than AND / OR.
//
// All values are integers; booleans are 0 / non-zero.
#pragma once

#include <vector>

#include "common/c_str.h"

class C_Script;

enum E_ScriptOperator {
    SOP_NONE = 0,
    SOP_ADD = 4,
    SOP_SUB = 5,
    SOP_MUL = 6,
    SOP_DIV = 7,
    SOP_AND = 8,
    SOP_OR = 9,
    SOP_GREATER = 10,
    SOP_LESS = 11,
    SOP_EQUAL = 12,
    SOP_LESS_EQUAL = 13,
    SOP_GREATER_EQUAL = 14,
    SOP_NOT_EQUAL = 15,
};

// Token kinds produced by AddToken(); kinds 4..15 are E_ScriptOperator values.
enum E_ScriptTokenKind {
    STOK_OPERAND = 0,             // value = variable index, type = E_ScriptType
    STOK_FUNCTION = 1,            // value = built-in function id
    STOK_OPEN = 2,                // (
    STOK_CLOSE = 3,               // )
    STOK_NOT = 16,                // NOT / !
};

struct S_ScriptToken {            // 12 bytes
    int kind;
    int value;
    int type;
};

class C_ScriptExpression {                                // 0x5C bytes
public:
    C_ScriptExpression() = default;
    ~C_ScriptExpression() { Clear(); }
    C_ScriptExpression(const C_ScriptExpression &) = delete;
    C_ScriptExpression &operator=(const C_ScriptExpression &) = delete;

    // Value of the expression for `script`'s variables (0 without a script).
    int Evaluate(C_Script *script) const;                 // 0x00411650
    // Concatenation of the string forms of the summed operands (Printf).
    C_str ToString(C_Script *script) const;               // 0x00411860
    // Deletes the subtree.
    void Clear();                                         // 0x00411c30

    // --- compilation --------------------------------------------------------------
    // Appends one source token; false on a syntax error.
    bool AddToken(const char *text);                      // 0x00411d00
    // Builds the tree from the collected tokens; false on a syntax error.
    bool Build();                                         // 0x00412760
    void FreeTokens() { m_tokens.clear(); m_tokens.shrink_to_fit(); }
    int GetParenDepth() const { return m_parenDepth; }

private:
    // Leaf for the operand token at `tokenIndex`.
    void AddOperand(int tokenIndex, bool negate);         // 0x00412480
    // Moves children [first, last] and the operators between them into a new child node.
    void GroupRange(int first, int last);                 // 0x00412560
    // True if all operators belong to the same precedence class.
    bool HasUniformPrecedence() const;                    // 0x00412d70
    void GroupOperators(int op1, int op2, int op3 = -1);

    int m_operands[4] = {};                               // +0x00 variable indices (leaf / call arguments)
    int m_operandTypes[4] = {};                           // +0x10 E_ScriptType
    bool m_isFunction = false;                            // +0x20 leaf is a built-in function call
    bool m_negate = false;                                // +0x21 NOT applied to the result
    int m_function = 0;                                   // +0x24 E_ScriptFunction
    std::vector<C_ScriptExpression *> m_children;         // +0x28
    std::vector<int> m_operators;                         // +0x38 m_operators[i-1] joins children i-1 and i
    std::vector<S_ScriptToken> m_tokens;                  // +0x48 only during compilation
    int m_parenDepth = 0;                                 // +0x58
};

// Built-in condition functions (g_scriptFunctionDefs[].id).
enum E_ScriptFunction {
    SFUNC_PLAYER_IN_RANGE = 0,
    SFUNC_ENEMY_IN_RANGE = 1,
    SFUNC_ALL_PLAYERS_IN_RANGE = 2,
    SFUNC_MINE_IN_RANGE = 3,
    SFUNC_ITEM_IN_RANGE = 4,
    SFUNC_ALL_PLAYERS_DEAD = 5,
    SFUNC_ALL_ENEMIES_DEAD = 6,
    SFUNC_GET_NUM_LIVE_ENEMIES = 7,
    SFUNC_GET_COUNT_OF_CARNAGE_ENEMIES = 8,
    SFUNC_SIGNAL_RECEIVED = 9,
    SFUNC_IS_LANGUAGE_VERSION = 10,
    SFUNC_RANDOM_INT = 11,
    SFUNC_LOAD_GAME_VALUE = 12,
    SFUNC_SEE_ENEMY = 13,
    SFUNC_SEE_FRIEND = 14,
    SFUNC_FRAME_IN_RANGE = 15,
    SFUNC_ACTOR_GET_STATE = 16,
    SFUNC_GET_OBJECTIVE_STATUS = 17,
    SFUNC_GET_ALARM_TYPE = 18,
    SFUNC_IS_IN_INVENTORY = 19,
    SFUNC_GET_NUM_MEMBERS_OF_CLIENT_TEAM = 20,
    SFUNC_GET_HUMAN_STORE_ID = 21,
    SFUNC_IS_TEAM_MEMBER_DEAD = 22,
    SFUNC_MP_ALL_TEAM_MEMBERS_ARE_IN_RANGE = 23,
    SFUNC_MP_IS_TEAM_MEMBER_IN_RANGE = 24,
    SFUNC_MP_IS_ITEM_IN_TEAM_INVENTORY = 25,
    SFUNC_SP_GET_GAME_TYPE = 26,
    SFUNC_SP_GET_GAME_DIFFICULTY = 27,
    SFUNC_GET_CAMERA_STATE = 28,
    SFUNC_GET_DIST_TO_ACTIVE_PLAYER = 29,
    SFUNC_CAMERA_IN_RANGE = 30,
    SFUNC_PLAYER_IN_BOX = 31,
    SFUNC_SIGNAL_PISTOL_FIRED = 32,
};
