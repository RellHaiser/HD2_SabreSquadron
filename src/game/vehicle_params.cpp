// The car's parameter block in the mission files: the property blob and its run-length code
// (original 0x004d0110, 0x004d6c50 and 0x004d7d80).
#include "game/vehicle_params.h"

#include <cstring>

#include "game/pending.h"                               // g_tyreTypes

static_assert(sizeof(S_VehicleParams) == 0x1240, "S_VehicleParams layout");

namespace {

constexpr uint32_t COMP_MAGIC = 0x504D4F43;             // "COMP"
constexpr uint32_t COMP_HEADER = 13;                    // magic, size, coded size, escape byte
constexpr uint32_t COMP_MAX_RUN = 0xFF;

// Reads the blob in order (the reader 0x004d7d80 copies each field from its fixed offset).
class C_BlobReader {
public:
    C_BlobReader(const uint8_t *blob, bool tyreIndices) : m_blob(blob), m_tyreIndices(tyreIndices) {}

    template <class T>
    void operator()(T &value)
    {
        std::memcpy(&value, m_blob + m_pos, sizeof value);
        m_pos += sizeof value;
    }
    // A name slot (lstrcpyA in the original): up to its terminator, all 16 bytes when there is
    // none. Shipped cars have such names ("engine_on_nf.wav"); as in the original the block
    // then holds them unterminated, and they read on into the next field. Fix: the copy stops
    // at the slot's end (the original wrote on into the fields that follow).
    void Name(char (&name)[VEHICLE_SOUND_NAME])
    {
        const char *source = reinterpret_cast<const char *>(m_blob + m_pos);
        for (int i = 0; i < VEHICLE_SOUND_NAME; ++i)
            if ((name[i] = source[i]) == '\0')
                break;
        m_pos += VEHICLE_SOUND_NAME;
    }
    void TyreType(int &type)
    {
        int stored;
        (*this)(stored);
        if (!m_tyreIndices) {
            type = stored;
            return;
        }
        // The index in tables\tyres.bin -> that type's id (0 out of range).
        type = stored >= 0 && stored < static_cast<int>(g_tyreTypes.size()) ? g_tyreTypes[stored].id : 0;
    }
    void Skip(uint32_t size) { m_pos += size; }

private:
    const uint8_t *m_blob;
    uint32_t m_pos = 0;
    bool m_tyreIndices;
};

// Writes the blob in order (the writer 0x004d6c50).
class C_BlobWriter {
public:
    C_BlobWriter(uint8_t *blob, bool tyreIndices) : m_blob(blob), m_tyreIndices(tyreIndices) {}

    template <class T>
    void operator()(const T &value)
    {
        std::memcpy(m_blob + m_pos, &value, sizeof value);
        m_pos += sizeof value;
    }
    // A name slot: the name and its terminator (lstrcpyA in the original).
    void Name(const char (&name)[VEHICLE_SOUND_NAME])
    {
        char *target = reinterpret_cast<char *>(m_blob + m_pos);
        int i = 0;
        for (; i < VEHICLE_SOUND_NAME && name[i]; ++i)
            target[i] = name[i];
        if (i < VEHICLE_SOUND_NAME)
            target[i] = '\0';
        m_pos += VEHICLE_SOUND_NAME;
    }
    void TyreType(const int &type)
    {
        if (!m_tyreIndices) {
            (*this)(type);
            return;
        }
        // The index of the last type in tables\tyres.bin with this id (0 when none).
        int index = static_cast<int>(g_tyreTypes.size());
        while (--index >= 0 && g_tyreTypes[index].id != type) {
        }
        if (index < 0)
            index = 0;
        (*this)(index);
    }
    void Skip(uint32_t size) { m_pos += size; }

private:
    uint8_t *m_blob;
    uint32_t m_pos = 0;
    bool m_tyreIndices;
};

// One set of sound layers: per layer its name, its 8 values and (sets A and B) its flag.
template <class T_Blob, class T_Names, class T_Values, class T_Flag = uint8_t>
void TransferLayers(T_Blob &blob, T_Names &names, T_Values &values, T_Flag *flags = nullptr)
{
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i) {
        blob.Name(names[i]);
        for (auto &row : values)
            blob(row[i]);
        if (flags)
            blob(flags[i]);
    }
}

// The four crash sounds: two names, a value and a flag each.
template <class T_Blob, class T_Names, class T_Values, class T_Flags>
void TransferCrashes(T_Blob &blob, T_Names &namesA, T_Names &namesB, T_Values &values, T_Flags &flags)
{
    for (int i = 0; i < VEHICLE_CRASH_SOUNDS; ++i) {
        blob.Name(namesA[i]);
        blob.Name(namesB[i]);
        blob(values[i]);
        blob(flags[i]);
    }
}

// The blob's fields in order (`T_Params` is const when writing). The offsets in the comments
// are the blob's.
template <class T_Blob, class T_Params>
void TransferParams(T_Blob &blob, T_Params &p)
{
    blob(p.mass);                                       // 0x000
    for (auto &v : p.unknown004)
        blob(v);
    for (int i = 0; i < VEHICLE_GEAR_SLOTS; ++i) {      // 0x050
        blob(p.gearRatios[i]);
        blob(p.shiftUpPercent[i]);
        blob(p.shiftDownPercent[i]);
    }
    for (auto &v : p.unknown110)                        // 0x110
        blob(v);
    for (auto &v : p.centreOfMass)
        blob(v);
    blob(p.gravity);
    blob(p.tankSize);
    blob(p.unknown158);

    for (int a = 0; a < VEHICLE_AXLES; ++a) {           // 0x15C, 0x70 bytes per axle
        blob(p.axleUnknown15C[a]);
        blob(p.axleUnknown18C[a]);
        blob(p.axleUnknown1BC[a]);
        blob(p.axleUnknown1EC[a]);
        blob(p.axleUnknown21C[a]);
        blob(p.axleUnknown24C[a]);
        blob(p.axleUnknown27C[a]);
        blob(p.axleUnknown2AC[a]);
        blob.TyreType(p.axleTyreType[a]);
        blob(p.axleUnknown30C[a]);
        blob(p.axleUnknown33C[a]);
        blob(p.axleUnknown36C[a]);
        blob(p.axleUnknown39C[a]);
        blob(p.axleUnknown3CC[a]);
        blob(p.axleUnknown3FC[a]);
        blob(p.axleUnknown42C[a]);
        blob(p.axleUnknown45C[a]);
        blob(p.axleUnknown48C[a]);
        blob(p.axleUnknown4BC[a]);
        blob(p.axleUnknown4EC[a]);
        blob(p.axleUnknown51C[a]);
        blob(p.axleUnknown54C[a]);
        blob(p.axleFlag2[a]);
        blob(p.axleFlag1[a]);
        blob(p.axleFlag4[a]);
        blob(p.axleLeftShare[a]);
        blob(p.axleRightShare[a]);
        blob(p.axleUnknown5DC[a]);
        blob(p.axleUnknown60C[a]);
        blob(p.axleUnknown63C[a]);
        blob(p.axleFlag800[a]);
    }

    for (auto &v : p.inertia)                           // 0x69C
        blob(v);
    blob(p.unknown678);
    blob(p.unknown67C);
    blob(p.unknown680);
    blob(p.unknown684);
    blob(p.unknown121C);                                // 0x6B8
    blob(p.unknown688);
    blob(p.unknown68C);
    blob(p.unknown690);
    blob(p.unknown121D);                                // 0x6C5
    blob(p.unknown121E);
    blob(p.unknown121F);
    blob(p.unknown694);                                 // 0x6C8
    blob(p.unknown698);
    blob(p.unknown69C);                                 // 0x6CD
    blob(p.unknown6A0);
    blob(p.unknown6A4);
    blob(p.unknown6A8);
    blob(p.unknown6AC);
    blob(p.unknown6B0);
    blob(p.unknown6B4);
    for (auto &v : p.unknown6B8)
        blob(v);
    blob(p.unknown6C4);
    blob(p.unknown1220);                                // 0x6F9
    for (auto &v : p.unknown6C8)                        // 0x6FA
        blob(v);
    for (auto &v : p.unknown6DC)
        blob(v);
    blob.Skip(VEHICLE_SOUND_NAME);                      // 0x75E: a slot the game neither reads nor writes
    for (auto &v : p.unknown72C)                        // 0x76E
        blob(v);
    blob.Name(p.glassTexture);                           // 0x7C2
    blob.Name(p.glassTextureAlpha);
    for (auto &v : p.unknown7A0)                        // 0x7E2
        blob(v);
    blob.Skip(1);                                       // 0x8D6: unused

    // Sound group 1 (0x8D7).
    for (auto &name : p.sounds1)
        blob.Name(name);
    TransferLayers(blob, p.layers1A, p.layers1AValues, p.layers1AFlags);   // 0x907, 0x31 bytes each
    TransferLayers(blob, p.layers1B, p.layers1BValues, p.layers1BFlags);   // 0x9FC
    TransferLayers(blob, p.layers1C, p.layers1CValues);                    // 0xAF1, 0x30 bytes each
    blob.Name(p.sound1_9B4);                            // 0xBE1
    blob(p.unknownCF4);
    blob(p.unknownCF8);
    blob.Name(p.sound1_9C4);                            // 0xBF9
    for (auto &v : p.unknownCFC)
        blob(v);
    blob.Name(p.sound1_9D4);                            // 0xC1D
    for (auto &name : p.sounds1_9E4)
        blob.Name(name);
    TransferCrashes(blob, p.crash1A, p.crash1B, p.crash1Values, p.crash1Flags);   // 0xCBD, 0x25 bytes each
    blob.Name(p.sound1_AF4);                            // 0xD51
    blob.Name(p.sound1_B04);

    // Sound group 2 (0xD71).
    for (auto &name : p.sounds2)
        blob.Name(name);
    for (auto &v : p.unknownFB0)                        // 0xDB1
        blob(v);
    TransferLayers(blob, p.layers2A, p.layers2AValues, p.layers2AFlags);   // 0xDC1
    TransferLayers(blob, p.layers2B, p.layers2BValues, p.layers2BFlags);   // 0xEB6
    TransferLayers(blob, p.layers2C, p.layers2CValues);                    // 0xFAB
    blob.Name(p.sound2_E50);                            // 0x109B
    // Fix: the original writer stores unknown11A0[4] twice instead of [5] (and unknown11B8[7]
    // twice instead of [8] below), leaving those two values' blob bytes (0x10BF, 0x10F3)
    // unwritten; the reader takes them.
    for (auto &v : p.unknown11A0)                       // 0x10AB
        blob(v);
    blob.Name(p.sound2_E60);                            // 0x10C3
    for (auto &v : p.unknown11B8)                       // 0x10D3
        blob(v);
    blob.Name(p.sound2_E70);                            // 0x10F7
    for (auto &name : p.sounds2_E80)
        blob.Name(name);
    TransferCrashes(blob, p.crash2A, p.crash2B, p.crash2Values, p.crash2Flags);   // 0x1197
    blob.Name(p.sound2_F90);                            // 0x122B
    blob.Name(p.sound2_FA0);                            // 0x123B, the end: 0x124B
}

}  // namespace

bool PackVehicleBlob(const uint8_t *data, uint32_t size, std::vector<uint8_t> &packed)   // 0x004d0110 (__fastcall)
{
    // The escape byte: the least frequent one (of equals, the lowest).
    uint32_t counts[256] = {};
    for (uint32_t i = 0; i < size; ++i)
        ++counts[data[i]];
    uint8_t escape = 0;
    uint32_t fewest = 0xFFFFFFFF;
    for (int b = 0; b < 256; ++b) {
        if (counts[b] < fewest) {
            escape = static_cast<uint8_t>(b);
            fewest = counts[b];
        }
    }

    packed.assign(COMP_HEADER, 0);
    // A run of `count` bytes `value`: coded from 4 on, or for the escape byte (even 0..3 long:
    // the run that follows a full one of 255 can be empty); else the bytes themselves.
    auto emit = [&](uint8_t value, uint32_t count) {
        if (count > 3 || value == escape) {
            packed.push_back(escape);
            packed.push_back(value);
            packed.push_back(static_cast<uint8_t>(count));
        } else {
            packed.insert(packed.end(), count, value);
        }
    };
    if (size) {
        uint8_t value = data[0];
        uint32_t count = 0;
        for (uint32_t i = 0; i < size; ++i) {
            if (data[i] == value) {
                if (++count == COMP_MAX_RUN) {
                    emit(value, count);
                    count = 0;
                }
            } else {
                emit(value, count);
                value = data[i];
                count = 1;
            }
        }
        emit(value, count);
    }

    const uint32_t magic = COMP_MAGIC;
    const uint32_t packedSize = static_cast<uint32_t>(packed.size());
    std::memcpy(&packed[0], &magic, 4);
    std::memcpy(&packed[4], &size, 4);
    std::memcpy(&packed[8], &packedSize, 4);
    packed[12] = escape;
    return true;
}

bool UnpackVehicleBlob(const uint8_t *packed, uint32_t packedSize, std::vector<uint8_t> &data)   // 0x004d0110 (__fastcall)
{
    uint32_t magic = 0;
    if (packedSize >= COMP_HEADER)
        std::memcpy(&magic, packed, 4);
    if (magic != COMP_MAGIC) {
        data.clear();
        return false;
    }
    uint32_t size, end;
    std::memcpy(&size, packed + 4, 4);
    std::memcpy(&end, packed + 8, 4);
    const uint8_t escape = packed[12];
    // Fix: the original trusts the header: it reads up to its coded size and writes the runs
    // without checking either buffer's end (and leaves what the code does not fill
    // uninitialised).
    if (end > packedSize)
        end = packedSize;
    data.assign(size, 0);
    uint32_t out = 0;
    for (uint32_t i = COMP_HEADER; i < end;) {
        if (packed[i] == escape) {
            if (i + 2 >= end)
                break;
            const uint8_t value = packed[i + 1];
            for (uint32_t n = packed[i + 2]; n && out < size; --n)
                data[out++] = value;
            i += 3;
        } else {
            if (out < size)
                data[out++] = packed[i];
            ++i;
        }
    }
    return true;
}

void __stdcall ReadVehicleParams(const uint8_t *blob, S_VehicleParams &params, bool tyreIndices)   // 0x004d7d80
{
    C_BlobReader reader(blob, tyreIndices);
    TransferParams(reader, params);
}

void __stdcall WriteVehicleParams(uint8_t *blob, const S_VehicleParams &params, bool tyreIndices)   // 0x004d6c50
{
    // Fix: the original leaves the gaps and what follows each name uninitialised (the
    // caller's buffer is fresh from operator new).
    std::memset(blob, 0, VEHICLE_PARAMS_BLOB_SIZE);
    C_BlobWriter writer(blob, tyreIndices);
    TransferParams(writer, params);
}
