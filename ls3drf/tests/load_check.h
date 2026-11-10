// Test support shared by compare_load, compare_singlemesh, compare_joint and compare_scene: the game's
// archives mounted for both sides (one rw_data.dll), the model and scene file names, and the walk of two
// loaded object graphs together.
#pragma once

#include "visual_check.h"

#include <algorithm>
#include <filesystem>
#include <set>

#include "../src/external/rw_data.h"
#include "../src/i3d/area.h"
#include "../src/i3d/billboard.h"
#include "../src/i3d/camera.h"
#include "../src/i3d/dummy.h"
#include "../src/i3d/emitter.h"
#include "../src/i3d/joint.h"
#include "../src/i3d/lens.h"
#include "../src/i3d/light.h"
#include "../src/i3d/litobject.h"
#include "../src/i3d/mirror.h"
#include "../src/i3d/model.h"
#include "../src/i3d/morph.h"
#include "../src/i3d/object.h"
#include "../src/i3d/occluder.h"
#include "../src/i3d/projector.h"
#include "../src/i3d/scene.h"
#include "../src/i3d/sector.h"
#include "../src/i3d/shadow.h"
#include "../src/i3d/singlemesh.h"
#include "../src/i3d/sound.h"
#include "../src/i3d/target.h"
#include "../src/i3d/user.h"
#include "../src/i3d/volume.h"

using namespace vc;
using namespace ls3d::i3d;

namespace lc {

struct S_file {
    std::string name;
    bool scene;
};
inline std::vector<S_file> g_files;
inline size_t g_next;
inline std::string g_gameDir;

inline void MountArchives()
{
    static const struct {
        const char *name;
        uint32_t low, high;
    } kArchives[] = {
        {"PatchX03.dta", 0xA0A0A0A4, 0xA0A0A0A0}, {"PatchX02.dta", 0xA0A0A0A3, 0xA0A0A0A0},
        {"PatchX01.dta", 0xA0A0A0A2, 0xA0A0A0A0}, {"SabreSquadron.dta", 0xA0A0A0A1, 0xA0A0A0A0},
        {"LangEnglish.dta", 0xA0A0A0A0, 0xA0A0A0A0}, {"Patch.dta", 0x5D805270, 0x10ACB252},
        {"Models.dta", 0x5D805259, 0x10ACB252}, {"Missions.dta", 0x7654A3F0, 0x22BCDA98},
        {"Maps.dta", 0xB438D0A5, 0xF26527FA}, {"Sounds.dta", 0x4FE85106, 0x8D2965CA},
        {"Scripts.dta", 0x0AB4E72D, 0xCF761298}, {"Others.dta", 0xEA85902A, 0x65F7AB23},
        {"Maps_C.dta", 0xB038D0A0, 0xE26520FA}, {"Maps_U.dta", 0xB038D1A1, 0xF26520FA},
    };
    for (const auto &a : kArchives) {
        const std::string path = g_gameDir + "\\" + a.name;
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        C_DtaArchive *archive = dtaCreate(path.c_str());
        if (!archive) {
            std::printf("cannot mount %s\n", path.c_str());
            std::exit(2);
        }
        archive->XorKey(a.low, a.high);
        const uint64_t key = archive->GetKey();
        archive->XorKey(static_cast<uint32_t>(key) ^ a.low, static_cast<uint32_t>(key >> 32) ^ a.high);
    }
}

inline void ListFiles(bool scenes)
{
    namespace fs = std::filesystem;
    const fs::path root = UnpackedDir(g_gameDir);
    for (const auto &e : fs::recursive_directory_iterator(root / "MODELS")) {
        if (e.is_regular_file() && e.path().extension().string().size() == 4 && _stricmp(e.path().extension().string().c_str(), ".4ds") == 0)
            g_files.push_back({"MODELS\\" + fs::relative(e.path(), root / "MODELS").string(), false});
    }
    std::sort(g_files.begin(), g_files.end(), [](const S_file &a, const S_file &b) { return a.name < b.name; });
    std::shuffle(g_files.begin(), g_files.end(), g_rng);
    if (scenes) {
        std::vector<S_file> missions;
        for (const auto &e : fs::directory_iterator(root / "MISSIONS")) {
            if (e.is_directory() && fs::exists(e.path() / "SCENE.4ds"))
                missions.push_back({"MISSIONS\\" + e.path().filename().string() + "\\SCENE.4ds", true});
        }
        std::sort(missions.begin(), missions.end(), [](const S_file &a, const S_file &b) { return a.name < b.name; });
        missions.insert(missions.end(), g_files.begin(), g_files.end());
        g_files = missions;
    }
}

inline std::set<const void *> g_walked;

inline void WalkMesh(Pair mesh)
{
    if (!mesh.o != !mesh.p)
        Fail("mesh: one is null");
    if (!mesh || !g_walked.insert(mesh.o).second)
        return;
    Register(mesh);
    Count("meshes");
    Compare("I3D_mesh", mesh, kLayout_I3D_mesh, {0x4, 0x8, 0xC, 0x1C, 0x2C});
    const int levels = reinterpret_cast<I3D_mesh *>(mesh.p)->numLevels;
    for (int l = 0; l < levels; ++l) {
        Pair level{*reinterpret_cast<uint8_t **>(mesh.o + 0x48 + 4 * l), *reinterpret_cast<uint8_t **>(mesh.p + 0x48 + 4 * l)};
        if (!level)
            continue;
        Register(level);
        Compare("I3D_mesh_level", level, kLayout_I3D_mesh_level, {0x4, 0x8, 0xC, 0x6C});
        const int nv = reinterpret_cast<I3D_mesh_level *>(level.p)->numVertices;
        const auto *vo = CallV<const uint8_t *>(level.o, 0x04, 1u), *vp = CallV<const uint8_t *>(level.p, 0x04, 1u);
        if (nv && std::memcmp(vo, vp, nv * 0x20))
            Fail("mesh vertices");
        Count("vertices", nv);
        const int groups = reinterpret_cast<I3D_mesh_level *>(level.p)->numFaceGroups;
        for (int g = 0; g < groups; ++g) {
            Pair fg{CallV<uint8_t *>(level.o, 0x14, g), CallV<uint8_t *>(level.p, 0x14, g)};
            Register(fg);
            const int nf = reinterpret_cast<I3D_face_group *>(fg.p)->numFaces;
            const auto *fo = CallV<const uint8_t *>(fg.o, 0x0C, 1u), *fp = CallV<const uint8_t *>(fg.p, 0x0C, 1u);
            if (nf && std::memcmp(fo, fp, nf * 6))
                Fail("mesh faces");
            Count("faces", nf);
            Pair mat{*reinterpret_cast<uint8_t **>(fg.o + 4), *reinterpret_cast<uint8_t **>(fg.p + 4)};
            if (!mat.o != !mat.p)
                Fail("face group material: one is null");
            if (mat && g_walked.insert(mat.o).second) {
                Register(mat);
                Compare("I3D_material", mat, kLayout_I3D_material, {});
                Count("materials");
            }
        }
    }
}

// (The renderer's fields and unknown blocks: never written here.)
inline void CompareScene(Pair f)
{
    Compare("I3D_scene", f, kLayout_I3D_scene, {VC_FRAME_SKIP, 0x1FC},
            {"screenProjection", "unknown148", "unknown190", "unknown1E8", "unknown30C", "weatherMatrices"});
}

inline void WalkFrame(Pair f)
{
    if (!g_walked.insert(f.o).second)
        return;
    Register(f);
    Count("frames");
    const uint16_t type = *reinterpret_cast<uint16_t *>(f.o + 0xF8);
    if (type != *reinterpret_cast<uint16_t *>(f.p + 0xF8))
        Fail("frame type");
    switch (type) {
    case FRAME_USER: Compare("I3D_user", f, kLayout_I3D_user, {VC_FRAME_SKIP}); break;
    case FRAME_DUMMY: Compare("I3D_dummy", f, kLayout_I3D_dummy, {VC_FRAME_SKIP, 0x144, 0x148, 0x14C}); break;
    case FRAME_TARGET: Compare("I3D_target", f, kLayout_I3D_target, {VC_FRAME_SKIP, 0x134, 0x138, 0x13C}); break;
    case FRAME_VOLUME: Compare("I3D_volume", f, kLayout_I3D_volume, {VC_FRAME_SKIP}); break;
    case FRAME_CAMERA: /* (the frustums: unset until the first MatrixChanged) */
        Compare("I3D_camera", f, kLayout_I3D_camera, {VC_FRAME_SKIP},
                {"frustum", "frustumD", "backdropFrustum", "backdropFrustumD", "nearSectorFrustum", "nearSectorFrustumD"});
        break;
    case FRAME_LIGHT: Compare("I3D_light", f, kLayout_I3D_light, {VC_FRAME_SKIP}); break;
    case FRAME_SECTOR: {
        Compare("I3D_sector", f, kLayout_I3D_sector, {VC_FRAME_SKIP, 0x1C8, 0x1CC});
        auto *ps = reinterpret_cast<I3D_sector *>(f.p);
        auto *oportals = *reinterpret_cast<uint8_t ***>(f.o + 0x24C);
        for (int k = 0; k < ps->portals.Size(); ++k) {
            Pair portal{oportals[k], reinterpret_cast<uint8_t *>(ps->portals[k])};
            Register(portal);
            // (LS3DRF sets nothing past the vertices read: the world values, the plane, the screen
            // rectangles and the centre stay as the heap left them, in both.)
            const int nv = *reinterpret_cast<int *>(portal.p + 0x1AC);
            if (*reinterpret_cast<int *>(portal.o + 0x1AC) != nv ||
                std::memcmp(portal.o + 0x10, portal.p + 0x10, 0x1C) ||
                std::memcmp(portal.o + 0x30, portal.p + 0x30, nv * 16))
                Fail("portal: count, head or vertices");
            Count("portals");
        }
        break;
    }
    case FRAME_SCENE:
        CompareScene(f);
        Count("scenes");
        break;
    case FRAME_OCCLUDER: Compare("I3D_occluder", f, kLayout_I3D_occluder, {VC_FRAME_SKIP}); break;
    case FRAME_AREA: Compare("I3D_area", f, kLayout_I3D_area, {VC_FRAME_SKIP}); break;
    case FRAME_MODEL: Compare("I3D_model", f, kLayout_I3D_model, {VC_FRAME_SKIP, 0x138, 0x13C}); break;
    case FRAME_JOINT: /* (modelMatrix: unset until the skin binds; +0x16C unset) */ Compare("I3D_joint", f, kLayout_I3D_joint, {VC_FRAME_SKIP, 0x120, 0x124, 0x128, 0x12c, 0x130, 0x134, 0x138, 0x13c, 0x140, 0x144, 0x148, 0x14c, 0x150, 0x154, 0x158, 0x15c, 0x16C}); Count("joints"); break;
    case FRAME_SHADOW: Compare("I3D_shadow", f, kLayout_I3D_shadow, {VC_FRAME_SKIP}); break;
    case FRAME_SOUND: Compare("I3D_sound", f, kLayout_I3D_sound, {VC_FRAME_SKIP}); break;
    case FRAME_VISUAL: {
        const uint32_t cc = Get<uint32_t>("GetVisualType", f, 0x04);
        Pair mesh;
        switch (cc) {
        case FourCC("OBJ_"): Compare("I3D_object", f, kLayout_I3D_object, {VC_VISUAL_SKIP}); mesh = {*reinterpret_cast<uint8_t **>(f.o + 0x1E0), *reinterpret_cast<uint8_t **>(f.p + 0x1E0)}; Count("objects"); break;
        case FourCC("BBRD"): Compare("I3D_billboard", f, kLayout_I3D_billboard, {VC_VISUAL_SKIP, 0x218, 0x21c, 0x220, 0x224, 0x228, 0x22c, 0x230, 0x234, 0x238, 0x23c, 0x240, 0x244, 0x248, 0x24c, 0x250, 0x254, 0x258, 0x25c}); mesh = {*reinterpret_cast<uint8_t **>(f.o + 0x1E0), *reinterpret_cast<uint8_t **>(f.p + 0x1E0)}; Count("billboards"); break;
        case FourCC("LMAP"): Compare("I3D_lit_object", f, kLayout_I3D_lit_object, {VC_VISUAL_SKIP}); mesh = {*reinterpret_cast<uint8_t **>(f.o + 0x1E0), *reinterpret_cast<uint8_t **>(f.p + 0x1E0)}; Count("lit objects"); break;
        case FourCC("MRPH"): Compare("I3D_morph", f, kLayout_I3D_morph, {VC_VISUAL_SKIP, 0x22C}); mesh = {*reinterpret_cast<uint8_t **>(f.o + 0x1E0), *reinterpret_cast<uint8_t **>(f.p + 0x1E0)}; Count("morphs"); break;
        case FourCC("SNGM"): Compare("I3D_singlemesh", f, kLayout_I3D_singlemesh, {VC_VISUAL_SKIP, 0x24C}); mesh = {*reinterpret_cast<uint8_t **>(f.o + 0x1E0), *reinterpret_cast<uint8_t **>(f.p + 0x1E0)}; Count("singlemeshes"); break;
        case FourCC("SMMR"): Compare("I3D_singlemorph", f, kLayout_I3D_singlemorph, {VC_VISUAL_SKIP, 0x24C, 0x26C}); mesh = {*reinterpret_cast<uint8_t **>(f.o + 0x1E0), *reinterpret_cast<uint8_t **>(f.p + 0x1E0)}; Count("singlemorphs"); break;
        case FourCC("LENS"): Compare("I3D_lens", f, kLayout_I3D_lens, {VC_VISUAL_SKIP, 0x20C, 0x21C}); Count("lenses"); break;
        case FourCC("MIRR"): Compare("I3D_mirror", f, kLayout_I3D_mirror, {VC_VISUAL_SKIP}); Count("mirrors"); break;
        case FourCC("EMTR"): Compare("I3D_emitor", f, kLayout_I3D_emitor, {VC_VISUAL_SKIP}); Count("emitors"); break;
        case FourCC("PROJ"): Compare("I3D_projector", f, kLayout_I3D_projector, {VC_VISUAL_SKIP}); Count("projectors"); break;
        default: Count("other visuals"); break;
        }
        if (mesh.o || mesh.p)
            WalkMesh(mesh);
        break;
    }
    default:
        Count("other frames");
        break;
    }
    const int n = Get<int>("NumChildren", f, 0x3C);
    for (int k = 0; k < n; ++k)
        WalkFrame({CallV<uint8_t *>(f.o, 0x40, k), CallV<uint8_t *>(f.p, 0x40, k)});
}


// Starts a file's graph: nothing known yet.
inline void StartGraph()
{
    g_walked.clear();
    g_partner.clear();
    g_partnerBack.clear();
}

// A model or scene loaded on both sides (`scene`: a mission's SCENE.4ds); its Open result in `r`.
inline Pair LoadFile(const char *name, bool scene, I3D_RESULT &r)
{
    Pair root = MakeFrame(scene ? FRAME_SCENE : FRAME_MODEL);
    r = Get<I3D_RESULT>("Open", root, scene ? 0xF0 : 0x74, name, 0u, static_cast<void *>(nullptr), static_cast<void *>(nullptr));
    return root;
}

// The game folder and the file list; call before Run's setUp needs them.
inline void FindGame(const char *arg)
{
    const char *orig = OriginalDllPath(arg, "LS3DRF.dll");
    g_gameDir = std::string(orig).substr(0, std::string(orig).find_last_of("\\/"));
}

}  // namespace lc
