// Part 9c-vi-b's factory (game/control_factory.cpp: C_Control::CreateChild), in the factory build
// (the text build with FACTORY_TEST: the real factory, control_test.inc's fake left out; the other
// tests' lines not checked), run by ControlsTest last: the control made per type found by its
// frame's first engine call throwing (its every slot), the object the first big allocation since
// (operator new replaced). Prints "factory:" with 1 per check met.
#include <cstdlib>
#include <new>
#include <typeinfo>

// The allocations of g_fmMin bytes or more counted, the first kept (0: not tracked).
void *g_fmMade;
std::size_t g_fmMin;
int g_fmBig;
void *operator new(std::size_t n)
{
    void *p = std::malloc(n ? n : 1);
    if (!p)
        throw std::bad_alloc();
    if (g_fmMin && n >= g_fmMin && g_fmBig++ == 0)
        g_fmMade = p;
    return p;
}
// (Not inlined: g++ warns of the malloc / free mismatch where it is.)
__attribute__((noinline)) void operator delete(void *p) noexcept { std::free(p); }
__attribute__((noinline)) void operator delete(void *p, std::size_t) noexcept { std::free(p); }

namespace {

struct S_FactoryResults {
    bool fmTypes = true, fmUnknown = true, fmMissing = true, fmMade = true, fmMessages = true, fmStrings = true;
    bool fmFailed = true;
};

// A frame whose every engine call throws (its first argument kept).
struct S_FmThrown {
    I3D_frame *frame;
};
void __stdcall F_FmThrow(I3D_frame *self) { throw S_FmThrown{self}; }
void *g_fmThrowVt[SLOTS];

void FmTrack()
{
    g_fmMade = nullptr;
    g_fmBig = 0;
    g_fmMin = sizeof(C_Control);
}

}  // namespace

void FactoryTest(I3D_scene *scene)
{
    S_FactoryResults r;
    for (void *&slot : g_fmThrowVt)
        slot = reinterpret_cast<void *>(&F_FmThrow);
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- each type: its control made with the parent's scene, the description's id, the parent;
    // made on the child frame (the description's name a mask) ---
    {
        const std::type_info *types[] = {
            &typeid(C_Button), &typeid(C_EditBox), &typeid(C_PictureControl), &typeid(C_ControlGroup),
            &typeid(C_ListItem), &typeid(C_Slider), &typeid(C_ScrollBar), &typeid(C_CheckBox),
            &typeid(C_ToggleButton), &typeid(C_ListGroup), &typeid(C_FrameSwitch), &typeid(C_HotKeyControl),
            &typeid(C_TitledEditBox), &typeid(C_ScrolledText), &typeid(C_SpinControl), &typeid(C_ComboBox),
            &typeid(C_LabelControl), &typeid(C_TitleControl), &typeid(C_TextButton), &typeid(C_Tooltip),
            &typeid(C_BarControl), &typeid(C_UpgradeBar), &typeid(C_ServerList), &typeid(C_SkillControl),
            &typeid(C_SelectList), &typeid(C_EditBox), &typeid(C_ItemInfo), &typeid(C_ItemPictures),
            &typeid(C_SideIndicator), &typeid(C_MarkBox), &typeid(C_PlayerList), &typeid(C_ChatInput),
            &typeid(C_MovieControl), &typeid(C_HidingList), &typeid(C_HighlightCheckBox), &typeid(C_PagesControl),
            &typeid(C_PoolList), &typeid(C_CheckGroup), &typeid(C_TitledComboBox)};
        int met = 0;
        for (int type = 2; type <= 0x28; ++type) {
            I3D_frame *frame = GfFrame(FRAME_DUMMY, "fmparent");
            GfFrame(FRAME_DUMMY, "fmother", frame);
            I3D_frame *kid = GfFrame(FRAME_DUMMY, "fmkid", frame);
            *reinterpret_cast<void ***>(kid) = g_fmThrowVt;
            C_Control parent(scene, 0x00100000, nullptr);
            S_ControlDesc desc(type, "fmki*", 0x00100000 | static_cast<uint32_t>(type) << 12);
            desc.children.push_back(new S_ControlDesc(5, "fmgrand", 0x00100001));
            FmTrack();
            I3D_frame *thrown = nullptr;
            try {
                parent.CreateChild(frame, frame, &desc);
            } catch (const S_FmThrown &t) {
                thrown = t.frame;
            }
            g_fmMin = 0;
            auto *made = static_cast<C_Control *>(g_fmMade);
            const bool ok = thrown == kid && made && typeid(*made) == *types[type - 2] && made->m_id == desc.id &&
                            made->m_parent == &parent && made->m_scene == scene && parent.m_children.empty();
            met += ok;
            if (!ok)
                std::printf("factory type %x: %d %s\n", type, thrown == kid, made ? typeid(*made).name() : "-");
            // (The made control leaked: its frame throws.)
        }
        WM(r.fmTypes, met == 0x27);
    }

    // --- an unknown type: false, nothing made; a frame not found: the same ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "fmparent");
        GfFrame(FRAME_DUMMY, "fmkid", frame);
        C_Control parent(scene, 0x00100000, nullptr);
        for (int type : {-1, 0, 1, 0x29, 0x2a, 0x102}) {
            S_ControlDesc desc(type, "fmkid", 0x00101000);
            FmTrack();
            const bool ok = parent.CreateChild(frame, frame, &desc);
            g_fmMin = 0;
            WM(r.fmUnknown, !ok && g_fmBig == 0 && parent.m_children.empty());
        }
        S_ControlDesc desc(5, "fmnone*", 0x00101000);
        g_gfLog.clear();
        FmTrack();
        const bool ok = parent.CreateChild(frame, frame, &desc);
        g_fmMin = 0;
        WM(r.fmMissing, !ok && g_fmBig == 0 && parent.m_children.empty() && GfLogIs({"find fmparent fmnone* 10021"}));
    }

    // --- made: on its frame (its data the id, the type kept), its children made under it, the
    // description's messages handled (in it: their answers written back; their target no child's),
    // added last ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "fmparent");
        I3D_frame *kid = GfFrame(FRAME_DUMMY, "fmkid", frame);
        I3D_frame *grand = GfFrame(FRAME_DUMMY, "fmgrand", kid);
        I3D_frame *root = GfFrame(FRAME_DUMMY, "fmroot");
        C_Control parent(scene, 0x00100000, nullptr);
        C_Control *first = new C_Control(scene, 0x00105000, &parent);
        parent.m_children.push_back(first);
        S_ControlDesc desc(5, "fmki*", 0x00102000);
        desc.children.push_back(new S_ControlDesc(0x13, "fmgr*", 0x00102001));
        AddDescMessage(&desc, 0xF0, CONTROL_MSG_SET_VALUE, 7);
        AddDescMessage(&desc, 0xF0, CONTROL_MSG_GET_VALUE, 0);
        AddDescMessage(&desc, 0xF0, CONTROL_MSG_SET_INDEX, 3);
        AddDescMessage(&desc, 0xF0, CONTROL_MSG_GET_INDEX, 0);
        g_gfLog.clear();
        const bool ok = parent.CreateChild(frame, root, &desc);
        WM(r.fmMade, ok && parent.m_children.size() == 2 && parent.m_children[0] == first);
        C_Control *c = parent.m_children.size() == 2 ? parent.m_children[1] : nullptr;
        WM(r.fmMade, c && typeid(*c) == typeid(C_ControlGroup) && c->m_frame == kid && c->m_type == 5 &&
                         kid->GetData() == reinterpret_cast<S_FrameData *>(0x00102000) && c->m_parent == &parent &&
                         c->m_id == 0x00102000 && c->m_scene == scene);
        WM(r.fmMade, c && c->m_children.size() == 1 && typeid(*c->m_children[0]) == typeid(C_TitleControl) &&
                         c->m_children[0]->m_frame == grand && c->m_children[0]->m_parent == c);
        WM(r.fmMessages, c && c->m_value == 7 && c->m_index == 3 && desc.messages[1].value == 7 &&
                             desc.messages[3].value == 3 && desc.messages.size() == 4);
        kid->SetData(nullptr);
        grand->SetData(nullptr);
    }

    // --- the strings set before the messages (a scrolled text's lines, then its message's); the
    // root passed on (its list's select frame linked to it) ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "fmparent");
        I3D_frame *kid = GfFrame(FRAME_DUMMY, "fmscroll", frame);
        I3D_frame *root = GfFrame(FRAME_DUMMY, "fmroot");
        CbScrolledFrames(kid, "fm");
        I3D_frame *select = GfVisual("selectfm", I3DVISUAL_OBJECT, Find(kid, "textfm", 0xFFFF));
        C_Control parent(scene, 0x00100000, nullptr);
        S_ControlDesc desc(0xf, "fmscroll", 0x00103000);
        desc.strings = {C_str("x"), C_str("y")};
        AddDescMessage(&desc, 0, CONTROL_MSG_ADD_TEXT, 3, C_str("m"));
        const bool ok = parent.CreateChild(frame, root, &desc);
        auto *t = parent.m_children.size() == 1 ? dynamic_cast<C_ScrolledText *>(parent.m_children[0]) : nullptr;
        WM(r.fmStrings, ok && t && t->m_list && CsRuns(*t->m_list->m_box) == "x/19 y/19 m/3 ");
        WM(r.fmStrings, t && t->m_list && t->m_list->m_selectFrame == select && select->m_parent == root);
    }

    // --- not made on its frame (a button without its 3 frames): deleted, nothing added, its
    // messages not handled; still true ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "fmparent");
        GfFrame(FRAME_DUMMY, "fmkid", frame);
        C_Control parent(scene, 0x00100000, nullptr);
        S_ControlDesc desc(2, "fmkid", 0x00104000);
        AddDescMessage(&desc, 0, CONTROL_MSG_GET_INDEX, 9);
        const int refs = g_csCursor.refs;
        g_gfLog.clear();
        FmTrack();
        const bool ok = parent.CreateChild(frame, frame, &desc);
        g_fmMin = 0;
        WM(r.fmFailed, ok && parent.m_children.empty() && g_csCursor.refs == refs && g_fmBig == 1 &&
                           desc.messages[0].value == 9 && GfLogIs({"find fmparent fmkid 10021", "cursor acquire 1", "cursor release"}));
    }

    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("factory: fmTypes %d fmUnknown %d fmMissing %d fmMade %d fmMessages %d fmStrings %d fmFailed %d\n",
                r.fmTypes, r.fmUnknown, r.fmMissing, r.fmMade, r.fmMessages, r.fmStrings, r.fmFailed);
}
