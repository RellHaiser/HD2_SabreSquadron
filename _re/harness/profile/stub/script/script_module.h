// Harness stub of script/script_module.h.
#pragma once

class C_ScriptModule {
public:
    int *GetSaveGameValues() { return m_values; }
    int m_values[256] = {};
};
extern C_ScriptModule g_scriptModule;
