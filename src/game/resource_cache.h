// Resource caches for engine objects loaded from files (models, sounds,
// animations, shadows).
//
// Opening a file through the engine is slow, so the game keeps the first loaded
// copy of every file ("entries", at most GetMaxEntries(); when the cache is full
// the oldest entry nobody else references is dropped) and afterwards either
// duplicates it into the caller's object (OpenInto) or hands out a shared
// reference (Get). Files are looked up in the cache's search directories in the
// order they were added (AddDataDirectory() adds a whole directory tree).
//
// One template in the original (vtable layout 0x00815f60), instantiated four
// times: models 0x006d77d0.., sounds 0x006d81e0.., animations 0x006d91a0..,
// shadows 0x006d6130.. (some members were emitted in other files, e.g.
// OpenInto<I3D_model> at 0x004290a0).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "common/c_str.h"
#include "ls3d/i3d.h"

// Called with "Failed to load file: <name>" when Get() finds a file nowhere.
using T_CacheErrorProc = void(__fastcall *)(const char *message, void *context);

// Open() flag bits added by the caches.
enum : uint32_t {
    CACHE_OPEN_REPORT = 0x04,      // a callback was passed (problems are reported)
    CACHE_OPEN_INTO = 0x10,        // always added by OpenInto (meaning not identified)
};

template <class T>
class C_ResourceCache {
public:
    C_ResourceCache() = default;
    C_ResourceCache(const C_ResourceCache &) = delete;
    C_ResourceCache &operator=(const C_ResourceCache &) = delete;
    virtual ~C_ResourceCache();                                         // vtable +0x14

    // Loads `fileName` into the caller's `object`: a copy of the cached file if
    // there is one, otherwise from disk (the cache then keeps a copy).
    // `callback` is only checked for null (CACHE_OPEN_REPORT); `context` and the sixth
    // argument are not used (every caller passes 0 for the last four).
    virtual HRESULT OpenInto(T *object, const char *fileName, uint32_t flags = 0,
                             void *callback = nullptr, void *context = nullptr,
                             int unused = 0);                           // vtable +0x04
    // Shared instance of `fileName`; the caller receives one reference.
    HRESULT Get(const char *fileName, T **out, T_CacheErrorProc onError = nullptr,
                void *context = nullptr);                               // vtable +0x08
    // Releases every cached object.
    void Clear();                                                       // vtable +0x10
    // Forgets what it has without releasing it, the spare released (what the game's shutdown does: the driver's Close comes next; 0x006dc770).
    void Forget();

    void SetMaxEntries(uint32_t count) { m_maxEntries = count; }        // vtable +0x00
    int NumEntries() const { return static_cast<int>(m_entries.size()); }   // vtable +0x34

    // Search directories (stored with a trailing backslash).
    void AddDir(const char *dir);                                       // vtable +0x18
    void RemoveDir(const char *dir);                                    // vtable +0x1C
    void ClearDirs() { m_dirs.clear(); }                                // vtable +0x20
    int NumDirs() const { return static_cast<int>(m_dirs.size()); }     // vtable +0x24
    const char *GetDir(int index) const { return m_dirs[index]; }       // vtable +0x28
    // Strips the working directory and then a search directory from the full
    // path in `path` (in place); false if it lies in none of the directories.
    bool MakeRelative(char *path) const;                                // vtable +0x2C

protected:
    // New empty object of the cached type.
    virtual T *Create() { return nullptr; }                             // vtable +0x0C
    // Makes `dest` a copy of `source`.
    virtual void Duplicate(T *dest, T *source) = 0;                     // vtable +0x30

private:
    struct S_Entry {    // dtor: animations 0x004203e0 (DS 0x00441bb0), sounds 0x00437740 (DS 0x004b0c20), shadows 0x0048b440 (DS 0x0070ba00); sounds' ctor 0x00437770 (DS 0x004b0c80)
        T *object = nullptr;                                            // holds one reference (sounds' assignment, AddRef / Release: 0x00437780 (DS 0x004b0ca0))
        C_str name;
    };

    // Cached object for `fileName`, null if there is none.
    T *Find(const char *fileName) const;
    // Tries every search directory; returns the result of the last attempt.
    HRESULT OpenFromDirs(T *object, const char *fileName, uint32_t flags) const;
    // When the cache is full, drops the oldest entry that only the cache uses.
    void MakeRoom();
    void RemoveEntry(std::size_t index);

    std::map<std::string, T *> m_byName;                                // +0x04
    std::vector<S_Entry> m_entries;                                     // +0x14 oldest first
    std::vector<C_str> m_dirs;                                          // +0x24
    uint32_t m_maxEntries = 256;                                        // +0x34
    T *m_spare = nullptr;                                               // +0x38 next object Get() loads into
};

class C_ModelCache : public C_ResourceCache<I3D_model> {               // vtable 0x00815fc8
protected:
    I3D_model *Create() override;                                       // 0x006d5330
    void Duplicate(I3D_model *dest, I3D_model *source) override;       // 0x006d52e0
};

class C_SoundCache : public C_ResourceCache<I3D_sound> {               // vtable 0x00816068
protected:
    I3D_sound *Create() override;                                       // 0x006d5660
    // Re-opens the source's file (sounds share their sample data in the engine).
    void Duplicate(I3D_sound *dest, I3D_sound *source) override;       // 0x006d5620
};

class C_AnimationCache : public C_ResourceCache<I3D_animation_set> {   // vtable 0x00816108
public:
    // Animation sets are only shared (Get); loading into a given set does nothing.
    HRESULT OpenInto(I3D_animation_set *, const char *, uint32_t = 0, void * = nullptr, void * = nullptr,
                     int = 0) override
    {
        return I3D_OK;                                                  // 0x006d5950
    }

protected:
    I3D_animation_set *Create() override;                               // 0x006d5970
    void Duplicate(I3D_animation_set *, I3D_animation_set *) override {}   // 0x006d5960
};

class C_ShadowCache : public C_ResourceCache<I3D_shadow> {             // vtable 0x008161a8
protected:
    I3D_shadow *Create() override;                                      // 0x006d5d20
    // Only detaches the game data from the destination's frames.
    void Duplicate(I3D_shadow *dest, I3D_shadow *source) override;     // 0x006d5c70
};

// Which search path AddDataDirectory() extends.
enum E_DataDirectory {
    DATA_DIR_MAPS = 0,              // textures (IGraph search path)
    DATA_DIR_MODELS = 1,            // g_modelCache
    DATA_DIR_SOUNDS = 2,            // g_soundCache
    DATA_DIR_ANIMATIONS = 3,        // g_animationCache
    DATA_DIR_SHADOWS = 4,           // g_shadowCache
    DATA_DIR_VERSIONS = 5,          // g_versionModelCache
};

// Adds `path` and every directory below it (lower-cased) to one search path.
void AddDataDirectory(const char *path, E_DataDirectory type);    // 0x006edb30

extern C_ShadowCache g_shadowCache;                                     // 0x008aecec (data dir 4)
extern C_AnimationCache g_animationCache;                               // 0x008aed28 (data dir 3)
extern C_SoundCache g_soundCache;                                       // 0x008aed64 (data dir 2)
extern C_ModelCache g_modelCache;                                       // 0x008aeda0 (data dir 1)
extern C_ModelCache g_versionModelCache;                                // 0x008aecb0 (data dir 5, versions module)
