// Harness stub of game/campaign.h.
#pragma once

class C_MissionDef {
public:
    int GetId() const { return m_id; }
    int m_id = 0;
};

class C_Campaign {
public:
    C_MissionDef *GetCurrentMission();
    int GetCurrentIndex() const { return m_current; }
    int GetMissionNumber(int campaign);
    int CountMissions(int campaign);
    int GetMissionId(int campaign, int missionNumber);
    int m_current = 1;
};
