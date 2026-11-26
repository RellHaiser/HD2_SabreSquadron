// The shipped car models' rigs for sw_diff.cpp's cases on real models: export_cars.py's dumps of MODELS\*.4ds (the
// suites' inputs\cars4ds, or HD2_SW_CARS): every frame's type, parent, name, local matrix and user properties. A model
// is kept when a seat carries a weapon (as check_sw.py counts them: a "SEATnn" dummy, "HORTnn*" and "VERTnn*" dummies,
// a "BARRELnn_00*" visual with an "itemid" property).

struct S_SwModelFrame {
    int type = 0, parent = 0;                           // (parent: 0 the model, else 1-based)
    std::string name, props;
    float local[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float scale[3] = {1, 1, 1};
};
struct S_SwModel {
    std::string file;
    std::vector<S_SwModelFrame> frames;
    std::vector<int> weaponSeats;
};
std::vector<S_SwModel> g_swModels;

std::string Upper(std::string s)
{
    for (char &ch : s)
        ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}
bool HasPrefix(const std::string &name, const std::string &prefix)
{
    return Upper(name).compare(0, prefix.size(), Upper(prefix)) == 0;
}
int FindModelFrame(const S_SwModel &m, int type, const std::string &prefix)
{
    for (size_t i = 0; i < m.frames.size(); ++i)
        if (m.frames[i].type == type && HasPrefix(m.frames[i].name, prefix))
            return int(i);
    return -1;
}
bool HasDescendant(const S_SwModel &m, int index, int type, const std::string &prefix)
{
    for (size_t i = 0; i < m.frames.size(); ++i) {
        if (m.frames[i].type != type || !HasPrefix(m.frames[i].name, prefix))
            continue;
        for (int p = m.frames[i].parent; p; p = m.frames[size_t(p - 1)].parent)
            if (p - 1 == index)
                return true;
    }
    return false;
}

void LoadSwModels(const std::string &dir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    std::vector<std::string> names;
    do
        names.push_back(fd.cFileName);
    while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        std::ifstream in(dir + "\\" + n);
        S_SwModel m;
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() < 2 || line[1] != ' ') {
                if (line.compare(0, 6, "model ") == 0) {
                    std::istringstream s(line.substr(6));
                    s >> m.file;
                }
                continue;
            }
            std::istringstream s(line.substr(2));
            if (line[0] == 'f') {
                S_SwModelFrame f;
                s >> f.type >> f.parent;
                s.get();
                std::getline(s, f.name);
                m.frames.push_back(f);
            } else if (line[0] == 'L' && !m.frames.empty()) {
                for (float &v : m.frames.back().local)
                    s >> v;
            } else if (line[0] == 'S' && !m.frames.empty()) {
                for (float &v : m.frames.back().scale)
                    s >> v;
            } else if (line[0] == 'P' && !m.frames.empty()) {
                std::string hex;
                s >> hex;
                std::string props;
                for (size_t i = 0; hex != "-" && i + 1 < hex.size(); i += 2)
                    props += char(std::stoi(hex.substr(i, 2), nullptr, 16));
                m.frames.back().props = props;
            }
        }
        for (int seat = 0; seat < 100; ++seat) {
            if (FindModelFrame(m, 6, F("SEAT%02d", seat)) < 0 || FindModelFrame(m, 6, F("HORT%02d", seat)) < 0 ||
                FindModelFrame(m, 6, F("VERT%02d", seat)) < 0)
                continue;
            const int barrel = FindModelFrame(m, 1, F("BARREL%02d_00", seat));
            if (barrel >= 0 && Lower(m.frames[size_t(barrel)].props).find("itemid") != std::string::npos)
                m.weaponSeats.push_back(seat);
        }
        if (!m.weaponSeats.empty())
            g_swModels.push_back(m);
    }
}

// The model's frames made under `model` (by the case: the same on both sides); the seats' frames found, the barrels
// without a muzzle noted (their animations never open, as BuildModel's).
void BuildRealModel(uint8_t *model, const S_SwModel &m, const std::vector<int> &seats)
{
    std::vector<uint8_t *> made;
    for (const S_SwModelFrame &f : m.frames) {
        uint8_t *parent = f.parent > 0 && size_t(f.parent) <= made.size() ? made[size_t(f.parent - 1)] : model;
        uint8_t *x = Child(parent, f.type == 1 ? FRAME_VISUAL : FRAME_DUMMY, f.name, model);
        if (!f.props.empty())
            AsFrame(x)->m_userProps = Text(f.props);
        // (As I3D_frame::SetLocalMatrix: the matrix, its scale, the rotation to be taken from it.)
        I3D_frame *fx = AsFrame(x);
        std::memcpy(&fx->m_localMatrix, f.local, sizeof f.local);
        fx->m_scale = S_vector(f.scale[0], f.scale[1], f.scale[2]);
        fx->m_frameFlags = (fx->m_frameFlags & ~0x28u) | FRMFLAGS_CHANGED | FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
        Stale(fx);
        made.push_back(x);
    }
    for (size_t i = 0; i < m.frames.size(); ++i) {
        const std::string up = Upper(m.frames[i].name);
        if (m.frames[i].type == 1 && up.compare(0, 6, "BARREL") == 0 && up.size() >= 11 && up[8] == '_' &&
            !HasDescendant(m, int(i), 6, "SHDUM"))
            g_noAnims.push_back("_f" + up.substr(6, 2) + "_" + up.substr(9, 2));
    }
    for (size_t i = 0; i < seats.size(); ++i) {
        const int f = FindModelFrame(m, 6, F("SEAT%02d", seats[i]));
        g_seatFrame[g_side][i] = f >= 0 ? made[size_t(f)] : nullptr;
    }
}
