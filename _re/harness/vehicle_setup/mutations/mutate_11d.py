"""Part 11d mutations: the copy protection pass-through and a frames data (game/copy_protection.cpp).

Run with the harness runner _re/harness/mutrun.py.
Generated (deletions, comparison, number, bool and negation flips; the lines unique in the file).

Result: 5 killed, 2 not building, 2 survived. Survivors: the srand (the seed itself cannot be checked), the delete of the block (a leak).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
T = os.path.join(ROOT, r"src/game/copy_protection.cpp")

MUTATIONS = [
    (T, 'T 13 del', '    std::srand(GetTickCount());\n', '\n'),
    (T, 'T 21 del', '        *frame->GetData() = data;\n', '\n'),
    (T, 'T 24 del', '    frame->SetData(new S_FrameData(data));\n', '\n'),
    (T, 'T 30 del', '        delete frame->GetData();\n', '\n'),
    (T, 'T 31 del', '        frame->SetData(nullptr);\n', '\n'),
    (T, 'T 39 del', '    FreeFrameData(frame);\n', '\n'),
    (T, 'T 40 num', '    return 0;\n', '    return 1;\n'),
    (T, 'T 48 del', '        scene->EnumFrames(FreeFrameDataCallback, nullptr, 0xFFFF, 0);\n', '\n'),
    (T, 'T 48 num', '        scene->EnumFrames(FreeFrameDataCallback, nullptr, 0xFFFF, 0);\n', '        scene->EnumFrames(FreeFrameDataCallback, nullptr, 0xFFFF, 1);\n'),
]
