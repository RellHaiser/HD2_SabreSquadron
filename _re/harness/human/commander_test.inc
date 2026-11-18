// Part 45 of the human harness (HUMAN_COMMANDER builds, build_commander.bat; included by human_test.cpp after
// squad_test.inc, whose fakes, pair, state and helpers it uses): the squad commander (game/squad_commander.cpp) against
// the original's object 0x0089ad40 (vtable 0x0080fb28) and its idle reports (0x0089ad30).
//
// The commander's members: the three humans, each with a squad controller pair of its own (the human's: part 44's
// g_psq / g_osq; the other two made alike). The squad controller's methods the commander calls are faked and logged
// on both sides (commander_fakes.inc: the port's by --wrap, the original's redirected); the controllers' fields it
// reads and writes (the action, quiet, the formation time, the combat mode, the order queue) are compared, each pair
// as part 44 compares its own. CancelOrders (a virtual slot) is logged on both sides.
//
// The commander's fields are converted and compared around each call (the groups and the manoeuvre's members as MSVC6
// vectors of mapped humans; the formation's scale without its w: a stack word in the original). The port's idle
// reports (an unnamed namespace's vector) are found once in the image's data by their contents.
#include "commander_fakes.inc"

// The human's order queue's (human_control.cpp's, not linked here: copied).
int C_Human::GetOrderCount() const
{
    if (m_controller->GetType() != HUMAN_CONTROLLER_SQUAD)
        return 0;
    return static_cast<C_SquadController *>(m_controller)->GetOrderCount();
}
void C_Human::CancelOrders() { static_cast<C_SquadController *>(m_controller)->CancelOrders(true); }
// The formation's type and scale (logged; the original's redirected alike).
void C_FormationShape::SetType(E_FormationType type) { Log(this, "FormationSetType", {double(type)}); }
void C_FormationShape::SetScale(const S_vector &scale) { Log(this, "FormationSetScale", {scale.x, scale.y, scale.z}); }

namespace {
void AC CM_FormationSetType(void *self, int type) { Log(self, "FormationSetType", {double(type)}); }
void AC CM_FormationSetScale(void *self, const S_vector *scale) { Log(self, "FormationSetScale", {scale->x, scale->y, scale->z}); }

typedef C_SquadCommander S_Cm;
DA_ROB(CmTimer3C, int S_Cm::*, &S_Cm::m_timer3C)
DA_ROB(CmTimer40, int S_Cm::*, &S_Cm::m_timer40)
DA_ROB(CmUnknown38, int S_Cm::*, &S_Cm::m_unknown38)
DA_ROB(CmFlag3C, bool S_Cm::*, &S_Cm::m_flag3C)
DA_ROB(CmFlag3D, bool S_Cm::*, &S_Cm::m_flag3D)
DA_ROB(CmFlag3E, bool S_Cm::*, &S_Cm::m_flag3E)
DA_ROB(CmFlag3F, bool S_Cm::*, &S_Cm::m_flag3F)
DA_ROB(CmFlag40, bool S_Cm::*, &S_Cm::m_flag40)
DA_ROB(CmFlag41, bool S_Cm::*, &S_Cm::m_flag41)
DA_ROB(CmFlag42, bool S_Cm::*, &S_Cm::m_flag42)
DA_ROB(CmWhispering, bool S_Cm::*, &S_Cm::m_whispering)
DA_ROB(CmUnknown44, int S_Cm::*, &S_Cm::m_unknown44)
DA_ROB(CmGroups, std::vector<C_Human *> (S_Cm::*)[5], &S_Cm::m_groups)
DA_ROB(CmSquadTarget, C_Actor *S_Cm::*, &S_Cm::m_squadTarget)
DA_ROB(CmSquadTargetTime, int S_Cm::*, &S_Cm::m_squadTargetTime)
DA_ROB(CmManoeuvre, int S_Cm::*, &S_Cm::m_manoeuvre)
DA_ROB(CmStage, int S_Cm::*, &S_Cm::m_stage)
DA_ROB(CmWaiting, int S_Cm::*, &S_Cm::m_waiting)
DA_ROB(CmDoorPos, S_vector4 S_Cm::*, &S_Cm::m_doorPos)
DA_ROB(CmDoorDir, S_vector4 S_Cm::*, &S_Cm::m_doorDir)
DA_ROB(CmLadderPos, S_vector4 S_Cm::*, &S_Cm::m_ladderPos)
DA_ROB(CmLadderDir, S_vector4 S_Cm::*, &S_Cm::m_ladderDir)
DA_ROB(CmManoeuvreActor, C_Actor *S_Cm::*, &S_Cm::m_manoeuvreActor)
DA_ROB(CmManoeuvreMembers, std::vector<C_Human *> S_Cm::*, &S_Cm::m_manoeuvreMembers)
DA_ROB(CmFormationScale, S_vector4 S_Cm::*, &S_Cm::m_formationScale)
DA_ROB(CmSpacing, int S_Cm::*, &S_Cm::m_spacing)
DA_ROB(CmDoorMotion, int C_Door::*, &C_Door::m_motion)
DA_ROB(CmDoorKey, int C_Door::*, &C_Door::m_key)
DA_ROB(CmDoorLocked, bool C_Door::*, &C_Door::m_locked)
DA_ROB(CmExplosiveItem, int C_ExplosivePlace::*, &C_ExplosivePlace::m_itemId)
DA_ROB(CmDocumentsItem, int C_DocumentPlace::*, &C_DocumentPlace::m_itemId)
DA_ROB(CmBoxLists, int C_SupplyBox::*, &C_SupplyBox::m_hasItemLists)
DA_ROB(CmFormationType, E_FormationType C_FormationShape::*, &C_FormationShape::m_type)

S_Cm *Cm() { return GetSquadCommander(); }
uint8_t *OCmObject() { return OriginalAt<uint8_t *>(0x0089ad40); }
uint8_t *OCmReports() { return OriginalAt<uint8_t *>(0x0089ad30); }
template <class R, class... A> R OCm(uint32_t at, A... a)
{
    using F = R(AC *)(void *, A...);
    return OriginalAt<F>(at)(static_cast<void *>(OCmObject()), a...);
}

// ---- the commander's fields
struct S_CmWord {
    const char *name;
    size_t orig;
    void *(*port)();
    size_t size;
    int skip = -1;                                      // a word copied but not compared (its offset)
};
#define CM_WORD(NAME, AT, TAG, SIZE) {NAME, AT, [] { return static_cast<void *>(&(Cm()->*DaGet(TAG()))); }, SIZE}
#define CM_SKIP(NAME, AT, TAG, SIZE, SKIP) {NAME, AT, [] { return static_cast<void *>(&(Cm()->*DaGet(TAG()))); }, SIZE, SKIP}
const S_CmWord CM_WORDS[] = {
    CM_WORD("timer3C", 0x30, CmTimer3C, 4),       CM_WORD("timer40", 0x34, CmTimer40, 4),
    CM_WORD("unknown38", 0x38, CmUnknown38, 4),   CM_WORD("flag3C", 0x3C, CmFlag3C, 1),
    CM_WORD("flag3D", 0x3D, CmFlag3D, 1),         CM_WORD("flag3E", 0x3E, CmFlag3E, 1),
    CM_WORD("flag3F", 0x3F, CmFlag3F, 1),         CM_WORD("flag40", 0x40, CmFlag40, 1),
    CM_WORD("flag41", 0x41, CmFlag41, 1),         CM_WORD("flag42", 0x42, CmFlag42, 1),
    CM_WORD("whispering", 0x43, CmWhispering, 1), CM_WORD("unknown44", 0x44, CmUnknown44, 4),
    CM_WORD("squadTargetTime", 0x9C, CmSquadTargetTime, 4), CM_WORD("manoeuvre", 0xA0, CmManoeuvre, 4),
    CM_WORD("stage", 0xA4, CmStage, 4),           CM_WORD("waiting", 0xA8, CmWaiting, 4),
    CM_SKIP("doorPos", 0xB0, CmDoorPos, 16, 12),      CM_SKIP("doorDir", 0xC0, CmDoorDir, 16, 12),
    CM_SKIP("ladderPos", 0xD0, CmLadderPos, 16, 12),  CM_SKIP("ladderDir", 0xE0, CmLadderDir, 16, 12),
    // (The places' and the scale's w: the original leaves stack words there.)
    CM_SKIP("formationScale", 0x110, CmFormationScale, 16, 12), CM_WORD("spacing", 0x120, CmSpacing, 4),
};
std::vector<uint32_t> CmMapped(const std::vector<C_Human *> &humans)
{
    std::vector<uint32_t> out;
    for (C_Human *h : humans)
        out.push_back(SqMapped(h));
    return out;
}
// The port's idle reports (found by FindPortReports) and their layout.
struct S_CmReport {
    C_Human *member;
    int time;
};
std::vector<S_CmReport> *g_cmReports;
std::vector<uint32_t> CmReportWords()
{
    std::vector<uint32_t> out;
    for (const S_CmReport &r : *g_cmReports) {
        out.push_back(SqMapped(r.member));
        out.push_back(static_cast<uint32_t>(r.time));
    }
    return out;
}

// ---- the members: each human's squad controller pair (the human's part 44's; two more made alike)
S_Sq *g_cmPsq[3];
uint8_t *g_cmOsq[3];
void *g_cmPsqVt[2][0x80];
void *g_cmOsqVt[2][0x60];
C_Human *CmHuman(int k) { return k == 0 ? g_h.human : k == 1 ? PortOther() : PortThird(); }
uint8_t *CmOrigHuman(int k) { return k == 0 ? o_human : k == 1 ? g_origOther : g_origThird; }
// `f` on each member's pair as part 44's (g_psq / g_osq swapped in); its first complaint.
template <class F> std::string CmEachMember(F f)
{
    S_Sq *const p = g_psq;
    uint8_t *const o = g_osq;
    std::string why;
    for (int k = 0; k < 3 && why.empty(); ++k) {
        g_psq = g_cmPsq[k];
        g_osq = g_cmOsq[k];
        why = f(k);
        if (!why.empty())
            why = "member " + std::to_string(k) + ": " + why;
    }
    g_psq = p;
    g_osq = o;
    return why;
}
// The other members' orders (made once, each side's; the queues take them in turn: the commander does not change the
// queues).
S_HumanOrder *g_cmOrders[4];
void AC CM_CancelOrders(void *self, int turn) { Log(self, "SqCancelOrders", {double(turn & 0xFF)}); }

// The selected soldier's controller as a player's (g_cm.selectedPlayer): ExecuteActionCommand writes the player
// controller's squad combat order (the port's member, the original's +0x548), past a squad controller's size. A block
// of a player controller's size per side, the selected member's squad pair copied into it.
alignas(16) uint8_t g_cmPlayerPort[sizeof(C_PlayerController) + 0x40], g_cmPlayerOrig[0x600];
DA_ROB(CmPlCombatOrder, int C_PlayerController::*, &C_PlayerController::m_squadCombatOrder)
int &CmPortCombatOrder() { return reinterpret_cast<C_PlayerController *>(g_cmPlayerPort)->*DaGet(CmPlCombatOrder()); }
void CmMakeSelectedPlayer()
{
    C_Human *selected = g_sq.selected;
    int k = 0;
    while (CmHuman(k) != selected)
        ++k;
    std::memset(g_cmPlayerPort, 0, sizeof g_cmPlayerPort);
    std::memset(g_cmPlayerOrig, 0, sizeof g_cmPlayerOrig);
    std::memcpy(g_cmPlayerPort, static_cast<void *>(g_cmPsq[k]), sizeof(C_SquadController));
    std::memcpy(g_cmPlayerOrig, g_cmOsq[k], 0x260);
    selected->m_controller = reinterpret_cast<C_HumanController *>(g_cmPlayerPort);
    void *o = g_cmPlayerOrig;
    std::memcpy(CmOrigHuman(k) + 0xB0, &o, 4);
    const int order = I(-1, 0x11);
    CmPortCombatOrder() = order;
    std::memcpy(g_cmPlayerOrig + 0x548, &order, 4);
    g_cm.selectedPlayer = true;
}

// ---- the commander's fields to the original's and compared, the members' with them
void CommanderToOrig()
{
    uint8_t *o = OCmObject();
    const uint32_t vt = 0x0080fb28;
    std::memcpy(o, &vt, 4);
    for (const S_CmWord &w : CM_WORDS)
        std::memcpy(o + w.orig, w.port(), w.size);
    const uint32_t target = SqMapped(Cm()->*DaGet(CmSquadTarget())), actor = SqMapped(Cm()->*DaGet(CmManoeuvreActor()));
    std::memcpy(o + 0x98, &target, 4);
    std::memcpy(o + 0xF0, &actor, 4);
    for (int k = 0; k < S_Cm::STORED_GROUPS; ++k)
        MsvcVectorPut(o + 0x48 + 0x10 * k, CmMapped((Cm()->*DaGet(CmGroups()))[k]), size_t(I(0, 2)));
    MsvcVectorPut(o + 0xF4, CmMapped(Cm()->*DaGet(CmManoeuvreMembers())), size_t(I(0, 2)));
    MsvcVectorPut(OCmReports(), CmReportWords(), size_t(2 * I(0, 1)));
    CmEachMember([](int) {
        SquadToOrig();
        return std::string();
    });
}
std::string CommanderDiffers()
{
    const uint8_t *o = OCmObject();
    for (const S_CmWord &w : CM_WORDS) {
        size_t at = 0;
        auto same = [&](size_t k) {
            return (w.skip >= 0 && k / 4 == size_t(w.skip) / 4) || static_cast<const uint8_t *>(w.port())[k] == o[w.orig + k];
        };
        while (at < w.size && same(at))
            ++at;
        if (at == w.size)
            continue;
        uint32_t a = 0, b = 0;
        const size_t word = at & ~size_t(3);
        std::memcpy(&a, static_cast<const uint8_t *>(w.port()) + word, std::min<size_t>(w.size - word, 4));
        std::memcpy(&b, o + w.orig + word, std::min<size_t>(w.size - word, 4));
        char text[128];
        std::snprintf(text, sizeof text, "commander %s+%u: port %08x original %08x", w.name, unsigned(word), a, b);
        return text;
    }
    uint32_t target, actor;
    std::memcpy(&target, o + 0x98, 4);
    std::memcpy(&actor, o + 0xF0, 4);
    if (target != SqMapped(Cm()->*DaGet(CmSquadTarget())))
        return "commander squadTarget";
    if (actor != SqMapped(Cm()->*DaGet(CmManoeuvreActor())))
        return "commander manoeuvreActor";
    for (int k = 0; k < S_Cm::STORED_GROUPS; ++k)
        if (MsvcVectorGet(o + 0x48 + 0x10 * k) != CmMapped((Cm()->*DaGet(CmGroups()))[k]))
            return "commander group " + std::to_string(k);
    if (MsvcVectorGet(o + 0xF4) != CmMapped(Cm()->*DaGet(CmManoeuvreMembers())))
        return "commander manoeuvreMembers";
    if (MsvcVectorGet(OCmReports()) != CmReportWords())
        return "idle reports";
    if (g_cm.selectedPlayer) {
        int order;
        std::memcpy(&order, g_cmPlayerOrig + 0x548, 4);
        if (order != CmPortCombatOrder())
            return "the player's squad combat order: port " + std::to_string(CmPortCombatOrder()) + " original " + std::to_string(order);
    }
    return CmEachMember([](int) { return SquadDiffers(); });
}
// A call on the commander (the members' facing w as part 44's SqCall).
template <class P, class O> void CmCall(const char *what, P port, O original)
{
    DaCall(what, CommanderToOrig, CommanderDiffers,
           [] {
               uint8_t *facing;
               std::memcpy(&facing, g_osq + 0x124, 4);
               std::memcpy(facing + 0x2C, reinterpret_cast<const uint8_t *>(g_psq->m_facing) + 0x2C, 4);
           },
           [&] {
               g_sqMarkerNext[0] = 0;
               SqWornFor(false);
               return port();
           },
           [&](void *o) {
               g_sqMarkerNext[1] = 0;
               SqWornFor(true);
               const int r = original(o);
               SqWornFor(false);
               return r;
           });
}

// ---- the set-up
// The third's own uniform (its own container table: the other containers the shared ones, the uniform a container of
// its own with an item of any side or none): the uniform swap's choice.
S_FakeContainer g_cmThirdUniform;
C_ItemContainer *g_cmThirdTable[16];
void CmRandomThirdUniform()
{
    for (int k = 0; k < 16; ++k)
        g_cmThirdTable[k] = g_wornTable[k];
    g_cmThirdUniform.vt = g_wornVt;
    g_cmThirdUniform.item = P(0.2f) ? nullptr : g_wornItems[I(0, 15)];
    g_cmThirdTable[ITEM_CONTAINER_UNIFORM] = reinterpret_cast<C_ItemContainer *>(&g_cmThirdUniform);
    const bool own = P(0.6f);
    PortThird()->m_containers = own ? g_cmThirdTable : g_wornTable;
    void *table = own ? static_cast<void *>(g_cmThirdTable) : static_cast<void *>(g_wornTable);
    std::memcpy(g_origThird + 0x74, &table, 4);
}
// Another sector for some places (TakeCover keeps to the selected soldier's): by the place's x, alike on both sides.
bool g_cmSectors = false;
I3D_sector *CmSectorAt(const S_vector &pos)
{
    if (g_cmSectors && (int(std::floor(pos.x * 0.5f)) & 3) == 0)
        return reinterpret_cast<I3D_sector *>(g_sqSectorOther);
    return reinterpret_cast<I3D_sector *>(g_sector);
}
// The port's idle reports: the vector in this image's data holding one report, then two.
void CmFindPortReports()
{
    S_Cm *c = Cm();
    c->Reset();
    // (All of this image's writable sections: the coverage build moves its objects apart.)
    auto *image = reinterpret_cast<uint8_t *>(GetModuleHandleA(nullptr));
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(image + reinterpret_cast<const IMAGE_DOS_HEADER *>(image)->e_lfanew);
    auto find = [&](size_t count, C_Human *last) -> std::vector<S_CmReport> * {
        const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
        for (int k = 0; k < nt->FileHeader.NumberOfSections; ++k, ++section) {
            if (!(section->Characteristics & IMAGE_SCN_MEM_WRITE))
                continue;
            uint8_t *from = image + section->VirtualAddress;
            for (uint8_t *p = from; p + 12 <= from + section->Misc.VirtualSize; p += 4) {
                uint32_t w[3];
                std::memcpy(w, p, 12);
                if (w[0] < 0x10000 || (w[0] & 3) || w[1] != w[0] + 8 * count || w[2] < w[1] || w[2] - w[0] > 0x1000)
                    continue;
                const auto *first = reinterpret_cast<const S_CmReport *>(static_cast<uintptr_t>(w[0]));
                if (!IsBadReadPtr(first, 8 * count) && first[count - 1].member == last)
                    return reinterpret_cast<std::vector<S_CmReport> *>(p);
            }
        }
        return nullptr;
    };
    c->OnMemberIdle(PortOther());
    std::vector<S_CmReport> *one = find(1, PortOther());
    c->OnMemberIdle(PortThird());
    std::vector<S_CmReport> *two = find(2, PortThird());
    if (!one || one != two) {
        std::printf("commander: the port's idle reports not found\n");
        std::exit(3);
    }
    g_cmReports = one;
    g_cmReports->clear();
    g_calls.clear();
}
void SetUpCommander()
{
    // The other two members' pairs (DaOrigController makes them the AI pair: the human's is put back).
    S_TestAi *const pa = g_pa;
    uint8_t *const oa = g_oa;
    g_cmPsq[0] = g_psq;
    g_cmOsq[0] = g_osq;
    for (int k = 1; k < 3; ++k) {
        S_Sq *p = S_Sq::Create();
        std::memcpy(g_cmPsqVt[k - 1], *reinterpret_cast<void ***>(p), sizeof g_cmPsqVt[0]);
        g_cmPsqVt[k - 1][VSlot(&C_HumanController::ExecuteCommand)] = reinterpret_cast<void *>(OA_ExecuteCommand);
        *reinterpret_cast<void ***>(p) = g_cmPsqVt[k - 1];
        uint8_t *o = DaOrigController(p, 0x260, HUMAN_CONTROLLER_SQUAD, 0x008109f8, g_cmOsqVt[k - 1], k == 1 ? "squad1" : "squad2");
        std::memcpy(g_cmOsqVt[k - 1] + 0x48, OriginalAt<void *const *>(0x008109f8) + 0x48, (83 - 0x48) * sizeof(void *));
        g_cmPsq[k] = p;
        g_cmOsq[k] = o;
    }
    g_pa = pa;
    g_oa = oa;
    for (S_HumanOrder *&order : g_cmOrders)
        order = SqAllocOrder(I(1, 4), I(0, 0x40), SqNear(25), AiDir(), 0, uint32_t(I(0, 4)), 0, nullptr, I(0, 5));
    void **const portVts[3] = {g_psqVt, g_cmPsqVt[0], g_cmPsqVt[1]};
    void **const origVts[3] = {g_osqVt, g_cmOsqVt[0], g_cmOsqVt[1]};
    for (int k = 0; k < 3; ++k) {
        portVts[k][VSlot(&C_SquadController::CancelOrders)] = reinterpret_cast<void *>(CM_CancelOrders);
        origVts[k][0x128 / 4] = reinterpret_cast<void *>(CM_CancelOrders);
    }
    for (const auto &r : CM_REDIRECTS)
        RedirectOriginal(r.first, r.second);
    RedirectOriginal(0x00597420, FAKE(CM_FormationSetType));
    RedirectOriginal(0x005977f0, FAKE(CM_FormationSetScale));
    // The original's object: its vtable (its static initialisers never ran here), its spacing's scale.
    std::memset(OCmObject(), 0, 0x124);
    std::memset(OCmReports(), 0, 0x10);
    // (The tables' static initialisers, never run in the original's copy: the cover slots, the unused set, MoveNear's
    // places, the checkpoints' eight directions.)
    for (uint32_t init : {0x004595f0u, 0x004596a0u, 0x0045b7d0u, 0x005244c0u})
        OriginalAt<void (*)()>(init)();
    g_names[Cm()] = g_names[OCmObject()] = "commander";
    g_extraPointers.push_back({Cm(), OCmObject()});
    CmFindPortReports();
    g_sectorAt = CmSectorAt;
    g_names[&g_cmThirdUniform] = "thirdUniform";
}

// ---- the state
int CmTimer(int edge) { return P(0.2f) ? edge + I(-1, 1) : P(0.3f) ? 0 : I(-100, 2 * edge); }
C_Human *CmSomeHuman() { return CmHuman(I(0, 2)); }
// Some of the three, each once, in a random order (the groups' members).
std::vector<C_Human *> CmSubset(int most)
{
    std::vector<C_Human *> all = {CmHuman(0), CmHuman(1), CmHuman(2)};
    for (int k = 2; k > 0; --k)
        std::swap(all[k], all[I(0, k)]);
    all.resize(size_t(I(0, most)));
    return all;
}
// The members within 25 m of the selected soldier (TakeCover: the original orders the word before its list when none is
// within 100 m of a cover).
void CmGather()
{
    const C_Human *at = g_sq.selected ? g_sq.selected : g_h.human;
    const S_vector centre = at->GetFrame()->GetWorldPos();
    for (int k = 0; k < 3; ++k) {
        I3D_frame *frame = CmHuman(k)->GetFrame();
        if (CmHuman(k) == at)
            continue;
        frame->SetPosInline(centre + S_vector(U(-25, 25), U(-1, 1), U(-25, 25)));
        UpdateTree(frame);
    }
}
std::vector<C_Human *> CmSomeHumans(int most)
{
    std::vector<C_Human *> out;
    for (int n = I(0, most); n > 0; --n)
        out.push_back(CmSomeHuman());
    return out;
}
S_vector4 CmDir() { return ToVector4(S_vector(U(-1, 1), P(0.3f) ? 0.0f : U(-0.3f, 0.3f), U(-1, 1)).Normalized(), 0.0f); }
// The selected soldier's position, spread.
S_vector4 CmNear(float spread)
{
    const C_Human *at = g_sq.selected ? g_sq.selected : g_h.human;
    return ToVector4(at->GetFrame()->GetWorldPos() + S_vector(U(-spread, spread), U(-1, 1), U(-spread, spread)), 1.0f);
}
// The door's state and key, the explosive's and documents' items, the seat car's armour flag (both sides').
void CmRandomActors()
{
    auto *door = static_cast<C_Door *>(static_cast<C_Actor *>(PortActor(0)));
    const int motion = I(0, 3), key = P(0.3f) ? -1 : I(0, 120);
    const bool locked = P(0.3f);
    door->*DaGet(CmDoorMotion()) = motion;
    door->*DaGet(CmDoorKey()) = key;
    door->*DaGet(CmDoorLocked()) = locked;
    uint8_t *od = static_cast<uint8_t *>(OrigActor(0));
    std::memcpy(od + 0x224, &motion, 4);
    std::memcpy(od + 0x230, &key, 4);
    od[0x234] = locked;
    const int explosive = P(0.5f) ? 0x44 : I(0, 120), documents = P(0.5f) ? 0x44 : I(0, 120);
    static_cast<C_ExplosivePlace *>(static_cast<C_Actor *>(PortActor(2)))->*DaGet(CmExplosiveItem()) = explosive;
    std::memcpy(static_cast<uint8_t *>(OrigActor(2)) + 0xA0, &explosive, 4);
    static_cast<C_DocumentPlace *>(static_cast<C_Actor *>(PortActor(10)))->*DaGet(CmDocumentsItem()) = documents;
    std::memcpy(static_cast<uint8_t *>(OrigActor(10)) + 0x98, &documents, 4);
    const int armour = P(0.5f) ? 0 : I(1, 2);
    std::memcpy(g_asCarPort + g_asCarFlagAt, &armour, 4);
    std::memcpy(g_asCarOrig + 0x5E0, &armour, 4);
}
// The uniforms' components (the axis and the shared definitions': their disguise side +0x0C; the allied one's
// component is the weapons', part 36's).
alignas(16) uint8_t g_cmUniformComp[3][0x40];
void CmRandomUniforms()
{
    for (int t = 1; t < 3; ++t) {
        void *component = g_cmUniformComp[t];
        std::memcpy(g_wornDefs[t] + 0x48, &component, 4);
        const int side = I(0, 2);
        std::memcpy(g_cmUniformComp[t] + 0x0C, &side, 4);
    }
}
// An actor's type on both sides (part 23's actors stand in for a ladder or a supply box).
void CmSetType(void *port, int type)
{
    static_cast<C_Actor *>(port)->m_type = static_cast<E_ActorType>(type);
    std::memcpy(static_cast<uint8_t *>(ToOrig(port)) + 0x18, &type, 4);
}
// The radio stands in for a ladder, the switch for a supply box (part 23's types put back by CmRandom).
C_Actor *CmLadder()
{
    CmSetType(PortActor(7), ACTOR_LADDER);
    return static_cast<C_Actor *>(PortActor(7));
}
C_Actor *CmSupplyBox()
{
    void *p = PortActor(8);
    CmSetType(p, ACTOR_SUPPLY_BOX);
    const int lists = P(0.5f);
    static_cast<C_SupplyBox *>(static_cast<C_Actor *>(p))->*DaGet(CmBoxLists()) = lists;
    std::memcpy(static_cast<uint8_t *>(ToOrig(p)) + 0x11C, &lists, 4);
    return static_cast<C_Actor *>(p);
}
// The walking network's points around the selected soldier: their places, radii, covers and values.
void CmRandomPoints()
{
    for (int k = 0; k < 8; ++k) {
        auto *point = reinterpret_cast<S_Checkpoint *>(g_aiPoints[k]);
        point->pos = XYZ(CmNear(P(0.3f) ? 4.0f : 20.0f)) + S_vector(0, P(0.2f) ? U(-4, 4) : 0.0f, 0);
        point->radius = uint8_t(P(0.3f) ? 0 : I(0, 64));
        point->cover1 = uint8_t(P(0.4f) ? 0 : I(0, 255));
        point->cover2 = uint8_t(P(0.4f) ? 0 : I(0, 255));
        point->cover4 = uint8_t(P(0.4f) ? 0 : I(0, 255));
        for (uint8_t &v : point->values)
            v = uint8_t(P(0.4f) ? 0xFF : P(0.3f) ? I(0, 60) : I(0, 254));
    }
}
// The squad's state (part 44's), the other members' pairs, the humans' leaders and formations, the commander's
// fields and idle reports, the answers.
void CmRandom()
{
    RandomSquad(false);
    // (Part 23's types back: the radio, the switch.)
    CmSetType(PortActor(7), ACTOR_RADIO);
    CmSetType(PortActor(8), ACTOR_SWITCH);
    // The other members: part 44's pair's fields copied, then their own action, quiet, formation time, combat mode,
    // orders.
    for (int k = 1; k < 3; ++k) {
        for (const S_SqWord &w : SQ_WORDS) {
            g_psq = g_cmPsq[0];
            void *from = w.port();
            g_psq = g_cmPsq[k];
            std::memcpy(w.port(), from, w.size);
        }
        g_psq = g_cmPsq[0];
        C_Human *const picked = g_psq->*DaGet(SqPickedTarget());
        C_Actor *const air = g_psq->*DaGet(SqAirTarget());
        I3D_frame *const airFrame = g_psq->*DaGet(SqAirTargetFrame());
        g_psq = g_cmPsq[k];
        g_psq->*DaGet(SqPickedTarget()) = picked;
        g_psq->*DaGet(SqAirTarget()) = air;
        g_psq->*DaGet(SqAirTargetFrame()) = airFrame;
        SqField(DaGet(SqOrders())).clear();
        g_psq = g_cmPsq[0];
    }
    for (int k = 0; k < 3; ++k) {
        g_psq = g_cmPsq[k];
        S_SquadAction &a = SqField(DaGet(SqAction()));
        a.finished = P(0.5f);
        a.completed = P(0.4f);
        a.radius = U(0, 3);
        a.arrived = P(0.5f);
        a.notifyModule = P(0.3f);
        SqField(DaGet(SqQuiet())) = P(0.5f);
        SqField(DaGet(SqFormationTime())) = P(0.5f) ? 0 : I(1, 3000);
        SqField(DaGet(SqCombatMode())) = P(0.5f) ? 0 : I(1, 2);
        if (k != 0) {
            // (Premade orders: the commander does not change the queues.)
            for (int n = P(0.5f) ? 0 : I(1, 2); n > 0; --n)
                SqField(DaGet(SqOrders())).push_back(g_cmOrders[2 * (k - 1) + n - 1]);
        } else if (P(0.4f)) {
            SqReleaseOrders();
        }
    }
    g_psq = g_cmPsq[0];
    // The humans: their controllers (each its pair), leaders, formations (the shared block or none), carried bodies.
    for (int k = 0; k < 3; ++k) {
        C_Human *h = CmHuman(k);
        uint8_t *o = CmOrigHuman(k);
        h->m_controller = g_cmPsq[k];
        std::memcpy(o + 0xB0, &g_cmOsq[k], 4);
        // (Each pair's human its member: the original's ApplyOrder asks the controller's human for its leader.)
        g_cmPsq[k]->m_human = h;
        std::memcpy(g_cmOsq[k] + 0x44, &o, 4);
        if (k != 0) {
            h->m_leader = P(0.4f) ? nullptr : CmSomeHuman();
            const uint32_t leader = SqMapped(h->m_leader);
            std::memcpy(o + 0x2CC, &leader, 4);
        }
        h->m_formation = P(0.3f) ? nullptr : reinterpret_cast<C_FormationShape *>(g_sqFormation);
        if (k == 0) {
            const int type = I(0, 3);
            reinterpret_cast<C_FormationShape *>(g_sqFormation)->*DaGet(CmFormationType()) = static_cast<E_FormationType>(type);
            std::memcpy(g_sqOrigFormation + 0x78, &type, 4);
        }
        void *of = h->m_formation ? static_cast<void *>(g_sqOrigFormation) : nullptr;
        std::memcpy(o + 0x2C8, &of, 4);
        h->m_carried = P(0.7f) ? nullptr : static_cast<C_Actor *>(CmSomeHuman());
        const uint32_t carried = SqMapped(h->m_carried);
        std::memcpy(o + 0x174, &carried, 4);
    }
    // The team: the members (mostly all three), the selected soldier.
    g_sq.memberCount = P(0.8f) ? 3 : I(0, 2);
    g_sq.selected = P(0.15f) ? nullptr : CmSomeHuman();
    g_cm.seatWeapon = P(0.5f);
    g_cm.projectile = P(0.5f);
    g_cm.launcher = P(0.5f);
    g_cm.selectedPlayer = false;
    CmRandomActors();
    CmRandomUniforms();
    CmRandomThirdUniform();
    g_daSight = P(0.7f);
    g_cmSectors = P(0.3f);
    CmRandomPoints();
    g_cs.nearest = I(0, 7);
    // (The ray tests' hit normal: TakeCover takes no cover on ground flatter than 0.6.)
    g_sq.rayNormal = S_vector(U(-1, 1), P(0.1f) ? 0.6f : U(-0.5f, 1), U(-1, 1));
    g_ai.ladderPoints = {uint16_t(I(0, 7)), uint16_t(I(0, 7))};
    // The commander.
    S_Cm *c = Cm();
    c->*DaGet(CmTimer3C()) = CmTimer(500);
    c->*DaGet(CmTimer40()) = CmTimer(500);
    c->*DaGet(CmUnknown38()) = I(-5, 5);
    for (auto tag : {DaGet(CmFlag3C()), DaGet(CmFlag3D()), DaGet(CmFlag3E()), DaGet(CmFlag3F()), DaGet(CmFlag40()),
                     DaGet(CmFlag41()), DaGet(CmFlag42()), DaGet(CmWhispering())})
        c->*tag = P(0.5f);
    c->*DaGet(CmUnknown44()) = P(0.5f) ? 5000 : I(0, 9000);
    for (std::vector<C_Human *> &group : c->*DaGet(CmGroups()))
        group = CmSubset(3);
    c->*DaGet(CmSquadTarget()) = P(0.5f) ? nullptr : static_cast<C_Actor *>(CmSomeHuman());
    c->*DaGet(CmSquadTargetTime()) = CmTimer(4000);
    static const int manoeuvres[] = {S_Cm::MANOEUVRE_NONE, S_Cm::MANOEUVRE_LADDER, S_Cm::MANOEUVRE_GRENADE_ROOM,
                                     S_Cm::MANOEUVRE_THROUGH_DOOR, 7};
    c->*DaGet(CmManoeuvre()) = manoeuvres[I(0, 4)];
    c->*DaGet(CmStage()) = P(0.7f) ? 0 : 1;
    c->*DaGet(CmWaiting()) = P(0.5f) ? 1 : I(0, 3);
    c->*DaGet(CmDoorPos()) = CmNear(10);
    c->*DaGet(CmDoorDir()) = CmDir();
    c->*DaGet(CmLadderPos()) = CmNear(10);
    c->*DaGet(CmLadderDir()) = CmDir();
    c->*DaGet(CmManoeuvreActor()) = static_cast<C_Actor *>(PortActor(P(0.8f) ? 0 : 7));
    c->*DaGet(CmManoeuvreMembers()) = CmSubset(3);
    {
        S_vector4 &scale = c->*DaGet(CmFormationScale());
        scale = {U(0, 7), U(0, 7), U(0, 7), 0.0f};
    }
    c->*DaGet(CmSpacing()) = P(0.3f) ? (P(0.5f) ? 0 : S_Cm::MAX_SPACING) : I(0, S_Cm::MAX_SPACING);
    // The idle reports: due, nearly due, later.
    g_cmReports->clear();
    const int now = int(GetGame()->GetTime());
    for (int n = P(0.4f) ? 0 : I(1, 3); n > 0; --n)
        g_cmReports->push_back({CmSomeHuman(), now + (P(0.2f) ? I(-1, 1) : I(-500, 1500))});
}

// ---- part 45: the commander's functions
const int CM_ON_ACTOR[] = {HUMAN_COMMAND_LOCK_DOOR, HUMAN_COMMAND_UNLOCK_DOOR, HUMAN_COMMAND_DEFUSE_MINE,
                           HUMAN_COMMAND_ENTER_VEHICLE, HUMAN_COMMAND_PLANT_EXPLOSIVE, HUMAN_COMMAND_LEAVE_VEHICLE,
                           HUMAN_COMMAND_PICK_UP_BODY, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, HUMAN_COMMAND_PICK_LOCK,
                           HUMAN_COMMAND_HEAL, HUMAN_COMMAND_CUT_WIRE, S_Cm::MANOEUVRE_GRENADE_ROOM, HUMAN_COMMAND_TIE_UP,
                           S_Cm::MANOEUVRE_THROUGH_DOOR, SQUAD_ACTION_ATTACK_VEHICLE, SQUAD_ACTION_SEARCH_BODY,
                           SQUAD_ACTION_SWAP_UNIFORM, SQUAD_ACTION_ATTACK, SQUAD_ACTION_ATTACK_SILENTLY,
                           HUMAN_COMMAND_OPEN_DOOR, HUMAN_COMMAND_CLOSE_DOOR, SQUAD_ACTION_FOLLOW, SQUAD_ACTION_21,
                           HUMAN_COMMAND_DELIVER_DOCUMENTS, SQUAD_ACTION_THROW, HUMAN_COMMAND_DISARM_EXPLOSIVE,
                           HUMAN_COMMAND_LAY_MINE, 0x30};
// An actor the command is for (the kinds the game gives it).
C_Actor *CmActorFor(int command)
{
    switch (command) {
    case HUMAN_COMMAND_LOCK_DOOR:
    case HUMAN_COMMAND_UNLOCK_DOOR:
    case HUMAN_COMMAND_PICK_LOCK:
    case HUMAN_COMMAND_OPEN_DOOR:
    case HUMAN_COMMAND_CLOSE_DOOR:
    case S_Cm::MANOEUVRE_GRENADE_ROOM:
    case S_Cm::MANOEUVRE_THROUGH_DOOR:
        return static_cast<C_Actor *>(PortActor(0));
    case HUMAN_COMMAND_DEFUSE_MINE:
        return static_cast<C_Actor *>(PortActor(1));
    case HUMAN_COMMAND_PLANT_EXPLOSIVE:
    case HUMAN_COMMAND_DISARM_EXPLOSIVE:
        return static_cast<C_Actor *>(PortActor(2));
    case HUMAN_COMMAND_ENTER_VEHICLE: {
        const int kind = I(0, 5);
        return kind == 0 ? CmLadder() : kind == 1 ? CmSupplyBox() : static_cast<C_Actor *>(PortActor(kind == 2 ? 3 : kind == 3 ? 4 : kind == 4 ? 5 : 7));
    }
    case HUMAN_COMMAND_LEAVE_VEHICLE:
        return P(0.3f) ? static_cast<C_Actor *>(CmSomeHuman()) : P(0.5f) ? reinterpret_cast<C_Actor *>(g_asCarPort) : static_cast<C_Actor *>(PortActor(5));
    case HUMAN_COMMAND_PICK_UP_BODY: {
        const int kind = I(0, 3);
        return kind == 0 ? static_cast<C_Actor *>(CmSomeHuman()) : kind == 1 ? SqItemActor(I(0, 1)) : kind == 2 ? CmSupplyBox() : static_cast<C_Actor *>(PortActor(1));
    }
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D:
        return P(0.5f) ? reinterpret_cast<C_Actor *>(g_asCarPort) : static_cast<C_Actor *>(PortActor(3));
    case HUMAN_COMMAND_CUT_WIRE:
        return static_cast<C_Actor *>(PortActor(11));
    case SQUAD_ACTION_ATTACK_VEHICLE:
        return P(0.7f) ? reinterpret_cast<C_Actor *>(g_asCarPort) : static_cast<C_Actor *>(PortActor(5));
    case HUMAN_COMMAND_DELIVER_DOCUMENTS:
        return static_cast<C_Actor *>(PortActor(10));
    case SQUAD_ACTION_ATTACK:
    case SQUAD_ACTION_ATTACK_SILENTLY:
    case SQUAD_ACTION_THROW:
        return P(0.7f) ? static_cast<C_Actor *>(CmSomeHuman()) : static_cast<C_Actor *>(PortActor(I(0, 5)));
    default:
        return static_cast<C_Actor *>(CmSomeHuman());
    }
}
// The original's S_ActionCommand (0x34 bytes; no vtable needed).
alignas(16) uint8_t g_cmOrigCommand[0x40];
void *CmOrigCommand(const S_ActionCommand &c)
{
    std::memset(g_cmOrigCommand, 0, sizeof g_cmOrigCommand);
    std::memcpy(g_cmOrigCommand + 0x04, &c.category, 4);
    std::memcpy(g_cmOrigCommand + 0x10, &c.dir, 16);
    std::memcpy(g_cmOrigCommand + 0x20, &c.action, 4);
    std::memcpy(g_cmOrigCommand + 0x24, &c.command, 4);
    const uint32_t actor = SqMapped(c.actor), member = SqMapped(c.member);
    std::memcpy(g_cmOrigCommand + 0x28, &actor, 4);
    std::memcpy(g_cmOrigCommand + 0x2C, &c.value, 4);
    std::memcpy(g_cmOrigCommand + 0x30, &member, 4);
    return g_cmOrigCommand;
}
// A vector the original fills (an MSVC6 one), its prior contents random; its contents read back as one number.
alignas(16) uint8_t g_cmOrigOut[16];
int CmHash(const std::vector<uint32_t> &words)
{
    uint32_t h = 17;
    for (uint32_t w : words)
        h = h * 31 + w;
    return static_cast<int>(h ^ uint32_t(words.size()) << 24);
}
int CmDt() { return P(0.1f) ? 0 : P(0.1f) ? I(1000, 6000) : I(1, 600); }

void TestCommanderCalls()
{
    // (HUMAN_COMMANDER_CASE: one case alone, 2,000 rounds.)
    const char *only = std::getenv("HUMAN_COMMANDER_CASE");
    const int rounds = only ? 2000 : 30000 / QUICK;
    S_Cm *c = Cm();
    for (int i = 0; i < rounds; ++i) {
        CmRandom();
        const int pick = I(0, 22);
        const int which = only ? std::atoi(only) : pick;
        switch (which) {
        case 0: {
            // (Now and then the time a timer or an idle report reaches exactly: the edges "< 1" and "> now".)
            int dt = CmDt();
            if (P(0.3f)) {
                const int timers[3] = {c->*DaGet(CmTimer3C()), c->*DaGet(CmTimer40()), c->*DaGet(CmSquadTargetTime())};
                dt = std::max(0, timers[I(0, 2)] - I(0, 1));
            }
            if (P(0.3f) && !g_cmReports->empty())
                (*g_cmReports)[size_t(I(0, int(g_cmReports->size()) - 1))].time = int(GetGame()->GetTime()) + dt + I(-1, 0);
            CmCall("Tick", [&] { c->Tick(dt); return 0; }, [&](void *) { OCm<void>(0x004564a0, dt); return 0; });
            break;
        }
        case 1: {
            C_Human *member = P(0.1f) ? nullptr : CmSomeHuman();
            CmCall("OnMemberIdle", [&] { c->OnMemberIdle(member); return 0; },
                   [&](void *) { OCm<void>(0x00455af0, SqOrigOf(member)); return 0; });
            break;
        }
        case 2: {
            const int group = I(0, 5);
            const std::vector<C_Human *> prior = CmSomeHumans(2);
            CmCall("CollectGroup",
                   [&] {
                       std::vector<C_Human *> out = prior;
                       c->CollectGroup(out, group);
                       return CmHash(CmMapped(out));
                   },
                   [&](void *) {
                       MsvcVectorPut(g_cmOrigOut, CmMapped(prior), size_t(I(0, 2)));
                       OCm<void>(0x004568d0, static_cast<void *>(g_cmOrigOut), group);
                       return CmHash(MsvcVectorGet(g_cmOrigOut));
                   });
            break;
        }
        case 3: {
            const int group = I(0, 5);
            CmCall("HasGroupMembers", [&] { return int(c->HasGroupMembers(group)); },
                   [&](void *) { return OCm<int>(0x00456b80, group) & 0xFF; });
            break;
        }
        case 4: {
            const int group = I(0, 4);
            C_Human *human = P(0.1f) ? nullptr : CmSomeHuman();
            CmCall("IsInGroup", [&] { return int(c->IsInGroup(human, group)); },
                   [&](void *) { return OCm<int>(0x00456890, SqOrigOf(human), group) & 0xFF; });
            break;
        }
        case 5: {
            const int command = P(0.3f) ? HUMAN_COMMAND_LEAVE_VEHICLE : P(0.3f) ? HUMAN_COMMAND_ENTER_VEHICLE : CM_ON_ACTOR[I(0, 32)];
            // (The squad near the selected soldier now and then: its 40 m.)
            if (g_sq.selected && P(0.5f))
                CmGather();
            C_Actor *actor = P(0.1f) ? nullptr : CmActorFor(command);
            // (The HUD offers leaving vehicles only: a human has no seats.)
            if (actor && command == HUMAN_COMMAND_LEAVE_VEHICLE && actor->GetType() == ACTOR_HUMAN)
                actor = reinterpret_cast<C_Actor *>(g_asCarPort);
            const int value = I(0, 5);
            const std::vector<C_Human *> prior = CmSomeHumans(2);
            CmCall("CollectFor",
                   [&] {
                       std::vector<C_Human *> out = prior;
                       c->CollectFor(out, command, actor, value);
                       return CmHash(CmMapped(out));
                   },
                   [&](void *) {
                       MsvcVectorPut(g_cmOrigOut, CmMapped(prior), size_t(I(0, 2)));
                       OCm<void>(0x00456c20, static_cast<void *>(g_cmOrigOut), command, SqOrigOf(actor), value);
                       return CmHash(MsvcVectorGet(g_cmOrigOut));
                   });
            break;
        }
        case 6: {
            C_Human *attacker = CmSomeHuman();
            CmCall("OnFriendlyFire", [&] { c->OnFriendlyFire(attacker); return 0; },
                   [&](void *) { OCm<void>(0x00459500, SqOrigOf(attacker)); return 0; });
            break;
        }
        case 7:
            CmCall("DelayFormationChecks", [&] { c->DelayFormationChecks(); return 0; },
                   [&](void *) { OCm<void>(0x00459490); return 0; });
            break;
        case 8:
            CmCall("OnPlayerNoise", [&] { c->OnPlayerNoise(); return 0; }, [&](void *) { OCm<void>(0x00459360); return 0; });
            break;
        case 9: {
            C_Human *leader = P(0.1f) ? nullptr : CmSomeHuman();
            CmCall("ReleaseFollowers", [&] { c->ReleaseFollowers(leader); return 0; },
                   [&](void *) { OCm<void>(0x004593e0, SqOrigOf(leader)); return 0; });
            break;
        }
        case 10: {
            // (The groups and the idle reports emptied first: the port's Reset clears them, the original's keeps them.)
            for (std::vector<C_Human *> &group : c->*DaGet(CmGroups()))
                group.clear();
            g_cmReports->clear();
            CmCall("Reset", [&] { c->Reset(); return 0; }, [&](void *) { OCm<void>(0x00456440); return 0; });
            break;
        }
        case 11: {
            // The getters and the trivial slots.
            const int slot = I(0, 17);
            C_Human *human = CmSomeHuman();
            static const uint32_t at[] = {0x00456860, 0x00456850, 0x00459480, 0x004595c0, 0x004595d0, 0x004567e0,
                                          0x004567f0, 0x00456800, 0x00456810, 0x00456820, 0x00456830, 0x00456840,
                                          0x0045b760, 0x0045b770, 0x0045b780, 0x0045b790, 0x0045b750, 0x00459470};
            CmCall("Getter",
                   [&] {
                       switch (slot) {
                       case 0: return DaWord(c->GetSquadTarget(), false);
                       case 1: return c->GetUnknown38();
                       case 2: return int(c->IsWhispering());
                       case 3: return int(c->IsSpacingMax());
                       case 4: return int(c->IsSpacingMin());
                       case 5: return int(c->GetFlag41());
                       case 6: return int(c->GetFlag42());
                       case 7: return int(c->GetFlag3D());
                       case 8: return int(c->GetFlag3E());
                       case 9: return int(c->GetFlag3F());
                       case 10: return int(c->GetFlag3C());
                       case 11: return int(c->GetFlag40());
                       case 12: return c->Unknown48(human);
                       case 13: return int(c->Unknown4C(human));
                       case 14: return c->Unknown50(human);
                       case 15: return int(c->Unknown54(human));
                       case 16: c->Unknown44(human); return 0;
                       default: c->SetWhispering(human == g_h.human); return 0;
                       }
                   },
                   [&](void *) {
                       if (slot == 0)
                           return DaWord(OCm<void *>(at[0]), true);
                       // (The getters take no argument, slots 12 .. 16 the human, 17 the flag.)
                       if (slot == 1)
                           return OCm<int>(at[slot]);
                       if (slot == 12 || slot == 14)
                           return OCm<int>(at[slot], SqOrigOf(human));
                       if (slot == 13 || slot == 15)
                           return OCm<int>(at[slot], SqOrigOf(human)) & 0xFF;
                       if (slot == 16) {
                           OCm<void>(at[slot], SqOrigOf(human));
                           return 0;
                       }
                       if (slot == 17) {
                           OCm<void>(at[slot], int(human == g_h.human));
                           return 0;
                       }
                       return OCm<int>(at[slot]) & 0xFF;
                   });
            break;
        }
        case 12:
        case 21:
        case 13:
        case 14:
        case 15:
        case 16: {
            // The HUD's commands (TakeCover and MoveNear with a selected soldier: the port's fix, the original reads
            // through null).
            S_ActionCommand command;
            command.action = which == 21 ? I(0x08, 0x09) : which == 12 ? I(0x08, 0x0B) : which == 13 ? I(0x1B, 0x20) : which == 14 && P(0.5f) ? I(0x15, 0x19) : I(0, 0x3F);
            if (command.action >= 0x08 && command.action <= 0x0B) {
                if (!g_sq.selected)
                    g_sq.selected = CmSomeHuman();
                CmGather();
                CmRandomPoints();
                // (Up to eight cover points, now and then one twice: spares for the empty slots.)
                g_sq.coverWithin.clear();
                for (int n = I(0, 8); n > 0; --n)
                    g_sq.coverWithin.push_back(uint16_t(I(0, 7)));
            }
            command.dir = P(0.5f) ? CmDir() : CmNear(15);
            command.member = P(0.6f) ? nullptr : CmSomeHuman();
            command.command = CM_ON_ACTOR[I(0, 32)];
            command.actor = CmActorFor(command.command);
            command.value = I(0, 5);
            // (A player's selected soldier now and then: the combat orders also its own.)
            // (Not the tested human: the harness links its original's controller again for every call.)
            if (g_sq.selected && g_sq.selected != g_h.human && command.member != g_sq.selected && command.action >= SQUAD_ORDER_MODE0 &&
                command.action <= SQUAD_ORDER_MODE1_ALT && P(0.5f))
                CmMakeSelectedPlayer();
            CmCall("ExecuteActionCommand", [&] { c->ExecuteActionCommand(command); return 0; },
                   [&](void *) { OCm<void>(0x00456020, CmOrigCommand(command)); return 0; });
            break;
        }
        case 17:
        case 18:
        case 19: {
            // (Now and then a manoeuvre: up a ladder, through a door, a grenade into a room; the squad's three idle.)
            const int manoeuvre = P(0.25f) ? I(0, 2) : -1;
            const int command = manoeuvre == 0 ? HUMAN_COMMAND_ENTER_VEHICLE : manoeuvre == 1 ? S_Cm::MANOEUVRE_THROUGH_DOOR
                                : manoeuvre == 2                              ? S_Cm::MANOEUVRE_GRENADE_ROOM
                                                                              : CM_ON_ACTOR[I(0, 32)];
            C_Actor *actor = manoeuvre == 0 ? CmLadder() : CmActorFor(command);
            if (manoeuvre >= 0 && P(0.5f)) {
                (c->*DaGet(CmGroups()))[0] = {CmHuman(0), CmHuman(1), CmHuman(2)};
                if (P(0.5f))
                    g_sq.selected = nullptr;
            }
            C_Human *member = P(0.6f) || manoeuvre >= 0 ? nullptr : CmSomeHuman();
            const bool quiet = P(0.5f);
            const int value = I(0, 5);
            CmCall("ExecuteCommand", [&] { c->ExecuteCommand(command, actor, member, quiet, value); return 0; },
                   [&](void *) {
                       OCm<void>(0x00458e50, command, SqOrigOf(actor), SqOrigOf(member), int(quiet), value);
                       return 0;
                   });
            break;
        }
        default: {
            // A member arrived (the manoeuvre's state random; its door and ladder).
            CmCall("OnOrderMoveDone", [&] { c->OnOrderMoveDone(); return 0; },
                   [&](void *) {
                       OriginalAt<void(__fastcall *)(void *)>(0x00456f00)(OCmObject());
                       return 0;
                   });
            break;
        }
        }
    }
}

void TestCommander()
{
    SetUpSenses();
    SetUpSensesB();
    SetUpCombatAi();
    g_humanAnswers = AsAnswers;
    g_visiblePartsOf = AsPartsOf;
    SetUpFireOrder();
    SetUpSquad();
    SetUpCommander();
    TestCommanderCalls();
    g_humanAnswers = nullptr;
    g_visiblePartsOf = nullptr;
}
}  // namespace

// The idle report's controller type (both sides): see commander_decls.inc.
namespace {
int CmControllerType(const void *human)
{
    const void *selected = g_sq.selected;
    if (g_cm.selectedPlayer && selected && (human == selected || human == SqOrigOf(selected)))
        return HUMAN_CONTROLLER_PLAYER;
    return HUMAN_CONTROLLER_SQUAD;
}
}  // namespace
