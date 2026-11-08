// The "vehicle" console commands: single player, a vehicle made next to the player, kept in saved
// games. Not in the original game: a port of a community patch to its executable ("vehicle v3",
// x86 assembly; its handler at 0x00BBC000, factory 0x00BBC049, save hooks in 0x0069CD94 / 0x0069D432).
#pragma once

class C_chunk;

// `line` as typed in the chat line. True: it was one of these commands (nothing else should see it).
bool ExecuteVehicleSpawnCommand(const char *line);

// The saved game's chunk of the spawned vehicles, written before the modules' so that the actors'
// chunks line up when they are read. Nothing is written when no vehicle was spawned: such a saved
// game is the same as ever. The chunk's form is the patch's, so its saved games still load.
void SaveSpawnedVehicles(C_chunk &chunk);
constexpr unsigned short CHUNK_GAME_SPAWNED_VEHICLES = 0x7A54;
// Inside that chunk: makes the saved vehicles again and removes those the saved game does not have.
void LoadSpawnedVehicles(C_chunk &chunk);
// A saved game without the chunk: loading in the same scene, the spawned vehicles still there go.
void RemoveSpawnedVehicles();
