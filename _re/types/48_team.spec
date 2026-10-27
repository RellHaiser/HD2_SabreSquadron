# Teams, formations and the team manager (src/game/team.h).

# Shape of a formation and its member slots (16-byte aligned; not ported, partly known).
struct C_FormationShape size=0xa0
  0x74 C_Human* m_leader
  0x78 int m_type                         # E_FormationType
  0x7c int m_memberCount
  0x8c bool m_unknown8C                   # set for the scripts' formations
  0x90 void* m_slots                      # 6 entries of 0xA0 bytes (+0x70: the member)
end

# An enemy a member of the team has seen.
struct S_EnemyRecord size=0x10
  0x00 C_Human* enemy
  0x04 int seenBy                         # members that see it now
  0x08 bool alive                         # cleared when it is recorded dead
  0x0c int forgetTime                     # ms left once nobody sees it (0: kept)
end

# The enemies a team knows (records of living enemies first).
struct C_EnemyRecords size=0x20
  0x00 std::vector<S_EnemyRecord*> m_records
  0x10 bool m_changed
  0x14 int m_aliveCount
  0x18 int m_deadCount
  0x1c bool m_tacticalView                # enemies fade in / out as they are seen / lost
end

# Members of a team.
class C_TeamBase size=0x28 vtable=0x80fcd8:9
  0x04 std::vector<C_Human*> m_members
  0x14 std::string m_name
  0x24 int m_id                           # formations: their index; teams: -1
  vslot 0x00 Clear | void __thiscall Clear(C_TeamBase *this)
  vslot 0x01 DeletingDestructor | C_TeamBase * __thiscall DeletingDestructor(C_TeamBase *this, uint flags)
  vslot 0x02 AddMember | bool __thiscall AddMember(C_TeamBase *this, C_Human * human)
  vslot 0x03 RemoveMember | bool __thiscall RemoveMember(C_TeamBase *this, C_Human * human, int * index)
  vslot 0x04 Update | void __thiscall Update(C_TeamBase *this, int timeDelta)
  vslot 0x05 Tick | void __thiscall Tick(C_TeamBase *this, int timeDelta)
  vslot 0x06 RestartMission | void __thiscall RestartMission(C_TeamBase *this, bool server)
  vslot 0x07 SaveGame | bool __thiscall SaveGame(C_TeamBase *this, C_chunk * chunk)
  vslot 0x08 LoadGame | bool __thiscall LoadGame(C_TeamBase *this, C_chunk * chunk)
end

# The player's squad (multiplayer: the players' humans).
class C_Team : C_TeamBase size=0xcc vtable=0x80fc20:10
  0x28 int m_selected
  0x2c int m_active
  0x30 int m_commanded
  0x34 uint32_t m_environment             # the members' HUMAN_ENV_* last tick
  0x38 C_EnemyRecords m_enemyRecords
  0x58 C_Human* m_watchedHuman            # spectating: the human the camera follows
  0x5c std::vector<void*> m_volumes       # per member: std::vector<I3D_frame*>* of its collision volumes
  0x6c std::vector<void*> m_unused6C[5]
  0xbc std::vector<C_MeleeTracker*> m_seenMeleeTrackers
  vslot 0x09 RemoveHuman | bool __thiscall RemoveHuman(C_Team *this, C_Human * human)
end

# The players of a co-operative game (server).
class C_CoopTeam : C_TeamBase size=0x4c vtable=0x80fc4c:10
  0x28 C_EnemyRecords m_enemyRecords
  0x48 int m_unknown48
  vslot 0x09 RemoveHuman | bool __thiscall RemoveHuman(C_CoopTeam *this, C_Human * human)
end

# Humans a mission script keeps in a formation behind a leader.
class C_Formation : C_TeamBase size=0x2c vtable=0x80fc74:9
  0x28 C_FormationShape* m_shape
end

class C_TeamManager : C_Module size=0x138 vtable=0x80fc98:16
  0x0c std::vector<C_Formation*> m_formations
  0x1c C_Team m_playerTeam
  0xe8 C_CoopTeam m_coopTeam
  0x134 bool m_clientRunning
  0x135 bool m_unknown135
end

# The squad's commands (src/game/squad_commander.h; the object 0x0089ad40): the HUD's commands
# to the soldiers, the commands on actors, the manoeuvres, the groups the HUD lists.
class C_SquadCommander size=0x130 vtable=0x80fb28:33
  0x030 int m_timer3C                     # ms until m_flag3C clears
  0x034 int m_timer40                     # ms until m_flag40 clears
  0x038 int m_unknown38
  0x03c bool m_flag3C
  0x03d bool m_flag3D
  0x03e bool m_flag3E
  0x03f bool m_flag3F
  0x040 bool m_flag40
  0x041 bool m_flag41
  0x042 bool m_flag42
  0x043 bool m_whispering
  0x044 int m_unknown44                   # 5000 (only set)
  0x048 std::vector<C_Human*> m_groups[5] # 0 near / busy, 1 idle near, 2 busy (3, 4 empty)
  0x098 C_Actor* m_squadTarget            # the one the squad turned on (OnFriendlyFire)
  0x09c int m_squadTargetTime             # ms
  0x0a0 int m_manoeuvre                   # -1 none, 3 ladder, 0x12 grenade into a room, 0x15 through a door
  0x0a4 int m_stage
  0x0a8 int m_waiting                     # members still to arrive
  0x0b0 S_vector4 m_doorPos               # the door's far side
  0x0c0 S_vector4 m_doorDir               # the way through
  0x0d0 S_vector4 m_ladderPos             # the ladder's far end
  0x0e0 S_vector4 m_ladderDir
  0x0f0 C_Actor* m_manoeuvreActor
  0x0f4 std::vector<C_Human*> m_manoeuvreMembers
  0x110 S_vector4 m_formationScale        # 0.75 * spacing + 1.5
  0x120 int m_spacing                     # 0..7
end
