# Harness coverage (gcov)

- Revision: `15d91609a23a16625e0b6791e32a79d9c2898a59` plus 1 uncommitted changes
- Suites measured: 35 (car_tables, checkpoints, controls, crash, crowd, explosion, formation, net.commands, net.console, net.firewall, net.game, net.gamespy, net.messages, net.replication, net.session, net_quantization, profile, protection, quick_messages, script_module, speech, stains, vehicle, vehicle.camera, vehicle.cursor, vehicle.factory, vehicle.game_texts, vehicle.item, vehicle.rigid, vehicle.rigid_step, vehicle.route, vehicle.seat_weapon, vehicle.text, vehicle.world, vehicle_params)
- Client .cpp files no suite compiles: 117 of 271 (104813 source lines): listed at the end. The percentages are of the measured files only.
- Production sources: those under src/ that a measured suite compiles (its .gcno); the suites' own files and fakes are left out. Files no suite compiles are not in this table.
- Branches: gcov's branch outcomes, exception edges left out; a header's inline code counts once per line however many objects include it.

| | Lines | Functions | Branch outcomes taken |
|---|---:|---:|---:|
| **all 273 files** | 53050 / 55743 (95.2 %) | 4698 / 5174 (90.8 %) | 37873 / 45754 (82.8 %) |

| Source | Lines | Functions | Branch outcomes taken |
|---|---:|---:|---:|
| src/common/c_str.cpp | 66 / 88 | 11 / 13 | 26 / 53 (49.1 %) |
| src/common/c_str.h | 12 / 12 | 3 / 3 | 436 / 665 (65.6 %) |
| src/common/chunk.cpp | 97 / 99 | 16 / 16 | 56 / 74 (75.7 %) |
| src/common/chunk.h | 10 / 10 | 0 / 0 | 43 / 47 (91.5 %) |
| src/common/color.cpp | 20 / 44 | 2 / 2 | 8 / 40 (20.0 %) |
| src/common/config_file.cpp | 53 / 74 | 7 / 8 | 45 / 71 (63.4 %) |
| src/common/config_file.h | 1 / 1 | 0 / 0 | 0 / 0 (- %) |
| src/common/msvc_sort.h | 42 / 42 | 13 / 13 | 33 / 34 (97.1 %) |
| src/common/os_files.cpp | 48 / 62 | 11 / 14 | 21 / 30 (70.0 %) |
| src/game/actor.h | 19 / 62 | 5 / 45 | 179 / 293 (61.1 %) |
| src/game/air_attack.cpp | 676 / 677 | 46 / 46 | 402 / 453 (88.7 %) |
| src/game/air_attack.h | 5 / 7 | 1 / 3 | 3 / 3 (100.0 %) |
| src/game/animation_actor.cpp | 437 / 438 | 45 / 45 | 301 / 332 (90.7 %) |
| src/game/animation_actor.h | 5 / 5 | 1 / 1 | 24 / 42 (57.1 %) |
| src/game/barrel.cpp | 465 / 468 | 41 / 41 | 230 / 273 (84.2 %) |
| src/game/barrel.h | 3 / 3 | 3 / 3 | 0 / 0 (- %) |
| src/game/bike.cpp | 311 / 316 | 19 / 19 | 140 / 185 (75.7 %) |
| src/game/bike.h | 3 / 6 | 4 / 7 | 3 / 4 (75.0 %) |
| src/game/birds.cpp | 164 / 172 | 11 / 11 | 96 / 130 (73.8 %) |
| src/game/birds.h | 2 / 2 | 0 / 0 | 10 / 20 (50.0 %) |
| src/game/camera_module.cpp | 1245 / 1248 | 75 / 76 | 854 / 903 (94.6 %) |
| src/game/camera_module.h | 2 / 2 | 0 / 0 | 2 / 2 (100.0 %) |
| src/game/campaign.h | 16 / 25 | 0 / 9 | 35 / 38 (92.1 %) |
| src/game/car.cpp | 50 / 56 | 10 / 12 | 34 / 43 (79.1 %) |
| src/game/car.h | 40 / 42 | 4 / 8 | 100 / 117 (85.5 %) |
| src/game/car_callbacks.cpp | 247 / 247 | 27 / 27 | 215 / 217 (99.1 %) |
| src/game/car_effects.cpp | 113 / 122 | 9 / 9 | 42 / 56 (75.0 %) |
| src/game/car_hits.cpp | 320 / 325 | 36 / 36 | 251 / 280 (89.6 %) |
| src/game/car_internal.h | 3 / 3 | 0 / 0 | 38 / 38 (100.0 %) |
| src/game/car_marks.cpp | 59 / 64 | 8 / 8 | 42 / 55 (76.4 %) |
| src/game/car_mission.cpp | 234 / 236 | 9 / 9 | 160 / 178 (89.9 %) |
| src/game/car_net.cpp | 366 / 377 | 31 / 31 | 226 / 275 (82.2 %) |
| src/game/car_parts.cpp | 246 / 246 | 22 / 22 | 184 / 195 (94.4 %) |
| src/game/car_save.cpp | 83 / 83 | 3 / 3 | 62 / 63 (98.4 %) |
| src/game/car_seats.cpp | 249 / 255 | 33 / 33 | 202 / 234 (86.3 %) |
| src/game/car_tables.cpp | 120 / 120 | 8 / 8 | 71 / 72 (98.6 %) |
| src/game/car_update.cpp | 238 / 238 | 17 / 17 | 245 / 254 (96.5 %) |
| src/game/checkpoint.cpp | 102 / 138 | 11 / 16 | 53 / 108 (49.1 %) |
| src/game/checkpoint_network.cpp | 408 / 471 | 33 / 43 | 329 / 443 (74.3 %) |
| src/game/checkpoint_search.cpp | 288 / 325 | 21 / 28 | 197 / 292 (67.5 %) |
| src/game/checkpoints.cpp | 22 / 48 | 3 / 7 | 10 / 22 (45.5 %) |
| src/game/checkpoints.h | 10 / 11 | 1 / 1 | 12 / 17 (70.6 %) |
| src/game/clock.cpp | 72 / 72 | 12 / 12 | 29 / 34 (85.3 %) |
| src/game/clock.h | 4 / 4 | 4 / 5 | 0 / 0 (- %) |
| src/game/collision.h | 2 / 2 | 1 / 1 | 8 / 8 (100.0 %) |
| src/game/control.cpp | 226 / 226 | 41 / 41 | 122 / 124 (98.4 %) |
| src/game/control.h | 12 / 17 | 2 / 8 | 665 / 677 (98.2 %) |
| src/game/control_factory.cpp | 52 / 52 | 2 / 2 | 66 / 67 (98.5 %) |
| src/game/controls.cpp | 3456 / 3476 | 360 / 361 | 2281 / 2486 (91.8 %) |
| src/game/controls.h | 94 / 98 | 75 / 115 | 120 / 140 (85.7 %) |
| src/game/copy_protection.cpp | 19 / 19 | 5 / 5 | 10 / 10 (100.0 %) |
| src/game/crash_report.cpp | 190 / 248 | 28 / 30 | 46 / 98 (46.9 %) |
| src/game/cursor.cpp | 177 / 177 | 21 / 21 | 77 / 80 (96.2 %) |
| src/game/cursor.h | 6 / 6 | 1 / 1 | 27 / 44 (61.4 %) |
| src/game/debriefing.cpp | 237 / 238 | 13 / 13 | 178 / 186 (95.7 %) |
| src/game/debriefing.h | 2 / 4 | 1 / 3 | 0 / 0 (- %) |
| src/game/debriefing_screens.cpp | 281 / 281 | 19 / 19 | 167 / 169 (98.8 %) |
| src/game/debris.cpp | 44 / 44 | 11 / 11 | 15 / 15 (100.0 %) |
| src/game/debris.h | 11 / 11 | 11 / 11 | 0 / 0 (- %) |
| src/game/decorators.cpp | 751 / 751 | 42 / 42 | 569 / 608 (93.6 %) |
| src/game/decorators.h | 7 / 7 | 2 / 2 | 20 / 20 (100.0 %) |
| src/game/document_place.cpp | 161 / 161 | 24 / 24 | 67 / 73 (91.8 %) |
| src/game/document_place.h | 5 / 5 | 2 / 2 | 24 / 46 (52.2 %) |
| src/game/door.cpp | 574 / 575 | 46 / 46 | 309 / 355 (87.0 %) |
| src/game/door.h | 11 / 12 | 5 / 6 | 29 / 53 (54.7 %) |
| src/game/equipment_frames.cpp | 26 / 30 | 6 / 6 | 15 / 28 (53.6 %) |
| src/game/equipment_menu.h | 1 / 6 | 0 / 5 | 0 / 0 (- %) |
| src/game/equipment_panel.cpp | 470 / 471 | 47 / 47 | 343 / 356 (96.3 %) |
| src/game/equipment_panel.h | 4 / 4 | 1 / 3 | 12 / 12 (100.0 %) |
| src/game/equipment_screen.cpp | 1002 / 1004 | 76 / 76 | 823 / 883 (93.2 %) |
| src/game/equipment_screen.h | 1 / 1 | 1 / 1 | 0 / 0 (- %) |
| src/game/explosion_wave.cpp | 252 / 255 | 19 / 19 | 199 / 233 (85.4 %) |
| src/game/explosion_wave.h | 2 / 2 | 1 / 1 | 1 / 2 (50.0 %) |
| src/game/explosive_place.cpp | 250 / 250 | 29 / 29 | 93 / 100 (93.0 %) |
| src/game/explosive_place.h | 7 / 7 | 4 / 5 | 16 / 26 (61.5 %) |
| src/game/first_person_view.h | 2 / 2 | 0 / 0 | 16 / 16 (100.0 %) |
| src/game/fish.cpp | 281 / 284 | 21 / 21 | 111 / 141 (78.7 %) |
| src/game/fish.h | 3 / 5 | 4 / 6 | 3 / 4 (75.0 %) |
| src/game/font.cpp | 397 / 397 | 41 / 41 | 290 / 310 (93.5 %) |
| src/game/font.h | 8 / 12 | 4 / 8 | 23 / 29 (79.3 %) |
| src/game/formation.cpp | 605 / 607 | 37 / 37 | 481 / 515 (93.4 %) |
| src/game/fowl_generator.cpp | 202 / 202 | 18 / 18 | 177 / 193 (91.7 %) |
| src/game/fowl_generator.h | 4 / 4 | 5 / 5 | 0 / 0 (- %) |
| src/game/frame_box_volume.h | 25 / 25 | 1 / 1 | 15 / 18 (83.3 %) |
| src/game/frame_volume.h | 27 / 28 | 1 / 1 | 9 / 12 (75.0 %) |
| src/game/game.h | 31 / 32 | 0 / 1 | 94 / 101 (93.1 %) |
| src/game/game_menu.cpp | 1105 / 1107 | 81 / 81 | 844 / 890 (94.8 %) |
| src/game/game_menu.h | 4 / 4 | 1 / 1 | 7 / 8 (87.5 %) |
| src/game/game_state.cpp | 26 / 26 | 13 / 14 | 2 / 2 (100.0 %) |
| src/game/game_state.h | 1 / 1 | 0 / 0 | 4 / 4 (100.0 %) |
| src/game/game_texts.cpp | 57 / 57 | 7 / 7 | 70 / 73 (95.9 %) |
| src/game/game_texts.h | 3 / 3 | 0 / 0 | 14 / 16 (87.5 %) |
| src/game/geffects.cpp | 904 / 908 | 64 / 66 | 693 / 746 (92.9 %) |
| src/game/geffects.h | 0 / 1 | 0 / 1 | 0 / 0 (- %) |
| src/game/hero_stats_menu.cpp | 141 / 141 | 12 / 12 | 63 / 75 (84.0 %) |
| src/game/hero_stats_menu.h | 2 / 3 | 0 / 1 | 1 / 1 (100.0 %) |
| src/game/human.h | 41 / 41 | 1 / 1 | 22 / 22 (100.0 %) |
| src/game/human_controller.h | 2 / 2 | 0 / 0 | 3 / 4 (75.0 %) |
| src/game/human_crowd.cpp | 1020 / 1028 | 54 / 54 | 765 / 830 (92.2 %) |
| src/game/human_crowd.h | 4 / 4 | 1 / 1 | 2 / 2 (100.0 %) |
| src/game/human_equipment.h | 1 / 1 | 0 / 0 | 0 / 0 (- %) |
| src/game/human_movement.h | 2 / 2 | 0 / 0 | 2 / 2 (100.0 %) |
| src/game/human_stains.cpp | 568 / 580 | 48 / 49 | 373 / 462 (80.7 %) |
| src/game/human_stains.h | 7 / 7 | 2 / 2 | 28 / 40 (70.0 %) |
| src/game/input_controls.cpp | 335 / 338 | 23 / 23 | 291 / 304 (95.7 %) |
| src/game/interface.h | 3 / 3 | 0 / 0 | 6 / 6 (100.0 %) |
| src/game/item_actor.cpp | 276 / 276 | 36 / 36 | 129 / 140 (92.1 %) |
| src/game/item_actor.h | 10 / 10 | 9 / 9 | 0 / 0 (- %) |
| src/game/item_defs.h | 26 / 41 | 1 / 16 | 59 / 67 (88.1 %) |
| src/game/items.h | 6 / 29 | 0 / 23 | 11 / 12 (91.7 %) |
| src/game/items_module.h | 1 / 1 | 0 / 0 | 0 / 0 (- %) |
| src/game/junkers88.cpp | 1414 / 1419 | 132 / 132 | 894 / 1033 (86.5 %) |
| src/game/junkers88.h | 11 / 13 | 4 / 6 | 3 / 3 (100.0 %) |
| src/game/key_bindings.cpp | 138 / 217 | 19 / 23 | 93 / 204 (45.6 %) |
| src/game/key_bindings.h | 2 / 2 | 0 / 0 | 6 / 6 (100.0 %) |
| src/game/ladder.cpp | 179 / 179 | 19 / 19 | 92 / 104 (88.5 %) |
| src/game/ladder.h | 13 / 13 | 6 / 6 | 14 / 21 (66.7 %) |
| src/game/lightmap_groups.cpp | 104 / 105 | 18 / 20 | 72 / 77 (93.5 %) |
| src/game/lightmap_groups.h | 2 / 2 | 0 / 0 | 2 / 2 (100.0 %) |
| src/game/loader_screen.cpp | 192 / 192 | 9 / 9 | 212 / 219 (96.8 %) |
| src/game/loader_screen.h | 2 / 2 | 1 / 1 | 6 / 6 (100.0 %) |
| src/game/loadout_presets.cpp | 154 / 154 | 21 / 21 | 89 / 90 (98.9 %) |
| src/game/loadout_presets.h | 5 / 5 | 1 / 1 | 5 / 10 (50.0 %) |
| src/game/main_menu.cpp | 2023 / 2030 | 128 / 129 | 1081 / 1126 (96.0 %) |
| src/game/main_menu.h | 3 / 3 | 4 / 4 | 0 / 0 (- %) |
| src/game/melee_tracker.h | 2 / 2 | 0 / 0 | 3 / 4 (75.0 %) |
| src/game/melee_tracker_route.cpp | 210 / 210 | 14 / 14 | 167 / 180 (92.8 %) |
| src/game/menu_soldier.cpp | 464 / 465 | 22 / 22 | 255 / 281 (90.7 %) |
| src/game/menu_soldier.h | 1 / 1 | 1 / 1 | 0 / 0 (- %) |
| src/game/menu_state.cpp | 479 / 481 | 31 / 32 | 332 / 346 (96.0 %) |
| src/game/menu_state.h | 12 / 12 | 9 / 9 | 2 / 2 (100.0 %) |
| src/game/mesh_player.cpp | 83 / 83 | 9 / 9 | 69 / 78 (88.5 %) |
| src/game/mine.cpp | 156 / 156 | 20 / 20 | 69 / 78 (88.5 %) |
| src/game/mine.h | 9 / 10 | 8 / 9 | 10 / 14 (71.4 %) |
| src/game/mission_loader.cpp | 552 / 552 | 18 / 18 | 653 / 681 (95.9 %) |
| src/game/mission_loader.h | 2 / 2 | 2 / 2 | 2 / 4 (50.0 %) |
| src/game/module.h | 1 / 16 | 0 / 15 | 3 / 6 (50.0 %) |
| src/game/mp_game_setup.cpp | 505 / 605 | 51 / 53 | 272 / 414 (65.7 %) |
| src/game/mp_game_setup.h | 10 / 10 | 4 / 4 | 16 / 18 (88.9 %) |
| src/game/mp_settings.cpp | 173 / 173 | 19 / 19 | 74 / 86 (86.0 %) |
| src/game/mp_zone.cpp | 365 / 372 | 31 / 31 | 306 / 344 (89.0 %) |
| src/game/mp_zone.h | 7 / 7 | 4 / 5 | 4 / 5 (80.0 %) |
| src/game/null_actor.cpp | 15 / 15 | 5 / 5 | 1 / 2 (50.0 %) |
| src/game/null_actor.h | 9 / 9 | 5 / 6 | 3 / 3 (100.0 %) |
| src/game/operation_end.cpp | 45 / 45 | 4 / 4 | 33 / 35 (94.3 %) |
| src/game/operation_end.h | 3 / 7 | 0 / 4 | 5 / 5 (100.0 %) |
| src/game/options_handler.cpp | 373 / 373 | 25 / 25 | 181 / 191 (94.8 %) |
| src/game/options_handler.h | 3 / 3 | 0 / 0 | 40 / 40 (100.0 %) |
| src/game/paged_text_box.cpp | 250 / 251 | 25 / 25 | 247 / 268 (92.2 %) |
| src/game/paged_text_box.h | 2 / 2 | 2 / 2 | 1 / 1 (100.0 %) |
| src/game/particles.h | 1 / 1 | 0 / 0 | 1 / 1 (100.0 %) |
| src/game/path_points.cpp | 62 / 62 | 5 / 5 | 47 / 47 (100.0 %) |
| src/game/pending.h | 2 / 4 | 0 / 2 | 2 / 4 (50.0 %) |
| src/game/perf_timer.cpp | 22 / 22 | 6 / 6 | 4 / 4 (100.0 %) |
| src/game/periodic_links.cpp | 166 / 172 | 16 / 17 | 158 / 176 (89.8 %) |
| src/game/photo_place.cpp | 57 / 57 | 11 / 11 | 22 / 25 (88.0 %) |
| src/game/photo_place.h | 5 / 5 | 3 / 4 | 6 / 12 (50.0 %) |
| src/game/physics.h | 2 / 2 | 0 / 0 | 1 / 1 (100.0 %) |
| src/game/pike.cpp | 267 / 267 | 23 / 23 | 119 / 132 (90.2 %) |
| src/game/pike.h | 5 / 5 | 4 / 5 | 17 / 28 (60.7 %) |
| src/game/player_profile.cpp | 294 / 528 | 33 / 51 | 178 / 490 (36.3 %) |
| src/game/profile.h | 15 / 16 | 3 / 5 | 21 / 35 (60.0 %) |
| src/game/profile_misc.cpp | 152 / 176 | 25 / 30 | 89 / 127 (70.1 %) |
| src/game/profile_roster.cpp | 181 / 286 | 19 / 26 | 87 / 193 (45.1 %) |
| src/game/profile_squad.cpp | 305 / 389 | 30 / 42 | 135 / 229 (59.0 %) |
| src/game/quick_messages.cpp | 171 / 171 | 17 / 17 | 129 / 134 (96.3 %) |
| src/game/radio.cpp | 93 / 93 | 17 / 17 | 43 / 47 (91.5 %) |
| src/game/radio.h | 5 / 5 | 3 / 4 | 12 / 24 (50.0 %) |
| src/game/resource_cache.h | 0 / 5 | 0 / 5 | 0 / 0 (- %) |
| src/game/rigid_body.cpp | 525 / 527 | 24 / 24 | 226 / 246 (91.9 %) |
| src/game/rigid_body.h | 2 / 2 | 0 / 0 | 4 / 4 (100.0 %) |
| src/game/rigid_body_internal.h | 22 / 23 | 2 / 2 | 38 / 48 (79.2 %) |
| src/game/rigid_body_step.cpp | 405 / 409 | 8 / 8 | 160 / 174 (92.0 %) |
| src/game/screen.cpp | 287 / 290 | 37 / 38 | 148 / 154 (96.1 %) |
| src/game/screen.h | 11 / 11 | 4 / 4 | 48 / 71 (67.6 %) |
| src/game/seat_weapon.cpp | 799 / 802 | 70 / 70 | 510 / 573 (89.0 %) |
| src/game/seat_weapon.h | 8 / 8 | 2 / 2 | 6 / 8 (75.0 %) |
| src/game/session_menu.cpp | 198 / 198 | 13 / 13 | 67 / 70 (95.7 %) |
| src/game/session_menu.h | 2 / 5 | 1 / 4 | 0 / 0 (- %) |
| src/game/sight.cpp | 510 / 517 | 46 / 48 | 424 / 488 (86.9 %) |
| src/game/sight.h | 9 / 13 | 4 / 7 | 8 / 15 (53.3 %) |
| src/game/soldier.h | 1 / 1 | 0 / 0 | 0 / 0 (- %) |
| src/game/sound_module.h | 2 / 2 | 0 / 0 | 9 / 10 (90.0 %) |
| src/game/sound_table.h | 3 / 3 | 1 / 1 | 5 / 5 (100.0 %) |
| src/game/spawn_point.cpp | 222 / 222 | 13 / 13 | 190 / 202 (94.1 %) |
| src/game/spawn_point.h | 10 / 10 | 6 / 7 | 10 / 20 (50.0 %) |
| src/game/speech_slots.cpp | 295 / 302 | 27 / 27 | 150 / 165 (90.9 %) |
| src/game/squad_menu.cpp | 725 / 727 | 54 / 54 | 477 / 504 (94.6 %) |
| src/game/squad_menu.h | 2 / 3 | 1 / 2 | 0 / 0 (- %) |
| src/game/static_weapon.cpp | 469 / 481 | 49 / 50 | 289 / 347 (83.3 %) |
| src/game/static_weapon.h | 22 / 24 | 19 / 21 | 38 / 54 (70.4 %) |
| src/game/store_list.cpp | 195 / 195 | 22 / 22 | 118 / 123 (95.9 %) |
| src/game/store_list.h | 6 / 6 | 0 / 2 | 20 / 23 (87.0 %) |
| src/game/submarine.cpp | 466 / 474 | 42 / 42 | 303 / 361 (83.9 %) |
| src/game/submarine.h | 4 / 5 | 2 / 3 | 4 / 4 (100.0 %) |
| src/game/supply_box.cpp | 684 / 684 | 48 / 48 | 253 / 299 (84.6 %) |
| src/game/supply_box.h | 8 / 10 | 4 / 6 | 21 / 40 (52.5 %) |
| src/game/switch_actor.cpp | 128 / 128 | 19 / 19 | 50 / 55 (90.9 %) |
| src/game/switch_actor.h | 3 / 3 | 2 / 3 | 21 / 37 (56.8 %) |
| src/game/team.h | 8 / 8 | 0 / 0 | 69 / 134 (51.5 %) |
| src/game/text_box.cpp | 802 / 802 | 81 / 81 | 642 / 676 (95.0 %) |
| src/game/text_box.h | 8 / 8 | 0 / 0 | 28 / 36 (77.8 %) |
| src/game/text_style.cpp | 34 / 40 | 8 / 9 | 24 / 30 (80.0 %) |
| src/game/track.cpp | 73 / 362 | 9 / 35 | 33 / 251 (13.1 %) |
| src/game/track.h | 7 / 9 | 1 / 2 | 51 / 66 (77.3 %) |
| src/game/train.cpp | 479 / 480 | 32 / 32 | 327 / 374 (87.4 %) |
| src/game/train.h | 4 / 6 | 1 / 3 | 5 / 7 (71.4 %) |
| src/game/ts_explosion.cpp | 193 / 193 | 23 / 23 | 62 / 72 (86.1 %) |
| src/game/ts_explosion.h | 4 / 4 | 3 / 4 | 5 / 10 (50.0 %) |
| src/game/tshift.cpp | 38 / 39 | 9 / 11 | 34 / 38 (89.5 %) |
| src/game/tshift.h | 1 / 1 | 0 / 0 | 0 / 0 (- %) |
| src/game/vehicle.cpp | 329 / 506 | 89 / 112 | 216 / 531 (40.7 %) |
| src/game/vehicle.h | 7 / 7 | 0 / 0 | 4 / 6 (66.7 %) |
| src/game/vehicle_collide.cpp | 169 / 212 | 18 / 18 | 111 / 175 (63.4 %) |
| src/game/vehicle_damage.cpp | 262 / 281 | 10 / 10 | 222 / 275 (80.7 %) |
| src/game/vehicle_drive.cpp | 286 / 310 | 25 / 27 | 176 / 244 (72.1 %) |
| src/game/vehicle_drivetrain.cpp | 508 / 592 | 14 / 14 | 437 / 590 (74.1 %) |
| src/game/vehicle_effects.cpp | 159 / 163 | 12 / 12 | 139 / 160 (86.9 %) |
| src/game/vehicle_lamps.cpp | 108 / 121 | 6 / 6 | 126 / 168 (75.0 %) |
| src/game/vehicle_params.cpp | 172 / 173 | 16 / 16 | 95 / 102 (93.1 %) |
| src/game/vehicle_parts.cpp | 210 / 308 | 9 / 11 | 193 / 349 (55.3 %) |
| src/game/vehicle_setup.cpp | 608 / 664 | 27 / 27 | 357 / 472 (75.6 %) |
| src/game/vehicle_skid_marks.cpp | 119 / 121 | 5 / 5 | 49 / 66 (74.2 %) |
| src/game/vehicle_solve.cpp | 348 / 394 | 15 / 16 | 222 / 308 (72.1 %) |
| src/game/vehicle_sounds.cpp | 451 / 451 | 44 / 44 | 412 / 444 (92.8 %) |
| src/game/vehicle_start.cpp | 630 / 670 | 21 / 21 | 234 / 318 (73.6 %) |
| src/game/vehicle_state.cpp | 509 / 521 | 20 / 20 | 137 / 165 (83.0 %) |
| src/game/vehicle_update.cpp | 320 / 354 | 12 / 12 | 233 / 290 (80.3 %) |
| src/game/vehicle_wheels.cpp | 247 / 281 | 15 / 16 | 188 / 270 (69.6 %) |
| src/game/vertex_anim.cpp | 222 / 223 | 22 / 24 | 229 / 249 (92.0 %) |
| src/game/vertex_anim.h | 3 / 5 | 0 / 2 | 11 / 12 (91.7 %) |
| src/game/video_state.cpp | 324 / 324 | 20 / 20 | 217 / 225 (96.4 %) |
| src/game/video_state.h | 10 / 11 | 6 / 10 | 6 / 11 (54.5 %) |
| src/game/video_text_box.cpp | 30 / 30 | 3 / 3 | 16 / 16 (100.0 %) |
| src/game/video_text_box.h | 3 / 3 | 2 / 3 | 4 / 5 (80.0 %) |
| src/game/volumetrics.cpp | 428 / 430 | 22 / 22 | 242 / 270 (89.6 %) |
| src/game/volumetrics.h | 2 / 2 | 0 / 0 | 2 / 2 (100.0 %) |
| src/game/wire_cut.cpp | 90 / 90 | 18 / 18 | 40 / 46 (87.0 %) |
| src/game/wire_cut.h | 6 / 6 | 4 / 5 | 2 / 4 (50.0 %) |
| src/game/world.h | 1 / 1 | 0 / 0 | 1 / 2 (50.0 %) |
| src/game/xml_node.cpp | 120 / 122 | 13 / 13 | 107 / 125 (85.6 %) |
| src/game/xml_node.h | 4 / 4 | 0 / 0 | 45 / 58 (77.6 %) |
| src/ls3d/i3d.h | 163 / 167 | 14 / 14 | 815 / 1338 (60.9 %) |
| src/ls3d/math.h | 48 / 48 | 4 / 4 | 877 / 1681 (52.2 %) |
| src/net/bitstream.cpp | 129 / 157 | 21 / 28 | 87 / 100 (87.0 %) |
| src/net/bitstream.h | 20 / 20 | 1 / 3 | 83 / 134 (61.9 %) |
| src/net/gamespy_browser.cpp | 884 / 942 | 83 / 90 | 446 / 524 (85.1 %) |
| src/net/gamespy_gcd.cpp | 222 / 223 | 23 / 23 | 101 / 112 (90.2 %) |
| src/net/gamespy_qr2.cpp | 347 / 359 | 28 / 28 | 188 / 220 (85.5 %) |
| src/net/gamespy_util.cpp | 192 / 435 | 29 / 68 | 66 / 213 (31.0 %) |
| src/net/net_channel.cpp | 262 / 262 | 43 / 44 | 99 / 100 (99.0 %) |
| src/net/net_channel.h | 16 / 19 | 6 / 15 | 10 / 10 (100.0 %) |
| src/net/net_commands.cpp | 394 / 394 | 7 / 7 | 248 / 273 (90.8 %) |
| src/net/net_console.cpp | 23 / 24 | 2 / 2 | 25 / 30 (83.3 %) |
| src/net/net_console_commands.cpp | 335 / 338 | 20 / 21 | 276 / 281 (98.2 %) |
| src/net/net_console_vars.cpp | 182 / 188 | 16 / 16 | 187 / 197 (94.9 %) |
| src/net/net_firewall.cpp | 180 / 180 | 10 / 10 | 78 / 98 (79.6 %) |
| src/net/net_game.cpp | 551 / 557 | 49 / 50 | 350 / 380 (92.1 %) |
| src/net/net_game.h | 5 / 5 | 0 / 0 | 7 / 7 (100.0 %) |
| src/net/net_messages.cpp | 488 / 488 | 15 / 15 | 257 / 267 (96.3 %) |
| src/net/net_object.cpp | 386 / 386 | 63 / 63 | 196 / 204 (96.1 %) |
| src/net/net_object.h | 19 / 21 | 3 / 8 | 16 / 25 (64.0 %) |
| src/net/net_players.cpp | 448 / 448 | 38 / 39 | 331 / 347 (95.4 %) |
| src/net/net_search.cpp | 365 / 365 | 28 / 28 | 173 / 180 (96.1 %) |
| src/net/net_session.cpp | 555 / 562 | 44 / 44 | 238 / 271 (87.8 %) |
| src/net/net_socket.cpp | 225 / 227 | 23 / 23 | 74 / 82 (90.2 %) |
| src/net/net_track.cpp | 271 / 276 | 20 / 20 | 116 / 130 (89.2 %) |
| src/net/net_track.h | 18 / 26 | 8 / 25 | 7 / 11 (63.6 %) |
| src/net/net_transport.cpp | 267 / 267 | 34 / 34 | 112 / 117 (95.7 %) |
| src/net/net_voice.cpp | 86 / 86 | 12 / 12 | 34 / 46 (73.9 %) |
| src/script/script.h | 1 / 1 | 0 / 0 | 1 / 2 (50.0 %) |
| src/script/script_module.h | 5 / 6 | 0 / 0 | 14 / 14 (100.0 %) |
| src/script/script_module_save.cpp | 387 / 387 | 16 / 16 | 264 / 283 (93.3 %) |

## Client sources no suite compiles

| Source | Source lines |
|---|---:|
| src/game/human_locomotion.cpp | 3170 |
| src/game/shots.cpp | 2449 |
| src/game/human_equipment.cpp | 2129 |
| src/game/human_movement_base.cpp | 2122 |
| src/game/melee_tracker_movement.cpp | 2004 |
| src/game/human_anim_player.cpp | 1956 |
| src/game/interface_command.cpp | 1877 |
| src/game/ai_movement.cpp | 1845 |
| src/game/squad_actions.cpp | 1816 |
| src/game/squad_combat.cpp | 1792 |
| src/game/melee_tracker_ai.cpp | 1750 |
| src/game/items_module.cpp | 1733 |
| src/game/interface.cpp | 1692 |
| src/game/human_controller.cpp | 1651 |
| src/game/human_anim_mixer.cpp | 1507 |
| src/game/campaign.cpp | 1436 |
| src/script/script_vm.cpp | 1429 |
| src/game/human_special_moves.cpp | 1417 |
| src/game/squad_controller.cpp | 1398 |
| src/game/squad_commander.cpp | 1395 |
| src/game/particles.cpp | 1357 |
| src/game/interface_inventory.cpp | 1343 |
| src/game/scripted_controller.cpp | 1306 |
| src/game/melee_tracker.cpp | 1300 |
| src/game/ai_parts.cpp | 1291 |
| src/game/human_weapon_pose.cpp | 1286 |
| src/game/sound_module.cpp | 1281 |
| src/game/game_rounds.cpp | 1280 |
| src/game/enemy_controller.cpp | 1246 |
| src/game/player_net.cpp | 1234 |
| src/game/player_controller.cpp | 1220 |
| src/game/human.cpp | 1211 |
| src/game/human_movement.cpp | 1204 |
| src/game/human_pose_physics.cpp | 1183 |
| src/game/enemy_tasks.cpp | 1173 |
| src/game/actors_module.cpp | 1145 |
| src/game/team.cpp | 1118 |
| src/game/human_combat.cpp | 1117 |
| src/game/interface_rollmenu.cpp | 1110 |
| src/game/human_idle.cpp | 1098 |
| src/game/first_person_view.cpp | 1053 |
| src/game/collision_sweep.cpp | 1028 |
| src/game/interface_map.cpp | 995 |
| src/game/enemy_combat.cpp | 994 |
| src/game/human_visual.cpp | 965 |
| src/game/collision_ray.cpp | 945 |
| src/game/human_items.cpp | 942 |
| src/game/human_pose_ragdoll.cpp | 936 |
| src/game/interface_signals.cpp | 932 |
| src/game/human_control.cpp | 919 |
| src/game/physics.cpp | 909 |
| src/game/human_work.cpp | 901 |
| src/game/human_pose_dead.cpp | 883 |
| src/game/versions.cpp | 860 |
| src/script/script_compiler.cpp | 851 |
| src/game/collision.cpp | 836 |
| src/game/game_app.cpp | 815 |
| src/game/human_pose_weapon.cpp | 810 |
| src/game/item_instances.cpp | 778 |
| src/game/human_pose_ik.cpp | 777 |
| src/game/ai_senses.cpp | 753 |
| src/script/script.cpp | 753 |
| src/script/script_module.cpp | 753 |
| src/game/ai_combat.cpp | 750 |
| src/game/game.cpp | 739 |
| src/game/app.cpp | 736 |
| src/game/interface_status.cpp | 735 |
| src/game/ai_controller.cpp | 733 |
| src/game/item_defs.cpp | 728 |
| src/game/interface_info.cpp | 719 |
| src/game/human_morph.cpp | 716 |
| src/game/sniper_controller.cpp | 710 |
| src/game/player_weapons.cpp | 684 |
| src/game/enemy_gunner.cpp | 680 |
| src/game/interface_selectors.cpp | 669 |
| src/game/heavy_fire_support.cpp | 639 |
| src/game/interface_overlays.cpp | 632 |
| src/game/melee_tracker_anim_mixer.cpp | 586 |
| src/game/human_pose_body.cpp | 576 |
| src/script/script_tables.cpp | 574 |
| src/game/controller_messages.cpp | 572 |
| src/game/surface_marks.cpp | 565 |
| src/game/interface_messages.cpp | 553 |
| src/script/actor_scripts.cpp | 537 |
| src/game/human_body.cpp | 508 |
| src/game/profile.cpp | 451 |
| src/game/physics_door.cpp | 437 |
| src/game/remote_controller.cpp | 437 |
| src/game/human_pose_seats.cpp | 434 |
| src/game/collision_body.cpp | 409 |
| src/game/resource_cache.cpp | 398 |
| src/game/interface_members.cpp | 395 |
| src/game/interface_compass.cpp | 390 |
| src/script/script_expression.cpp | 386 |
| src/game/human_events.cpp | 379 |
| src/game/human_senses.cpp | 365 |
| src/game/squad_order.cpp | 361 |
| src/game/game_save.cpp | 354 |
| src/script/script_functions.cpp | 347 |
| src/game/human_anim_tables.cpp | 344 |
| src/game/actor.cpp | 338 |
| src/game/squad_movement.cpp | 323 |
| src/game/human_pose_hold.cpp | 321 |
| src/game/human_speech.cpp | 268 |
| src/game/sound_table.cpp | 266 |
| src/game/physics_box.cpp | 250 |
| src/game/item_containers.cpp | 234 |
| src/game/console.cpp | 208 |
| src/net/net_autorestart.cpp | 186 |
| src/game/globals.cpp | 182 |
| src/game/human_net.cpp | 155 |
| src/game/human_lighting.cpp | 134 |
| src/game/module.cpp | 108 |
| src/script/script_tables_lookup.cpp | 73 |
| src/net/net_com.cpp | 30 |
| src/net/net_create.cpp | 27 |
| src/game/mp_text_ids.cpp | 23 |
