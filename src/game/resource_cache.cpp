#include "game/resource_cache.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <direct.h>
#include <windows.h>

#include "ls3d/igraph.h"

// Constructed by the static initializers 0x006d5020 (model), 0x006d5360 (sound),
// 0x006d5690 (animation), 0x006d59b0 (shadow) and 0x006d5d50 (versions).
C_ShadowCache g_shadowCache;
C_AnimationCache g_animationCache;
C_SoundCache g_soundCache;
C_ModelCache g_modelCache;
// The versions module's: in the original a class of its own (vtable 0x008161e0), its members copies of
// C_ModelCache's (Create and Duplicate: resource_cache.h).
C_ModelCache g_versionModelCache;   // dtor 0x006d5d90 (DS 0x0071cf10), ctor 0x006d5f10 (DS 0x0071d0f0), deleting dtor 0x006d6070 (DS 0x0071d2c0)

// Animation sets created by the animation cache (statistics only).
int g_animationSetsCreated = 0;                          // 0x008aedec

namespace {

// --- per-type operations ---------------------------------------------------------

void AddRefObject(I3D_frame *frame) { frame->AddRef(); }
void AddRefObject(I3D_animation_set *anim) { anim->AddRef(); }

HRESULT OpenObject(I3D_model *model, const char *path, uint32_t flags)
{
    return model->Open(path, flags, nullptr, nullptr);
}
HRESULT OpenObject(I3D_sound *sound, const char *path, uint32_t flags) { return sound->Open(path, flags); }
HRESULT OpenObject(I3D_shadow *shadow, const char *path, uint32_t flags) { return shadow->Open(path, flags); }
HRESULT OpenObject(I3D_animation_set *anim, const char *path, uint32_t flags) { return anim->Open(path, flags); }

// True if the cache holds the only reference to `object`.
template <class T>
bool IsReferencedOnlyByCache(T *object)
{
    AddRefObject(object);
    return object->Release() == 1;
}

// File name pattern match with '*' (rest matches) and '?' (any one character),
// case-insensitive (the C_str wildcard compare inlined in 0x006d7db0).
bool MatchWildcard(const char *text, const char *pattern)
{
    if (!*text || !*pattern) {
        if (!*text && !*pattern)
            return true;
        return *text == '*' || *pattern == '*';
    }
    for (; *pattern; ++pattern, ++text) {
        if (*pattern == '*')
            return true;
        if (*pattern == '?') {
            if (!*text)
                return false;
        } else if (std::tolower(static_cast<unsigned char>(*pattern)) !=
                   std::tolower(static_cast<unsigned char>(*text))) {
            return false;
        }
    }
    return *text == '\0';
}

C_str WithTrailingBackslash(const char *dir)
{
    C_str path = dir;
    if (path.IsEmpty() || path.c_str()[path.Size() - 1] != '\\')
        path += "\\";
    return path;
}

// I3D_frame::EnumFrames callback: detaches the game data copied along with a
// duplicated hierarchy (0x006d5310 (DS 0x0071c0d0), 0x006d5d00 (DS 0x0071ce10), 0x006d6040 (DS 0x0071d270)).
uint32_t __stdcall ClearFrameData(I3D_frame *frame, void *)
{
    frame->SetData(nullptr);
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// C_ResourceCache

template <class T>
C_ResourceCache<T>::~C_ResourceCache()
{
    Clear();
}

template <class T>
T *C_ResourceCache<T>::Find(const char *fileName) const
{
    const auto it = m_byName.find(fileName);
    return it != m_byName.end() ? it->second : nullptr;
}

template <class T>
void C_ResourceCache<T>::RemoveEntry(std::size_t index)
{
    S_Entry &entry = m_entries[index];
    m_byName.erase(entry.name.c_str());
    if (entry.object)
        entry.object->Release();
    m_entries.erase(m_entries.begin() + index);
}

template <class T>
void C_ResourceCache<T>::MakeRoom()
{
    if (m_entries.size() < m_maxEntries)
        return;
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        if (IsReferencedOnlyByCache(m_entries[i].object)) {
            RemoveEntry(i);
            return;
        }
    }
}

template <class T>
HRESULT C_ResourceCache<T>::OpenFromDirs(T *object, const char *fileName, uint32_t flags) const
{
    HRESULT result = I3DERR_NOFILE;
    for (const C_str &dir : m_dirs) {
        C_str path = dir;
        path += fileName;
        result = OpenObject(object, path, flags);
        if (result == I3DERR_NOFILE)
            continue;
        if (result == I3DERR_OUTOFMEM || result == I3DERR_CANCELED)
            return result;
        if (result == I3DERR_BADFILEVERSION)
            dbgPrintf("Cache : BADFILEVERSION '%s' (3ds filter)", path.c_str());
        else if (result == I3DERR_BADFILETIME)
            dbgPrintf("Cache : BADFILETIME '%s' (3ds filter)", path.c_str());
        if (result >= 0)
            return result;
    }
    return result;
}

template <class T>
HRESULT C_ResourceCache<T>::OpenInto(T *object, const char *fileName, uint32_t flags, void *callback,
                                     void * /*context*/, int /*unused*/)
{
    // The object may be one of our cached copies that is about to be overwritten.
    for (std::size_t i = m_entries.size(); i-- > 0;) {
        if (m_entries[i].object == object) {
            RemoveEntry(i);
            break;
        }
    }

    if (T *cached = Find(fileName)) {
        Duplicate(object, cached);
        return I3D_OK;
    }

    flags |= CACHE_OPEN_INTO;
    if (callback)
        flags |= CACHE_OPEN_REPORT;
    const HRESULT result = OpenFromDirs(object, fileName, flags);
    if (result < 0)
        return result;

    // Keep a copy of the freshly loaded file.
    MakeRoom();
    T *copy = Create();
    if (!copy)
        return result;
    Duplicate(copy, object);
    AddRefObject(copy);
    m_entries.push_back(S_Entry{copy, C_str(fileName)});
    m_byName[fileName] = copy;
    copy->Release();
    return result;
}

template <class T>
HRESULT C_ResourceCache<T>::Get(const char *fileName, T **out, T_CacheErrorProc onError, void *context)
{
    *out = nullptr;
    if (T *cached = Find(fileName)) {
        AddRefObject(cached);
        *out = cached;
        return I3D_OK;
    }

    if (!m_spare)
        m_spare = Create();
    if (!m_spare)
        return I3DERR_GENERIC;
    const HRESULT result = OpenFromDirs(m_spare, fileName, onError ? CACHE_OPEN_REPORT : 0);
    if (result < 0) {
        if (onError)
            onError(C_fstr("Failed to load file: %s", fileName), context);
        return result;
    }

    MakeRoom();
    T *nextSpare = Create();
    if (!nextSpare)
        return I3DERR_GENERIC;
    // The spare's own reference goes to the caller, the entry takes another one.
    AddRefObject(m_spare);
    m_entries.push_back(S_Entry{m_spare, C_str(fileName)});
    m_byName[fileName] = m_spare;
    *out = m_spare;
    m_spare = nextSpare;
    return result;
}

template <class T>
void C_ResourceCache<T>::Clear()
{
    for (S_Entry &entry : m_entries) {
        if (entry.object)
            entry.object->Release();
    }
    m_entries.clear();
    m_byName.clear();
    if (m_spare)
        m_spare->Release();
    m_spare = nullptr;
}

template <class T>
void C_ResourceCache<T>::Forget()
{
    m_entries.clear();
    m_byName.clear();
    if (m_spare)
        m_spare->Release();
    m_spare = nullptr;
}

template <class T>
void C_ResourceCache<T>::AddDir(const char *dir)
{
    // models 0x006d78e0 (DS 0x0071f190), sounds 0x006d88a0 (DS 0x00720590), animations 0x006d9990 (DS 0x00721b10),
    // shadows 0x006d6ed0 (DS 0x0071e4b0)
    RemoveDir(dir);
    m_dirs.push_back(WithTrailingBackslash(dir));
}

template <class T>
void C_ResourceCache<T>::RemoveDir(const char *dir)
{
    const C_str path = WithTrailingBackslash(dir);
    for (std::size_t i = m_dirs.size(); i-- > 0;) {
        if (m_dirs[i] == path) {
            m_dirs.erase(m_dirs.begin() + i);
            return;
        }
    }
}

template <class T>
bool C_ResourceCache<T>::MakeRelative(char *path) const
{
    char cwd[0x104];
    if (!_getcwd(cwd, sizeof(cwd)))
        cwd[0] = '\0';
    const C_fstr cwdPattern("%s\\*", cwd);

    // Path relative to the working directory, if it is inside it.
    C_str relative = path;
    if (MatchWildcard(relative, cwdPattern))
        relative = C_str(relative.c_str() + cwdPattern.Size() - 1);

    for (const C_str &dir : m_dirs) {
        C_str pattern = dir;
        pattern += "*";
        if (MatchWildcard(relative, pattern)) {
            std::strcpy(path, relative.c_str() + dir.Size());
            return true;
        }
    }
    return false;
}

template class C_ResourceCache<I3D_model>;
template class C_ResourceCache<I3D_sound>;
template class C_ResourceCache<I3D_animation_set>;
template class C_ResourceCache<I3D_shadow>;

// ---------------------------------------------------------------------------
// specialisations

I3D_model *C_ModelCache::Create()
{
    return static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
}

void C_ModelCache::Duplicate(I3D_model *dest, I3D_model *source)
{
    dest->DuplicateModel(source);
    dest->EnumFrames(ClearFrameData, nullptr, 0xFFFF, 0);
}

I3D_sound *C_SoundCache::Create()
{
    return static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
}

void C_SoundCache::Duplicate(I3D_sound *dest, I3D_sound *source)
{
    dest->Open(source->GetFileName(), source->GetOpenFlags());
}

I3D_animation_set *C_AnimationCache::Create()
{
    ++g_animationSetsCreated;
    return I3DGetDriver()->CreateAnimationSet();
}

I3D_shadow *C_ShadowCache::Create()
{
    return static_cast<I3D_shadow *>(I3DGetDriver()->CreateFrame(FRAME_SHADOW));
}

void C_ShadowCache::Duplicate(I3D_shadow *dest, I3D_shadow *)
{
    dest->EnumFrames(ClearFrameData, nullptr, 0xFFFF, 0);
}

// ---------------------------------------------------------------------------
// search directories

namespace {

void AddSearchDir(const char *dir, E_DataDirectory type)
{
    switch (type) {
    case DATA_DIR_MAPS:
#ifndef HD2_DEDICATED   // no texture directories in LS3DRF.dll
        GetIGraph()->AddDir(dir, 0);
#endif
        break;
    case DATA_DIR_MODELS:
        g_modelCache.AddDir(dir);
        break;
    case DATA_DIR_SOUNDS:
        g_soundCache.AddDir(dir);
        break;
    case DATA_DIR_ANIMATIONS:
        g_animationCache.AddDir(dir);
        break;
    case DATA_DIR_SHADOWS:
        g_shadowCache.AddDir(dir);
        break;
    case DATA_DIR_VERSIONS:
        g_versionModelCache.AddDir(dir);
        break;
    }
}

void AddSubdirectories(const char *dir, E_DataDirectory type);

// One directory entry found below `dir` (0x006ed940, DS 0x00738d50).
void AddDirectoryEntry(const char *dir, const char *name, uint32_t attributes, E_DataDirectory type)
{
    char path[0x200];
    std::snprintf(path, sizeof(path), "%s\\%s", dir, name);
    _strlwr(path);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return;
    if (!std::strcmp(name, ".") || !std::strcmp(name, ".."))
        return;
    AddSearchDir(path, type);
    AddSubdirectories(path, type);
}

// 0x006eda80
void AddSubdirectories(const char *dir, E_DataDirectory type)
{
    char pattern[0x200];
    std::snprintf(pattern, sizeof(pattern), "%s\\*.*", dir);
    WIN32_FIND_DATAA found;
    const HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do {
        AddDirectoryEntry(dir, found.cFileName, found.dwFileAttributes, type);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

} // namespace

void AddDataDirectory(const char *path, E_DataDirectory type)
{
    AddSearchDir(path, type);
    AddSubdirectories(path, type);
}
