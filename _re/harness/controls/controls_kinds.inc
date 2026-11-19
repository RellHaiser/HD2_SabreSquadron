// The earlier parts' kinds' helpers (parts 49-52: their random states, their fields into the original's blocks and back
// compared), for the later parts' groups made of them. Included inside the test's anonymous namespace, after
// controls_kit.inc.
const uint32_t O_BOX = 0x6C;
template <class T> C_Control *Make(I3D_scene *scene, uint32_t id, C_Control *parent) { return new C_Logged<T>(scene, id, parent); }
I3D_frame *RandomFrame() { return FrameAt(I(0, 15)); }
void NoFields(C_Control *) {}
void NoToOrig(C_Control *, uint8_t *) {}
std::string NoDiffers(C_Control *, const uint8_t *) { return ""; }
std::string FloatText(float f)
{
    char text[24];
    std::snprintf(text, sizeof text, "%08x", Bits(f));
    return text;
}
bool SameFloat(float a, float b) { return Bits(a) == Bits(b); }
std::string FramesDiffer(const uint8_t *o, std::initializer_list<std::pair<uint32_t, I3D_frame *>> fields)
{
    for (const auto &f : fields)
        if (OF<I3D_frame *>(o, f.first) != f.second)
            return "frame +" + std::to_string(f.first) + " " + PtrName(OF<I3D_frame *>(o, f.first)) + " / " + PtrName(f.second);
    return "";
}

// Text controls (+0x6C the box).
void RandomText(C_Control *c) { RandomBox(static_cast<C_TextControl *>(c)->m_box); }
void TextToOrig(C_Control *c, uint8_t *o) { BoxToOrig(static_cast<C_TextControl *>(c)->m_box, o, O_BOX); }
std::string TextDiffers(C_Control *c, const uint8_t *o) { return BoxDiffers(static_cast<C_TextControl *>(c)->m_box, o, O_BOX); }

// The group: +0x6C its focus.
void RandomGroup(C_Control *c)
{
    auto *g = static_cast<C_ControlGroup *>(c);
    g->m_focus = I(-1, int(g->m_children.size()));
}
void GroupToOrig(C_Control *c, uint8_t *o) { OF<int>(o, 0x6C) = static_cast<C_ControlGroup *>(c)->m_focus; }
std::string GroupDiffers(C_Control *c, const uint8_t *o)
{
    const int focus = static_cast<C_ControlGroup *>(c)->m_focus;
    return OF<int>(o, 0x6C) == focus ? "" : "focus " + std::to_string(OF<int>(o, 0x6C)) + " / " + std::to_string(focus);
}

// The button: +0x6C the cursor, +0x70 normal, +0x74 active, +0x78 inactive, +0x7C held, +0x80 the repeat time.
void RandomButton(C_Control *c)
{
    auto *b = static_cast<C_Button *>(c);
    b->m_cursor = TheCursor();
    b->m_normalFrame = RandomFrame();
    b->m_activeFrame = RandomFrame();
    b->m_inactiveFrame = RandomFrame();
    b->m_held = P(0.5);
    b->m_repeatTime = P(0.5) ? Pick({300, -1, 0, 299, 301, 500}) : I(-5, 2000);
}
void ButtonToOrig(C_Control *c, uint8_t *o)
{
    auto *b = static_cast<C_Button *>(c);
    OF<void *>(o, 0x6C) = b->m_cursor;
    OF<void *>(o, 0x70) = b->m_normalFrame;
    OF<void *>(o, 0x74) = b->m_activeFrame;
    OF<void *>(o, 0x78) = b->m_inactiveFrame;
    OF<bool>(o, 0x7C) = b->m_held;
    OF<int>(o, 0x80) = b->m_repeatTime;
}
std::string ButtonDiffers(C_Control *c, const uint8_t *o)
{
    auto *b = static_cast<C_Button *>(c);
    if (PtrName(OF<void *>(o, 0x6C)) != PtrName(b->m_cursor))
        return "button cursor";
    std::string why = FramesDiffer(o, {{0x70, b->m_normalFrame}, {0x74, b->m_activeFrame}, {0x78, b->m_inactiveFrame}});
    if (why.empty() && (OF<bool>(o, 0x7C) != b->m_held || OF<int>(o, 0x80) != b->m_repeatTime))
        why = "button held " + std::to_string(OF<bool>(o, 0x7C)) + " " + std::to_string(OF<int>(o, 0x80)) + " / " + std::to_string(b->m_held) +
              " " + std::to_string(b->m_repeatTime);
    return why;
}

// The slider: +0x6C the cursor, +0x70 normal, +0x74 active, +0x78 base, +0x7C min x, +0x80 min y, +0x84 max x, +0x88 max y,
// +0x8C range x, +0x90 range y, +0x98 horizontal, +0x99 vertical, +0x9C the value.
float RandomPlace() { return P(0.1) ? Pick({0.0f, 1.0f, -0.75f, -1.0f, 0.5f}) : F(-1.2f, 1.2f); }
void RandomSlider(C_Control *c)
{
    auto *s = static_cast<C_Slider *>(c);
    s->m_cursor = TheCursor();
    s->m_normalFrame = RandomFrame();
    s->m_activeFrame = RandomFrame();
    s->m_baseFrame = RandomFrame();
    s->m_minX = RandomPlace();
    s->m_minY = RandomPlace();
    s->m_maxX = P(0.9) ? s->m_minX + F(0, 1) : RandomPlace();
    s->m_maxY = P(0.9) ? s->m_minY + F(0, 1) : RandomPlace();
    s->m_rangeX = P(0.9) ? s->m_maxX - s->m_minX : P(0.5) ? 0.0f : RandomPlace();
    s->m_rangeY = P(0.9) ? s->m_maxY - s->m_minY : P(0.5) ? 0.0f : RandomPlace();
    s->m_horizontal = P(0.5);
    s->m_vertical = P(0.9) ? !s->m_horizontal : P(0.5);
    s->m_value = P(0.3) ? Pick({0.0f, 50.0f, 100.0f, 33.333336f}) : F(-10, 110);
}
void SliderToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_Slider *>(c);
    OF<void *>(o, 0x6C) = s->m_cursor;
    OF<void *>(o, 0x70) = s->m_normalFrame;
    OF<void *>(o, 0x74) = s->m_activeFrame;
    OF<void *>(o, 0x78) = s->m_baseFrame;
    OF<float>(o, 0x7C) = s->m_minX;
    OF<float>(o, 0x80) = s->m_minY;
    OF<float>(o, 0x84) = s->m_maxX;
    OF<float>(o, 0x88) = s->m_maxY;
    OF<float>(o, 0x8C) = s->m_rangeX;
    OF<float>(o, 0x90) = s->m_rangeY;
    OF<bool>(o, 0x98) = s->m_horizontal;
    OF<bool>(o, 0x99) = s->m_vertical;
    OF<float>(o, 0x9C) = s->m_value;
}
std::string SliderDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_Slider *>(c);
    if (PtrName(OF<void *>(o, 0x6C)) != PtrName(s->m_cursor) || OF<void *>(o, 0x70) != s->m_normalFrame || OF<void *>(o, 0x74) != s->m_activeFrame ||
        OF<void *>(o, 0x78) != s->m_baseFrame)
        return "slider frames";
    const float of[] = {OF<float>(o, 0x7C), OF<float>(o, 0x80), OF<float>(o, 0x84), OF<float>(o, 0x88), OF<float>(o, 0x8C), OF<float>(o, 0x90),
                        OF<float>(o, 0x9C)};
    const float pf[] = {s->m_minX, s->m_minY, s->m_maxX, s->m_maxY, s->m_rangeX, s->m_rangeY, s->m_value};
    static const char *const names[] = {"min x", "min y", "max x", "max y", "range x", "range y", "value"};
    for (int k = 0; k < 7; ++k)
        if (!SameFloat(of[k], pf[k]))
            return std::string("slider ") + names[k] + " " + FloatText(of[k]) + " / " + FloatText(pf[k]);
    if (OF<bool>(o, 0x98) != s->m_horizontal || OF<bool>(o, 0x99) != s->m_vertical)
        return "slider axes";
    return "";
}

// The scroll bar: +0x6C the repeat time, +0x70 held, +0x74 up, +0x78 down, +0x7C the slider, +0x80 count, +0x84 position,
// +0x88 step, +0x8C focus, +0x90 the page step.
void RandomScroll(C_Control *c)
{
    auto *s = static_cast<C_ScrollBar *>(c);
    s->m_repeatTime = P(0.3) ? Pick({0, 50, 300, -1}) : I(-50, 400);
    s->m_held = P(0.5);
    s->m_count = P(0.2) ? 0 : P(0.8) ? I(1, 12) : I(13, 300);
    s->m_position = s->m_count ? I(0, s->m_count) : 0;
    s->m_step = s->m_count > 1 ? 100.0f / float(s->m_count - 1) : 100.0f;
    // (Never 0, as SET_COUNT makes it: an infinite place sent to the original's own CRT floor takes its error path,
    // which needs the CRT the test never starts.)
    s->m_pageStep = P(0.6) ? 1 : I(0, 6);
    if (P(0.5) && !g_portNodes.empty())
        s->m_value = g_portNodes[size_t(I(0, int(g_portNodes.size()) - 1))]->m_id;
}
void ScrollToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_ScrollBar *>(c);
    OF<int>(o, 0x6C) = s->m_repeatTime;
    OF<bool>(o, 0x70) = s->m_held;
    OF<uint8_t *>(o, 0x74) = s->m_upButton ? ToOrigNode(s->m_upButton) : nullptr;
    OF<uint8_t *>(o, 0x78) = s->m_downButton ? ToOrigNode(s->m_downButton) : nullptr;
    OF<uint8_t *>(o, 0x7C) = s->m_slider ? ToOrigNode(s->m_slider) : nullptr;
    OF<int>(o, 0x80) = s->m_count;
    OF<int>(o, 0x84) = s->m_position;
    OF<float>(o, 0x88) = s->m_step;
    OF<int>(o, 0x8C) = s->m_focus;
    OF<int>(o, 0x90) = s->m_pageStep;
}
std::string ScrollDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_ScrollBar *>(c);
    char text[200];
    if (OF<int>(o, 0x6C) != s->m_repeatTime || OF<bool>(o, 0x70) != s->m_held || PtrName(OF<void *>(o, 0x74)) != PtrName(s->m_upButton) ||
        PtrName(OF<void *>(o, 0x78)) != PtrName(s->m_downButton) || PtrName(OF<void *>(o, 0x7C)) != PtrName(s->m_slider) ||
        OF<int>(o, 0x80) != s->m_count || OF<int>(o, 0x84) != s->m_position || !SameFloat(OF<float>(o, 0x88), s->m_step) ||
        OF<int>(o, 0x8C) != s->m_focus || OF<int>(o, 0x90) != s->m_pageStep) {
        std::snprintf(text, sizeof text, "scroll %d %d %s %s %s %d %d %s %d %d / %d %d %s %s %s %d %d %s %d %d", OF<int>(o, 0x6C), OF<bool>(o, 0x70),
                      PtrName(OF<void *>(o, 0x74)).c_str(), PtrName(OF<void *>(o, 0x78)).c_str(), PtrName(OF<void *>(o, 0x7C)).c_str(),
                      OF<int>(o, 0x80), OF<int>(o, 0x84), FloatText(OF<float>(o, 0x88)).c_str(), OF<int>(o, 0x8C), OF<int>(o, 0x90),
                      s->m_repeatTime, s->m_held, PtrName(s->m_upButton).c_str(), PtrName(s->m_downButton).c_str(), PtrName(s->m_slider).c_str(),
                      s->m_count, s->m_position, FloatText(s->m_step).c_str(), s->m_focus, s->m_pageStep);
        return text;
    }
    return "";
}

// The toggle buttons: +0x6C active down, +0x70 normal down, +0x74 active up, +0x78 normal up, +0x80 down, +0x81 over.
void RandomToggle(C_Control *c)
{
    auto *t = static_cast<C_ToggleButton *>(c);
    t->m_activeDown = RandomFrame();
    t->m_normalDown = RandomFrame();
    t->m_activeUp = RandomFrame();
    t->m_normalUp = RandomFrame();
    t->m_down = P(0.5);
    t->m_over = P(0.5);
}
void ToggleToOrig(C_Control *c, uint8_t *o)
{
    auto *t = static_cast<C_ToggleButton *>(c);
    OF<I3D_frame *>(o, 0x6C) = t->m_activeDown;
    OF<I3D_frame *>(o, 0x70) = t->m_normalDown;
    OF<I3D_frame *>(o, 0x74) = t->m_activeUp;
    OF<I3D_frame *>(o, 0x78) = t->m_normalUp;
    OF<bool>(o, 0x80) = t->m_down;
    OF<bool>(o, 0x81) = t->m_over;
}
std::string ToggleDiffers(C_Control *c, const uint8_t *o)
{
    auto *t = static_cast<C_ToggleButton *>(c);
    std::string why = FramesDiffer(o, {{0x6C, t->m_activeDown}, {0x70, t->m_normalDown}, {0x74, t->m_activeUp}, {0x78, t->m_normalUp}});
    if (why.empty() && (OF<bool>(o, 0x80) != t->m_down || OF<bool>(o, 0x81) != t->m_over))
        why = "down/over";
    return why;
}

// The edit controls (part 52's). The profile's unicode font (the port's member, the original's byte at g_gameProfile +
// 0x30): a round's.
bool g_unicode;
void SetUnicode(bool on)
{
    g_unicode = on;
    g_gameProfile.m_unicodeFont = on;
    *OriginalAt<bool *>(0x008aea0c + 0x30) = on;
}
const char *const FORBIDDEN[] = {"abc", "0123456789", "-", " x"};
// A text the buffer may hold (no '%': the original shows the buffer through C_fstr, a flaw the port fixes; valid UTF-8 at
// its start when the font is unicode: the original's backspace reads before the buffer at a lone continuation byte).
std::string RandomBufferText(int maxLength)
{
    std::string s;
    const int n = P(0.3) ? std::max(0, maxLength - I(0, 2)) : I(0, maxLength);
    if (g_unicode && P(0.2) && n >= 3)
        s = P(0.5) ? "\xC3\xA9" : "\xE2\x82\xAC";
    while (int(s.size()) < n) {
        const int kind = I(0, 9);
        if (kind < 6) {
            char c = char(I(0x20, 0x7E));
            if (c == '%')
                c = 'x';
            s += c;
        } else if (kind < 8) {
            s += char('0' + I(0, 9));
        } else if (g_unicode && !s.empty() && int(s.size()) + 5 <= n) {
            static const char *const seqs[] = {"\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xC3\xA9\xA9", "\xE2\x82",
                                               "\xF8\x80\x80\x80\x80", "\xF0\x9F\x98", "\xC3"};
            s += Pick(seqs);
        } else if (!g_unicode) {
            s += char(I(0x80, 0xFF));
        }
    }
    return s;
}

// The edit control: the box, +0x70 the saved text, +0x74 the buffer (the original's a char *), +0x78 the forbidden
// characters, +0x7C editing, +0x7D digits only, +0x80 the run, +0x84 the longest.
void RandomEdit(C_Control *c)
{
    auto *e = static_cast<C_EditControl *>(c);
    RandomBox(e->m_box);
    e->m_savedText = P(0.3) ? C_str() : C_str(P(0.5) ? "saved" : "");
    e->m_maxLength = P(0.6) ? 30 : I(1, 40);
    e->m_buffer.assign(size_t(e->m_maxLength) + 1, '\0');
    const std::string text = RandomBufferText(e->m_maxLength);
    std::memcpy(e->m_buffer.data(), text.data(), text.size());
    if (P(0.5))
        for (size_t k = text.size() + 1; k < e->m_buffer.size(); ++k)
            e->m_buffer[k] = char(P(0.5) ? I(0x21, 0x7E) : I(0x80, 0xBF));
    e->m_forbidden = P(0.5) ? nullptr : Pick(FORBIDDEN);
    e->m_editing = P(0.5);
    e->m_digitsOnly = P(0.3);
    e->m_run = I(-1, 7);
}
void EditToOrig(C_Control *c, uint8_t *o)
{
    auto *e = static_cast<C_EditControl *>(c);
    BoxToOrig(e->m_box, o, O_BOX);
    OF<uint32_t>(o, 0x70) = OrigText(e->m_savedText);
    // (The buffer in a block of the original's allocator: it frees it when it makes another.)
    auto *buffer = static_cast<char *>(std::calloc(1, e->m_buffer.size() + 1));
    std::memcpy(buffer, e->m_buffer.data(), e->m_buffer.size());
    OF<char *>(o, 0x74) = buffer;
    OF<const char *>(o, 0x78) = e->m_forbidden;
    OF<bool>(o, 0x7C) = e->m_editing;
    OF<bool>(o, 0x7D) = e->m_digitsOnly;
    OF<int>(o, 0x80) = e->m_run;
    OF<int>(o, 0x84) = e->m_maxLength;
}
std::string EditDiffers(C_Control *c, const uint8_t *o)
{
    auto *e = static_cast<C_EditControl *>(c);
    if (TextOf(OF<uint32_t>(o, 0x70)) != TextOf(e->m_savedText))
        return "saved text " + TextOf(OF<uint32_t>(o, 0x70)) + " / " + TextOf(e->m_savedText);
    if (OF<int>(o, 0x84) != e->m_maxLength)
        return "max length";
    const char *ob = OF<const char *>(o, 0x74);
    if (e->m_buffer.size() != size_t(e->m_maxLength) + 1)
        return "the port's buffer of " + std::to_string(e->m_buffer.size()) + " for " + std::to_string(e->m_maxLength);
    if (!ob || std::memcmp(ob, e->m_buffer.data(), e->m_buffer.size()) != 0)
        return "buffer '" + std::string(ob ? ob : "(null)") + "' / '" + e->m_buffer.data() + "'";
    if (OF<const char *>(o, 0x78) != e->m_forbidden || OF<bool>(o, 0x7C) != e->m_editing || OF<bool>(o, 0x7D) != e->m_digitsOnly ||
        OF<int>(o, 0x80) != e->m_run)
        return "edit fields " + std::to_string(OF<bool>(o, 0x7C)) + std::to_string(OF<bool>(o, 0x7D)) + " " + std::to_string(OF<int>(o, 0x80)) +
               " / " + std::to_string(e->m_editing) + std::to_string(e->m_digitsOnly) + " " + std::to_string(e->m_run);
    return BoxDiffers(e->m_box, o, O_BOX);
}

// The edit box: +0x88 has a range, +0x8C min, +0x90 max, +0x94 normal, +0x98 active.
void RandomEditBox(C_Control *c)
{
    auto *e = static_cast<C_EditBox *>(c);
    RandomEdit(c);
    e->m_min = P(0.5) ? I(0, 10) : I(-100, 100);
    e->m_max = P(0.2) ? e->m_min : e->m_min + I(0, 100);
    e->m_hasRange = P(0.9) ? e->m_min != e->m_max : P(0.5);
    e->m_normalFrame = RandomFrame();
    e->m_activeFrame = RandomFrame();
}
void EditBoxToOrig(C_Control *c, uint8_t *o)
{
    auto *e = static_cast<C_EditBox *>(c);
    EditToOrig(c, o);
    OF<bool>(o, 0x88) = e->m_hasRange;
    OF<int>(o, 0x8C) = e->m_min;
    OF<int>(o, 0x90) = e->m_max;
    OF<I3D_frame *>(o, 0x94) = e->m_normalFrame;
    OF<I3D_frame *>(o, 0x98) = e->m_activeFrame;
}
std::string EditBoxDiffers(C_Control *c, const uint8_t *o)
{
    auto *e = static_cast<C_EditBox *>(c);
    if (OF<bool>(o, 0x88) != e->m_hasRange || OF<int>(o, 0x8C) != e->m_min || OF<int>(o, 0x90) != e->m_max)
        return "range";
    if (OF<I3D_frame *>(o, 0x94) != e->m_normalFrame || OF<I3D_frame *>(o, 0x98) != e->m_activeFrame)
        return "edit box frames";
    return EditDiffers(c, o);
}

// The selection list: +0x88 the select frame, +0x8C the cursor, +0x94 the selected line.
void RandomSelect(C_Control *c)
{
    auto *s = static_cast<C_SelectList *>(c);
    RandomEdit(c);
    s->m_selectFrame = P(0.2) ? nullptr : RandomFrame();
    s->m_cursor = TheCursor();
    s->m_selected = I(-1, 7);
}
void SelectToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_SelectList *>(c);
    EditToOrig(c, o);
    OF<I3D_frame *>(o, 0x88) = s->m_selectFrame;
    OF<void *>(o, 0x8C) = s->m_cursor;
    OF<int>(o, 0x94) = s->m_selected;
}
std::string SelectDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_SelectList *>(c);
    if (OF<I3D_frame *>(o, 0x88) != s->m_selectFrame || PtrName(OF<void *>(o, 0x8C)) != PtrName(s->m_cursor))
        return "select frame / cursor";
    if (OF<int>(o, 0x94) != s->m_selected)
        return "selected " + std::to_string(OF<int>(o, 0x94)) + " / " + std::to_string(s->m_selected);
    return EditDiffers(c, o);
}
