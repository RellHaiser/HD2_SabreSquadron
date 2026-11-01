// A text box whose text flows through pages, each in one of its areas in turn (game/paged_text_box.cpp,
// part 9b-iv): the interface's text control's (its object's +0x6C; vtable 0x00814d38's messages: an
// area added by a frame's name, the page shown, the number of pages, the last page break). Its own
// layout fills the pages (LayoutPages, part 9b-iv-b); the render draws the shown page in the first
// area, the next in the second, and so on.
#pragma once

#include <vector>

#include "game/text_box.h"

// An area: a rectangle as C_TextBox's (pixels: left, top, right, bottom, width, height, depth).
struct S_TextArea {                                     // 0x1C bytes
    float left, top, right, bottom, width, height, depth;
};

// A page: the lines laid out in one area.
struct S_TextPage {                                     // 0x20 bytes
    S_TextPage() = default;
    // Its lines deleted. (The original never deletes a page: see ~C_PagedTextBox.)
    ~S_TextPage();
    S_TextPage(const S_TextPage &) = delete;
    S_TextPage &operator=(const S_TextPage &) = delete;

    int room = 0;                                       // +0x00 the height left (the layout's)
    bool overflows = false;                             // +0x04 its last line under the area: drawn clipped
    // Its area: an index into the box's areas (the original: a pointer into their vector, left
    // dangling when an area is added after the layout; the fix: the index, the same rectangle).
    int area = 0;                                       // +0x08
    std::vector<S_TextLine *> lines;                    // +0x0C
};

// S_TextStyleDesc::flags30: a run starting a new page (the paged box's; its page not empty).
constexpr uint32_t STYLE_NEW_PAGE = 0x20;

class C_PagedTextBox : public C_TextBox {               // 0xC8 bytes, vtable 0x008154a4
public:
    C_PagedTextBox() = default;                         // (inline in 0x00662160)
    // (The pages deleted: a fix, see S_TextPage.)
    ~C_PagedTextBox() override;                         // vtable +0x0C 0x006622f0 (0x00662310)

    // The pages shown from the current one, one per area (laid out first when something changed;
    // nothing before an area is added).
    void Render() override;                             // vtable +0x04 0x0064ae70

    // The visual's screen rectangle (AttachTo's) the box's and a new area's after the others.
    void AddArea(I3D_scene *scene, I3D_frame *visual);  // 0x0064b200
    // The page shown in the first area (0 always taken; another out of range: nothing).
    void SetPage(int page);                             // 0x0064bb30
    int GetPage() const { return m_page; }
    int NumPages() const { return static_cast<int>(m_pages.size()); }   // 0x0064bb60 (__fastcall)
    // Laid out again now.
    void Relayout();                                    // 0x0064bb80 (__fastcall)
    // The last page starting with a STYLE_NEW_PAGE run (its first line's first word's style; none: -1).
    int LastPageBreak() const;                          // 0x0064bb90 (__fastcall)

protected:
    // The runs laid out into the pages (part 9b-iv-b).
    bool LayoutPages();                                 // 0x006494a0
    // A page's lines drawn at their bases (no scroll), clipped to its area when it overflows (its
    // area off the screen: nothing).
    void RenderPage(const S_TextPage *page);            // 0x0064af10
    // An area's rectangle on the screen as a viewport (false: nothing of it on the screen).
    bool ClipArea(S_IGraphViewport *viewport, const S_TextArea &area) const;   // 0x0064b070

    int m_page = 0;                                     // +0x9C the first area's
    std::vector<S_TextArea> m_areas;                    // +0xA0
    std::vector<S_TextPage *> m_pages;                  // +0xB4
};
