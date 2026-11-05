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

// The flag CancelAsyncOperation takes to cancel every operation.
enum : DWORD {
    DPNCANCEL_ALL_OPERATIONS = 0x00008000,
};
