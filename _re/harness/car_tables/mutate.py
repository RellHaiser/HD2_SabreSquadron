"""Mutations of src/game/car_tables.cpp: each copy is built against the real headers and must fail check.py.

python mutate.py <game folder> <scratch folder> [name ...]
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, r"..\..\..\src"))
FILE = os.path.join(SRC, r"game\car_tables.cpp")
GPP = r"C:\nim-2.2.6\dist\mingw32\bin\g++.exe"

MUTATIONS = [
    # the tyre surface's values
    ("decode thousands", "surface.unknown04 = v[0] * 1000.0f;", "surface.unknown04 = v[0];"),
    ("decode degrees", "surface.unknown0C = v[2] * 0.017453292f;", "surface.unknown0C = v[2];"),
    ("decode order", "surface.unknown18 = v[5];\n    surface.unknown1C = v[6];", "surface.unknown18 = v[6];\n    surface.unknown1C = v[5];"),
    ("decode second thousands", "surface.unknown24 = v[8] * 1000.0f;", "surface.unknown24 = v[8] * 100.0f;"),
    ("decode last", "surface.unknown30 = v[11];", "surface.unknown30 = v[12];"),
    # the types
    ("no develop", "    g_tyreTypes.push_back(develop);\n", ""),
    ("develop name", 'std::strcpy(develop.name, "develop");', 'std::strcpy(develop.name, "Develop");'),
    ("table not copied", "            *surfaces = *type.surfaces;", "            surfaces->clear();"),
    ("table shared", "        auto *surfaces = new std::vector<S_TyreSurface>;\n        g_tyreTypes.back().surfaces = surfaces;",
     "        auto *surfaces = new std::vector<S_TyreSurface>;\n        g_tyreTypes.back().surfaces = type.surfaces;"),
    ("null table", "        if (type.surfaces)\n            *surfaces = *type.surfaces;", "        *surfaces = *type.surfaces;"),
    ("free kept", "    for (size_t i = types.size(); i-- > 0;)\n        delete types[i].surfaces;\n    types.clear();",
     "    for (size_t i = types.size(); i-- > 0;)\n        delete types[i].surfaces;"),
    # the file
    ("no first reset", "    SetTyreTypes(types);                                // \"develop\" alone", "    //"),
    ("version", "    if (version != 1) {", "    if (version < 1) {"),
    ("version kept open", "    if (version != 1) {\n        dtaClose(handle);\n        return false;", "    if (version != 1) {\n        return false;"),
    ("record size", "        dtaRead(handle, &size, 4);\n", ""),
    ("name and id", "        dtaRead(handle, type.name, sizeof type.name);\n        dtaRead(handle, &type.id, 4);",
     "        dtaRead(handle, &type.id, 4);\n        dtaRead(handle, type.name, sizeof type.name);"),
    ("entry fields", "            dtaRead(handle, &surface.surface, 4);\n            uint32_t bytes = 0;\n            dtaRead(handle, &bytes, 4);",
     "            uint32_t bytes = 0;\n            dtaRead(handle, &bytes, 4);\n            dtaRead(handle, &surface.surface, 4);"),
    ("short values", "std::vector<float> values(std::max<size_t>((bytes + 3) / 4, 12), 0.0f);",
     "std::vector<float> values(std::max<size_t>((bytes + 3) / 4, 12), 1.0f);"),
    ("no close", "    dtaClose(handle);\n    SetTyreTypes(types);", "    SetTyreTypes(types);"),
    # the surfaces
    ("default friction", "        surface.friction = 0.35f;\n", "        surface.friction = 0.3f;\n"),
    ("default size", "surface.unknown04 = surface.unknown08 = surface.unknown0C = surface.unknown10 = 100.0f;",
     "surface.unknown04 = surface.unknown08 = surface.unknown0C = 100.0f;"),
    ("friction low", "            if (surface.friction < 0.0f)\n", "            if (surface.friction < -1.0f)\n"),
    ("friction high", "                surface.friction = 1.0f;", "                surface.friction = 0.9f;"),
    ("bounce low", "            if (surface.bounce < 1.0f)\n                surface.bounce = 1.0f;", "            if (surface.bounce < 0.5f)\n                surface.bounce = 1.0f;"),
    ("bounce high", "            else if (surface.bounce > 2.0f)", "            else if (surface.bounce > 2.5f)"),
    ("min 04", "            if (surface.unknown04 > surface.unknown08)\n", "            if (surface.unknown04 < surface.unknown08)\n"),
    ("min 0C", "                surface.unknown0C = surface.unknown10;", "                surface.unknown0C = surface.unknown08;"),
    ("cap 14", "            else if (surface.unknown14 > 0.2f)", "            else if (surface.unknown14 > 0.25f)"),
    ("floor 14", "            if (surface.unknown14 < 0.0f)\n", "            if (surface.unknown14 < -0.2f)\n"),
    ("material fields", "            surface.unknown08 = m.carUnknownE0;\n            surface.unknown04 = m.carUnknownE4;",
     "            surface.unknown08 = m.carUnknownE4;\n            surface.unknown04 = m.carUnknownE0;"),
    ("effects", "            surface.skidEffect = m.carSkidEffect;", "            surface.skidEffect = m.carDustEffect;"),
    ("hard 0x5F", "            case 0x5F:\n", ""),
    ("hard skid only", "                surface.tyreSqueal = surface.skidMarks = 1;", "                surface.skidMarks = 1;"),
    ("material index", "            surface.material = static_cast<int>(i);", "            surface.material = static_cast<int>(i) + 1;"),
    ("map last", "std::fill(g_carSurfaceOfMaterial, g_carSurfaceOfMaterial + 255, 0);", "std::fill(g_carSurfaceOfMaterial, g_carSurfaceOfMaterial + 256, 0);"),
    ("map kept", "            g_carSurfaceOfMaterial[surfaces[i].material] = static_cast<int>(i);", "            ;"),
    ("not cleared first", "    ClearCarSurfaces();\n    // Quirk kept", "    // Quirk kept"),
    ("clear", "    g_carSurfaces.clear();\n}", "}"),
]


def main():
    game, scratch = sys.argv[1], sys.argv[2]
    only = set(sys.argv[3:])
    work = os.path.join(scratch, "mut_car_tables")
    os.makedirs(work, exist_ok=True)
    source = open(FILE, encoding="utf-8", newline="").read()
    survived = 0
    for name, a, b in MUTATIONS:
        if only and name not in only:
            continue
        if source.count(a) != 1:
            print(f"{name}: pattern found {source.count(a)} times")
            survived += 1
            continue
        mutated = os.path.join(work, "car_tables.cpp")
        open(mutated, "w", encoding="utf-8", newline="").write(source.replace(a, b))
        exe = os.path.join(work, "harness.exe")
        build = subprocess.run([GPP, "-std=c++17", "-O1", "-msse2", "-mfpmath=sse", "-DRW_DATA_API=",
                                "-I" + os.path.join(HERE, "stub"), "-I" + SRC, os.path.join(HERE, "harness.cpp"),
                                mutated, "-o", exe], capture_output=True, text=True,
                               env=dict(os.environ, PATH=os.path.dirname(GPP) + ";" + os.environ["PATH"]))
        if build.returncode != 0:
            print(f"{name}: does not build")
            continue
        out = os.path.join(work, "out.txt")
        with open(out, "w") as f:
            try:
                run = subprocess.run([exe, game, work], stdout=f, timeout=60)
                code = run.returncode
            except subprocess.TimeoutExpired:
                code = "timeout"
        check = subprocess.run([sys.executable, os.path.join(HERE, "check.py"), game, out], capture_output=True, text=True)
        caught = code != 0 or check.returncode != 0
        survived += not caught
        print(f"{name}: exit {code} -> {'caught' if caught else 'SURVIVED'}")
    print(f"{survived} survived")


if __name__ == "__main__":
    main()
