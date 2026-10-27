"""Mutations of the car's calls of its seat weapons' message handlers (src/game/car_net.cpp;
part 3b: the arguments the original passes and the weapon does not read) for ../mutfast.py with the
car build (build.bat / check.py)."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
N = os.path.join(ROOT, r"src\game\car_net.cpp")

MUTATIONS = [
    (N, "play shot barrel", "                weapon->PlayShot(shot->barrel);", "                weapon->PlayShot(0);"),
    (N, "fire shot unused", "weapon->FireShot(&shot->pos, &shot->dir, shot->barrel, 1000)", "weapon->FireShot(&shot->pos, &shot->dir, shot->barrel, 0)"),
    (N, "show reload store", "                weapon->ShowReload(GetContainer(0));", "                weapon->ShowReload(nullptr);"),
    (N, "reload store", "                weapon->Reload(GetContainer(0));", "                weapon->Reload(nullptr);"),
]
