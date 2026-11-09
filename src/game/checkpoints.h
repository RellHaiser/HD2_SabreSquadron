// Checkpoints: the AI's navigation graph. A network holds points (S_Checkpoint) with their
// links, sorted for spatial lookups, plus tables naming points and tying them to frames and
// actors. The mission has two networks, the walking network and the road network after it
// (C_Checkpoints, C_game +0x54), both in missions\<name>\check2.bin (or a scene's chunk
// 0xAE40).
//
// The original sources were three units: the network (0x0051c760..0x00523a70, here
// checkpoint_network.cpp and checkpoint_search.cpp), the pair (0x00523a80..0x00524430,
// checkpoints.cpp) and the point (0x00524450..0x00524b50, checkpoint.cpp).
#pragma once

#include <cstdint>
#include <vector>

#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class I3D_frame;

constexpr int CHECKPOINT_NONE = 0xFFFF;

// S_Checkpoint::flags. The low byte counts the links; the high byte holds the point's flags.
enum : uint16_t {
    CHECKPOINT_LINK_COUNT = 0x003F,                     // number of links (a joined one included)
    CHECKPOINT_SPARE_LINK = 0x0040,                     // one more slot after the links, for Join
    CHECKPOINT_JOINED = 0x0080,                         // the spare slot holds a link of Join
    CHECKPOINT_FLAG_NO_STOP = 0x0100,                   // not picked as a destination
    CHECKPOINT_FLAG_CAR_0200 = 0x0200,                  // set / cleared by the cars (0x004db060 / 0x004dab60)
    CHECKPOINT_SHARED_LINKS = 0x0400,                   // the links live in the network's storage
    CHECKPOINT_FLAG_UNDER_CAR = 0x2000,                 // a car stands on the point (0x004dac80 / 0x004dafe0)
    CHECKPOINT_SEARCH_FLAGS = 0xFB40,                   // the flags a path search's exclude mask tests
};

// S_Checkpoint::type, the values the code tests. (The frames of the actors tied to points
// suggest more: 3 ladders (m_zebr_*), 11 static machine guns, 13 barriers (zavora), 14 radios.)
enum : uint8_t {
    CHECKPOINT_TYPE_NORMAL = 0,
    CHECKPOINT_TYPE_DOOR = 1,                           // a door's side (its door in the actor table)
    CHECKPOINT_TYPE_FENCE_GAP = 7,                      // either side of a cut fence (C_WireCut, local games)
    CHECKPOINT_TYPE_SNIPER = 10,                        // a sniper's post (C_SniperController; inferred)
    CHECKPOINT_TYPE_GUN = 11,                           // a static gun's place (its actor in the table)
    CHECKPOINT_TYPE_15 = 15,                            // FindNearestType15's
};

// A link to another point; the cost is the distance (shortened by both points' preference)
// in 1/256 m.
struct S_CheckpointLink {                               // 4 bytes
    uint16_t target;
    uint16_t cost;
};

struct S_Checkpoint {                                   // 0x24 bytes
    S_vector pos;                                       // +0x00
    int cell = 0;                                       // +0x0C GetCell(pos.x): the points are sorted by cell, then z
    uint16_t unknown10 = 0;                             // +0x10
    uint8_t preference = 0;                             // +0x12 makes the links cheaper by preference/1024
    uint8_t radius = 0;                                 // +0x13 in 1/16 m
    uint16_t flags = 0;                                 // +0x14 CHECKPOINT_*
    uint8_t type = 0;                                   // +0x16 CHECKPOINT_TYPE_*
    // Directions (GetDirectionBit) in which the point gives cover to stance 2, 4 and 1 (the
    // stance bits of GetCoverMask; the file stores the masks in the order 1, 2, 4).
    uint8_t cover2 = 0;                                 // +0x17
    uint8_t cover4 = 0;                                 // +0x18
    uint8_t cover1 = 0;                                 // +0x19
    // (+0x1A: ResetState zeroes it; the cars count down its bits 5..7, 0x004dab60.)
    uint8_t unknown1A = 0;                              // +0x1A
    uint8_t values[4] = {0xFF, 0xFF, 0xFF, 0xFF};       // +0x1B GetValuePair (n/254, 0xFF none)
    uint8_t unknown1F = 0;                              // +0x1F
    S_CheckpointLink *links = nullptr;                  // +0x20

    S_Checkpoint() = default;                           // 0x0051e110
    ~S_Checkpoint();                                    // 0x00524840 (thunk 0x00524610)
    S_Checkpoint(const S_Checkpoint &) = delete;
    S_Checkpoint &operator=(const S_Checkpoint &) = delete;

    int GetLinkCount() const { return flags & CHECKPOINT_LINK_COUNT; }
    // Link `index`, null past the links.
    S_CheckpointLink *GetLink(int index);               // 0x00524780
    // Index of the link to point `target`, 0xFF if there is none.
    uint8_t FindLink(int16_t target) const;             // 0x005247a0
    // Cost of a link to `to` in metres.
    float GetLinkCost(const S_Checkpoint &to) const;    // 0x005247e0

    // Zeroes the flags, the type and the links. Not called.
    void Clear();                                       // 0x00524600 (DS 0x00512010)
    // The destructor's work, then no links, flags or type.
    void ClearLinks();                                  // 0x005245e0 (DS 0x00511fe0)
    S_vector GetPos() const { return pos; }             // 0x0041d7c0 (DS 0x0043e330)

    // Reads a point of checkpoint data (0x20 bytes, then the links); its links go into the
    // network's link storage at `storage`, which moves past them. Returns the data after it.
    const uint8_t *Read(const uint8_t *data, S_CheckpointLink *&storage);   // 0x00524620
    // Clears the run-time state: the car flags, +0x1A, a joined link.
    void ResetState();                                  // 0x00524870
    // Puts a link to point `index` (= `target`) into the spare slot; false if the point has no
    // free spare slot, already links to it, or the link would cost under 0.001 or over 250 m.
    bool AddJoinedLink(int16_t index, const S_Checkpoint *target);   // 0x005248c0
    // Removes the joined link to point `index`; false if it is not the joined link.
    bool RemoveJoinedLink(int16_t index);               // 0x005249a0

    // Cover directions of the stances in `stances` (1, 2, 4 or several: all three ORed).
    uint8_t GetCoverMask(int stances) const;            // 0x00524b10
    // The stances of `stances` the point covers in direction bit `direction`.
    uint8_t GetCoverStances(uint8_t direction, uint8_t stances) const;   // 0x0045acf0
    // Two 0..1 values of set 0 (+0x1C, +0x1E) or set 1 (+0x1B, +0x1D), -1 where missing;
    // true when both are there. (The AI interpolates them between points; meaning unknown.)
    bool GetValuePair(int set, float *first, float *second) const;   // 0x00524b50

    // The cell of x: 10 m columns, x / 10 rounded down (but see the .cpp).
    static int __stdcall GetCell(float x);              // 0x00524750
    // Compass direction bit of `dir` in the xz plane: 1 +z, 2 +x+z, 4 +x, 8 +x-z, 0x10 -z,
    // 0x20 -x-z, 0x40 -x, 0x80 -x+z.
    static uint8_t GetDirectionBit(const S_vector &dir);   // 0x00524a30 (__fastcall)
    // Vector of the lowest direction bit in `bits` (+z if none).
    static const S_vector4 *GetDirectionVector(uint8_t bits);   // 0x005245c0 (__fastcall)
};

// A named point (the names are not NUL-terminated at 16 characters).
struct S_CheckpointName {                               // 0x12 bytes
    char name[16] = {};
    uint16_t index = 0xFFFF;

    S_CheckpointName() = default;                       // 0x0051c790
    // Reads {uint16 index, char name[16]}; returns the data after it.
    const uint8_t *Read(const uint8_t *data);           // 0x0051c7b0
};

// A point tied to a scene frame.
struct S_CheckpointFrame {                              // 8 bytes
    I3D_frame *frame = nullptr;
    uint16_t index = 0xFFFF;

    // Reads {uint16 index, char frameName[]} and looks the frame up.
    const uint8_t *Read(const uint8_t *data);           // 0x0051c870 (DS 0x00508000)
};

// A point tied to an actor (the doors' sides); the table is sorted by point.
struct S_CheckpointActor {                              // 8 bytes
    C_Actor *actor = nullptr;
    uint16_t index = 0xFFFF;

    // Reads {uint16 index, char frameName[]} and takes the actor of that frame.
    const uint8_t *Read(const uint8_t *data);           // 0x0051c910 (DS 0x005080e0)
};

// The positions FindEscapePath flees from: the AI controller's growable aligned array.
struct S_Vector4Array {                                 // 0x10 bytes
    S_vector4 *data = nullptr;                          // _aligned_malloc'd
    int capacity = 0;
    int count = 0;
    int growBy = 8;
};

// Flags of PickCoverPoint (the AI's calls pass e.g. 0x40004104, 0xC0004102).
enum : uint32_t {
    COVER_PICK_FROM_SELF = 0x00000001,                  // distances from `self` (else from `enemy`)
    COVER_PICK_FARTHEST = 0x00000004,                   // the farthest candidate (else the nearest)
    COVER_PICK_STANCE = 0x00000700,                     // 0x100 / 0x200 / 0x400: must cover stance 1 / 2 / 4
    COVER_PICK_EXPOSED = 0x00006000,                    // 0x2000 / 0x4000: must not cover stance 2 / 4
    COVER_PICK_DROP_REJECTED = 0x40000000,              // removes what the stance tests reject
    COVER_PICK_NOT_FARTHER = 0x80000000,                // removes points farther from the enemy than self
};

// A network of checkpoints (0x3C bytes).
class C_CheckpointNetwork {
public:
    C_CheckpointNetwork() = default;                    // (inline in C_Checkpoints())
    ~C_CheckpointNetwork();                             // 0x00523bb0
    C_CheckpointNetwork(const C_CheckpointNetwork &) = delete;
    C_CheckpointNetwork &operator=(const C_CheckpointNetwork &) = delete;

    // --- points and tables -----------------------------------------------------------------
    int GetCount() const { return m_pointCount; }
    S_Checkpoint *Get(int index);                       // 0x0051d5a0 (null past the points)
    const S_Checkpoint *Get(int index) const { return const_cast<C_CheckpointNetwork *>(this)->Get(index); }
    // Point of a name (case-insensitive, 16 characters), CHECKPOINT_NONE if missing.
    int Find(const char *name) const;                   // 0x0051e1a0
    // Appends the points whose names start with `prefix`.
    void FindByPrefix(const char *prefix, std::vector<uint16_t> &out) const;   // 0x0051c9c0
    // Actor tied to point `index`, null if none.
    C_Actor *GetActor(int index) const;                 // 0x0051e520
    // A point tied to `actor` (the last in the table), CHECKPOINT_NONE if none.
    int FindActorPoint(const C_Actor *actor) const;     // 0x0051e690
    // Entries of the tables, 0xFFFF if there is none: by name, and by point in the sorted tables.
    uint16_t FindNameEntry(const char *name) const;     // 0x0051e150 (DS 0x00509fa0)
    uint16_t FindNameEntryOfPoint(uint16_t index) const;    // 0x0051e2c0 (DS 0x0050a180)
    uint16_t FindFrameEntryOfPoint(uint16_t index) const;   // 0x0051e450 (DS 0x0050a380)
    uint16_t FindActorEntryOfPoint(uint16_t index) const;   // 0x0051e5e0 (DS 0x0050a580)
    // Appends the points within `radius` of link costs from `from`, breadth first, `from` first.
    void CollectReachableWithCosts(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out);   // 0x0051ff20 (DS 0x0050c580)
    // The points tied to `actor` (replaces `out`).
    void GetActorPoints(const C_Actor *actor, std::vector<uint16_t> &out) const;   // 0x0051e6d0

    // --- spatial lookups ---------------------------------------------------------------
    // Nearest point within `maxDist` and at least `minDist` away (-1: no minimum),
    // CHECKPOINT_NONE if none. FindNearest takes the points' radius off the distance;
    // FindNearestType15 too, measuring in the xz plane and only among points of type 15 (the
    // AI's fallback when FindNearest finds nothing).
    int FindNearest(S_vector pos, float maxDist, float minDist) const;        // 0x0051d780
    int FindNearestPoint(S_vector pos, float maxDist, float minDist) const;   // 0x0051d5c0
    int FindNearestType15(S_vector pos, float maxDist, float minDist) const;    // 0x0051d970
    // marks[i] = 1 for the points within `radius` (all of them for radius 0), 0 for the rest.
    void MarkWithin(uint8_t *marks, S_vector pos, float radius) const;       // 0x0051dbd0
    // Appends the cover points within `radius`.
    bool FindCoverWithin(const S_vector &pos, float radius, std::vector<uint16_t> *out) const;   // 0x0051f830

    // --- reachable points (link costs summed up to `radius`; the start point included) -----
    bool GetNeighbours(int from, float radius, std::vector<uint16_t> *out);   // 0x00520830
    bool GetReachable(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out);   // 0x00520860
    bool GetReachableCopy(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out);   // 0x005208a0
    // ... only the cover points.
    bool GetNeighbourCover(int from, float radius, std::vector<uint16_t> *out);   // 0x005208e0
    bool GetReachableCover(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out);   // 0x00520910
    // ... found depth first (a point keeps the cost of the first way found to it).
    bool GetReachableDepthFirst(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out);   // 0x0051f5e0
    // ... from the point nearest to `pos`, only points of type 1 or 2.
    bool GetReachableType12(const S_vector &pos, float radius, std::vector<uint16_t> *out);   // 0x0051f700
    // Nearest point to `from` without a car on it, looking within 5, 10, 15 and 20 m of
    // reach; CHECKPOINT_NONE if none (0 without points or `from`).
    int FindNearestFree(const S_Checkpoint *from);      // 0x0051e8a0
    // Picks a cover point against `enemy` among `candidates` (COVER_PICK_*); removes the
    // candidates that are not cover points (and others, by the flags). CHECKPOINT_NONE if none.
    int PickCoverPoint(const S_vector &self, const S_vector &enemy, std::vector<uint16_t> *candidates,
                       uint32_t flags);                 // 0x00520a30
    // Length of `path` in metres; 0 if it has under two points or a step is not a link.
    float GetPathLength(const std::vector<uint16_t> &path) const;   // 0x005230d0

    // --- path searches (`path`: the points from `from` to the goal; false if none) -----------
    // Skips the points with any of `excludeFlags` (CHECKPOINT_SEARCH_FLAGS).
    bool FindPath(int from, int to, std::vector<uint16_t> *path, uint32_t excludeFlags);   // 0x005223b0
    // The meleeTracker's search: no closed doors, nor links of type 3 to 3 (ladders), 4 to 4 and 6 to 5.
    bool FindMeleeTrackerPath(int from, int to, std::vector<uint16_t> *path);   // 0x00522470
    // A way away from `threats` to a point at least `safeDist` from each; fails when the
    // start is within `minDist` of one, and goes round the points that are.
    bool FindEscapePath(int from, const S_Vector4Array &threats, float minDist, float safeDist,
                        std::vector<uint16_t> &path, uint32_t excludeFlags);   // 0x00521460

    // --- joined links (scripts' CheckpointJoin / CheckpointSeparate) -------------------------
    bool Join(const char *a, const char *b);            // 0x0051f490
    bool Separate(const char *a, const char *b);        // 0x0051f5a0
    bool JoinPoints(int a, int b);                      // 0x0051f280
    bool SeparatePoints(int a, int b);                  // 0x0051f4d0

    // --- data and state -----------------------------------------------------------------
    // Frees the points and the tables (the joins stay).
    void Clear();                                       // 0x0051dd60
    // Builds the network from checkpoint data (the header included; version 9 only); returns
    // where the data it used ends, null if it is not version 9.
    const uint8_t *Load(const uint8_t *data);           // 0x0051de80
    // The joins, for saved games (chunk 0x4C5).
    bool SaveState(C_chunk &chunk);                     // 0x00522a30
    bool LoadState(C_chunk &chunk);                     // 0x00522b60
    // Undoes the joins and clears the points' run-time state (mission restart).
    void ResetState();                                  // 0x0051f230

    // The index of `point` (a pointer into the points; the callers check the range).
    int GetIndex(const S_Checkpoint *point) const { return static_cast<int>(point - m_points); }

private:
    // First point at (cell, z) or after it within [lo, hi] (CHECKPOINT_NONE: from the first /
    // to the last), CHECKPOINT_NONE if every point there is before it.
    int FindFirst(int cell, float z, int lo, int hi) const;   // 0x0051cba0
    // Scan cell `cell` around z for a nearer point than `best` / `bestSq` (squared).
    void ScanNearestPoint(int cell, float x, float y, float z, uint16_t &best, float &bestSq,
                          float minSq) const;           // 0x0051ccd0
    void ScanNearest(int cell, float x, float y, float z, uint16_t &best, float &bestSq,
                     float minSq) const;                // 0x0051d160
    void ScanNearestType15(int cell, float x, float y, float z, uint16_t &best, float &bestSq,
                         float minSq) const;            // 0x0051ce70
    void ScanWithin(uint8_t *marks, int cell, float x, float y, float z, float radiusSq) const;   // 0x0051d460
    void FloodFill(const S_Checkpoint *from, float radius, std::vector<uint16_t> &out);   // 0x00520530
    void CollectDepthFirst(const S_Checkpoint *point, float budget, std::vector<uint16_t> &list);   // 0x0051fcb0
    bool IsMeleeTrackerPassable(const S_Checkpoint &from, const S_Checkpoint &to) const;   // 0x005223d0
    bool RunPathSearch(int from, int to, std::vector<uint16_t> *path, uint32_t excludeFlags, bool meleeTracker);

    uint16_t m_pointCount = 0;                          // +0x00
    S_Checkpoint *m_points = nullptr;                   // +0x04 new[]
    S_CheckpointLink *m_linkStorage = nullptr;          // +0x08 the points' links and spare slots
    uint16_t m_nameCount = 0;                           // +0x0C
    S_CheckpointName *m_names = nullptr;                // +0x10
    uint16_t m_frameCount = 0;                          // +0x14
    S_CheckpointFrame *m_frames = nullptr;              // +0x18
    uint16_t m_actorCount = 0;                          // +0x1C
    S_CheckpointActor *m_actors = nullptr;              // +0x20 sorted by point
    // A cost and a previous point per point, used only by a Dijkstra search the game never
    // calls (0x0051ec70; a debug comparison with FindPath, 0x005211c0, is its only caller).
    uint32_t *m_searchCosts = nullptr;                  // +0x24
    uint16_t *m_searchPrev = nullptr;                   // +0x28
    std::vector<uint32_t> m_joins;                      // +0x2C point pairs (a | b << 16)
};

// The mission's walking network and its road network (C_game +0x54, 0x78 bytes).
class C_Checkpoints : public C_CheckpointNetwork {
public:
    C_Checkpoints() = default;                          // 0x00523ab0
    ~C_Checkpoints();                                   // 0x00523b20
    // Both networks.
    void Clear();                                       // 0x00523e10
    // The walking network's run-time state (a thunk of 0x0051f230).
    void ResetState();                                  // 0x00523e30
    // Chunks 0x81 (the walking network's joins), 0x82 and 0x84 (both empty).
    bool SaveGame(C_chunk &chunk);                      // 0x00523f30
    bool LoadGame(C_chunk &chunk);                      // 0x005241a0

    C_CheckpointNetwork *GetRoadNetwork() { return &m_roads; }   // this + 0x3C

private:
    C_CheckpointNetwork m_roads;                        // +0x3C
};

// Versions of the checkpoint data (its first int).
enum : uint32_t {
    CHECKPOINTS_VERSION_1 = 0x7ED00101,                 // a header of 5 ints
    CHECKPOINTS_VERSION_2 = 0x7ED00102,                 // 6 ints
    CHECKPOINTS_VERSION_3 = 0x7ED00103,                 // 0x7ED00103..0x7ED00109: 7 ints
    CHECKPOINTS_VERSION_9 = 0x7ED00109,                 // the only one C_CheckpointNetwork::Load takes
};

// Reads a network's checkpoint data from a chunk (or a raw file opened as one); returns its
// version (the CHECKPOINTS_VERSION_* the data starts with), 0 when it is not checkpoint data
// or does not parse to its full size.
int __stdcall LoadCheckpointNetwork(C_chunk *chunk, C_CheckpointNetwork *network);   // 0x00523c00

// The squared distance from `p` to the segment a..b; `t`: where along it the nearest point is
// (0..1). (The AI's movement, game/ai_movement.cpp; the cars' marks.)
float SegmentDistance2(const S_vector &p, const S_vector &a, const S_vector &b, float *t);   // 0x00581210 (__fastcall)
