// The Direct3D 8 device the tests' fake devices go in. LS3DRF.dll has no Direct3D (no d3d8.dll, no
// render device): nothing in the DLL reads it; only the tests set and read it.
#include "d3d8_device.h"

// (The name is IGraph's, where the original kept it.)
namespace ls3d::ig {

IDirect3DDevice8 *g_pDevice = nullptr;

}  // namespace ls3d::ig
