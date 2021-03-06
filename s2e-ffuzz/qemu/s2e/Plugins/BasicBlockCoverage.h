///
/// Copyright (C) 2013, Dependable Systems Laboratory, EPFL
/// Copyright (C) 2016, Cyberhaven, Inc
/// All rights reserved. Proprietary and confidential.
///
/// Distributed under the terms of S2E-LICENSE
///


#ifndef S2E_PLUGINS_BasicBlockCoverage_H
#define S2E_PLUGINS_BasicBlockCoverage_H

#include <s2e/Plugin.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/Plugins/ModuleExecutionDetector.h>
#include <s2e/Plugins/ControlFlowGraph.h>

#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/DenseSet.h>
#include <klee/Internal/ADT/ImmutableSet.h>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>


namespace s2e {
namespace plugins {
namespace coverage {

class BasicBlockCoverage : public Plugin
{
    S2E_PLUGIN
public:

    // Use an immutable set to share as much information between the states.
    // This also avoids costly copying when forking.
    typedef klee::ImmutableSet<uint64_t> BasicBlocks;
    typedef std::map<std::string, BasicBlocks> ModuleBasicBlocks;

    struct StateBB {
        S2EExecutionState *state;
        uint64_t pc;
    };

    struct state_t {};
    struct pc_t {};

    typedef boost::multi_index_container<
        StateBB,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<state_t>,
                BOOST_MULTI_INDEX_MEMBER(StateBB,S2EExecutionState *,state)
            >,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<pc_t>,
                BOOST_MULTI_INDEX_MEMBER(StateBB,uint64_t,pc)
            >
        >
    > MultiStatesBB;

    typedef MultiStatesBB::index<state_t>::type StatesByPointer;
    typedef MultiStatesBB::index<pc_t>::type StatesByPc;
    typedef std::map<std::string, MultiStatesBB> StateLocations;



    typedef llvm::DenseSet<uint64_t> CoveredBlocks;
    typedef std::map<std::string, CoveredBlocks> Coverage;

    BasicBlockCoverage(S2E* s2e): Plugin(s2e) {}

    void initialize();

    sigc::signal<void, S2EExecutionState *>
            onNewBlockCovered;

    bool isCovered(const std::string &module, uint64_t block) {
        Coverage::iterator mit = m_coveredBlocks.find(module);
        if (mit == m_coveredBlocks.end()){
            return false;
        }

        const ControlFlowGraph::BasicBlock *bb = m_cfg->findBasicBlock(module, block);
        if (!bb) {
            return false;
        }

        return (*mit).second.find(bb->start_pc) != (*mit).second.end();
    }


    //block is a BASIC BLOCK start, not a random address
    void addNonCoveredBlock(S2EExecutionState *state, const std::string &module, uint64_t block);
    void printNonCoveredBlocks();

    std::string generateJsonCoverageFile(S2EExecutionState *state);
    void generateJsonCoverageFile(S2EExecutionState *state, const std::string &filePath);
    void generateJsonCoverage(S2EExecutionState *state, std::stringstream &ss);

    S2EExecutionState *getNonCoveredState(llvm::DenseSet<S2EExecutionState*> &filter);
    const ModuleBasicBlocks& getCoverage(S2EExecutionState *state);

private:
    ModuleExecutionDetector *m_detector;
    ControlFlowGraph *m_cfg;

    void onCfgReload();
    void onStateKill(S2EExecutionState *state);
    void onModuleTranslateBlockComplete(S2EExecutionState *state,
                               const ModuleDescriptor &module,
                               TranslationBlock *tb, uint64_t pc);

    void onTimer();

    void onUpdateStates(S2EExecutionState* state,
                        const klee::StateSet & addedStates,
                        const klee::StateSet & removedStates);

    Coverage m_coveredBlocks;
    StateLocations m_nonCoveredBasicBlocks;
};

} // namespace coverage
} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_BasicBlockCoverage_H
