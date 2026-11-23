// Raw storage for the objects script.cpp names but the compiler test never uses (a variable's symbol carries no type).
alignas(16) unsigned char g_itemsModule[0x1000];
// (Not null: a frame script's data must show it, script part 5; script.cpp's constructor alone uses it.)
static const int g_noPhysicsHandle = 0;
extern const void *const g_noPhysicsObject;
const void *const g_noPhysicsObject = &g_noPhysicsHandle;
