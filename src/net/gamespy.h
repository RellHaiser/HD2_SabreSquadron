// The GameSpy SDK functions the network game calls (the query & reporting 2, the CD key and the server browser
// parts; ported in part 10f, 0x00733000..0x0073d260). The game's build of the SDK is __fastcall throughout; the
// port's is plain C++. Their GameSpy names are kept.
#pragma once

#include <cstdint>

struct S_Qr2Buffer;                                 // qr2_buffer_t: a reply being built
struct S_Qr2KeyBuffer;                              // qr2_keybuffer_t: the keys of a reply
struct S_ServerBrowser;                             // ServerBrowser
struct S_SbServer;                                  // SBServer: one server the browser found

// qr2's key types (the key-list and count callbacks).
enum : int {
    QR2_KEY_SERVER = 0,
    QR2_KEY_PLAYER = 1,
    QR2_KEY_TEAM = 2,
};
// The standard keys the game reports (qr2's numbering) and its own (0x33..0x35).
enum : int {
    QR2_HOSTNAME = 1,
    QR2_GAMEVER = 3,
    QR2_HOSTPORT = 4,
    QR2_MAPNAME = 5,
    QR2_GAMETYPE = 6,
    QR2_NUMPLAYERS = 8,
    QR2_MAXPLAYERS = 10,
    QR2_GAMEMODE = 11,
    QR2_PASSWORD = 19,
    QR2_PLAYER = 21,
    QR2_SCORE = 22,
    QR2_SKILL = 23,
    QR2_PING = 24,
    QR2_TEAM = 25,
    QR2_DEATHS = 26,
    QR2_TEAM_NAME = 28,
    QR2_TEAM_SCORE = 29,
    QR2_ISDEDICATED = 0x33,
    QR2_VOICECHAT = 0x34,
    QR2_EXPANSION = 0x35,
};

using Qr2ServerKeyCallback = void (*)(int key, S_Qr2Buffer *buffer, void *userData);
using Qr2PlayerKeyCallback = void (*)(int key, int index, S_Qr2Buffer *buffer, void *userData);
using Qr2TeamKeyCallback = void (*)(int key, int index, S_Qr2Buffer *buffer, void *userData);
using Qr2KeyListCallback = void (*)(int keyType, S_Qr2KeyBuffer *keys, void *userData);
using Qr2CountCallback = int (*)(int keyType, void *userData);
using Qr2AddErrorCallback = void (*)(int error, char *message, void *userData);
// The browser's news of a server (`reason` 1: one added or updated).
using ServerBrowserCallback = void (*)(S_ServerBrowser *browser, int reason, S_SbServer *server, void *instance);
// A CD key checked (`authenticated`; else `message` says why: "C..." the key is in use).
using GcdAuthCallback = void (*)(int gameId, int localId, int authenticated, char *message, void *instance);

void qr2_register_key(int key, const char *name);                                   // 0x00739750
// The reporting started (`record` null: the SDK's own) on `basePort`; < 0 failed.
int qr2_init(void *record, const char *ip, int basePort, const char *gameName, const char *secretKey, int isPublic,
             int natNegotiate, Qr2ServerKeyCallback serverKey, Qr2PlayerKeyCallback playerKey,
             Qr2TeamKeyCallback teamKey, Qr2KeyListCallback keyList, Qr2CountCallback count,
             Qr2AddErrorCallback addError, void *userData);                         // 0x00738500
void qr2_think(void *record);                                                        // 0x007385d0
void qr2_buffer_add(S_Qr2Buffer *buffer, const char *value);                         // 0x00738830
void qr2_buffer_add_int(S_Qr2Buffer *buffer, int value);                             // 0x00738800
void qr2_keybuffer_add(S_Qr2KeyBuffer *keys, int key);                               // 0x007387d0

int gcd_init(void *record, int gameId);                                              // 0x00733cb0
void gcd_think();                                                                    // 0x00734040
void gcd_authenticate_user(int gameId, int localId, uint32_t address, const char *challenge, const char *response,
                           GcdAuthCallback callback, void *instance);               // 0x00733d80
void gcd_disconnect_user(int gameId, int localId);                                   // 0x00733f40
// The answer to the server's `challenge` worked out from `cdKey` into `response`.
void gcd_compute_response(const char *cdKey, const char *challenge, char *response); // 0x00733af0

S_ServerBrowser *ServerBrowserNew(const char *queryForGame, const char *queryFromGame, const char *queryFromKey,
                                  int queryFromVersion, int maxUpdates, int queryVersion,
                                  ServerBrowserCallback callback, void *instance);  // 0x0073ab70
// The servers asked for (`fields` the keys of each).
void ServerBrowserUpdate(S_ServerBrowser *browser, int async, int disconnectOnComplete, const uint8_t *fields,
                         int fieldCount, const char *filter);                       // 0x0073af00
void ServerBrowserThink(S_ServerBrowser *browser);                                   // 0x0073b240
void ServerBrowserClear(S_ServerBrowser *browser);                                   // 0x0073b280
const char *SBServerGetStringValue(S_SbServer *server, const char *key, const char *fallback);   // 0x0073a3f0
int SBServerGetIntValue(S_SbServer *server, const char *key, int fallback);         // 0x0073a420
uint32_t SBServerGetPing(S_SbServer *server);                                        // 0x0073aa10
uint32_t SBServerGetPublicInetAddress(S_SbServer *server);                           // 0x0073a6a0

// The game's GameSpy ids: the game name, the secret key, the CD-key game id.
constexpr const char *kGameSpyName = "hd2";         // 0x00844bb0
constexpr const char *kGameSpyKey = "sK8pQ9";       // 0x00844ba8
constexpr int kGameSpyGameId = 0x2EE;
