// The DirectPlay 8 interfaces C_NetGame uses (part 10d): the MSVC SDK no longer ships dplay8.h, so they are
// declared here by hand in their COM vtable order (DirectX 8.1's dplay8.h). Parameters the game does not look
// into are left as void pointers.
#pragma once

#include <cstdint>

#include <windows.h>
#include <unknwn.h>

using DPNID = uint32_t;
using DPNHANDLE = uint32_t;
using PFNDPNMESSAGEHANDLER = HRESULT(WINAPI *)(void *context, DWORD messageType, void *message);

// IDirectPlay8Server (the session's host).
struct IDirectPlay8Server : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Initialize(void *context, PFNDPNMESSAGEHANDLER handler, DWORD flags) = 0;   // +0x0C
    virtual HRESULT STDMETHODCALLTYPE EnumServiceProviders(const GUID *sp, const GUID *application, void *buffer,
                                                          DWORD *size, DWORD *count, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE CancelAsyncOperation(DPNHANDLE handle, DWORD flags) = 0;   // +0x14
    virtual HRESULT STDMETHODCALLTYPE GetSendQueueInfo(DPNID player, DWORD *messages, DWORD *bytes, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetApplicationDesc(void *desc, DWORD *size, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetServerInfo(const void *info, void *context, DPNHANDLE *handle, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClientInfo(DPNID player, void *info, DWORD *size, DWORD flags) = 0;   // +0x24
    virtual HRESULT STDMETHODCALLTYPE GetClientAddress(DPNID player, void **address, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLocalHostAddresses(void **addresses, DWORD *count, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetApplicationDesc(const void *desc, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Host(const void *desc, void **addresses, DWORD count, const void *security,
                                           const void *credentials, void *context, DWORD flags) = 0;   // +0x34
    virtual HRESULT STDMETHODCALLTYPE SendTo(DPNID player, const void *buffers, DWORD count, DWORD timeout,
                                             void *context, DPNHANDLE *handle, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateGroup(const void *info, void *context, void *asyncContext,
                                                  DPNHANDLE *handle, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE DestroyGroup(DPNID group, void *context, DPNHANDLE *handle, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddPlayerToGroup(DPNID group, DPNID player, void *context, DPNHANDLE *handle,
                                                       DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemovePlayerFromGroup(DPNID group, DPNID player, void *context,
                                                            DPNHANDLE *handle, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetGroupInfo(DPNID group, void *info, void *context, DPNHANDLE *handle,
                                                   DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGroupInfo(DPNID group, void *info, DWORD *size, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumPlayersAndGroups(DPNID *ids, DWORD *count, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumGroupMembers(DPNID group, DPNID *ids, DWORD *count, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Close(DWORD flags) = 0;                                   // +0x5C
    virtual HRESULT STDMETHODCALLTYPE DestroyClient(DPNID player, const void *data, DWORD size, DWORD flags) = 0;   // +0x60
    virtual HRESULT STDMETHODCALLTYPE ReturnBuffer(DPNHANDLE buffer, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlayerContext(DPNID player, void **context, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGroupContext(DPNID group, void **context, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCaps(void *caps, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCaps(const void *caps, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSPCaps(const GUID *sp, const void *caps, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSPCaps(const GUID *sp, void *caps, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectionInfo(DPNID player, void *info, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterLobby(void *lobby, void *application, DWORD flags) = 0;
};

// IDirectPlay8Client (a player's connection to the host).
struct IDirectPlay8Client : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Initialize(void *context, PFNDPNMESSAGEHANDLER handler, DWORD flags) = 0;   // +0x0C
    virtual HRESULT STDMETHODCALLTYPE EnumServiceProviders(const GUID *sp, const GUID *application, void *buffer,
                                                          DWORD *size, DWORD *count, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumHosts(void *desc, void *host, void *device, const void *data, DWORD size,
                                                DWORD count, DWORD interval, DWORD timeout, void *context,
                                                DPNHANDLE *handle, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE CancelAsyncOperation(DPNHANDLE handle, DWORD flags) = 0;   // +0x18
    virtual HRESULT STDMETHODCALLTYPE Connect(const void *desc, void *host, void *device, const void *security,
                                              const void *credentials, const void *data, DWORD size, void *context,
                                              DPNHANDLE *handle, DWORD flags) = 0;   // +0x1C
    virtual HRESULT STDMETHODCALLTYPE Send(const void *buffers, DWORD count, DWORD timeout, void *context,
                                           DPNHANDLE *handle, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSendQueueInfo(DWORD *messages, DWORD *bytes, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetApplicationDesc(void *desc, DWORD *size, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClientInfo(const void *info, void *context, DPNHANDLE *handle, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetServerInfo(void *info, DWORD *size, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetServerAddress(void **address, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Close(DWORD flags) = 0;                                   // +0x38
    virtual HRESULT STDMETHODCALLTYPE ReturnBuffer(DPNHANDLE buffer, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCaps(void *caps, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCaps(const void *caps, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSPCaps(const GUID *sp, const void *caps, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSPCaps(const GUID *sp, void *caps, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectionInfo(void *info, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterLobby(void *lobby, void *application, DWORD flags) = 0;
};

// IDirectPlay8Address.
struct IDirectPlay8Address : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE BuildFromURLW(const wchar_t *url) = 0;          // +0x0C
    virtual HRESULT STDMETHODCALLTYPE BuildFromURLA(const char *url) = 0;
    virtual HRESULT STDMETHODCALLTYPE Duplicate(IDirectPlay8Address **copy) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEqual(IDirectPlay8Address *other) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsEqual(IDirectPlay8Address *other) = 0;
    virtual HRESULT STDMETHODCALLTYPE Clear() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetURLW(wchar_t *url, DWORD *size) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetURLA(char *url, DWORD *size) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSP(GUID *sp) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUserData(void *data, DWORD *size) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSP(const GUID *sp) = 0;                      // +0x34
    virtual HRESULT STDMETHODCALLTYPE SetUserData(const void *data, DWORD size) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNumComponents(DWORD *count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetComponentByName(const wchar_t *name, void *buffer, DWORD *size,
                                                         DWORD *type) = 0;           // +0x40
    virtual HRESULT STDMETHODCALLTYPE GetComponentByIndex(DWORD index, wchar_t *name, DWORD *nameLength, void *buffer,
                                                          DWORD *size, DWORD *type) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddComponent(const wchar_t *name, const void *data, DWORD size,
                                                   DWORD type) = 0;                  // +0x48
    virtual HRESULT STDMETHODCALLTYPE GetDevice(GUID *device) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDevice(const GUID *device) = 0;              // +0x50
};

// DirectPlay Voice: the client, the server, the audio check.
using PDVMESSAGEHANDLER = HRESULT(WINAPI *)(void *context, DWORD messageType, void *message);
struct IDirectPlayVoiceClient : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Initialize(IUnknown *session, PDVMESSAGEHANDLER handler, void *context,
                                                 DWORD *notifyMask, DWORD count) = 0;   // +0x0C
    virtual HRESULT STDMETHODCALLTYPE Connect(void *soundConfig, void *clientConfig, DWORD flags) = 0;   // +0x10
    virtual HRESULT STDMETHODCALLTYPE Disconnect(DWORD flags) = 0;                    // +0x14
    virtual HRESULT STDMETHODCALLTYPE GetSessionDesc(void *desc) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClientConfig(void *config) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClientConfig(void *config) = 0;              // +0x20
    virtual HRESULT STDMETHODCALLTYPE GetCaps(void *caps) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCompressionTypes(void *buffer, DWORD *size, DWORD *count, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTransmitTargets(const DWORD *targets, DWORD count, DWORD flags) = 0;   // +0x2C
    virtual HRESULT STDMETHODCALLTYPE GetTransmitTargets(DWORD *targets, DWORD *count, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Create3DSoundBuffer(DWORD id, void *buffer, DWORD priority, DWORD flags,
                                                          void **user3DBuffer) = 0;  // +0x34
    virtual HRESULT STDMETHODCALLTYPE Delete3DSoundBuffer(DWORD id, void **user3DBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetNotifyMask(DWORD *mask, DWORD count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSoundDeviceConfig(void *config, DWORD *size) = 0;   // +0x40
};
struct IDirectPlayVoiceServer : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Initialize(IUnknown *session, PDVMESSAGEHANDLER handler, void *context,
                                                 DWORD *notifyMask, DWORD count) = 0;   // +0x0C
    virtual HRESULT STDMETHODCALLTYPE StartSession(void *desc, DWORD flags) = 0;      // +0x10
    virtual HRESULT STDMETHODCALLTYPE StopSession(DWORD flags) = 0;                   // +0x14
};
struct IDirectPlayVoiceTest : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CheckAudioSetup(const GUID *playback, const GUID *capture, HWND window,
                                                      DWORD flags) = 0;              // +0x0C
};
// IDirectSound3DBuffer (the two setters the voice uses).
struct IDirectSound3DBufferLite : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Unused0C() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused10() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused14() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused18() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused1C() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused20() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused24() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused28() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused2C() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused30() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused34() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused38() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused3C() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMaxDistance(float distance, DWORD apply) = 0;   // +0x40
    virtual HRESULT STDMETHODCALLTYPE SetMinDistance(float distance, DWORD apply) = 0;   // +0x44
};

// The structures the game fills (their sizes and the fields it sets).
struct DPN_APPLICATION_DESC {                           // 0x48
    DWORD size = 0x48;
    DWORD flags = 0;                                    // 1: client / server
    GUID instance = {};
    GUID application = {};
    DWORD maxPlayers = 0;
    DWORD currentPlayers = 0;
    const wchar_t *sessionName = nullptr;               // +0x30
    const wchar_t *password = nullptr;
    void *reservedData = nullptr;
    DWORD reservedSize = 0;
    void *applicationData = nullptr;
    DWORD applicationSize = 0;
};
struct DPN_CAPS {                                       // 0x14
    DWORD size;
    DWORD flags;
    DWORD connectTimeout;                               // ms
    DWORD connectRetries;
    DWORD timeoutUntilKeepAlive;                        // ms
};
struct DPN_PLAYER_INFO {                                // 0x18
    DWORD size;
    DWORD infoFlags;                                    // 1: the name
    const wchar_t *name;
    void *data;
    DWORD dataSize;
    DWORD playerFlags;
};
struct DPN_CONNECTION_INFO {                            // 0x5C
    DWORD size;
    DWORD roundTripLatency;                             // +0x04 ms
    DWORD unknown08[2];
    DWORD bytesSentGuaranteed;                          // +0x10
    DWORD unknown14;
    DWORD bytesSentNonGuaranteed;                       // +0x18
    DWORD unknown1C[11];
    DWORD bytesReceivedGuaranteed;                      // +0x48
    DWORD unknown4C;
    DWORD bytesReceivedNonGuaranteed;                   // +0x50
    DWORD unknown54[2];
};
static_assert(sizeof(DPN_CONNECTION_INFO) == 0x5C, "DPN_CONNECTION_INFO");
// The messages the handlers get (their DPNMSG_* layouts: the fields read).
enum : DWORD {
    DPN_MSGID_CONNECT_COMPLETE = 0xFFFF0005,
    DPN_MSGID_ENUM_HOSTS_RESPONSE = 0xFFFF000B,
    DPN_MSGID_RECEIVE = 0xFFFF0011,
    DPN_MSGID_SEND_COMPLETE = 0xFFFF0013,
    DPN_MSGID_TERMINATE_SESSION = 0xFFFF0016,
};
struct DPNMSG_CONNECT_COMPLETE {
    DWORD size;
    DPNHANDLE asyncOp;
    void *context;
    HRESULT result;                                     // +0x0C
    const uint8_t *replyData;                           // +0x10
    DWORD replySize;                                    // +0x14
};
struct DPNMSG_ENUM_HOSTS_RESPONSE {
    DWORD size;
    IDirectPlay8Address *sender;                        // +0x04
    IDirectPlay8Address *device;
    const DPN_APPLICATION_DESC *application;            // +0x0C
    void *responseData;                                 // +0x10
    DWORD responseSize;                                 // +0x14
    void *context;                                      // +0x18 (EnumHosts': the server pinged, or null)
    DWORD roundTripLatency;                             // +0x1C
};
struct DPNMSG_RECEIVE {
    DWORD size;
    DPNID sender;
    void *playerContext;
    void *data;                                         // +0x0C
    DWORD dataSize;                                     // +0x10
    DPNHANDLE buffer;
};
struct DPNMSG_SEND_COMPLETE {
    DWORD size;
    DPNHANDLE asyncOp;
    void *context;                                      // +0x08 (the game's: the data sent, freed)
};

// The GUIDs (DirectPlay 8, DirectPlay Voice, the game's application, the voice codecs, the default voice devices).
extern const GUID CLSID_NetDirectPlay8Server;           // 0x008190a8
extern const GUID IID_NetDirectPlay8Server;             // 0x00819058
extern const GUID CLSID_NetDirectPlay8Client;           // 0x008190b8
extern const GUID IID_NetDirectPlay8Client;             // 0x00819068
extern const GUID CLSID_NetDirectPlay8Address;          // 0x008190e8
extern const GUID IID_NetDirectPlay8Address;            // 0x008190d8
extern const GUID CLSID_NetDP8SP_TCPIP;                 // 0x00818fe8
extern const GUID g_netApplicationGuid;                 // 0x00816350
extern const GUID CLSID_NetVoiceClient;                 // 0x008191b8
extern const GUID IID_NetVoiceClient;                   // 0x00819188
extern const GUID CLSID_NetVoiceServer;                 // 0x008191a8
extern const GUID IID_NetVoiceServer;                   // 0x00819178
extern const GUID CLSID_NetVoiceTest;                   // 0x00819198
extern const GUID IID_NetVoiceTest;                     // 0x00819168
extern const GUID g_netVoicePlayback;                   // 0x00819488 DSDEVID_DefaultVoicePlayback
extern const GUID g_netVoiceCapture;                    // 0x00819478 DSDEVID_DefaultVoiceCapture
extern const GUID g_netVoiceCodecs[8];                  // 0x008190f8.. (by the settings' +0x178, 1..7; 0: the default)

// CoCreateInstance for the network game (the harness supplies its own fakes).
HRESULT NetCreateInstance(const GUID &clsid, DWORD context, const GUID &iid, void **object);

// The flag CancelAsyncOperation takes to cancel every operation.
enum : DWORD {
    DPNCANCEL_ALL_OPERATIONS = 0x00008000,
};
