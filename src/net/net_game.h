// The network game (part 10d): C_NetGame (g_netGame), the session over DirectPlay 8 (hosting, joining, the
// players) and the game's data over the transport (part 10c): the messages each frame, the replication, the
// players' table, the teams and scores. Original files 0x00722b60..0x00733000. (The offsets are the original's;
// the port's containers are its own sizes.)
#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

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

// The voice chat's DirectPlay Voice client / server wrapper (vtable 0x00816598, part 10d-iii): its deleting
// destructor; the client's configuration set (the eight dwords of DVCLIENTCONFIG; `config`).
class C_NetVoice {
public:
    virtual ~C_NetVoice();                          // 0x007421c0
    int SetClientConfig(void *config);              // 0x007425e0
};
// An object C_NetGame keeps at +0x4EE0 (not identified; Shutdown destroys it through its vtable +0x0C).
class C_NetUnknownObject {
public:
    virtual void Unknown00() = 0;
    virtual void Unknown04() = 0;
    virtual void Unknown08() = 0;
    virtual void Destroy(bool free) = 0;            // +0x0C
};

// Server-side state per player (0xC4 bytes; C_NetGame::LockPlayers()).
struct S_NetPlayerState {
    S_NetPlayerState();                             // 0x00725e10
    int connection = 0;                             // +0x00 the DirectPlay player id (0: none)
    int unknown04 = 0;
    char name[0x34] = {};                           // +0x08 (size inferred) empty: a free slot
    class C_Human *human = nullptr;                 // +0x3C cleared when the human is destroyed
    // (+0x0C the round's score, +0x10 the ping's share of the lag, +0x14 the soldier class (SetPlayerClass),
    // +0x20 the total.)
    int stats[9] = {};                              // +0x40
    int team = 0;                                   // +0x64 E_HumanTeam
    int pendingTeam = 0;                            // +0x68 the team the player switches to
    C_NetConnection *link = nullptr;                // +0x6C its transport connection (the server's)
    int unknown70 = 0;
    // The server's handshake with it (0 none, 2 sending the game to it, 1 done).
    int joinStage = 0;                              // +0x74
    uint8_t unknown78[0x2A] = {};
    bool left = false;                              // +0xA2 the player left (C_game::OnPlayerLeft);
                                                    //       cleared when the actor is removed
    uint8_t unknownA3 = 0;
    // Pings over the server's limit (three: kicked).
    int highPings = 0;                              // +0xA4
    // The server: timeGetTime() of the player's last controls or request (the player
    // controller stamps it).
    uint32_t activityTime = 0;                      // +0xA8
    uint8_t unknownAC[4] = {};
    int livesUsed = 0;                              // +0xB0 co-op respawns (-1 before the first spawn)
    uint8_t unknownB4[0x10] = {};

    bool IsConnected() const { return name[0] != 0; }
    // No human, or its (player / remote) controller watches.
    bool IsSpectating() const;                      // 0x00725e90
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
    // Moves players between the teams towards the sizes the mission asks for.
    void BalanceTeams(int axis, int allied);        // 0x007306a0
    // The connected players (their count).
    int CountPlayers();                             // 0x00730410
    // Highest round score of the players in the game.
    int GetBestScore();                             // 0x0072f110
    void AddScore(int slot, int points);            // 0x0072f1a0
    // The stats of the player in `slot` (S_NetPlayerState::stats; not locked).
    int *GetPlayerStats(int slot);                  // 0x0072f890
    // The local player's stats (the slot g_netLocalSlot 0x00982104's).
    int *GetLocalPlayerStats();                     // 0x0072f8b0
    // Sets the soldier class of the player in `slot` (stats[5]) and prints its change (texts
    // 0x22C4 / 0x22C5 about the local player, 0x22C6 / 0x22C7 about the others, by whether it
    // went down).
    void SetPlayerClass(int soldierClass, int slot);   // 0x0072fe90
    // Server: the team's score (+0x44C, float per team).
    void AddTeamScore(int team, float points);      // 0x00730a20
    float GetTeamScore(int team) const { return m_teamScores[team]; }
    // Rounds won per team (+0x458, three ints; 0x0097d4c8).
    int *GetTeamWins() { return m_teamWins; }
    // Every player's round stats and the teams' scores cleared.
    void ResetScores();                             // 0x0072ebb0
    void SendScores();                              // 0x007301d0
    // A player's score sent to everyone (`all`: every field).
    void SendPlayerScore(int slot, bool all);       // 0x00730130
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
    void PingServers(void(__fastcall *callback)(S_ServerEntry *server), std::vector<S_ServerEntry *> &servers);   // 0x00724090
    // Windows XP's firewall keeps `port` closed (the main menu asks to open it; part 9h-xxi).
    bool FirewallBlocks(uint16_t port);             // 0x007328d0
    // Administration commands (NET_COMMAND_*; `unknown` is passed as 0).
    bool Command(int command, const char *arg, int unknown);   // 0x0072d680
    // The level's bounds (Command 3) widened to cover the frames and handed to the replication's quantisations.
    void SetLevelBounds(const float *min, const float *size);   // 0x0072f8d0
    // State of the connection to the server (NET_CONNECTION_LOST: nothing heard for 600 ms).
    int GetConnectionState();                       // 0x00731ac0
    // The server made (the session closed first; the name and DirectPlay's fields of `desc`; `unknown`:
    // the session menu passes true): < 0 failed.
    int Host(const S_NetSessionDesc &desc, bool unknown);   // 0x00722b60
    // A client connecting to the server at `address` (S_MultiplayerSettings +0x17C).
    int Connect(void *address);                     // 0x007240f0
    // A dedicated server made (the main menu's connect screen; `unknown` its +0x1F0): false failed.
    bool HostDedicated(bool unknown);               // 0x00722a80
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
    // The session's GameSpy reporting stopped (`unknown` 0, false).
    void StopReporting(int *unknown, bool flag);    // 0x00732730
    // A player kicked (`reason`: 2 its ping, 3 idle).
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
    uint8_t m_quantizations[3 * 0x80];              // +0x020 position, rate, delta (S_NetQuantization)
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
    int m_teamStats[3][9] = {};                     // +0x464
    S_NetVote m_vote;                               // +0x4D0
    int m_unknown4E4 = 0;
    uint8_t m_unknown4E8[0x4D3C - 0x4E8] = {};       // (+0x4F4 a vector freed by the destructor, +0x4E00 ...)
    uintptr_t m_searchSocket = static_cast<uintptr_t>(-1);   // +0x4D3C the LAN search's socket
    uint8_t m_unknown4D40[0xB8] = {};
    bool m_unknown4DF8 = false;                     // +0x4DF8
    uint32_t m_graphFlags = 0;                      // +0x4DFC IGraph's flags, put back by Close
    uint8_t m_unknown4E00[0xA0] = {};
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
    int m_unknown503C = 0;
    int m_unknown5040 = 0;
    int m_joinStatus = 0;                           // +0x5044 NET_JOIN_*
    int m_joinTime = 0;                             // +0x5048 ms
    int m_unknown504C[2] = {};
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
// C_NetGame::GetJoinStatus(): the client's joining (0 and 2, 6: still on its way).
enum : int {
    NET_JOIN_CONNECTED = 1, NET_JOIN_JOINING = 2, NET_JOIN_FULL = 3, NET_JOIN_BAD_PASSWORD = 4, NET_JOIN_BAD_CD_KEY = 5,
    NET_JOIN_BAD_VERSION = 7, NET_JOIN_TIMED_OUT = 8,
};
// The message types the streams carry besides the modules' (5 bits).
enum : uint32_t {
    NET_MESSAGE_END = 0x13,
    NET_MESSAGE_GAME = 0x14,                        // a game command: its 32-bit id, then its data
};
// The votable server commands' names (0x204 bytes each: "maxclients", ...).
extern char g_votableCommands[][0x204];             // 0x00846088
// The thread of a server status query (Close waits for it).
extern void *g_netQueryThread;                      // 0x009820f8
// The text a closed session's +0x502C string becomes.
extern char g_netDefaultSessionText[];              // 0x00867a90
// The GameSpy queries stopped (the SDK; `socket` null).
void StopGameSpyQueries(uintptr_t *socket);         // 0x00738760
// The transport's packet callback: the server queues what it gets as received, a client as outgoing.
void __fastcall TransportCallback(int connection, void *data, int size, int kind, uint32_t flags);   // 0x0072c730
// A frame's bounds widened into the level scan's (the frames' enumeration callback of Command 3).
uint32_t __stdcall ScanFrameBounds(class I3D_frame *frame, void *context);   // 0x0072d9c0
// The bits a quantised range of `range` steps needs (at least 1): `*steps` the range truncated.
int BitsForRange(uint32_t *steps, float range);     // 0x0072dcd0 (__fastcall; the range in ST0)
