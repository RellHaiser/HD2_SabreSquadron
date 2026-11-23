// Raw storage for the objects script.cpp names but the compiler test never uses (a variable's symbol carries no type).
alignas(16) unsigned char g_itemsModule[0x1000];
extern const void *const g_noPhysicsObject;
const void *const g_noPhysicsObject = nullptr;
