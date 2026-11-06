// (Part 10d-vi.) The game commands (net/net_commands.cpp): the server's and the client's. The commands' build only
// (build_commands.bat: -DNET_SESSION_TEST -DNET_MESSAGES_TEST -DNET_COMMANDS_TEST; the fake HandleServerCommand /
// HandleClientCommand left out; game/game_state.cpp linked); included after the messages' tests (their MessageNet,
// T_Reader, Flush, MessageHuman, SampleState, ...).

// ---- the fakes ---------------------------------------------------------------------------------------------------------

// The game: made / deleted / started (g_gameStarts) logged; its hooks logged.
bool g_gameStarts = true;
void *g_currentMission = reinterpret_cast<void *>(0x4D15);
C_game::C_game() { Log("game made"); }
C_game::~C_game() { Log("game deleted"); }
bool C_game::Start(void *mission)
{
    Log("game start %d", mission == g_currentMission);
    return g_gameStarts;
}
bool C_game::Stop()
{
    Log("game stop");
    return true;
}
bool C_game::HandleInput(float *, float *) { return false; }
void C_game::Tick(int) {}
void C_game::Update(int) {}
void C_game::Render() {}
void C_game::EndMission(int unused) { Log("end mission %d", unused); }
std::string StateText(const S_SoldierState *state)
{
    char text[160];
    std::snprintf(text, sizeof text, "%s team %d profile %d health %d vip %d belt %d", state->name.c_str(), state->team,
                  state->profileIndex, state->health, state->vip, static_cast<int>(state->belt.size()));
    return text;
}
std::string StatsText(const int *stats)
{
    if (!stats)
        return "none";
    std::string text;
    for (int i = 0; i < STAT_COUNT; ++i)
        text += (i ? " " : "") + std::to_string(stats[i]);
    return text;
}
void LogDefaults(const S_SoldierState *state)
{
    Log("defaults %d %d %d %d", state->speechSet, state->unknown34, state->unknown38, state->deployed);
}
void C_game::OnPlayerJoined(int slot, S_SoldierState *state, int *stats)
{
    LogDefaults(state);
    Log("joined %d %s / %s", slot, StateText(state).c_str(), StatsText(stats).c_str());
}
void C_game::OnRespawnRequest(int slot, S_SoldierState *state, int *stats)
{
    LogDefaults(state);
    Log("respawn request %d %s / %s", slot, StateText(state).c_str(), StatsText(stats).c_str());
    delete state;
    delete[] stats;
}
void C_game::RespawnPlayer(int slot, const S_vector4 &pos, const S_vector4 &dir, S_SoldierState *state,
                           const int *stats)
{
    LogDefaults(state);
    Log("respawn %d %.1f %.1f %.1f / %.1f %.1f %.1f %.2f %.2f / %s / %s", slot, pos.x, pos.y, pos.z, dir.x, dir.y, dir.z,
        pos.w, dir.w, StateText(state).c_str(), StatsText(stats).c_str());
}
void C_game::OnRespawnScreen(bool cancel) { Log("respawn screen %d", cancel); }
void C_game::OnRoundOver(int winner) { Log("round over %d", winner); }
void C_game::OnSessionClosing(int reason) { Log("session closing %d", reason); }
void C_game::OnWarmupFinished() { Log("warmup finished"); }
void C_game::OnWarmupStarted() { Log("warmup started"); }
void C_game::ReadObjectiveStates(C_BitStream *stream) { Log("objectives %u", stream->ReadBits(8)); }
void C_game::RestartMission(bool newRound) { Log("restart %d", newRound); }
void C_game::SetCameraMode(int mode) { Log("camera %d", mode); }
void C_game::ApplyObjectiveStatus(int objective, int status) { Log("objective %d %d", objective, status); }
C_GameState *g_openedMenu;
C_GameState *C_game::OpenGameMenu(int arg)
{
    Log("open menu %d", arg);
    return g_openedMenu;
}
void ShowGameMenuMessage(int textId) { Log("menu message %d", textId); }
void LoadNextMap() { Log("next map"); }

// The menu states: made / started logged (their text boxes and hint inert).
C_TextBox::C_TextBox() {}
C_TextBox::~C_TextBox() {}
void C_TextBox::UpdateScrollRange() {}
void C_TextBox::Render() {}
bool C_TextBox::ScrollTo(int) { return false; }
void S_MenuHint::Reset() {}
C_MenuState::C_MenuState() { Log("menu made"); }
C_MenuState::~C_MenuState() {}
bool C_MenuState::Start(void *arg)
{
    Log("menu start %d", arg != nullptr);
    return true;
}
bool C_MenuState::Stop() { return true; }
bool C_MenuState::HandleInput(float *, float *) { return false; }
void C_MenuState::Update(int) {}
void C_MenuState::Render() {}
void C_MenuState::DrawScreens() {}
void C_MainMenu::OnMessage(S_ControlMessage *) {}
bool C_MainMenu::BuildScreens() { return true; }
void C_MainMenu::OnStop() {}
bool C_MainMenu::OnStart(void *) { return true; }
void C_MainMenu::OnUpdate(int) {}
void C_GameMenu::AddChatLine(int team, const char *name, const char *text) { Log("chat line %d %s: %s", team, name, text); }
void C_GameMenu::SetTeam(int team) { Log("menu team %d", team); }

// A state of a type (its GetTypeId), big enough for C_game's and C_GameMenu's fields.
struct T_State : C_GameState {
    explicit T_State(int type) : type(type) {}
    ~T_State() override { Log("state %d deleted", type); }
    bool Stop() override
    {
        Log("state %d stop", type);
        return true;
    }
    int GetTypeId() const override { return type; }
    void SetPendingState(int state) override
    {
        Log("state %d pending %d", type, state);
        C_GameState::SetPendingState(state);
    }
    int type;
    alignas(16) unsigned char room[sizeof(C_GameMenu) + sizeof(C_game)] = {};
};

// The campaign, the soldiers' profiles (by index), the items' definitions (by type), the stats by rank.
C_MissionDef *C_Campaign::GetCurrentMission() { return static_cast<C_MissionDef *>(g_currentMission); }
bool C_Campaign::SetupMultiplayerMission()
{
    Log("setup mp mission");
    return true;
}
std::map<int, S_SoldierProfile *> g_profiles;
S_SoldierProfile *C_HumanStore::Get(int index) const
{
    auto it = g_profiles.find(index);
    return it == g_profiles.end() ? nullptr : it->second;
}
void C_HumanStore::GetStats(int *stats, int index, int level) const
{
    Log("store stats %d %d", index, level);
    stats[11] = 1000 + level;
}
std::map<int, C_ItemDef *> g_itemDefs;
C_ItemDef *GetItemDef(int typeId)
{
    auto it = g_itemDefs.find(typeId);
    return it == g_itemDefs.end() ? nullptr : it->second;
}
bool HasUtf8Sequences(const char *text)
{
    for (; *text; ++text)
        if (static_cast<uint8_t>(*text) >= 0x80)
            return true;
    return false;
}
void SplitSoldierStacks(S_SoldierState &state) { Log("split %s", state.name.c_str()); }
void S_SquadSoldier::ClearItems() { Log("clear items %s", name.c_str()); }

// The loading screen, the modules', the temporary actors', the settings' readers: a marker byte each.
void C_LoaderScreen::AddPlayer(const char *text, int team) { Log("loader %s %d", text, team); }
namespace ModuleManager {
void BroadcastReadState(C_BitStream *stream) { Log("modules %u", stream->ReadBits(8)); }
}  // namespace ModuleManager
void C_TemporaryActorsList::ReadState(C_BitStream *stream) { Log("temp actors %u", stream->ReadBits(8)); }
void S_MultiplayerSettings::ReadCurrentMap(C_BitStream *stream)
{
    Log("map %u %d", stream->ReadBits(8), this == &g_testMp);
}
void S_MultiplayerSettings::ReadRules(C_BitStream *stream) { Log("rules %u %d", stream->ReadBits(8), this == &g_testMp); }
S_MultiplayerSettings::S_MultiplayerSettings() { Log("settings made"); }
S_MultiplayerSettings::~S_MultiplayerSettings() { Log("settings gone"); }
void S_MultiplayerSettings::Reset() { Log("settings reset %d", this == &g_testMp); }

// The humans: hits, respawn requests, chat, remote hits logged; made soldiers (C_SpawnPoint::CreateSoldier) with a
// vtable whose StartMission is logged and a remote controller whose SetSpectating is.
void C_Human::ApplyRemoteHit(int type, int zone, int health, int killerId, const S_vector &pos, const S_vector &dir)
{
    Log("remote hit %d %d %d %d %.2f %.2f", type, zone, health, killerId, pos.x + pos.y + pos.z, dir.x + dir.y + dir.z);
}
void C_Human::RequestRespawn() { Log("request respawn %d", m_netId); }
void C_Human::SetChatText(const char *text) { Log("chat text %d %s", m_netId, text); }
void __attribute__((thiscall)) FakeHit(C_Human *self, int type, float amount, const S_vector *pos, const S_vector *dir,
                                       C_Actor *attacker, int flags)
{
    Log("hit %d %d %.1f %.2f %.2f %d %d", self->m_netId, type, amount, pos->x + pos->y + pos->z, dir->x + dir->y + dir->z,
        attacker == self, flags);
}
void __attribute__((thiscall)) FakeStartMission(C_Human *self, bool server) { Log("start mission %d %d", self->m_netId, server); }
void __attribute__((thiscall)) FakeSetSpectating(C_HumanController *, bool on) { Log("spectating %d", on); }
// A human whose virtual calls all go to `fn` (one signature per human).
C_Human *VirtualHuman(void *fn, int netId, int health)
{
    static void *vtables[8][128];
    alignas(16) static unsigned char storage[8][sizeof(C_Human)];
    static int used;
    const int i = used++ % 8;
    for (void *&slot : vtables[i])
        slot = fn;
    std::memset(storage[i], 0, sizeof storage[i]);
    *reinterpret_cast<void ***>(storage[i]) = vtables[i];
    auto *human = reinterpret_cast<C_Human *>(storage[i]);
    human->m_netId = netId;
    human->m_stats[STAT_HEALTH] = health;
    return human;
}
C_HumanController *RemoteController()
{
    static void *vtable[128];
    alignas(16) static unsigned char storage[sizeof(C_RemoteController)];
    for (void *&slot : vtable)
        slot = reinterpret_cast<void *>(&FakeSetSpectating);
    std::memset(storage, 0, sizeof storage);
    *reinterpret_cast<void ***>(storage) = vtable;
    return reinterpret_cast<C_HumanController *>(storage);
}
C_Human *g_madeSoldier;
C_Human *C_SpawnPoint::CreateSoldier(int slot, int actorSlot, const S_vector4 &pos, const S_vector4 &dir,
                                     S_SoldierState *state, const int *stats, int uniform, bool npc)
{
    LogDefaults(state);
    Log("create soldier %d %d %.1f %.1f %.1f / %.1f %.1f %.1f / %s / %d %d %d", slot, actorSlot, pos.x, pos.y, pos.z,
        dir.x, dir.y, dir.z, StateText(state).c_str(), stats != nullptr, uniform, npc);
    return g_madeSoldier;
}
// The player's team (g_teamManager: raw storage; its player team's vtable logs AddMember).
bool __attribute__((thiscall)) FakeAddMember(void *, C_Human *human)
{
    Log("team add %d", human->m_netId);
    return true;
}
void SetupTeamManager()
{
    static void *vtable[16];
    for (void *&slot : vtable)
        slot = reinterpret_cast<void *>(&FakeAddMember);
    *reinterpret_cast<void ***>(g_teamManager.GetPlayerTeam()) = vtable;
}

// ---- helpers -----------------------------------------------------------------------------------------------------------

// A command's data written, then handed to the handler as a reader.
struct T_Command {
    C_NetMessage out;
    T_Command &Bits(uint32_t value, int bits)
    {
        out.WriteBits(value, bits);
        return *this;
    }
    T_Command &Int(int value) { return Bits(static_cast<uint32_t>(value), 32); }
    T_Command &Float(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        return Bits(bits, 32);
    }
    T_Command &Str(const char *text)
    {
        out.WriteString(text);
        return *this;
    }
    T_Command &State(S_SoldierState state)
    {
        NetWriteSoldierState(&out, state, false);
        return *this;
    }
    // Run by the server (`server`) or the client; the bits left unread returned.
    int Run(int id, bool server)
    {
        T_Reader reader(out);
        if (server)
            GetGameApp()->HandleServerCommand(id, &reader.msg);
        else
            GetGameApp()->HandleClientCommand(id, &reader.msg);
        return static_cast<int>(reader.Left());
    }
};

alignas(16) unsigned char g_storeStorage[64], g_campaignStorage[64], g_squadStorage[sizeof(S_MissionSquad)];
alignas(16) unsigned char g_mpMissionStorage[sizeof(C_MpMissionDef)];
T_State *g_gameState;
C_MpMissionDef *g_mpMission;
// The global net game ready (MessageNet's) with the game around it: the profile's store, campaign and squad, a game
// (T_State of type 1 whose C_game fields lie in its room) on its multiplayer mission, the app's state none.
C_NetGame &CommandNet(uint32_t role)
{
    C_NetGame &net = MessageNet(role);
    Flush(0x17, 0, 0, 0);                           // (what earlier tests left queued)
    net.m_outgoing.clear();
    g_gameProfile.m_humanStore = reinterpret_cast<C_HumanStore *>(g_storeStorage);
    g_gameProfile.m_campaign = reinterpret_cast<C_Campaign *>(g_campaignStorage);
    g_gameProfile.m_missionSquad = reinterpret_cast<S_MissionSquad *>(g_squadStorage);
    g_gameProfile.m_missionSquad->states.clear();
    g_gameProfile.m_unicodeFont = false;
    g_mpMission = reinterpret_cast<C_MpMissionDef *>(g_mpMissionStorage);
    delete g_gameState;
    g_gameState = new T_State(GAME_STATE_TYPE_GAME);
    g_game = reinterpret_cast<C_game *>(g_gameState);
    g_game->m_mission = g_mpMission;
    g_game->m_roundTime = 0;
    g_game->m_respawnWaveTime = 0;
    GetGameApp()->m_state = nullptr;
    g_profiles.clear();
    g_itemDefs.clear();
    g_controllerTypes.clear();
    g_testMp.autoTeamBalance = 0;
    g_testMp.adminPassword = C_str("pw");
    g_log.clear();
    return net;
}
S_SoldierProfile *Profile(int index, float first)
{
    alignas(16) static unsigned char storage[4][sizeof(S_SoldierProfile)];
    static int used;
    auto *profile = reinterpret_cast<S_SoldierProfile *>(storage[used++ % 4]);
    for (int i = 0; i < 9; ++i)
        profile->stats[i] = first + static_cast<float>(i) + 0.75f;
    g_profiles[index] = profile;
    return profile;
}
C_ItemDef *ItemDef(int typeId, float weight)
{
    alignas(16) static unsigned char storage[4][sizeof(C_ItemDef)];
    static int used;
    auto *def = reinterpret_cast<C_ItemDef *>(storage[used++ % 4]);
    def->m_weight = weight;
    g_itemDefs[typeId] = def;
    return def;
}
// The messages queued (unreliable) by a command, sent: everyone's stream / the outgoing queue (the local player's).
T_Reader Sent()
{
    return Flush(0x17, 0, 0, 0);
}

// ---- the server's ----------------------------------------------------------------------------------------------------

void TestServerJoin()
{
    // 0: a player joined: its soldier (split), stats from the profile (a VIP: + the uniform's weight + 1).
    C_NetGame &net = CommandNet(GAME_ROLE_SERVER);
    S_SoldierState s = SampleState();
    s.profileIndex = 4;
    s.vip = true;
    s.uniform.typeId = 9;
    Profile(4, 100.0f);
    ItemDef(9, 2.5f);
    CHECK(T_Command().Int(3).State(s).Run(0, true) == 0);
    CHECK(Logged("split Joe") && Logged("defaults -1 -1 -1 1"));
    CHECK(Logged("joined 3 Joe team 1 profile 4 health 600 vip 1 belt 2 / 100 104 102 103 104 105 106 107 108 50 0 80"));
    // Not a VIP: the profile's; no profile: the defaults; a local game: no stats.
    s.vip = false;
    g_log.clear();
    T_Command().Int(3).State(s).Run(0, true);
    CHECK(Logged("joined 3 Joe team 1 profile 4 health 600 vip 0 belt 2 / 100 101 102 103 104 105 106 107 108 50 0 80"));
    s.profileIndex = 7;
    g_log.clear();
    T_Command().Int(3).State(s).Run(0, true);
    CHECK(Logged("joined 3 Joe team 1 profile 7 health 600 vip 0 belt 2 / 800 80 80 80 80 80 50 30 20 50 0 80"));
    g_gameRole = GAME_ROLE_LOCAL;
    g_log.clear();
    T_Command().Int(3).State(s).Run(0, true);
    CHECK(Logged("joined 3 Joe team 1 profile 7 health 600 vip 0 belt 2 / none"));
    // A VIP in an unknown uniform: no weight (Fix).
    g_gameRole = GAME_ROLE_SERVER;
    s.profileIndex = 4;
    s.vip = true;
    s.uniform.typeId = 33;
    g_log.clear();
    T_Command().Int(3).State(s).Run(0, true);
    CHECK(Logged("joined 3 Joe team 1 profile 4 health 600 vip 1 belt 2 / 100 102 102 103 104 105 106 107 108 50 0 80"));
    (void)net;
}

void TestServerRespawnRequest()
{
    // 1: a respawn asked for: the state and stats handed over (by the profile, then the store by the player's rank).
    C_NetGame &net = CommandNet(GAME_ROLE_SERVER);
    net.m_players.push_back(Player(5, "a"));
    net.m_players.push_back(Player(6, "b"));
    net.m_players[1].stats[NET_STAT_RANK] = 6;
    S_SoldierState s = SampleState();
    s.profileIndex = 4;
    Profile(4, 10.0f);
    CHECK(T_Command().Int(1).State(s).Run(1, true) == 0);
    CHECK(Logged("split Joe") && Logged("store stats 4 6") && Logged("defaults -1 -1 -1 1"));
    CHECK(Logged("respawn request 1 Joe team 1 profile 4 health 600 vip 0 belt 2 / 10 11 12 13 14 15 16 17 18 50 0 1006"));
}

void TestServerSmall()
{
    // 2: where everybody is: message 5 to the player.
    C_NetGame &net = CommandNet(GAME_ROLE_SERVER);
    net.m_players.push_back(Player(5, "a"));
    net.m_players.push_back(Player(6, "b"));
    g_localPlayerSlot = 1;
    CHECK(T_Command().Int(1).Run(2, true) == 0);
    Sent();
    CHECK(net.m_outgoing.size() == 1);
    if (net.m_outgoing.size() == 1) {
        T_Reader out(net.m_outgoing.front().data, net.m_outgoing.front().size);
        CHECK(out.Game(5) && out.Int() == 2);
    }
    g_localPlayerSlot = 0;
    // 3: chat: the sender active, the line sent on (4).
    net.m_players[1].activityTime = 0;
    CHECK(T_Command().Int(1).Int(-7).Bits(1, 1).Str("hello").Run(3, true) == 0);
    CHECK(net.m_players[1].activityTime != 0);
    T_Reader chat = Sent();
    CHECK(chat.Game(4) && chat.Int() == 1 && chat.Int() == -7 && chat.Bits(1) == 1 && chat.Str() == "hello");
    net.m_players[0].activityTime = 0;
    T_Command().Int(0).Int(0).Bits(0, 1).Str("z").Run(3, true);
    CHECK(net.m_players[0].activityTime != 0);
    Sent();
    net.m_players[0].activityTime = 0;
    net.m_players[1].activityTime = 0;
    T_Command().Int(-1).Int(0).Bits(0, 1).Str("x").Run(3, true);
    CHECK(net.m_players[0].activityTime == 0 && net.m_players[1].activityTime == 0);
    T_Reader chat2 = Sent();
    CHECK(chat2.Game(4) && chat2.Int() == -1);
    // 4: a suicide (a hit of the environment by itself); no human: nothing.
    net.m_players[1].human = VirtualHuman(reinterpret_cast<void *>(&FakeHit), 42, 100);
    CHECK(T_Command().Int(1).Run(4, true) == 0);
    CHECK(Logged("hit 42 6 -1.0 0.00 0.00 1 0"));
    g_log.clear();
    T_Command().Int(0).Run(4, true);
    CHECK(g_log.empty());
    // 6: the player's +0x80 text.
    CHECK(T_Command().Bits(1, 5).Str("abcdef").Run(6, true) == 0 &&
          std::strcmp(reinterpret_cast<const char *>(net.m_players[1].unknown80), "abcdef") == 0);
    // 8: an admin's command.
    g_log.clear();
    T_Command().Str("kick x").Run(8, true);
    CHECK(Logged("execute kick x"));
    // 10 / 11: a vote asked for, a yes.
    net.m_vote = S_NetVote();
    CHECK(T_Command().Bits(5, 7).Bits(700, 10).Run(10, true) == 0);
    CHECK(net.m_vote.kind == 5 && net.m_vote.arg == 700);
    const int yes = net.m_vote.yes;
    T_Command().Run(11, true);
    CHECK(net.m_vote.yes == yes + 1 || net.m_vote.kind == 0x42);
    net.m_vote = S_NetVote();
    // Others: nothing.
    g_log.clear();
    CHECK(T_Command().Int(9).Run(12, true) == 32 && g_log.empty());
}

void TestServerTeams()
{
    // 5: a team asked for (2: the smaller of the two, the allies on a tie), kept unless the automatic balance and the
    // map's balance say no; told (0xE).
    C_NetGame &net = CommandNet(GAME_ROLE_SERVER);
    for (int i = 0; i < 4; ++i)
        net.m_players.push_back(Player(5 + i, "p"));
    net.m_players[0].pendingTeam = 0;
    net.m_players[1].pendingTeam = 0;
    net.m_players[2].pendingTeam = 1;
    net.m_players[3].pendingTeam = 1;
    CHECK(T_Command().Int(3).Int(0).Run(5, true) == 0);
    CHECK(net.m_players[3].pendingTeam == 0);
    T_Reader told = Sent();
    CHECK(told.Game(0xE) && told.Bits(5) == 3 && told.Bits(2) == 0);
    net.m_players[3].pendingTeam = 1;
    // (2 with no balance: the count's pick.)
    T_Command().Int(3).Int(2).Run(5, true);
    int axis, allied;
    net.m_players[3].pendingTeam = 1;
    net.CountTeams(&axis, &allied, true, false);
    T_Command().Int(3).Int(2).Run(5, true);
    const int pick = allied <= axis ? 1 : 0;
    CHECK(net.m_players[3].pendingTeam == pick);
    Sent();
    // With the balance: 2 is the pick at once.
    g_testMp.autoTeamBalance = 1;
    g_mpMission->m_balanceAllied = 1;
    g_mpMission->m_balanceAxis = 1;
    net.m_players[3].pendingTeam = 0;
    net.CountTeams(&axis, &allied, true, false);
    T_Command().Int(3).Int(2).Run(5, true);
    CHECK(net.m_players[3].pendingTeam == (allied <= axis ? 1 : 0));
    T_Reader picked = Sent();
    CHECK(picked.Game(0xE) && picked.Bits(5) == 3 && picked.Bits(2) == static_cast<uint32_t>(allied <= axis ? 1 : 0));
    // A team the balance refuses: the old one kept and told.
    net.m_players[0].pendingTeam = 0;
    net.m_players[1].pendingTeam = 0;
    net.m_players[2].pendingTeam = 0;
    net.m_players[3].pendingTeam = 1;
    T_Command().Int(3).Int(0).Run(5, true);
    const bool balanced = net.TeamsBalanced(1, 1, true);
    CHECK(net.m_players[3].pendingTeam == (balanced ? 0 : 1));
    T_Reader refused = Sent();
    CHECK(refused.Game(0xE) && refused.Bits(5) == 3 && refused.Bits(2) == (balanced ? 0u : 1u));
    // The map's balance read +0x58 first: 3 to 1 allowed only that way round.
    g_mpMission->m_balanceAllied = 3;
    g_mpMission->m_balanceAxis = 1;
    net.m_players[3].pendingTeam = 0;
    const bool allowed = net.TeamsBalanced(3, 1, true);
    net.m_players[3].pendingTeam = 1;
    T_Command().Int(3).Int(0).Run(5, true);
    CHECK(net.m_players[3].pendingTeam == (allowed ? 0 : 1));
    Sent();
    g_testMp.autoTeamBalance = 0;
}

void TestServerAdmin()
{
    // 7: the admin login: right: admin, told 1; wrong: told 0; a free slot: nothing.
    C_NetGame &net = CommandNet(GAME_ROLE_SERVER);
    net.m_players.push_back(Player(5, "a"));
    net.m_players.push_back(Player(6, "b"));
    net.m_players.push_back(Player(0, ""));
    g_localPlayerSlot = 1;
    T_Command().Bits(1, 5).Str("pw").Run(7, true);
    Sent();
    CHECK(net.m_players[1].admin && net.m_outgoing.size() == 1);
    if (!net.m_outgoing.empty()) {
        T_Reader out(net.m_outgoing.back().data, net.m_outgoing.back().size);
        CHECK(out.Game(0x13) && out.Int() == 1);
    }
    net.m_players[1].admin = false;
    T_Command().Bits(1, 5).Str("PW").Run(7, true);
    Sent();
    CHECK(!net.m_players[1].admin && net.m_outgoing.size() == 2);
    if (net.m_outgoing.size() == 2) {
        T_Reader out(net.m_outgoing.back().data, net.m_outgoing.back().size);
        CHECK(out.Game(0x13) && out.Int() == 0);
    }
    T_Command().Bits(2, 5).Str("pw").Run(7, true);
    T_Reader none = Sent();
    CHECK(!net.m_players[2].admin && net.m_outgoing.size() == 2 && none.bits == 0);
    g_localPlayerSlot = 0;
}

void TestServerJoinCount()
{
    // 9: the joining player's temporary actors counted: the whole game to it (1: the counts agree).
    C_NetGame &net = CommandNet(GAME_ROLE_SERVER);
    net.m_players.push_back(Player(5, "a"));
    g_actorsModule.m_actors.assign(37, nullptr);
    C_Actor *actor = reinterpret_cast<C_Actor *>(MakeHuman(false));
    g_actors[31] = actor;
    g_actors[32] = actor;
    g_actors[34] = actor;
    g_actors[36] = actor;
    g_actors[37] = actor;
    alignas(16) static unsigned char gameStorage[sizeof(C_game)];
    (void)gameStorage;
    CHECK(T_Command().Bits(0, 5).Bits(3, 16).Run(9, true) == 0);
    Sent();
    CHECK(net.m_outgoing.size() == 1);
    if (!net.m_outgoing.empty()) {
        T_Reader out(net.m_outgoing.back().data, net.m_outgoing.back().size);
        CHECK(out.Game(0x15) && out.Bits(1) == 1);
    }
    T_Command().Bits(0, 5).Bits(2, 16).Run(9, true);
    Sent();
    CHECK(net.m_outgoing.size() == 2);
    if (net.m_outgoing.size() == 2) {
        T_Reader out(net.m_outgoing.back().data, net.m_outgoing.back().size);
        CHECK(out.Game(0x15) && out.Bits(1) == 0);
    }
    // (The count is a signed 16 bits.)
    g_actors.clear();
    g_actorsModule.m_actors.clear();
}

// ---- the client's ----------------------------------------------------------------------------------------------------

void TestClientJoin()
{
    // 0: the session joined: the ids, rules, map; the old state stopped and deleted; a game made; the players on the
    // loading screen (their ranks' names: the allies' from 0x27A6, the others' 10 on); started: a player slot, command
    // 9 queued.
    C_NetGame &net = CommandNet(GAME_ROLE_CLIENT);
    net.m_players.clear();
    net.m_joinStatus = 0;
    GetGameApp()->m_state = new T_State(GAME_STATE_TYPE_MAIN_MENU);
    g_gameStarts = true;
    CHECK(T_Command().Int(0x1234).Int(0x77).Bits(0xA1, 8).Bits(0xA2, 8).Bits(2, 8).Str("Ann").Bits(1, 2).Bits(3, 5)
              .Str("Bob").Bits(0, 2).Bits(4, 5).Run(0, false) == 0);
    CHECK(GetGameApp()->m_randomId == 0x1234 && net.m_unknown448 == 0x77 && net.m_joinStatus == NET_JOIN_CONNECTED);
    CHECK(Logged("rules 161 1") && Logged("map 162 1") && Logged("state 2 stop") && Logged("state 2 deleted") &&
          Logged("game made") && Logged("setup mp mission") && Logged("game start 1"));
    CHECK(Logged("loader T27A9 Ann 1") && Logged("loader T27B4 Bob 0"));
    CHECK(net.m_players.size() == 1 && net.m_players[0].connection == 0 && net.m_players[0].keyQuery == -1);
    g_localPlayerSlot = 2;
    GetGameApp()->PostCommand(0, 0, 0, true);
    CHECK(net.m_messages[0].BitPosition() >= 37);
    {
        T_Reader posted(net.m_messages[0]);
        CHECK(posted.Game(9) && posted.Bits(5) == 2);
    }
    g_localPlayerSlot = 0;
    net.m_messages[0].ResetPosition();
    // Not started: the game deleted (not stopped), the main menu made and started.
    delete GetGameApp()->m_state;
    GetGameApp()->m_state = nullptr;
    g_gameStarts = false;
    g_log.clear();
    net.m_players.clear();
    T_Command().Int(1).Int(2).Bits(0xA1, 8).Bits(0xA2, 8).Bits(0, 8).Run(0, false);
    CHECK(Logged("game made") && Logged("game deleted") && !Logged("game stop") && Logged("menu made") &&
          Logged("menu start 0") && net.m_players.empty());
    CHECK(GetGameApp()->m_state && GetGameApp()->m_state->GetTypeId() == GAME_STATE_TYPE_MAIN_MENU);
    delete GetGameApp()->m_state;
    GetGameApp()->m_state = nullptr;
    g_gameStarts = true;
}

void TestClientSmall()
{
    C_NetGame &net = CommandNet(GAME_ROLE_CLIENT);
    net.m_players.push_back(Player(5, "Ann"));
    // 1: the mission ends (a client).
    T_Command().Run(1, false);
    CHECK(Logged("end mission 100"));
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    g_log.clear();
    T_Command().Run(1, false);
    CHECK(g_log.empty());
    // 2: a player left (told; the game's).
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(T_Command().Int(5).Run(2, false) == 0);
    CHECK(Logged("app print Ann T22CB") && Logged("left 5"));
    g_log.clear();
    T_Command().Int(9).Run(2, false);
    CHECK(Logged("left 9") && g_log.size() == 1);
    g_game = nullptr;
    g_log.clear();
    CHECK(T_Command().Int(5).Run(2, false) == 0 && g_log.empty());
    g_game = reinterpret_cast<C_game *>(g_gameState);
    // 6: the map; the next one loaded (a game).
    g_log.clear();
    CHECK(T_Command().Bits(0xB1, 8).Run(6, false) == 0 && Logged("map 177 1") && Logged("next map"));
    // 7: the mission restarts, the temporary actors read.
    g_log.clear();
    CHECK(T_Command().Bits(0xB2, 8).Run(7, false) == 0 && Logged("restart 1") && Logged("temp actors 178"));
    // 8: the scores.
    {
        T_Command scores;
        scores.Bits(0x21, 6);
        for (int i = 0; i < 12; ++i)
            scores.Int(i * 7);
        T_Reader direct(scores.out);
        net.ReadScores(&direct.msg);
        CHECK(scores.Run(8, false) == static_cast<int>(direct.Left()));
    }
    // 9: a player joined (told).
    g_log.clear();
    CHECK(T_Command().Str("Cy").Run(9, false) == 0 && Logged("app print Cy T22CA"));
    // 10: an objective's status.
    g_log.clear();
    CHECK(T_Command().Bits(17, 5).Bits(9, 4).Run(10, false) == 0 && Logged("objective 17 9"));
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    g_log.clear();
    CHECK(T_Command().Bits(17, 5).Bits(9, 4).Run(10, false) == 0 && g_log.empty());
    g_gameRole = GAME_ROLE_CLIENT;
    // 11 / 13: the warmup finished, the round over.
    g_log.clear();
    T_Command().Run(11, false);
    CHECK(T_Command().Bits(2, 2).Run(13, false) == 0 && Logged("warmup finished") && Logged("round over 2"));
    // 15: the respawn screen (the local player's).
    g_log.clear();
    CHECK(T_Command().Bits(0, 5).Bits(1, 1).Run(15, false) == 0);
    T_Command().Bits(1, 5).Bits(0, 1).Run(15, false);
    CHECK(Logged("respawn screen 1") && g_log.size() == 1);
    // 17: a player kicked (the local one: the session closes).
    g_log.clear();
    net.m_players.push_back(Player(6, "Bob"));
    CHECK(T_Command().Int(3).Int(1).Run(17, false) == 0);
    CHECK(!Logged("session closing 3") && Logged("app print T22AC"));
    g_log.clear();
    T_Command().Int(4).Int(0).Run(17, false);
    CHECK(Logged("session closing 4"));
    // 18: the rules (a server's: read into a scratch copy).
    g_log.clear();
    CHECK(T_Command().Bits(0xC7, 8).Run(18, false) == 0 && Logged("rules 199 1"));
    g_gameRole = GAME_ROLE_SERVER;
    g_log.clear();
    CHECK(T_Command().Bits(0xC8, 8).Run(18, false) == 0 && Logged("settings made") && Logged("settings reset 0") &&
          Logged("rules 200 0") && Logged("settings gone"));
    g_gameRole = GAME_ROLE_CLIENT;
    // 19: the admin login's answer.
    g_log.clear();
    CHECK(T_Command().Int(1).Run(19, false) == 0);
    CHECK(net.m_players[0].admin && Logged("app print Admin rights granted!"));
    T_Command().Int(0).Run(19, false);
    CHECK(!net.m_players[0].admin && Logged("app print Incorrect admin password!"));
    // 20: the temporary actors.
    g_log.clear();
    CHECK(T_Command().Bits(0xB3, 8).Run(20, false) == 0 && Logged("temp actors 179"));
    // 22 / 23: a vote begun, its result.
    net.m_vote = S_NetVote();
    CHECK(T_Command().Bits(3, 7).Bits(9, 10).Run(22, false) == 0);
    CHECK(net.m_vote.kind == 3 && net.m_vote.arg == 9);
    net.m_vote = S_NetVote();
    g_log.clear();
    CHECK(T_Command().Bits(1, 1).Run(23, false) == 0 && Logged("print 50 150 50 T22E3"));
    g_log.clear();
    CHECK(T_Command().Bits(0, 1).Run(23, false) == 0 && Logged("print 50 150 50 T22E4"));
    // 5: the places read past.
    CHECK(T_Command().Int(2).Float(1).Float(2).Float(3).Float(4).Float(5).Float(6).Float(7).Float(8).Float(9).Float(10)
              .Float(11).Float(12).Bits(0x55, 8).Run(5, false) == 8);
}

void TestClientRespawn()
{
    // 3: a respawn: the slot, place, facing, soldier, stats (health 10 bits, the others 7); a client's game told.
    CommandNet(GAME_ROLE_CLIENT);
    T_Command c;
    c.Int(4);
    float pos[3] = {10.5f, -20.25f, 300.0f};
    c.out.m_sender = c.out.m_recipient = reinterpret_cast<S_NetPlayer *>(&g_quantization);
    c.out.WriteQuantized(pos, &g_quantization, 7);
    c.out.WriteDirection3D(S_vector(0, 0, 1), -1);
    c.State(SampleState());
    c.Bits(700, 10);
    for (int i : {2, 1, 3, 4, 5, 6, 7, 8, 9, 11, 10})
        c.Bits(static_cast<uint32_t>(10 * i + 1), 7);
    CHECK(c.Run(3, false) == 0);
    CHECK(Logged("split Joe") && Logged("defaults -1 -1 -1 1"));
    CHECK(Logged("respawn 4 10.5 -20.3 300.0 / 0.0 0.0 1.0 0.00 0.00 / Joe team 1 profile 12 health 600 vip 0 belt 2 / "
                 "700 11 21 31 41 51 61 71 81 91 101 111"));
    // The server: read, not told.
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    g_log.clear();
    CHECK(c.Run(3, false) == 0 && g_log.empty());
}

void TestClientChat()
{
    // 4: chat: shown when the local player may see it (both alive or dead; a team line: the same team).
    C_NetGame &net = CommandNet(GAME_ROLE_CLIENT);
    net.m_players.push_back(Player(5, "Ann"));
    net.m_players.push_back(Player(6, "Bob"));
    net.m_players[0].team = 1;
    net.m_players[1].team = 1;
    T_State game(GAME_STATE_TYPE_GAME), menu(GAME_STATE_TYPE_GAME_MENU), other(GAME_STATE_TYPE_MAIN_MENU);
    GetGameApp()->m_state = &game;
    CHECK(T_Command().Int(1).Int(0).Bits(0, 1).Str("hi").Run(4, false) == 0);
    CHECK(Logged("print 255 255 255 Bob: hi"));
    // Over the sender's head (its human).
    net.m_players[1].human = VirtualHuman(reinterpret_cast<void *>(&FakeHit), 61, 100);
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(0, 1).Str("yo").Run(4, false);
    CHECK(Logged("print 255 255 255 Bob: yo") && Logged("chat text 61 yo"));
    // A team line: prefixed; another team's: not shown.
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(1, 1).Str("go").Run(4, false);
    CHECK(Logged("print 255 255 255 Bob: <TEAM> go") && Logged("chat text 61 <TEAM> go"));
    net.m_players[1].team = 0;
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(1, 1).Str("go").Run(4, false);
    CHECK(g_log.empty());
    T_Command().Int(1).Int(0).Bits(0, 1).Str("all").Run(4, false);
    CHECK(Logged("print 255 255 255 Bob: all"));
    // The dead and the living do not hear each other.
    net.m_players[0].human = VirtualHuman(reinterpret_cast<void *>(&FakeHit), 60, 0);
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(0, 1).Str("dead?").Run(4, false);
    CHECK(g_log.empty());
    net.m_players[1].human->m_stats[STAT_HEALTH] = 0;
    T_Command().Int(1).Int(0).Bits(0, 1).Str("both").Run(4, false);
    CHECK(Logged("print 255 255 255 Bob: both"));
    net.m_players[0].human->m_stats[STAT_HEALTH] = 1;
    net.m_players[1].human->m_stats[STAT_HEALTH] = 5;
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(0, 1).Str("one").Run(4, false);
    CHECK(Logged("print 255 255 255 Bob: one"));
    net.m_players[0].human->m_stats[STAT_HEALTH] = 5;
    net.m_players[1].human->m_stats[STAT_HEALTH] = 1;
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(0, 1).Str("two").Run(4, false);
    CHECK(Logged("print 255 255 255 Bob: two"));
    // UTF-8 without the unicode font: hidden (the name XXX, the text ????????); with it: shown.
    std::strcpy(net.m_players[1].name, "B\xc3\xb6");
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(0, 1).Str("\xc3\xa4").Run(4, false);
    CHECK(Logged("print 255 255 255 XXX: ????????"));
    g_gameProfile.m_unicodeFont = true;
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(0, 1).Str("\xc3\xa4").Run(4, false);
    CHECK(Logged("print 255 255 255 B\xc3\xb6: \xc3\xa4"));
    g_gameProfile.m_unicodeFont = false;
    std::strcpy(net.m_players[1].name, "Bob");
    // The in-game menu: its chat (the sender's team); other states: nothing.
    GetGameApp()->m_state = &menu;
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(0, 1).Str("m").Run(4, false);
    CHECK(Logged("chat line 0 Bob: m") && g_log.size() == 1);
    GetGameApp()->m_state = &other;
    g_log.clear();
    T_Command().Int(1).Int(0).Bits(0, 1).Str("o").Run(4, false);
    CHECK(g_log.empty());
    // The admin's (slot -1).
    GetGameApp()->m_state = &menu;
    T_Command().Int(-1).Int(0).Bits(0, 1).Str("hey").Run(4, false);
    CHECK(Logged("chat line 0 ADMIN: hey"));
    GetGameApp()->m_state = &game;
    g_log.clear();
    CHECK(T_Command().Int(-1).Int(0).Bits(1, 1).Str("hey").Run(4, false) == 0);
    CHECK(Logged("print 160 160 160 ADMIN: hey"));
    GetGameApp()->m_state = nullptr;
    g_log.clear();
    CHECK(T_Command().Int(-1).Int(0).Bits(0, 1).Str("x").Run(4, false) == 0 && g_log.empty());
    CHECK(T_Command().Int(1).Int(0).Bits(0, 1).Str("x").Run(4, false) == 0 && g_log.empty());
}

void TestClientTeams()
{
    // 12: the teams; the local player's soldier on a new team: in the game its items cleared and the in-game menu
    // opened, in the in-game menu its team shown; then the warmup.
    C_NetGame &net = CommandNet(GAME_ROLE_CLIENT);
    net.m_players.push_back(Player(5, "Ann"));
    net.m_players.push_back(Player(6, "Bob"));
    S_SquadSoldier soldier;
    soldier.name = "Sol";
    soldier.team = 0;
    g_gameProfile.m_missionSquad->states.push_back(&soldier);
    T_State game(GAME_STATE_TYPE_GAME), menu(GAME_STATE_TYPE_GAME_MENU);
    g_openedMenu = &menu;
    GetGameApp()->m_state = &game;
    g_localPlayerSlot = 1;
    CHECK(T_Command().Bits(0, 6).Bits(1, 2).Bits(1, 6).Bits(1, 2).Bits(0x21, 6).Run(12, false) == 0);
    CHECK(net.m_players[0].team == 1 && net.m_players[0].pendingTeam == 1 && net.m_players[1].team == 1 &&
          net.m_players[1].pendingTeam == 1);
    CHECK(soldier.team == 1 && Logged("clear items Sol") && Logged("open menu -2") && GetGameApp()->m_state == &menu &&
          Logged("warmup started"));
    g_log.clear();
    T_Command().Bits(1, 6).Bits(0, 2).Bits(0x21, 6).Run(12, false);
    CHECK(Logged("menu team 0") && soldier.team == 1 && net.m_players[1].team == 0);
    // The same team: nothing; no soldier: nothing (Fix).
    g_log.clear();
    T_Command().Bits(1, 6).Bits(1, 2).Bits(0x21, 6).Run(12, false);
    CHECK(!Logged("menu team 1") && Logged("warmup started"));
    g_gameProfile.m_missionSquad->states.clear();
    g_log.clear();
    T_Command().Bits(1, 6).Bits(0, 2).Bits(0x21, 6).Run(12, false);
    CHECK(Logged("warmup started") && g_log.size() == 1);
    g_game = nullptr;
    g_log.clear();
    CHECK(T_Command().Bits(0x21, 6).Run(12, false) == 0 && g_log.empty());
    g_game = reinterpret_cast<C_game *>(g_gameState);
    // 14: a player's team: another's taken while unknown (2); the local player's pending one (the menu told).
    net.m_players[0].team = 2;
    CHECK(T_Command().Bits(0, 5).Bits(1, 2).Run(14, false) == 0);
    CHECK(net.m_players[0].team == 1);
    T_Command().Bits(0, 5).Bits(0, 2).Run(14, false);
    CHECK(net.m_players[0].team == 1 && net.m_players[0].pendingTeam == 1);
    GetGameApp()->m_state = &menu;
    g_log.clear();
    net.m_players[1].pendingTeam = 0;
    T_Command().Bits(1, 5).Bits(1, 2).Run(14, false);
    CHECK(net.m_players[1].pendingTeam == 1 && Logged("menu message -1") && Logged("menu team 1"));
    g_gameRole = GAME_ROLE_LOCAL;
    g_log.clear();
    T_Command().Bits(1, 5).Bits(0, 2).Run(14, false);
    CHECK(!Logged("menu message -1") && Logged("menu team 0"));
    g_gameRole = GAME_ROLE_CLIENT;
    GetGameApp()->m_state = &game;
    g_log.clear();
    T_Command().Bits(1, 5).Bits(1, 2).Run(14, false);
    CHECK(g_log.empty() && net.m_players[1].pendingTeam == 1);
    g_localPlayerSlot = 0;
    GetGameApp()->m_state = nullptr;
}

void TestClientRespawnWave()
{
    // 16: the wave: the round time's milliseconds kept; a dead local player in a uniform asks to respawn, else the
    // in-game menu respawns it on apply.
    C_NetGame &net = CommandNet(GAME_ROLE_CLIENT);
    net.m_players.push_back(Player(5, "Ann"));
    g_game->m_roundTime = 12345;
    T_Command().Run(16, false);
    CHECK(g_game->m_respawnWaveTime == 345);
    C_Human *human = VirtualHuman(reinterpret_cast<void *>(&FakeHit), 70, 0);
    net.m_players[0].human = human;
    S_SquadSoldier soldier;
    soldier.uniform = {1, 5, 1};
    g_gameProfile.m_missionSquad->states.push_back(&soldier);
    g_log.clear();
    T_Command().Run(16, false);
    CHECK(Logged("request respawn 70"));
    // Not one uniform: the menu's flag.
    T_State menu(GAME_STATE_TYPE_GAME_MENU);
    GetGameApp()->m_state = &menu;
    auto *gameMenu = reinterpret_cast<C_GameMenu *>(&menu);
    gameMenu->m_respawnOnApply = false;
    soldier.uniform.present = 2;
    g_log.clear();
    T_Command().Run(16, false);
    CHECK(g_log.empty() && gameMenu->m_respawnOnApply);
    soldier.uniform = {1, -1, 1};
    gameMenu->m_respawnOnApply = false;
    T_Command().Run(16, false);
    CHECK(gameMenu->m_respawnOnApply);
    // Alive: nothing; the server: the time kept.
    human->m_stats[STAT_HEALTH] = 1;
    gameMenu->m_respawnOnApply = false;
    T_Command().Run(16, false);
    CHECK(!gameMenu->m_respawnOnApply);
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    g_game->m_roundTime = 999;
    T_Command().Run(16, false);
    CHECK(g_game->m_respawnWaveTime == 345);
    g_gameRole = GAME_ROLE_CLIENT;
    // No soldier (Fix): the menu's flag.
    human->m_stats[STAT_HEALTH] = 0;
    g_gameProfile.m_missionSquad->states.clear();
    T_Command().Run(16, false);
    CHECK(gameMenu->m_respawnOnApply);
    GetGameApp()->m_state = nullptr;
}

void TestClientFullState()
{
    // 21 with nothing: back to the lobby.
    C_NetGame &net = CommandNet(GAME_ROLE_CLIENT);
    T_State game(GAME_STATE_TYPE_GAME), menu(GAME_STATE_TYPE_GAME_MENU);
    GetGameApp()->m_state = &game;
    CHECK(T_Command().Bits(0, 1).Run(21, false) == 0 && Logged("state 1 pending 11"));
    // Else: the players (a client's list replaced; their humans made: started, health, the local one in the team,
    // spectating, a remote one's VIP), the modules', the objectives, the free camera, the in-game menu (the lobby
    // passed on), the message, the stage.
    SetupTeamManager();
    net.m_players.push_back(Player(99, "old"));
    g_openedMenu = &menu;
    C_HumanController *controller = RemoteController();
    g_madeSoldier = VirtualHuman(reinterpret_cast<void *>(&FakeStartMission), 77, 100);
    g_madeSoldier->m_controller = controller;
    g_controllerTypes[g_madeSoldier] = HUMAN_CONTROLLER_REMOTE;
    S_SoldierState s = SampleState();
    s.vip = true;
    s.spectating = true;
    T_Command c;
    c.Bits(1, 1).Bits(2, 8);
    c.Int(5).Str("Ann").Int(-1);
    S_NetStats stats;
    stats[NET_STAT_SCORE] = 12;
    stats.Write(&c.out);
    c.Int(6).Str("Bob").Int(31);
    stats[NET_STAT_SCORE] = 13;
    stats.Write(&c.out);
    c.Float(1).Float(2).Float(3).Float(0).Float(1).Float(0).State(s);
    c.Bits(0xD1, 8).Bits(0xD2, 8);
    g_localPlayerSlot = 1;
    net.m_unknown8 = false;
    g_log.clear();
    CHECK(c.Run(21, false) == 0);
    CHECK(net.m_players.size() == 2 && net.m_players[0].connection == 5 && std::strcmp(net.m_players[0].name, "Ann") == 0 &&
          net.m_players[0].stats[NET_STAT_SCORE] == 12 && net.m_players[1].connection == 6 &&
          net.m_players[1].stats[NET_STAT_SCORE] == 13);
    CHECK(Logged("defaults -1 -1 -1 1") && Logged("create soldier 1 31 1.0 2.0 3.0 / 0.0 1.0 0.0 / Joe team 1 profile 12 health 600 vip 1 belt 2 / 0 -1 0"));
    CHECK(Logged("start mission 77 0") && Logged("remote hit 14 0 600 -1 0.00 0.00") && Logged("team add 77") &&
          Logged("spectating 1") && static_cast<C_RemoteController *>(controller)->m_vip);
    CHECK(Logged("modules 209") && Logged("objectives 210") && Logged("camera 2") && Logged("open menu -2") &&
          GetGameApp()->m_state == &menu && Logged("menu message 2348"));
    CHECK(net.m_unknown8 && net.m_players[1].joinStage == 2 && !Logged("state 11 pending 11"));
    // The game going to the lobby: the menu too; not the local player's soldier: no team; not remote: no VIP.
    GetGameApp()->m_state = &game;
    g_gameState->SetPendingState(GAME_STATE_LOBBY);
    g_localPlayerSlot = 0;
    g_controllerTypes[g_madeSoldier] = HUMAN_CONTROLLER_PLAYER;
    static_cast<C_RemoteController *>(controller)->m_vip = false;
    net.m_unknown8 = false;
    g_log.clear();
    CHECK(c.Run(21, false) == 0);
    CHECK(Logged("state 11 pending 11") && !Logged("team add 77") && !static_cast<C_RemoteController *>(controller)->m_vip && net.m_players[0].joinStage == 2);
    // No soldier made: nothing more.
    g_madeSoldier = nullptr;
    g_log.clear();
    CHECK(c.Run(21, false) == 0 && !Logged("start mission 77 0"));
    // A server: its list kept, no humans.
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    net.m_players.assign(1, Player(42, "srv"));
    g_log.clear();
    CHECK(c.Run(21, false) == 0 && net.m_players.size() == 1 && net.m_players[0].connection == 42 &&
          !Logged("menu message 2348"));
    g_gameRole = GAME_ROLE_CLIENT;
    GetGameApp()->m_state = nullptr;
    g_localPlayerSlot = 0;
}

void RunNetCommandsTests()
{
    TestServerJoin();
    TestServerRespawnRequest();
    TestServerSmall();
    TestServerTeams();
    TestServerAdmin();
    TestServerJoinCount();
    TestClientJoin();
    TestClientSmall();
    TestClientRespawn();
    TestClientChat();
    TestClientTeams();
    TestClientRespawnWave();
    TestClientFullState();
}
