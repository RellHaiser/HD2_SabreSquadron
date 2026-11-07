// The original's copy protection, as a pass-through (part 11d; the owner's decision, 2026-11-05): a copy always counts as genuine.
//
// The original keeps shuffled tables and random values (0x008ca840, 0x008caf40, set up by InitProtectionTables) that about 70 places
// in the game test, some of them to make a cracked copy slowly go wrong. Those tests were left out as each part was ported (each says
// so where it was), always on the genuine copy's path. Nothing but the function below touches the tables any more, so there are none.
//
// Also left out: GetExecutableMd5 0x006ea340 and its MD5 (0x006ea490..0x006ea630): the original stamps the digest of its own exe into a
// saved game's chunk 0x3EA, and nothing checks it. The port keeps the chunk (interface.cpp's s_saveStamp).
#pragma once

// At the start of InitGraphics. Of the original's work only its srand is kept. Its seed is what the protection's own routine returns
// (CALL [0x00a59000], into the .dogui section; obfuscated, its value not known), so the port takes the tick instead. RunGame seeds again
// from the time after InitGraphics returns, so this seed only reaches rand() calls inside InitGraphics.
void InitProtectionTables();                            // 0x006ea270
