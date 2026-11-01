#include "game/xml_node.h"

#include <cctype>
#include <cstring>

#include "ls3d/rw_data.h"

namespace {

// The largest file read.
constexpr int MAX_FILE_SIZE = 0x100000;
// An element's tag: '<' to '>' at least 3 and at most 255 apart; a closing tag at most 100.
constexpr int MIN_TAG = 3;
constexpr int MAX_TAG = 0xFF;
constexpr int MAX_CLOSE_TAG = 100;
// The text copied of an element (Fix: kept in the original's 4 KB buffer).
constexpr size_t MAX_TEXT = 0xFFF;

const char COMMENT_START[] = "<!--";
const char COMMENT_END[] = "-->";

// `length` characters of `text` (up to a NUL among them).
C_str Copy(const char *text, size_t length)
{
    return C_str(text, strnlen(text, length));
}

// A comment at `at`: false when it is not one, else `*end` after its end.
bool SkipComment(const char *at, const char *from, const char **end)
{
    if (std::strstr(at, COMMENT_START) != at)
        return false;
    const char *close = std::strstr(at, COMMENT_END);
    if (!close || close - at < 4)
        return false;
    *end = std::strstr(from, COMMENT_END) + 3;
    return true;
}

bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

} // namespace

C_XmlNode::~C_XmlNode()
{
    for (size_t i = m_children.size(); i-- > 0;)
        delete m_children[i];
}

bool C_XmlNode::Load(const char *fileName)
{
    if (!fileName)
        return false;
    const int file = dtaOpen(fileName, false);
    if (file == -1)
        return false;
    const int size = dtaSeek(file, 0, 2);
    if (size > MAX_FILE_SIZE) {
        dtaClose(file);
        return false;
    }
    dtaSeek(file, 0, 0);
    std::vector<char> buffer(size + 1);
    if (dtaRead(file, buffer.data(), size) < size) {
        dtaClose(file);
        return false;
    }
    buffer[size] = '\0';
    const char *text = buffer.data();
    const bool parsed = ParseElement(&text);
    dtaClose(file);
    m_walk = m_unknown0C = -1;
    return parsed;
}

bool C_XmlNode::IsNamed(const char *name) const
{
    return std::strcmp(name, m_name) == 0;
}

const char *C_XmlNode::GetAttribute(const char *name) const
{
    for (const auto &attribute : m_attributes)
        if (std::strcmp(name, attribute.first) == 0)
            return attribute.second;
    return nullptr;
}

C_XmlNode *C_XmlNode::Next()
{
    if (m_walk == -1) {
        m_walk = 0;
        if (!m_children.empty())
            m_children[0]->m_walk = m_children[0]->m_unknown0C = -1;
        return this;
    }
    while (static_cast<size_t>(m_walk) < m_children.size()) {
        if (C_XmlNode *next = m_children[m_walk]->Next())
            return next;
        if (static_cast<size_t>(++m_walk) < m_children.size())
            m_children[m_walk]->m_walk = m_children[m_walk]->m_unknown0C = -1;
    }
    return nullptr;
}

bool C_XmlNode::ParseElement(const char **text)
{
    const char *open;
    const char *close;
    for (;;) {
        open = std::strchr(*text, '<');
        close = std::strchr(*text, '>');
        if (!open || !close || close - open < MIN_TAG || close - open > MAX_TAG)
            return false;
        if (open[1] != '!')
            break;
        if (!SkipComment(open, *text, text))
            return false;
    }
    const bool empty = close[-1] == '/';
    *text = open + 1;
    if (empty)
        return ParseTag(text);
    if (!ParseTag(text))
        return false;
    ++*text;
    return ParseContent(text);
}

bool C_XmlNode::ParseTag(const char **text)
{
    const char *const start = *text;
    for (;;) {
        const char c = **text;
        if (c == '\0')
            return ParseAttributes(text);
        if (IsSpace(c)) {
            m_name = Copy(start, *text - start);
            return ParseAttributes(text);
        }
        if (c == '/' || c == '>') {
            m_name = Copy(start, *text - start);
            return m_name.Size() != 0;
        }
        ++*text;
    }
}

bool C_XmlNode::ParseAttributes(const char **text)
{
    const char *const end = std::strchr(*text, '>');
    while (**text != '\0') {
        const char c = **text;
        if ((c == '/' && (*text)[1] == '>') || c == '>')
            return true;
        if (IsSpace(c)) {
            ++*text;
            continue;
        }
        // name="value" (the name up to its '=', the value between the next two quotes).
        const char *const equals = std::strchr(*text, '=');
        if (!equals || equals - *text < 1 || !end || end - equals < 4)
            return false;
        const C_str name = Copy(*text, equals - *text);
        *text = equals;
        const char *const quote = std::strchr(*text, '"');
        if (!quote || quote == *text || end - quote < 2)
            return false;
        *text = quote + 1;
        // (Quirk kept: the closing quote is looked for past the first character, an empty value's
        // quote is not found.)
        const char *const endQuote = std::strchr(quote + 2, '"');
        if (!endQuote || endQuote - *text < 1 || endQuote == end || end - endQuote < 0)
            return false;
        const C_str value = Copy(*text, endQuote - *text);
        *text = endQuote + 1;
        m_attributes.push_back({name, value});
    }
    return false;
}

bool C_XmlNode::ParseContent(const char **text)
{
    const char *const start = *text;
    char c = **text;
    while (c != '\0') {
        const char next = (*text)[1];
        if (next == '\0')
            break;
        if (c == '<') {
            if (next == '/') {
                // The text only of an element without children.
                if (m_children.empty()) {
                    const size_t length = static_cast<size_t>(*text - start);
                    m_text = Copy(start, length < MAX_TEXT ? length : MAX_TEXT);
                }
                return ParseClose(text);
            }
            if (next == '!') {
                const char *after;
                if (!SkipComment(*text, *text, &after))
                    break;
                *text = after;
                c = **text;
                continue;
            }
            auto *child = new C_XmlNode();
            m_children.push_back(child);
            if (!child->ParseElement(text))
                break;
            // (Its closing tag is not left: the scan goes on through it.)
        }
        ++*text;
        c = **text;
    }
    return false;
}

bool C_XmlNode::ParseClose(const char **text) const
{
    const char *const open = std::strchr(*text, '<');
    const char *const slash = std::strchr(*text, '/');
    const char *const close = std::strchr(*text, '>');
    if (!open || !close || !slash || slash - open != 1 || close - open < MIN_TAG || close - open > MAX_CLOSE_TAG)
        return false;
    const C_str name = Copy(slash + 1, close - slash - 1);
    return std::strcmp(name, m_name) == 0;
}
