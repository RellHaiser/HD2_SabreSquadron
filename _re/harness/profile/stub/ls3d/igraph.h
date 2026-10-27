// Harness stub of ls3d/igraph.h.
#pragma once

#include <cstdint>

typedef long HRESULT_T;

class IImage {
public:
    virtual uint32_t Release() = 0;
};

class IGraph {
public:
    long LoadTexture(const char *fileName, const char *alphaFileName, uint32_t mode, IImage **out);
    const wchar_t *GetKeyName(int key);
    void SetGammaRamp(const uint16_t *ramp, uint32_t flags);
};
IGraph *GetIGraph();
