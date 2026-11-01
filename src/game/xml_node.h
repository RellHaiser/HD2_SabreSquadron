// The menus' XML files (game/xml_node.cpp, original source 0x006e5df0..0x006e6fe0 and
// 0x0061b410..0x0061be50, part 9a-ii): FontConfig.hxml and the missions' item lists. A small
// reader: one element per node with its attributes (in their order), its children and, for an
// element without children, its text; comments (<!-- -->) skipped. No entities, no declarations.
#pragma once

#include <utility>
#include <vector>

#include "common/c_str.h"

class C_XmlNode {                                       // 0x30 bytes
public:
    C_XmlNode() = default;
    ~C_XmlNode();                                       // 0x006e5e50
    C_XmlNode(const C_XmlNode &) = delete;
    C_XmlNode &operator=(const C_XmlNode &) = delete;

    // The file's first element into this node (the file at most 1 MB). False when it does not
    // open or read, or the element is not well formed.
    bool Load(const char *fileName);                    // 0x006e68c0
    bool IsNamed(const char *name) const;               // 0x006e6740
    // The attribute's value (none: null).
    const char *GetAttribute(const char *name) const;   // 0x006e67a0
    // The node's elements in document order, the node itself first, then null (each node keeps
    // its place: a node's own walk also moves the walk of the node above it).
    C_XmlNode *Next();                                  // 0x006e6800 (and 0x006e69a0)

    const C_str &GetName() const { return m_name; }
    const C_str &GetText() const { return m_text; }
    int NumChildren() const { return static_cast<int>(m_children.size()); }
    C_XmlNode *GetChild(int i) const { return m_children[i]; }

private:
    // An element at `*text` (comments before it skipped): its tag, then its content.
    bool ParseElement(const char **text);               // 0x006e6670
    // The tag's name, then its attributes.
    bool ParseTag(const char **text);                   // 0x006e6250
    bool ParseAttributes(const char **text);            // 0x006e6340
    // The content up to the closing tag: the children, the text.
    bool ParseContent(const char **text);               // 0x006e5f90
    // The closing tag: this element's name.
    bool ParseClose(const char **text) const;           // 0x006e6160

    C_str m_name;                                       // +0x00
    C_str m_text;                                       // +0x04
    int m_walk = -1;                                    // +0x08 the child the walk is in (-1: not started)
    int m_unknown0C = -1;                               // +0x0C (set with m_walk, not read)
    std::vector<std::pair<C_str, C_str>> m_attributes;  // +0x10
    std::vector<C_XmlNode *> m_children;                // +0x20
};
