///
/// Copyright (C) 2014, Dependable Systems Laboratory, EPFL
/// Copyright (C) 2015, Cyberhaven, Inc
/// All rights reserved. Proprietary and confidential.
///
/// Distributed under the terms of S2E-LICENSE
///


#ifndef S2E_PLUGINS_ModuleMap_H
#define S2E_PLUGINS_ModuleMap_H

#include <s2e/Plugin.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/S2EExecutionState.h>

#include <s2e/Plugins/OSMonitor.h>
#include "WindowsMonitor2.h"

namespace s2e {
namespace plugins {

class ModuleMap : public Plugin
{
    S2E_PLUGIN
public:
    ModuleMap(S2E* s2e): Plugin(s2e) {}

    void initialize();

    ModuleDescriptorList getModulesByPid(S2EExecutionState *state, uint64_t pid);
    const ModuleDescriptor* getModule(S2EExecutionState *state, uint64_t pc);
    const ModuleDescriptor* getModule(S2EExecutionState *state, uint64_t pid, uint64_t pc);
    const ModuleDescriptor* getModule(S2EExecutionState *state, uint64_t pid, const std::string &name);

    void dump(S2EExecutionState *state);

private:
    OSMonitor *m_monitor;


    void onModuleLoad(
        S2EExecutionState* state,
        const ModuleDescriptor &module
    );


    void onModuleUnload(
        S2EExecutionState* state,
        const ModuleDescriptor &module
    );

    void onProcessUnload(
        S2EExecutionState* state,
        uint64_t addressSpace, uint64_t pid
    );

    void onNtUnmapViewOfSection(S2EExecutionState *state, const S2E_WINMON2_UNMAP_SECTION &s);
    void onMonitorLoad(S2EExecutionState *state);
};

} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_ModuleMap_H
