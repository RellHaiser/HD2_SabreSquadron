#include "script/script_expression.h"

#include <cstdlib>
#include <cstring>

#include "script/script.h"
#include "script/script_compiler.h"
#include "script/script_tables.h"

namespace {

bool IsBinaryOperator(int kind)                         // 0x00412540
{
    // Note: <= and >= are missing here, so the compiler drops them from
    // expressions (Evaluate() would support them).
    return kind > 3 && (kind < SOP_LESS_EQUAL || kind == SOP_NOT_EQUAL);
}

int PrecedenceClass(int op)
{
    switch (op) {
    case SOP_ADD:
    case SOP_SUB:
        return 2;
    case SOP_MUL:
    case SOP_DIV:
        return 3;
    case SOP_GREATER:
    case SOP_LESS:
    case SOP_EQUAL:
    case SOP_NOT_EQUAL:
        return 1;
    default:                                            // AND, OR (and <=, >=)
        return 0;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// evaluation

int C_ScriptExpression::Evaluate(C_Script *script) const
{
    if (!script)
        return 0;

    int result = 0;
    if (m_children.empty()) {
        if (m_isFunction)
            result = script->CallFunction(m_function, m_operandTypes, m_operands);
        else if (m_operandTypes[0] == SCRIPT_TYPE_INTEGER)
            result = script->NumberVar(m_operands[0]);
    } else {
        result = m_children[0]->Evaluate(script);
        for (size_t i = 1; i < m_children.size(); ++i) {
            const C_ScriptExpression *child = m_children[i];
            // The original reads past the operator list when the compiler dropped an
            // operator (see IsBinaryOperator); treat that as "no operation".
            if (!child || i - 1 >= m_operators.size())
                continue;
            switch (m_operators[i - 1]) {
            case SOP_ADD: result += child->Evaluate(script); break;
            case SOP_SUB: result -= child->Evaluate(script); break;
            case SOP_MUL: result *= child->Evaluate(script); break;
            case SOP_DIV: {
                const int divisor = child->Evaluate(script);
                result = divisor ? result / divisor : 0;    // the original faults on 0
                break;
            }
            case SOP_AND: result = result != 0 && child->Evaluate(script) != 0; break;
            case SOP_OR: result = result != 0 || child->Evaluate(script) != 0; break;
            case SOP_GREATER: result = result > child->Evaluate(script); break;
            case SOP_LESS: result = result < child->Evaluate(script); break;
            case SOP_EQUAL: result = result == child->Evaluate(script); break;
            case SOP_LESS_EQUAL: result = result <= child->Evaluate(script); break;
            case SOP_GREATER_EQUAL: result = result >= child->Evaluate(script); break;
            case SOP_NOT_EQUAL: result = result != child->Evaluate(script); break;
            default: break;
            }
        }
    }
    if (m_negate)
        result = result == 0;
    return result;
}

C_str C_ScriptExpression::ToString(C_Script *script) const
{
    C_str text = "";
    if (!script)
        return text;

    if (m_children.empty()) {
        if (m_isFunction)
            return text;
        for (int i = 0; i < 4; ++i) {
            if (m_operandTypes[i] == SCRIPT_TYPE_INTEGER)
                text += C_fstr("%d", script->NumberVar(m_operands[i]));
            else if (m_operandTypes[i] == SCRIPT_TYPE_STRING)
                text += script->StringVar(m_operands[i]);
        }
        return text;
    }

    // Only '+' joins parts; other operators drop their right operand.
    text = m_children[0]->ToString(script);
    for (size_t i = 1; i < m_children.size(); ++i) {
        if (m_children[i] && i - 1 < m_operators.size() && m_operators[i - 1] == SOP_ADD)
            text += m_children[i]->ToString(script);
    }
    return text;
}

void C_ScriptExpression::Clear()
{
    for (size_t i = m_children.size(); i-- > 0;) {
        delete m_children[i];
        m_children[i] = nullptr;
    }
    m_children.clear();
    m_children.shrink_to_fit();
    m_tokens.clear();
    m_tokens.shrink_to_fit();
    m_operators.clear();
    m_operators.shrink_to_fit();
}

// ---------------------------------------------------------------------------
// compilation

bool C_ScriptExpression::AddToken(const char *text)
{
    S_ScriptToken token = {};
    switch (FindScriptKeyword(text)) {
    case 0x108: token.kind = SOP_ADD; break;            // +
    case 0x109: token.kind = SOP_SUB; break;            // -
    case 0x10A: token.kind = SOP_MUL; break;            // *
    case 0x10B: token.kind = SOP_DIV; break;            // /
    case 0x10C: token.kind = SOP_GREATER; break;        // >
    case 0x10D: token.kind = SOP_LESS; break;           // <
    case 0x10E: token.kind = SOP_EQUAL; break;          // ==
    case 0x10F: token.kind = SOP_GREATER_EQUAL; break;  // >=
    case 0x110: token.kind = SOP_LESS_EQUAL; break;     // <=
    case 0x111: token.kind = SOP_NOT_EQUAL; break;      // != <>
    default:
        if (!_stricmp(text, "AND")) {
            token.kind = SOP_AND;
        } else if (!_stricmp(text, "OR")) {
            token.kind = SOP_OR;
        } else if (!_stricmp(text, "NOT") || !_stricmp(text, "!")) {
            token.kind = STOK_NOT;
        } else if (!_stricmp(text, "(")) {
            token.kind = STOK_OPEN;
            ++m_parenDepth;
        } else if (!_stricmp(text, ")")) {
            token.kind = STOK_CLOSE;
            if (--m_parenDepth < 0)
                return false;
        } else if (!_stricmp(text, ";")) {
            return false;
        } else {
            C_Script *script = g_scriptCompiler.GetScript();
            int type = 0;
            // A variable may not follow a '-' (only literals are negated).
            const bool afterMinus = !m_tokens.empty() && m_tokens.back().kind == SOP_SUB;
            if (g_scriptCompiler.FindVariable(text, &type, nullptr) && !afterMinus) {
                token.kind = STOK_OPERAND;
                token.type = type;
                if (const S_ScriptVariable *var = g_scriptCompiler.GetVariable(text))
                    token.value = var->index;
            } else if (const S_ScriptFunctionDef *def = FindScriptFunction(text)) {
                token.kind = STOK_FUNCTION;
                token.value = def->id;
            } else {
                char *end = nullptr;
                const float number = static_cast<float>(std::strtod(text, &end));
                const size_t length = std::strlen(text);
                if (end != text + length) {
                    // "string literal", formatted like the original's C_fstr(literal)
                    if (text[0] != '"' || text[length - 1] != '"')
                        return false;
                    token.kind = STOK_OPERAND;
                    token.type = SCRIPT_TYPE_STRING;
                    // (A lone '"' at the end of a line gave the original an empty string.)
                    token.value = script->AddString(ScriptStringLiteral(text + 1, length > 1 ? length - 2 : 0));
                } else {
                    // numeric literal, truncated like the original __ftol
                    token.kind = STOK_OPERAND;
                    token.type = SCRIPT_TYPE_INTEGER;
                    token.value = script->AddNumber(static_cast<int>(number));

                    // A '-' at the start or after '(' / another operator is a sign:
                    // fold it into the literal (the '-' token itself stays).
                    if (!m_tokens.empty() && m_tokens.back().kind == SOP_SUB) {
                        bool unary = m_tokens.size() == 1;
                        if (!unary) {
                            const int before = m_tokens[m_tokens.size() - 2].kind;
                            unary = before == STOK_OPEN || before == SOP_MUL || before == SOP_DIV ||
                                    before == SOP_EQUAL || before == SOP_NOT_EQUAL ||
                                    before == SOP_LESS_EQUAL || before == SOP_GREATER_EQUAL ||
                                    before == SOP_LESS;
                        }
                        if (unary)
                            script->NumberVar(token.value) = -script->NumberVar(token.value);
                    }
                }
            }
        }
        break;
    }
    m_tokens.push_back(token);
    return true;
}

void C_ScriptExpression::AddOperand(int tokenIndex, bool negate)
{
    auto *leaf = new C_ScriptExpression;
    m_children.push_back(leaf);
    leaf->m_operands[0] = m_tokens[tokenIndex].value;
    leaf->m_operandTypes[0] = m_tokens[tokenIndex].type;
    leaf->m_negate = negate;
}

void C_ScriptExpression::GroupRange(int first, int last)
{
    // Always called with last == first + 1.
    if (last >= static_cast<int>(m_children.size())) {
        // More operators than operands: a '-' sign was kept as an operator
        // ("x == -1", "-5 * 2"). Original behaviour: the group was stored in the last
        // slot (or past the end) and the list then shortened, which dropped the last
        // operand and this operator, so "x == -1" evaluates to x.
        if (!m_children.empty()) {
            delete m_children.back();
            m_children.pop_back();
        }
        m_operators.erase(m_operators.begin() + first);
        return;
    }

    auto *group = new C_ScriptExpression;
    for (int i = first; i <= last; ++i) {
        if (i < static_cast<int>(m_children.size()) && m_children[i])
            group->m_children.push_back(m_children[i]);
    }
    for (int i = first; i < last; ++i)
        group->m_operators.push_back(m_operators[i]);

    // The original also deletes m_children[first] here although `group` keeps
    // using it (use after free); the port keeps the node alive.
    m_children.erase(m_children.begin() + first + 1);
    m_children[first] = group;
    m_operators.erase(m_operators.begin() + first);
}

bool C_ScriptExpression::HasUniformPrecedence() const
{
    int previous = -1;
    for (size_t i = m_operators.size(); i-- > 0;) {
        const int cls = PrecedenceClass(m_operators[i]);
        if (previous != -1 && cls != previous)
            return false;
        previous = cls;
    }
    return true;
}

void C_ScriptExpression::GroupOperators(int op1, int op2, int op3)
{
    for (int i = 0; i < static_cast<int>(m_operators.size()); ++i) {
        const int op = m_operators[i];
        if (op == op1 || op == op2 || op == op3) {
            GroupRange(i, i + 1);
            --i;
        }
    }
}

bool C_ScriptExpression::Build()
{
    m_children.clear();
    m_operators.clear();

    // NOT applies to the operand or function call that follows it.
    int notState = 0;                                   // 1 = NOT seen, 2 = applies now
    size_t i = 0;
    while (i < m_tokens.size()) {
        notState = notState == 1 ? 2 : 0;
        const S_ScriptToken &token = m_tokens[i];
        switch (token.kind) {
        case STOK_OPERAND:
            AddOperand(static_cast<int>(i), notState == 2);
            ++i;
            break;

        case STOK_FUNCTION: {
            const int id = token.value;
            const int argCount = GetScriptFunctionArgCount(id);
            if (m_tokens.size() <= i + argCount + 2 || m_tokens[i + 1].kind != STOK_OPEN)
                return false;
            const S_ScriptFunctionDef *def = nullptr;
            for (const S_ScriptFunctionDef *d = g_scriptFunctionDefs; d->name; ++d) {
                if (d->id == id) {
                    def = d;
                    break;
                }
            }
            auto *call = new C_ScriptExpression;
            call->m_isFunction = true;
            call->m_function = id;
            call->m_negate = notState == 2;
            size_t pos = i + 1;
            for (int a = 0; a < argCount; ++a) {
                ++pos;
                if (m_tokens[pos].kind == SOP_SUB)      // sign of a literal argument
                    ++pos;
                if (m_tokens[pos].kind != STOK_OPERAND || !def || m_tokens[pos].type != def->argTypes[a]) {
                    delete call;
                    return false;
                }
                call->m_operands[a] = m_tokens[pos].value;
                call->m_operandTypes[a] = m_tokens[pos].type;
            }
            m_children.push_back(call);
            i = pos + 1;
            if (m_tokens[i].kind != STOK_CLOSE)
                return false;
            ++i;
            break;
        }

        case STOK_OPEN: {
            // Parenthesised sub-expression (NOT before it is ignored).
            auto *group = new C_ScriptExpression;
            int depth = 1;
            size_t last = i;
            size_t j = i + 1;
            for (; j < m_tokens.size(); ++j) {
                if (m_tokens[j].kind == STOK_OPEN)
                    ++depth;
                else if (m_tokens[j].kind == STOK_CLOSE)
                    --depth;
                if (depth == 0)
                    break;
                group->m_tokens.push_back(m_tokens[j]);
                last = j;
            }
            if (depth != 0 || !group->Build()) {
                delete group;
                return false;
            }
            m_children.push_back(group);
            i = last + 2;
            break;
        }

        case STOK_NOT:
            notState = 1;
            ++i;
            break;

        default:
            if (IsBinaryOperator(token.kind))
                m_operators.push_back(token.kind);
            ++i;
            break;
        }
    }

    // Bind * / first, then + -, then comparisons, then AND / OR.
    if (!HasUniformPrecedence()) {
        GroupOperators(SOP_MUL, SOP_DIV);
        if (!HasUniformPrecedence()) {
            GroupOperators(SOP_ADD, SOP_SUB);
            if (!HasUniformPrecedence()) {
                GroupOperators(SOP_GREATER, SOP_LESS, SOP_EQUAL);
                if (!HasUniformPrecedence()) {
                    GroupOperators(SOP_OR, SOP_AND);
                    if (!HasUniformPrecedence())
                        return false;
                }
            }
        }
    }
    return !m_tokens.empty();
}
