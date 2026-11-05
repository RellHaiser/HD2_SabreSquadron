// The network game (part 10d): C_NetGame (g_netGame), the session over DirectPlay 8 (hosting, joining, the
// players) and the game's data over the transport (part 10c): the messages each frame, the replication, the
// players' table, the teams and scores. Original files 0x00722b60..0x00733000. (The offsets are the original's;
// the port's containers are its own sizes.)
#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "common/c_str.h"
#include "net/bitstream.h"

class C_Human;
class C_NetReplication;
class C_NetConnection;
class C_NetTransport;

// The game's handler of a transport packet (net/net_transport.h's).
using NetPacketCallback = void(__fastcall *)(int connection, void *data, int size, int kind, uint32_t flags);

// A critical section kept opaque here (windows.h stays out of the game's headers: its SendMessage and
// DispatchMessage macros would rename C_NetGame's methods).
class C_NetLock {
public:
    void Init();
    void Delete();
    void Enter();
    void Leave();

private:
    alignas(8) unsigned char m_storage[40];
};
struct IDirectPlay8Server;
struct IDirectPlay8Client;
struct S_ServerEntry;
struct S_ServerStatus;
struct S_NetSessionDesc;

struct IUnknown;
struct _GUID;
struct IDirectPlay8Address;
struct IDirectPlayVoiceClient;
struct IDirectPlayVoiceServer;
// A DirectPlay Voice message handler (PDVMESSAGEHANDLER).
using NetVoiceHandler = long(__stdcall *)(void *context, unsigned long type, void *message);

// The voice chat (DirectPlay Voice; vtable 0x00816598, 0x18 bytes): a client and / or a server on the session's
// DirectPlay object.
class C_NetVoice {
public:
    C_NetVoice(NetVoiceHandler clientHandler, NetVoiceHandler serverHandler);   // 0x00742190
    virtual ~C_NetVoice();                          // 0x007421e0 (deleting: 0x007421c0)
    // The server and / or the client started on `session` (`window` for the audio check and the client;
    // `sessionType`, `codec` the server's session; `clientConfig` the client's DVCLIENTCONFIG). < 0 failed.
    long Init(void *window, bool server, bool client, IUnknown *session, int sessionType, const _GUID *codec,
              void *clientConfig, int unused);  // 0x007421f0
    long StartServer(IUnknown *session, int sessionType, const _GUID *codec);   // 0x007423b0
    // The audio's setup checked (DirectPlay Voice's wizard when it must run).
    static long __stdcall CheckAudio(void *window); // 0x007422c0 (thiscall, `this` unused: RET 4)
    long StartClient(void *window, IUnknown *session, void *clientConfig, int unused);   // 0x00742460
    // The client's configuration set (the eight dwords of DVCLIENTCONFIG; no client: CO_E_NOTINITIALIZED).
    long SetClientConfig(void *config);             // 0x007425e0
    long Stop();                                    // 0x00742250 (both)
    long StopClient();                              // 0x00742610
    long StopServer();                              // 0x00742640

    IDirectPlayVoiceClient *m_client = nullptr;     // +0x04
    IDirectPlayVoiceServer *m_server = nullptr;     // +0x08
    NetVoiceHandler m_clientHandler;                // +0x0C
    NetVoiceHandler m_serverHandler;                // +0x10
    int m_halfDuplex = 0;                           // +0x14 the sound device works half duplex
};
// An object C_NetGame keeps at +0x4EE0 (not identified; Shutdown destroys it through its vtable +0x0C).
class C_NetUnknownObject {
public:
    virtual void Unknown00() = 0;
    virtual void Unknown04() = 0;
    virtual void Unknown08() = 0;
    virtual void Destroy(bool free) = 0;            // +0x0C
};

// A player's or a team's stats (9 ints, NET_STAT_*; read as an int array).
struct S_NetStats {
    int values[9] = {};

    operator int *() { return values; }
    operator const int *() const { return values; }
    // In a message: the score, kills, deaths, ping and rank in 11, 10, 10, 10, 5 bits (the ping clamped to 0..1023
    // in place first).
    void Write(C_BitStream *stream);                // 0x00749ed0
    void Read(C_BitStream *stream);                 // 0x00749f40
};

// Server-side state per player (0xC4 bytes; C_NetGame::LockPlayers()).
struct S_NetPlayerState {
    S_NetPlayerState();                             // 0x00725e10
    int connection = 0;                             // +0x00 the DirectPlay player id (0: none)
    uint32_t address = 0;                           // +0x04 the player's (INDICATE_CONNECT)
    char name[0x34] = {};                           // +0x08 (size inferred) empty: a free slot
    class C_Human *human = nullptr;                 // +0x3C cleared when the human is destroyed
    S_NetStats stats;                               // +0x40 NET_STAT_*
    int team = 0;                                   // +0x64 E_HumanTeam
    int pendingTeam = 0;                            // +0x68 the team the player switches to
    C_NetConnection *link = nullptr;                // +0x6C its transport connection (the server's)
    int keyQuery = 0;                               // +0x70 its CD-key check (-1: none)
    // The server's handshake with it (0 none, 2 sending the game to it, 1 done).
    int joinStage = 0;                              // +0x74
    int unknown78 = 0;
    int teamKills = 0;                              // +0x7C (three: kicked; ResetScores clears them)
    uint8_t unknown80[0x21] = {};
    bool admin = false;                             // +0xA1 may use the admin commands (IsPlayerAdmin)
    bool left = false;                              // +0xA2 the player left (C_game::OnPlayerLeft);
                                                    //       cleared when the actor is removed
    uint8_t unknownA3 = 0;
    // Pings over the server's limit (three: kicked).
    int highPings = 0;                              // +0xA4
    // The server: timeGetTime() of the player's last controls or request (the player
    // controller stamps it).
    uint32_t activityTime = 0;                      // +0xA8
    // The session bit of its packets (flipped each round, FlipSessionParity; the connection's +0x08 follows).
    uint32_t session = 0;                           // +0xAC
    int livesUsed = 0;                              // +0xB0 co-op respawns (-1 before the first spawn)
    void *voiceBuffer = nullptr;                    // +0xB4 its voice's IDirectSound3DBuffer (MakeVoiceBuffer)
    int unknownB8 = 50;                             // +0xB8
    uint8_t unknownBC[8] = {};

    bool IsConnected() const { return name[0] != 0; }
    // No human, or its (player / remote) controller watches.
    bool IsSpectating() const;                      // 0x00725e90
    // The name of its rank (stats[NET_STAT_RANK]): texts 0x2792 + rank (the allies'; a deathmatch: the uniform's
    // side) or 0x279C + rank.
    // (Fix: a C_str; the original hands back the text's characters, freed for an undefined text.)
    C_str GetRankName() const;                      // 0x00725ed0 (__fastcall)
};
// S_NetPlayerState::stats (and the teams' sums, C_NetGame +0x464).
enum : int {
    NET_STAT_KILLS = 1,
    NET_STAT_DEATHS = 2,
    NET_STAT_SCORE = 3,                             // the round's
    NET_STAT_PING = 4,                              // the server's: half the round trip (ms)
    NET_STAT_RANK = 5,                              // 0..7 by the score (10, 20, .. 80)
    NET_STAT_TOTAL_KILLS = 6,
    NET_STAT_TOTAL_DEATHS = 7,
    NET_STAT_TOTAL_SCORE = 8,
};

// A vote of the players (C_NetGame +0x4D0, 0x14 bytes): a server command (its kind indexes the votable commands'
// table at 0x00846088, 0x204 bytes each) and its argument; carried when half the players say yes, dropped after 15 s.
struct S_NetVote {
    S_NetVote();                                    // 0x0072dd30 (__fastcall)
    // Asked by a player (none running: told to everyone, message 0x16).
    void Start(int kind, int arg);                  // 0x0072dd70
    // Asked by the server (the result printed: text 0x22E2, "%s %d (%s)").
    void Begin(int kind, int arg);                  // 0x0072ddb0
    // The local player's yes (once: game command 0xB).
    bool Vote();                                    // 0x0072dfb0 (__fastcall)
    // The server counts a yes: over half the players: the command executed, the vote over.
    void CountYes();                                // 0x0072dfe0 (__fastcall)
    // Over (told to everyone, message 0x17: `passed` 0 / 1).
    void End(uint32_t passed);                      // 0x0072e070

    int kind = 0x42;                                // +0x00 0x42: none
    bool voted = false;                             // +0x04
    int time = 0;                                   // +0x08 ms
    int yes = 0;                                    // +0x0C
    int arg = 0;                                    // +0x10
};
// The vote's result printed (text 0x22E3 passed, 0x22E4 failed).
void PrintVoteResult(bool passed);                  // 0x0072de90
// A vote asked through the game app (command 10).
void PostVote(int kind, int arg);                   // 0x0072dd50

// A server's status (the net module's answer to QueryServerStatus; read by the main menu's game info screen,
// part 9h-xii): its players, its rules, the teams' scores.
struct S_StatusPlayer {                                 // 0x5C bytes
    // In a message: the stats (S_NetStats'), the team (32 bits), the name.
    void Write(C_BitStream *stream);                    // 0x00726260
    void Read(C_BitStream *stream);                     // 0x00726230

    int unknown00;
    int kills;                                          // +0x04 (the player's stats, NET_STAT_*)
    int unknown08;
    int points;                                         // +0x0C
    uint8_t unknown10[0x14];
    int team;                                           // +0x24 (0, 1, 2: its mark)
    char name[0x34];                                    // +0x28 (empty: no player)
};
struct S_ServerStatus {
    // In a message (part 10d-v writes it for the queries; a client reads it): the players (their count in 32
    // bits), the rules, the teams' scores (10 bits).
    void Write(C_BitStream *stream);                    // 0x007263c0
    void Read(C_BitStream *stream);                     // 0x00726380
    // The rules (+0x10 .. +0x5C; +0x40 not sent; the original's `this` is +0x10).
    void WriteRules(C_BitStream *stream);               // 0x00726120
    void ReadRules(C_BitStream *stream);                // 0x00726010

    int unknown00;
    std::vector<S_StatusPlayer> players;                // +0x04 (C_NetGame::FillServerStatus)
    int maxClients;                                     // +0x10
    int fragLimit;                                      // +0x14
    int roundLimit;                                     // +0x18
    int roundCount;                                     // +0x1C
    int preparationTime;                                // +0x20
    int respawnTime;                                    // +0x24
    int allowRespawn;                                   // +0x28
    int friendlyFire;                                   // +0x2C
    int autoTeamBalance;                                // +0x30
    int thirdPersonView;                                // +0x34
    int spawnProtectionTime;                            // +0x38
    int fallingDamage;                                  // +0x3C
    int unknown40;                                      // +0x40 (S_MultiplayerSettings +0x25C)
    int allowVehicles;                                  // +0x44
    int difficulty;                                     // +0x48 (1 .. 4)
    int lives;                                          // +0x4C
    int allowCrosshair;                                 // +0x50
    int spawnOnStart;                                   // +0x54
    int teamLives;                                      // +0x58
    int inverseDamage;                                  // +0x5C
    int axisScore;                                      // +0x60
    int alliedScore;                                    // +0x64
};

// The join request a client sends with DirectPlay's Connect (0x54 bytes).
struct S_JoinRequest {
    char password[0x14];                            // +0x00
    int versionMajor;                               // +0x14
    int versionMinor;                               // +0x18
    int unknown1C;                                  // +0x1C (2)
    char name[0x34];                                // +0x20
};

// A CD-key check over the key socket (0xB8 bytes): the server keeps one for each player asking (C_NetGame +0x55C,
// a hundred), the client one for itself (+0x4D40). The packets (XORed with 0xA0, a checksum after): the client's
// "c:<name>", the server's "c:<challenge>" every 200 ms, the client's "r:<response>:<name>", the server's verdict
// 'm' (good), 'e' (bad), 'u' (in use).
struct S_KeyQuery {
    char challenge[0x20] = {};                      // +0x00 the server's (8 random letters)
    char response[0x4C] = {};                       // +0x20 the client's answer (gcd_compute_response)
    uint32_t address = 0;                           // +0x6C the other side's
    int port = 0;                                   // +0x70
    int answered = 0;                               // +0x74 the server: the response came
    char name[0x34] = {};                           // +0x78 the player's (made unique)
    int state = 0;                                  // +0xAC KEY_*
    int expiry = 0;                                 // +0xB0 ms (the perf clock)
    int lastSend = 0;                               // +0xB4
};
enum : int {
    KEY_FREE = 0,
    KEY_CHALLENGED = 1,                             // the server sends its challenge; the client its response
    KEY_ASKING = 2,                                 // the client asks
    KEY_CHECKING = 3,                               // the server: GameSpy checks the response
    KEY_GOOD = 4,
    KEY_BAD = 5,
    KEY_IN_USE = 6,
    KEY_KEPT = 7,                                   // a good one taken by its player's connection (kept past the expiry)
};

// A received or queued datagram (the queues' nodes, 0x18 bytes).
struct S_NetQueued {
    int time;                                       // +0x08 clock()
    uint32_t flags;                                 // +0x0C the channel | the kind << 5 (0x20: replication)
    int size;                                       // +0x10
    void *data;                                     // +0x14 (the pool's)
};

class C_NetGame {
public:
    C_NetGame();                                    // 0x0072e1d0 (__fastcall)
    ~C_NetGame();                                   // 0x0072e3a0 (__fastcall)

    // The message of `type` (5 bits) begun on the stream of `channel` (0..2; the server's unreliable ones on
    // their own, `target` other than 0 too); CreateMessageEx: unreliable.
    C_BitStream *CreateMessage(int type, int channel);      // 0x0072c280
    C_BitStream *CreateMessageEx(int type, int channel);    // 0x0072c2a0 (other message kind)
    C_BitStream *BeginMessage(int target, int type, bool reliable, int channel);   // 0x0072c2c0
    // A game message (type 0x14, channel 0) of `id` (32 bits) to `to` (0: everyone); BeginCommand: an unreliable
    // one to everyone (PostCommand's), BeginReply: a reliable one (SendGameMessage's).
    C_BitStream *BeginGameMessage(int to, int id, bool reliable);   // 0x0072c1d0
    C_BitStream *BeginCommand(int id);              // 0x0072c190
    C_BitStream *BeginReply(int to, int id);        // 0x0072c1b0
    // A message to one player (its target), ended and sent (to itself: queued as received).
    // (SendNetMessage: windows.h's SendMessage macro would rename it where windows.h comes first.)
    void SendNetMessage(C_BitStream *msg);          // 0x0072c360
    // Player states, locked for the caller until UnlockPlayers(). (The original
    // returns the vector that holds them.)
    S_NetPlayerState *LockPlayers();                // 0x0072f040
    void UnlockPlayers();                           // 0x0072f100
    void Init();                                    // 0x0072e570
    void Shutdown();                                // 0x0072e830
    // A dedicated server's loading progress (`percent` 0..100) on its console.
    void PrintLoadProgress(int percent);            // 0x00731af0
    // The queued datagrams' messages handled (`beforeUpdate`: the server's received ones).
    void ReceiveMessages(int timeDelta, bool beforeUpdate);   // 0x0072c780
    void SendMessages(int timeDelta);               // 0x0072d240
    // Name of the player in `slot` (+0x08 of its state), null if there is none.
    const char *GetPlayerName(int slot);            // 0x0072efa0
    // A client: the state of `slot` (the table grown to it) gets the connection and the name.
    void SetPlayer(int slot, int connection, const char *name);   // 0x0072ed10
    // The slot of the connected player of DirectPlay id `connection` (-1: none).
    int FindPlayer(int connection);                 // 0x0072f060
    // The slot of the player named `name` (-1: none).
    int FindPlayerByName(const char *name);         // 0x0072eeb0
    // Teams of the players (E_HumanTeam; 2 for unknown slots).
    int GetPlayerTeam(int slot, bool pending);      // 0x00730900
    void SetPlayerTeam(int slot, int team, bool pending);   // 0x00730510
    int GetLocalTeam();                             // 0x00730880
    // Score keeping of a kill (`killer` -1: none).
    void OnPlayerKilled(int victim, int killer);    // 0x0072f230
    // Number of player slots (the states LockPlayers() returns).
    int GetPlayerCount() const { return static_cast<int>(m_players.size()); }
    C_Human *GetPlayerHuman(int slot);              // 0x0072eff0 (null: none)
    C_Human *GetLocalHuman();                       // 0x00730830
    // The player in `slot` watches (no human: true).
    bool IsPlayerSpectating(int slot);              // 0x007308a0
    // Some player of `team` has a living human that is not spectating.
    bool IsTeamAlive(int team);                     // 0x007305e0
    // Connected players with a human, by team.
    void CountTeamPlayers(int *axis, int *allies);  // 0x00730230
    // Players by team (`pending`: their next team; `inGame`: only those with a human).
    void CountTeams(int *axis, int *allies, bool pending, bool inGame);   // 0x00730320
    // The players' pending teams become their teams.
    void ApplyPendingTeams();                       // 0x007304a0
    // Moves players (from the last slot) between the teams while team 0's are more than one off `percent` of the
    // players (the first argument is not used: the mission's axis share; `percent` its allied one).
    void BalanceTeams(int unused, int percent);     // 0x007306a0
    // The connected players (their count).
    int CountPlayers();                             // 0x00730410
    // Highest round score of the players in the game.
    int GetBestScore();                             // 0x0072f110
    void AddScore(int slot, int points);            // 0x0072f1a0
    // The stats of the player in `slot` (S_NetPlayerState::stats; not locked).
    int *GetPlayerStats(int slot);                  // 0x0072f890
    // The local player's stats (the slot g_netLocalSlot 0x00982104's).
    int *GetLocalPlayerStats();                     // 0x0072f8b0
    // Sets the rank of the player in `slot` (stats[NET_STAT_RANK]) and prints its change (texts 0x22C4 / 0x22C5
    // about the local player, 0x22C6 / 0x22C7 about the others, by whether it went down).
    void SetPlayerRank(int rank, int slot);         // 0x0072fe90
    // Server: the team's score (+0x44C, float per team).
    void AddTeamScore(int team, float points);      // 0x00730a20
    float GetTeamScore(int team) const { return m_teamScores[team]; }
    // Rounds won per team (+0x458, three ints; 0x0097d4c8).
    int *GetTeamWins() { return m_teamWins; }
    // Every player's round stats and the teams' scores cleared.
    void ResetScores();                             // 0x0072ebb0
    void SendScores();                              // 0x007301d0
    // Server: the rank of the player in `slot` from its score; a new one sent by its controller (`lock`: the
    // players locked here).
    void SendPlayerScore(int slot, bool lock);      // 0x00730130
    // Server: the players' and teams' scores, the round's times and the co-op lives written (game message 2, part
    // 10d-iv); a client reads them (the server's stream read into a scratch block).
    void WriteScores(C_BitStream *stream);          // 0x0072fc50
    void ReadScores(C_BitStream *stream);           // 0x0072fa50
    // The player named `name` kicked (`reason` as KickPlayer).
    void KickPlayerByName(const char *name, int reason);   // 0x0072f5e0
    // The teams' sizes are within 1.5 of `axis` : `allied` (`pending`: the players' next teams).
    bool TeamsBalanced(int axis, int allied, bool pending);   // 0x00730590
    // The players and the teams' kills and scores printed (C_game::Print: nothing in the release build).
    void PrintTeams();                              // 0x00730a50 (__fastcall; not called)
    // A server's status for a query: its players, its rules, the teams' scores.
    void FillServerStatus(S_ServerStatus *status);  // 0x00730be0
    // Flips bit 0 of the players' +0xAC (and of the copy their connection holds) each round
    // (meaning not identified).
    void FlipSessionParity();                       // 0x00730e10
    // Every player's human and statistics, the replicated objects, the teams' scores and wins.
    void ResetPlayerStats();                        // 0x0072ec60
    void ResetReplication(bool all);                // 0x007319e0
    // Sends and receives for `timeDelta` ms now (1000: everything pending; `replicate`: the replication's data
    // written for the players).
    void Process(int timeDelta, bool replicate);    // 0x0072c9a0
    // Every 3 s: the connections' resend times and the objects' lags from the pings (the server kicks players
    // pinging over its limit three times or idle too long).
    void UpdateLags(int timeDelta);                 // 0x0072cde0 (the time unused)
    // Server: the players ready for it sent the game (the handshake, game messages 0, 9).
    void SendJoinData();                            // 0x0072d120 (__fastcall)
    // `data` to the player in `slot` (-1: all; the server) or to the server (a client), with `flags` (the channel
    // in the low 2 bits, the kind >> 5).
    void SendToPlayers(int slot, const void *data, int size, uint32_t flags);   // 0x0072d4b0
    // `data` queued as received / to process locally (locked; a pool copy, the time of clock()).
    void QueueReceived(const void *data, int size, uint32_t flags);   // 0x0072c540
    void QueueOutgoing(const void *data, int size, uint32_t flags);   // 0x0072c600
    // A message of a queued stream handled (false: the end, or not handled): a game command (the clients' handler
    // or the server's) or a module's message (DispatchMessage or CallUnknown28 of the module manager).
    static bool DispatchNetMessage(C_BitStream *stream, bool toClient);   // 0x0072c480
    // Ends the session (`now` false: at the next frame).
    void Close(bool now);                           // 0x0072e8b0
    // A server's status asked for on a thread of its own; `callback` gets it there (part 9h-xii).
    int QueryServerStatus(void(__fastcall *callback)(S_ServerStatus *status), S_ServerEntry *server);   // 0x007240d0
    // A server pinged (`callback`, the net thread's, gets it when it answers; part 9h-xix).
    void PingServer(void(__fastcall *callback)(S_ServerEntry *server), S_ServerEntry *server);   // 0x00724070
    // The search's answers read (its socket polled; the browser's tick, every 10 ms; part 9h-xx).
    void PollSearch();                              // 0x00723610 (__fastcall)
    // The LAN searched / the servers listed pinged (`callback` gets each answering; part 9h-xxi).
    void SearchServers(void(__fastcall *callback)(S_ServerEntry *server));   // 0x00724040
    // The search started: GameSpy's LAN browser (the settings' listing 1), else DirectPlay's EnumHosts. 0.
    int StartSearch();                              // 0x00724010 (__fastcall)
    // GameSpy's LAN browser made (or cleared) and asked for the servers' keys; the role 0x12. 1.
    int StartLanSearch();                           // 0x00723620 (__fastcall)
    // DirectPlay's hosts enumerated (`server` null: the LAN's broadcast) with `request` (4 bytes the servers read:
    // 0 a search or a ping, 1 a status query, 2 a list's ping). < 0 failed.
    int EnumHosts(S_ServerEntry *server, int request);   // 0x00723e80
    // The server's GameSpy reporting (qr2 on the port + 3, the CD keys' gcd) and its key socket started.
    void StartReporting();                          // 0x00723d20 (__fastcall)
    // The key socket (UDP, the port + 2) opened, the hundred checks cleared.
    void OpenKeyServer();                           // 0x00730f00 (__fastcall)
    // A key packet waiting (none: false): its data (unXORed; `size` in: the room, out: the length without the
    // checksum), the sender's address and port.
    bool ReceiveKeyPacket(char *data, int *size, uint32_t *address, uint32_t *port);   // 0x00730fa0
    // `data` (`size` bytes, 508 at most) XORed with 0xA0, its checksum after, to `address`:`port`.
    void SendKeyPacket(const char *data, int size, uint32_t address, uint16_t port);   // 0x007310e0
    // The server's key checks: the expired dropped, the due packets sent, one received ("c": a new or refreshed
    // check; "r": the response handed to GameSpy).
    void ProcessKeyQueries();                       // 0x00731250 (__fastcall)
    // A client's check of its CD key with the server `server` begun (its key socket, the port + 2).
    void StartKeyQuery(const S_ServerEntry *server);   // 0x007316f0
    // ... run until the server's verdict: true good; bad / in use / 15 s: the join status 5 / 6 / 8, false.
    bool RunKeyQuery();                             // 0x007317a0 (__fastcall)
    // The server's check in `index` dropped (GameSpy told).
    void DropKeyQuery(int index);                   // 0x00731990
    // The lowest number from `number` that makes `name` (n >= 2: "<name><n>") no player's (the players locked by
    // the caller).
    int FindFreeNameNumber(const char *name, int number);   // 0x00724c20
    void PingServers(void(__fastcall *callback)(S_ServerEntry *server), std::vector<S_ServerEntry *> &servers);   // 0x00724090
    // Windows XP's firewall keeps `port` closed (the main menu asks to open it; part 9h-xxi).
    bool FirewallBlocks(uint16_t port);             // 0x007328d0
    // Administration commands (NET_COMMAND_*; `unknown` is passed as 0).
    bool Command(int command, const char *arg, int unknown);   // 0x0072d680
    // The level's bounds (Command 3) widened to cover the frames and handed to the replication's quantisations.
    void SetLevelBounds(const float *min, const float *size);   // 0x0072f8d0
    // State of the connection to the server (NET_CONNECTION_LOST: nothing heard for 600 ms).
    int GetConnectionState();                       // 0x00731ac0
    // The server made (the session closed first; the name, the service provider and the password of `desc`;
    // `firewall`: Windows XP's firewall opened for its port): < 0 failed (m_lastError / m_lastErrorText).
    int Host(const S_NetSessionDesc &desc, bool firewall);   // 0x00722b60
    // A client connecting to `server` (S_MultiplayerSettings +0x17C; its port 31567 without GameSpy's listing;
    // the CD key checked first with it): < 0 failed.
    int Connect(S_ServerEntry *server);             // 0x007240f0
    // A dedicated server started (its description written, hd2ds_sabresquadron.exe run; the main menu's connect
    // screen): false failed.
    bool HostDedicated(bool firewall);              // 0x00722a80
    // DirectPlay's client made and initialised (once).
    long CreateClient();                            // 0x00723e00 (__fastcall)
    // DirectPlay addresses on TCP/IP: `device` only that, `host` the server's address and port (none: the
    // broadcast's, port 31567).
    void CreateAddresses(const S_ServerEntry *server, IDirectPlay8Address **device, IDirectPlay8Address **host);   // 0x00723700
    // The server accepted the client: joined, its voice, the replication.
    void OnConnected();                             // 0x007244e0 (__fastcall)
    // The voice chat's server (the settings' codec) / client (on the server's DirectPlay object or the client's).
    void StartVoiceServer();                        // 0x00724520 (__fastcall)
    void StartVoiceClient(bool onServer);           // 0x00724740
    // The voice of the player in `slot` given a 3D buffer (5 m to 20 m; not called).
    void MakeVoiceBuffer(int slot);                 // 0x00724840
    // (The players visited under the lock, nothing done; not called.)
    void VisitPlayers();                            // 0x007248f0 (__fastcall)
    // The client's DirectPlay messages (the session's; `net` g_netGame): the connection, a host's answer, data, the
    // end. HandleServerMessage: the server's (part 10d-v).
    static long __stdcall HandleClientMessage(C_NetGame *net, void *context, uint32_t type, void *message);   // 0x00722550
    static long __stdcall HandleServerMessage(C_NetGame *net, void *context, uint32_t type, void *message);   // 0x00726470
    // Windows XP's firewall: `port` opened (`open`; the server's or the client's) or the game's ports closed.
    void ConfigureFirewall(int port, bool open);    // 0x00732730
    // The client's joining (+0x5044; NET_JOIN_*).
    int GetJoinStatus() const { return m_joinStatus; }   // 0x007244c0
    // Server: the vote's timeout (15 s: failed, message 0x17).
    void UpdateVote(int timeDelta);                 // 0x0072e0a0
    // The voice chat value the player options give ((int)(option * 0.01)) to the voice
    // object at +0x0C (meaning not identified).
    void SetVoiceValue(int value);                  // 0x0072edf0
    // The voice chat transmits to the local player's team ("Set Transmition team").
    void UpdateVoiceTeam();                         // 0x00724940
    void StopVoiceClient();                         // 0x00724820 (__fastcall)
    void StopVoiceServer();                         // 0x00724720 (__fastcall)
    // A chat line (its first 99 characters; `team`: to the local player's team) posted to the app
    // (command 3) to send. True.
    bool SendChat(const char *text, bool team);     // 0x0072ee60
    // The replication made / deleted (its quantisations, the local object's id).
    void MakeReplication();                         // 0x007233d0 (__fastcall)
    void DeleteReplication();                       // 0x007235f0 (__fastcall)
    // A player kicked: told to everyone (the server; game message 0x11) and printed (`reason`: 0 by the server,
    // 1 team kills, 2 its ping, 3 idle; texts 0x22AB, 0x22AA, 0x22AD, 0x22AC).
    void KickPlayer(int slot, int reason);          // 0x0072f6c0
    // The LAN listing's answering of a server (the session menu's listing kind 1).
    void AnswerQueries();                           // 0x00723dc0 (__fastcall)
    // The bandwidth figures of the last 100 ms (+0x4FEC's seven).
    void TakeBandwidth(int *figures);               // 0x00724d30

    IDirectPlay8Server *m_server = nullptr;         // +0x000
    IDirectPlay8Client *m_client = nullptr;         // +0x004
    bool m_unknown8 = false;                        // +0x008
    class C_NetVoice *m_voiceClient = nullptr;      // +0x00C
    class C_NetVoice *m_voiceServer = nullptr;      // +0x010
    C_NetReplication *m_replication = nullptr;      // +0x014
    S_NetQuantization m_quantizations[3];           // +0x020 position, rate, delta (SetLevelBounds: the first)
    C_NetMessage m_replicationMessage;              // +0x1A0
    std::vector<S_NetPlayerState> m_players;        // +0x1E0
    std::list<S_NetQueued> m_received;              // +0x1F0
    std::list<S_NetQueued> m_outgoing;              // +0x1FC
    C_NetMessage m_messages[3];                     // +0x208 by channel
    C_NetMessage m_unreliable;                      // +0x2BC the server's unreliable messages
    C_NetMessage m_direct;                          // +0x2F8 the targeted ones
    C_NetConnection *m_serverLink = nullptr;        // +0x334 a client's connection to the server
    int m_unknown338 = 0;
    C_NetTransport *m_transport = nullptr;          // +0x33C (by value in the original: made by the constructor)
    NetPacketCallback m_packetCallback = nullptr;   // +0x414 (TransportCallback)
    C_NetLock m_playersLock;                        // +0x418
    C_NetLock m_queueLock;                          // +0x430
    int m_unknown448 = 0;
    float m_teamScores[3] = {};                     // +0x44C
    int m_teamWins[3] = {};                         // +0x458
    S_NetStats m_teamStats[3];                      // +0x464
    S_NetVote m_vote;                               // +0x4D0
    struct S_ServerBrowser *m_serverBrowser = nullptr;   // +0x4E4 the LAN search's (GameSpy)
    uint8_t m_unknown4E8[4] = {};
    void(__fastcall *m_searchCallback)(S_ServerEntry *server) = nullptr;   // +0x4EC each server found
    void(__fastcall *m_statusCallback)(S_ServerStatus *status) = nullptr;  // +0x4F0 a server's status
    S_ServerStatus m_status = {};                   // +0x4F4 the status a query read
    S_KeyQuery m_keyQueries[100];                   // +0x55C the server's CD-key checks
    uintptr_t m_keySocket = static_cast<uintptr_t>(-1);   // +0x4D3C the CD keys' socket
    S_KeyQuery m_keyClient;                         // +0x4D40 the client's own check
    bool m_reporting = false;                       // +0x4DF8 the server reports to GameSpy (AnswerQueries)
    uint32_t m_graphFlags = 0;                      // +0x4DFC IGraph's flags, put back by Close
    alignas(4) uint8_t m_serverEntry[0xA0] = {};   // +0x4E00 S_ServerEntry: the server joined (or hosted)
    float m_levelMin[4] = {};                       // +0x4EA0 (Command 3; w: unset)
    float m_levelSize[4] = {};                      // +0x4EB0
    int m_levelBits[3] = {};                        // +0x4EC0 bits for each axis of the level
    uint32_t m_levelSteps[3] = {};                  // +0x4ECC
    uint8_t m_unknown4ED8[8] = {};
    class C_NetUnknownObject *m_unknown4EE0 = nullptr;   // +0x4EE0 (deleted by Shutdown, vtable +0x0C)
    int16_t m_receivedHistory[50] = {};             // +0x4EE4 bytes received a 100 ms
    int16_t m_sentHistory[50] = {};                 // +0x4F48
    int m_historyIndex = 0;                         // +0x4FAC
    int m_historyIndex2 = 0;                        // +0x4FB0
    int m_bandwidthTotal[7] = {};                   // +0x4FB4 summed (scaled every 6th)
    int m_bandwidthAverage[7] = {};                 // +0x4FD0
    int m_bandwidthLast[7] = {};                    // +0x4FEC
    int m_unknown5008[4] = {};
    bool m_locksMade = false;                       // +0x5018
    std::string m_string501C;                       // +0x501C
    std::string m_string502C;                       // +0x502C (Close: the text at 0x00867a90)
    uint32_t m_enumHandle = 0;                      // +0x503C DirectPlay's EnumHosts operation
    int m_unknown5040 = 0;
    int m_joinStatus = 0;                           // +0x5044 NET_JOIN_*
    int m_joinTime = 0;                             // +0x5048 ms
    long m_lastError = 0;                           // +0x504C the last DirectPlay failure's
    const char *m_lastErrorText = nullptr;          // +0x5050
    int m_sendPeriod = 50;                          // +0x5054 ms
    int m_sendTime = 0;                             // +0x5058
    int m_unknown505C = 0;
    bool m_unknown5060 = false;                     // +0x5060
    int m_maxPing = 0;                              // +0x5064 the server's limit (0: none)
    int m_unknown5068 = 50;                         // +0x5068
    int m_maxInactivity = 300000;                   // +0x506C ms
    bool m_closePending = false;                    // +0x5070
};
// C_NetGame::Command() commands.
enum : int {
    NET_COMMAND_KICK_NAME = 0,
    NET_COMMAND_KICK_SLOT = 1,
    NET_COMMAND_SCAN_LEVEL = 3,                     // the level's bounds (g_levelMinY)
};
enum : int {
    NET_CONNECTION_LOST = 9,
};
extern C_NetGame g_netGame;                         // 0x0097d070
// C_NetGame::GetJoinStatus(): the client's joining (0 and 2: still on its way).
enum : int {
    NET_JOIN_CONNECTED = 1, NET_JOIN_JOINING = 2, NET_JOIN_FULL = 3, NET_JOIN_BAD_PASSWORD = 4, NET_JOIN_BAD_CD_KEY = 5,
    NET_JOIN_KEY_IN_USE = 6, NET_JOIN_BAD_VERSION = 7, NET_JOIN_TIMED_OUT = 8,
};
// The message types the streams carry besides the modules' (5 bits).
enum : uint32_t {
    NET_MESSAGE_END = 0x13,
    NET_MESSAGE_GAME = 0x14,                        // a game command: its 32-bit id, then its data
};
// The votable server commands' names (0x204 bytes each: "maxclients", ...).
extern char g_votableCommands[][0x204];             // 0x00846088
// The random name RandomName makes (32 letters at most).
extern char g_randomName[33];                       // 0x0097d048
// `length` (32 at most) random lowercase letters.
const char *RandomName(int length);                 // 0x007311b0 (__fastcall)
// The GameSpy browser's news: a server added or updated becomes an entry for the main menu.
void OnBrowserCallback(struct S_ServerBrowser *browser, int reason, struct S_SbServer *server, void *instance);   // 0x00723190
void AddBrowserServer(struct S_SbServer *server);   // 0x007231b0
// The server's GameSpy reporting: its keys, the players', the teams', which keys, their counts, the errors.
void ReportServerKey(int key, struct S_Qr2Buffer *buffer, void *userData);    // 0x007237d0
void ReportPlayerKey(int key, int index, struct S_Qr2Buffer *buffer, void *userData);   // 0x00723a40
void ReportTeamKey(int key, int index, struct S_Qr2Buffer *buffer, void *userData);     // 0x00723b70
void ReportKeyList(int keyType, struct S_Qr2KeyBuffer *keys, void *userData);   // 0x00723bb0
int ReportCount(int keyType, void *userData);       // 0x00723cc0
void ReportError(int error, char *message, void *userData);   // 0x00723cf0 (nothing)
// A CD key checked by GameSpy: the check's state (good, in use: a message "C...", bad).
void OnKeyChecked(int gameId, int localId, int authenticated, char *message, void *instance);   // 0x007311f0

// The thread of a server status query (Close waits for it).
extern void *g_netQueryThread;                      // 0x009820f8
// The text a closed session's +0x502C string becomes.
extern char g_netDefaultSessionText[];              // 0x00867a90
// The player in `slot` may use the admin commands (+0xA1; false for a free slot).
bool IsPlayerAdmin(int slot);                       // 0x007307e0
// The player in `slot` has lives left (co-op: its own or the team's against the lives; otherwise the respawn
// rule).
bool CanPlayerRespawn(int slot);                    // 0x00730970
// DirectPlay's message handlers: the client's (not in role 7) and the server's (the server only) on g_netGame; the
// voice's (nothing).
long __stdcall ClientMessageHandler(void *context, unsigned long type, void *message);   // 0x00722520
long __stdcall ServerMessageHandler(void *context, unsigned long type, void *message);   // 0x00726440
long __stdcall VoiceClientHandler(void *context, unsigned long type, void *message);     // 0x00722510
long __stdcall VoiceServerHandler(void *context, unsigned long type, void *message);     // 0x00726430
// Text between the ANSI / UTF-8 code pages and wide characters (`length` -1: the whole text; the last character
// of the room the terminator).
void NetAnsiToWide(wchar_t *out, const char *text, unsigned length);    // 0x00724ad0
void NetWideToAnsi(char *out, const wchar_t *text, int length);         // 0x00724b30
void NetUtf8ToWide(wchar_t *out, const char *text, unsigned length);    // 0x00724b80
void NetWideToUtf8(char *out, const wchar_t *text, int length);         // 0x00724bd0
// DirectPlay is missing (always false).
bool DirectPlayMissing();                           // 0x00724b20
// The address a DirectPlay address names ("hostname" a.b.c.d; a in the low byte).
uint32_t GetHostAddress(IDirectPlay8Address *address);   // 0x00725190

// The quantisations the net library keeps by index (0x00982de8, vtable 0x00816380; 0 the level, set by
// SetLevelBounds; nothing reads them).
class C_NetQuantizationTable {
public:
    C_NetQuantizationTable();                       // 0x0073cfa0 (the defaults, 0x0073d0c0)
    virtual ~C_NetQuantizationTable();              // 0x0073cff0 (deleting: 0x0073cfd0)
    // Entry `index` (the table doubled while too small) set up from `span`, `from`, `step` (100).
    void Set(int index, const float *span, const float *from, const float *step);   // 0x0073d080
    std::vector<S_NetQuantization> m_entries;       // +0x04 (10 at first; aligned blocks of 0x80 bytes)
};
extern C_NetQuantizationTable g_netQuantizations;   // 0x00982de8
// The GameSpy queries stopped (the SDK; `socket` null).
void StopGameSpyQueries(uintptr_t *socket);         // 0x00738760
// The transport's packet callback: the server queues what it gets as received, a client as outgoing.
void __fastcall TransportCallback(int connection, void *data, int size, int kind, uint32_t flags);   // 0x0072c730
// A frame's bounds widened into the level scan's (the frames' enumeration callback of Command 3).
uint32_t __stdcall ScanFrameBounds(class I3D_frame *frame, void *context);   // 0x0072d9c0
// The bits a quantised range of `range` steps needs (at least 1): `*steps` the range truncated.
int BitsForRange(uint32_t *steps, float range);     // 0x0072dcd0 (__fastcall; the range in ST0)
