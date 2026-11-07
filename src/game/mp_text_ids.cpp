// The multiplayer texts' ids: the voice chat's codecs, the game types (part 9i-ii; not linked in the harness,
// whose builds fake them).
#include "game/pending.h"

namespace {

// The codecs 0 .. 7 and any other; the game types -1 (any) .. 3 and any other.
const int VOICE_CODEC_TEXTS[8] = {0xB40, 0xB41, 0xB42, 0xB43, 0xB44, 0xB45, 0xB46, 0xB47};
constexpr int VOICE_CODEC_OTHER = 0xB48;
const int GAME_TYPE_TEXTS[5] = {0xB4A, 0xB4B, 0xB4C, 0xB4D, 0x4E21};
constexpr int GAME_TYPE_OTHER = 0xB4E;

} // namespace

int GetVoiceCodecTextId(int codec)
{
    return codec >= 0 && codec < 8 ? VOICE_CODEC_TEXTS[codec] : VOICE_CODEC_OTHER;
}

int GetGameTypeTextId(int gameType)
{
    return gameType >= -1 && gameType < 4 ? GAME_TYPE_TEXTS[gameType + 1] : GAME_TYPE_OTHER;
}
