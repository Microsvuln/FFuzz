///
/// Copyright (C) 2015, Cyberhaven, Inc
/// All rights reserved. Proprietary and confidential.
///
/// Distributed under the terms of S2E-LICENSE
///


#ifndef S2E_PLUGINS_ProcessExecutionDetector_H
#define S2E_PLUGINS_ProcessExecutionDetector_H

#include <s2e/Plugin.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/Plugins/OSMonitor.h>
#include <s2e/S2EExecutionState.h>

namespace s2e {
namespace plugins {


class ProcessExecutionDetector : public Plugin
{
    S2E_PLUGIN
public:
    ProcessExecutionDetector(S2E* s2e): Plugin(s2e) {}

    void initialize();


    bool isTracked(S2EExecutionState *state);
    bool isTrackedPc(S2EExecutionState *state, uint64_t pc, bool checkCpl = false);
    bool isTracked(S2EExecutionState *state, uint64_t pid);
    bool isTracked(const std::string &module) const {
        return m_trackedModules.find(module) != m_trackedModules.end();
    }

    sigc::signal<void, S2EExecutionState*> onMonitorLoad;

private:
    typedef std::tr1::unordered_set<std::string> StringSet;

    OSMonitor *m_monitor;

    StringSet m_trackedModules;

    void onProcessLoad(S2EExecutionState *state, uint64_t pageDir, uint64_t pid, const std::string &ImageFileName);
    void onProcessUnload(S2EExecutionState *state, uint64_t pageDir, uint64_t pid);

    void onMonitorLoadCb(S2EExecutionState *state);

};

} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_ProcessExecutionDetector_H
