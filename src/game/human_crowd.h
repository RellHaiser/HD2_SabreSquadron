// The humans keeping out of each other's way: see game/human_crowd.cpp.
#pragma once

#include <cstdint>
#include <vector>

#include "ls3d/math.h"

class C_Actor;
class C_Car;
class C_CheckpointNetwork;
class C_Human;
struct C_HumanAIMemory;
struct S_Checkpoint;

constexpr int CROWD_MAX_NODES = 200;
constexpr int CROWD_MAX_PATH = 10;
constexpr int CROWD_MAX_CHILDREN = 8;
constexpr int CROWD_MAX_AVOIDED = 4;

// A place on a planned way around what blocks it (0x50 bytes).
struct S_PlanNode {
    S_vector4 pos;                                      // +0x00
    const S_Checkpoint *point;                          // +0x10
    uintptr_t children[CROWD_MAX_CHILDREN];                   // +0x14 bit 0: that way was looked at
    int childCount;                                     // +0x34
    C_Human *avoided[CROWD_MAX_AVOIDED];                      // +0x38 humans not counted as blocking from here
    int avoidedCount;                                   // +0x48
    C_Car *avoidedCar;                                  // +0x4C
};

// Called C_HumanCrowd to give it access to the human's parts. (0x008aae90 or so; the static object of 0x00563f90.)
class C_HumanCrowd {
public:
    C_HumanCrowd()                                      // 0x00563f90
    {
        m_scratch.reserve(100);
    }

    void Tick(int timeDelta, const std::vector<C_Actor *> &active);   // 0x005647c0
    S_PlanNode *Results() { return m_results; }

    // Finds the point at `pos`, from `*point` outward (0x00569890): 1 at it, 2 only near (the nearest then), 0 no start.
    int FindPointNear(const C_CheckpointNetwork *network, const S_Checkpoint **point, const S_vector &pos, int depth, const S_Checkpoint *hint);

    // (0x00564ef0) True: nobody near, nothing to work out.
    bool StepAlone(C_Human *human, float time);
    void Prepare(C_HumanAIMemory &memory, float time);  // 0x005656e0
    bool Replan(C_HumanAIMemory &memory);               // 0x0056a0f0
    bool IsPointBlocked(const C_HumanAIMemory &memory, const S_vector4 &point);   // 0x0056be90
    void ExpandNode(C_HumanAIMemory &memory, S_PlanNode *node, int depth);   // 0x0056a8e0
    void AddSideWay(S_PlanNode *node, S_PlanNode *child, int blockKind, const S_vector4 *corners, int cornerCount, C_Human *human, C_Car *car);
    // (0x0056c230; the original also returns the time of the hit, which nobody reads)
    int FindBlocker(C_HumanAIMemory &memory, const S_PlanNode *from, S_PlanNode *to, S_vector4 *left, int *leftCount, S_vector4 *right, int *rightCount, C_Human **human, C_Car **car);
    void CollectBest(C_HumanAIMemory &memory, const S_PlanNode *node, float cost, bool notRoot, S_PlanNode **path, int depth);   // 0x0056a6d0
    void PushFromCar(C_HumanAIMemory &memory, int car, float step);   // 0x00568580
    // (0x00568dc0; a third argument in the original, not used)
    void PushTargetFromCar(C_HumanAIMemory &memory, int car);

    std::vector<C_Human *> m_humans;                    // +0x04
    std::vector<C_Car *> m_cars;                        // +0x14
    std::vector<S_vector> m_carHalves;                  // +0x24 (free 0x0056e810 (DS 0x0056f6a0))
    std::vector<const S_Checkpoint *> m_scratch;        // +0x34
    S_PlanNode m_nodes[CROWD_MAX_NODES];                      // +0x40
    int m_nodeCount = 0;                                // +0x48
    S_PlanNode m_results[CROWD_MAX_PATH];                     // +0x4C
    int m_resultCount = 0;                              // +0x50
    float m_bestCost = 0.0f;                            // +0x54
    float m_bestDistance = 0.0f;                        // +0x58
    const S_Checkpoint *m_goalPoint = nullptr;          // +0x5C
    S_vector4 m_goal = {};                              // +0x60
};

extern C_HumanCrowd g_humanCrowd;                       // 0x008aae90 or so

namespace crowd {
void AddPush(S_vector4 &total, const S_vector &push);   // 0x00565540
bool KeepInsideStrip(S_vector &pos, const S_vector &a, float radiusA, const S_vector &b, float radiusB);   // 0x00567df0
bool KeepOnNetwork(const C_CheckpointNetwork *network, S_vector4 &pos, const S_Checkpoint *point, const S_Checkpoint **outPoint);   // 0x00568190
void SortCorners(const S_vector &pos, const S_vector &side, const S_vector &dir, S_vector corners[4], int *negative, int *positive, int *negativeCount, int *positiveCount);   // 0x0056de90
float FastInvSqrt(float value);
bool FirstRootAhead(float doubled, float discriminant, float a, float *t);
bool SideStepEllipsoid(const S_vector &l, const S_vector &r, float c, float scale, S_vector *out);
S_vector NormalizedZero(S_vector v);
void AdvanceWaypoint(C_HumanAIMemory &memory, float time);   // 0x005653c0
void MoveOneStep(C_HumanAIMemory &memory, float step);   // 0x00565f30
// (0x00566130; a third stack argument in the original, not used)
void PushFromHuman(C_HumanAIMemory &self, const C_HumanAIMemory &other);
void MoveTarget(C_HumanAIMemory &memory, const S_vector &by);
// (0x00566e50; a third stack argument in the original, not used)
void PushTargetFromHuman(C_HumanAIMemory &self, C_HumanAIMemory &other);
// (0x00567ab0; a second stack argument in the original, not used)
void PushOntoNetwork(C_HumanAIMemory &memory);
// (0x00567ba0; a second stack argument in the original, not used)
void HoldOnNetwork(C_HumanAIMemory &memory);
void TurnPushAside(C_HumanAIMemory &memory, float step);   // 0x00567c80
void UpdateStuckTimer(C_HumanAIMemory &memory, int16_t timeDelta);   // 0x00569530
}  // namespace crowd
