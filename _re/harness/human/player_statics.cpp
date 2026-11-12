// The animation player (game/human_anim_player.cpp) compiled here, in a translation unit of its own (HUMAN_PLAYER
// builds, build_player.bat): its update statics live in an anonymous namespace, which this file can reach. The table
// gives each one's place and size on the port's side and its address in the original executable.
#define private public
#include "game/human_anim_player.cpp"   // (through -I src: a mutation run's mutated copy, mutrun.py MUT_WRAPPER)

struct S_PlayerStatic {
    const char *name;
    void *port;
    unsigned size;
    unsigned orig;
};

#define PLAYER_STATIC(v, at) {#v, &v, sizeof(v), at}
extern const S_PlayerStatic g_playerStatics[] = {
    PLAYER_STATIC(g_curAnim, 0x009833f0),
    PLAYER_STATIC(g_curCarry, 0x009833e4),
    PLAYER_STATIC(g_curActivity, 0x009833dc),
    PLAYER_STATIC(g_nextAnim, 0x009833cc),
    PLAYER_STATIC(g_nextCarry, 0x009833c0),
    PLAYER_STATIC(g_nextActivity, 0x009833b8),
    PLAYER_STATIC(g_oldAnim, 0x00983478),
    PLAYER_STATIC(g_oldCarry, 0x0098346c),
    PLAYER_STATIC(g_oldActivity, 0x00983464),
    PLAYER_STATIC(g_jointFrames, 0x00983400),
    PLAYER_STATIC(g_jointCount, 0x0084e934),
    PLAYER_STATIC(g_activityMode, 0x00983488),
    PLAYER_STATIC(g_activityTable, 0x0098348c),
    PLAYER_STATIC(g_carryMode, 0x00983490),
    PLAYER_STATIC(g_carryTable, 0x00983494),
    PLAYER_STATIC(g_animMode, 0x00983498),
    PLAYER_STATIC(g_animTable, 0x0098349c),
    PLAYER_STATIC(g_startAnims, 0x009834a0),
    PLAYER_STATIC(g_anims, 0x009834f0),
    PLAYER_STATIC(g_changed, 0x00983540),
    PLAYER_STATIC(g_animSet, 0x00983544),
    PLAYER_STATIC(g_carrySet, 0x00983548),
    PLAYER_STATIC(g_activitySet, 0x0098354c),
    PLAYER_STATIC(g_animSpeed, 0x0084e924),
    PLAYER_STATIC(g_animBlendTime, 0x0084e928),
    PLAYER_STATIC(g_carrySpeed, 0x0084e92c),
    PLAYER_STATIC(g_activitySpeed, 0x0084e930),
};
extern const int g_playerStaticCount = sizeof(g_playerStatics) / sizeof(g_playerStatics[0]);
