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
    void Clear();                                                       // vtable +0x10; models 0x006d77e0 (DS 0x0071f050), sounds 0x006d87a0 (DS 0x00720450), animations 0x006d9890 (DS 0x007219d0), shadows 0x006d6dd0 (DS 0x0071e370)
    // Forgets what it has without releasing it, the spare released (what the game's shutdown does: the driver's Close comes next; 0x006dc770).
    void Forget();

    void SetMaxEntries(uint32_t count) { m_maxEntries = count; }        // vtable +0x00; models 0x006d77d0 (DS 0x0071f030), sounds 0x006d81e0 (DS 0x0071fd10), animations 0x006d91a0 (DS 0x00721110), shadows 0x006d6130 (DS 0x0071d390)
    int NumEntries() const { return static_cast<int>(m_entries.size()); }   // vtable +0x34; models 0x006d81a0 (DS 0x0071fca0), sounds 0x006d9160 (DS 0x007210a0), animations 0x006da250 (DS 0x00722620), shadows 0x006d7790 (DS 0x0071efc0)

    // Search directories (stored with a trailing backslash).
    void AddDir(const char *dir);                                       // vtable +0x18
    void RemoveDir(const char *dir);                                    // vtable +0x1C; models 0x006d7ac0 (DS 0x0071f3f0), animations 0x006d9b70 (DS 0x00721d70), shadows 0x006d70b0 (DS 0x0071e710)
    void ClearDirs() { m_dirs.clear(); }                                // vtable +0x20; models 0x006d7d00 (DS 0x0071f6c0), sounds 0x006d8cc0 (DS 0x00720ac0), animations 0x006d9db0 (DS 0x00722040), shadows 0x006d72f0 (DS 0x0071e9e0)
    int NumDirs() const { return static_cast<int>(m_dirs.size()); }     // vtable +0x24; models 0x006d7d70 (DS 0x0071f750), sounds 0x006d8d30 (DS 0x00720b50), animations 0x006d9e20 (DS 0x007220d0), shadows 0x006d7360 (DS 0x0071ea70)
    const char *GetDir(int index) const { return m_dirs[index]; }       // vtable +0x28; models 0x006d7d90 (DS 0x0071f780), sounds 0x006d8d50 (DS 0x00720b80), animations 0x006d9e40 (DS 0x00722100), shadows 0x006d7380 (DS 0x0071eaa0)
    // Strips the working directory and then a search directory from the full
    // path in `path` (in place); false if it lies in none of the directories.
    bool MakeRelative(char *path) const;                                // vtable +0x2C; models 0x006d7db0 (DS 0x0071f7b0), animations 0x006d9e60 (DS 0x00722130), shadows 0x006d73a0 (DS 0x0071ead0)

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

class C_ModelCache : public C_ResourceCache<I3D_model> {               // vtable 0x00815fc8; deleting dtor 0x006d5340 (DS 0x0071c120)
protected:
    I3D_model *Create() override;                                       // 0x006d5330 (DS 0x0071c100)
    void Duplicate(I3D_model *dest, I3D_model *source) override;       // 0x006d52e0 (DS 0x0071c090)
};

class C_SoundCache : public C_ResourceCache<I3D_sound> {               // vtable 0x00816068
protected:
    I3D_sound *Create() override;                                       // 0x006d5660 (DS 0x0071c520)
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
    I3D_animation_set *Create() override;                               // 0x006d5970 (DS 0x0071c950)
    void Duplicate(I3D_animation_set *, I3D_animation_set *) override {}   // 0x006d5960
};

class C_ShadowCache : public C_ResourceCache<I3D_shadow> {             // vtable 0x008161a8
protected:
    I3D_shadow *Create() override;                                      // 0x006d5d20 (DS 0x0071ce40)
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
extern C_ModelCache g_versionModelCache;                                // 0x008aecb0 (data dir 5, versions module); its class's Create 0x006d6060 (DS 0x0071d2a0), Duplicate 0x006d6010 (DS 0x0071d230)
