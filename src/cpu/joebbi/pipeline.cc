/*
 * Copyright (c) 2013-2014, 2020 ARM Limited
 * All rights reserved
 *
 * [unchanged license header elided for brevity]
 */

#include "cpu/joebbi/pipeline.hh"

#include <algorithm>

#include "cpu/joebbi/decode.hh"
#include "cpu/joebbi/execute.hh"
#include "cpu/joebbi/execute1.hh"   // <-- NEW: dummy execute stage
#include "cpu/joebbi/fetch1.hh"
#include "cpu/joebbi/fetch2.hh"
#include "debug/Drain.hh"
#include "debug/JoebbiCPU.hh"
#include "debug/JoebbiTrace.hh"
#include "debug/Quiesce.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Joebbi, joebbi);
namespace joebbi
{

Pipeline::Pipeline(JoebbiCPU &cpu_, const JoebbiCPUParams &params) :
    Ticked(cpu_, &(cpu_.BaseCPU::baseStats.numCycles)),
    cpu(cpu_),
    allow_idling(params.enableIdling),

    // ----- Inter-stage latches -----
    f1ToF2(cpu.name() + ".f1ToF2", "lines",
        params.fetch1ToFetch2ForwardDelay),
    f2ToF1(cpu.name() + ".f2ToF1", "prediction",
        params.fetch1ToFetch2BackwardDelay, true),
    f2ToD(cpu.name() + ".f2ToD", "insts",
        params.fetch2ToDecodeForwardDelay),

    // SPLIT: Decode -> Execute1 -> Execute
    dToE1(cpu.name() + ".dToE1", "insts",
        // new param (recommend adding in your SimObject); if you haven’t
        // added it yet, temporarily point this to params.decodeToExecuteForwardDelay
        params.decodeToExecute1ForwardDelay),
    e1ToE(cpu.name() + ".e1ToE", "insts",
        params.execute1ToExecuteForwardDelay),

    eToF1(cpu.name() + ".eToF1", "branch",
        params.executeBranchDelay),

    // ----- Stages (construct from later to earlier for correct wiring) -----
    execute(cpu.name() + ".execute", cpu, params,
        e1ToE.output(),           // now fed from Execute1
        eToF1.input()),
    execute1(cpu.name() + ".execute1", cpu, params,
        dToE1.output(),            // fed from Decode
        e1ToE.input(),             // forwards to Execute
        execute.inputBuffer),      // reserves in Execute’s input buffers
    decode(cpu.name() + ".decode", cpu, params,
        f2ToD.output(),
        dToE1.input(),             // now forwards to Execute1
        execute1.inputBuffer),     // reserves in Execute1’s input buffers
    fetch2(cpu.name() + ".fetch2", cpu, params,
        f1ToF2.output(), eToF1.output(), f2ToF1.input(), f2ToD.input(),
        decode.inputBuffer),
    fetch1(cpu.name() + ".fetch1", cpu, params,
        eToF1.output(), f1ToF2.input(), f2ToF1.output(), fetch2.inputBuffer),

    activityRecorder(cpu.name() + ".activity", Num_StageId,
        /* The max depth of inter-stage FIFOs (include the new ones) */
        std::max(params.fetch1ToFetch2ForwardDelay,
        std::max(params.fetch2ToDecodeForwardDelay,
        std::max(
            std::max(params.decodeToExecute1ForwardDelay,
                     params.execute1ToExecuteForwardDelay),
            params.executeBranchDelay)))),

    needToSignalDrained(false)
{
    if (params.fetch1ToFetch2ForwardDelay < 1) {
        fatal("%s: fetch1ToFetch2ForwardDelay must be >= 1 (%d)\n",
            cpu.name(), params.fetch1ToFetch2ForwardDelay);
    }

    if (params.fetch2ToDecodeForwardDelay < 1) {
        fatal("%s: fetch2ToDecodeForwardDelay must be >= 1 (%d)\n",
            cpu.name(), params.fetch2ToDecodeForwardDelay);
    }

    // New forward delays for the split execute path
    if (params.decodeToExecute1ForwardDelay < 1) {
        fatal("%s: decodeToExecute1ForwardDelay must be >= 1 (%d)\n",
            cpu.name(), params.decodeToExecute1ForwardDelay);
    }
    if (params.execute1ToExecuteForwardDelay < 1) {
        fatal("%s: execute1ToExecuteForwardDelay must be >= 1 (%d)\n",
            cpu.name(), params.execute1ToExecuteForwardDelay);
    }

    if (params.executeBranchDelay < 1) {
        fatal("%s: executeBranchDelay must be >= 1\n", cpu.name());
    }
}

void
Pipeline::joebbiTrace() const
{
    fetch1.joebbiTrace();
    f1ToF2.joebbiTrace();
    f2ToF1.joebbiTrace();
    fetch2.joebbiTrace();
    f2ToD.joebbiTrace();
    decode.joebbiTrace();
    dToE1.joebbiTrace();       // new
    execute1.joebbiTrace();    // new
    e1ToE.joebbiTrace();       // new
    execute.joebbiTrace();
    eToF1.joebbiTrace();
    activityRecorder.joebbiTrace();
}

void
Pipeline::evaluate()
{
    /** We tick the CPU to update the BaseCPU cycle counters */
    cpu.tick();

    /* Evaluate stages from later to earlier so 0-time-buffer activity flows
     * “backwards” within the same cycle (Minor/Joebbi convention). */
    execute.evaluate();
    execute1.evaluate(); 
    decode.evaluate();
    fetch2.evaluate();
    fetch1.evaluate();

    if (debug::JoebbiTrace)
        joebbiTrace();

    /* Update the time buffers after the stages */
    f1ToF2.evaluate();
    f2ToF1.evaluate();
    f2ToD.evaluate();
    dToE1.evaluate();  
    e1ToE.evaluate();  
    eToF1.evaluate();

    /* Activity recorder must run after stages and before idler */
    activityRecorder.evaluate();

    if (allow_idling) {
        /* Become idle if we can but are not draining */
        if (!activityRecorder.active() && !needToSignalDrained) {
            DPRINTF(Quiesce, "Suspending as the processor is idle\n");
            stop();
        }

        /* Deactivate all stages each cycle */
        activityRecorder.deactivateStage(Pipeline::CPUStageId);
        activityRecorder.deactivateStage(Pipeline::Fetch1StageId);
        activityRecorder.deactivateStage(Pipeline::Fetch2StageId);
        activityRecorder.deactivateStage(Pipeline::DecodeStageId);
        activityRecorder.deactivateStage(Pipeline::Execute1StageId); // new
        activityRecorder.deactivateStage(Pipeline::ExecuteStageId);
    }

    if (needToSignalDrained) /* Must be draining */
    {
        DPRINTF(Drain, "Still draining\n");
        if (isDrained()) {
            DPRINTF(Drain, "Signalling end of draining\n");
            cpu.signalDrainDone();
            needToSignalDrained = false;
            stop();
        }
    }
}

JoebbiCPU::JoebbiCPUPort &
Pipeline::getInstPort()
{
    return fetch1.getIcachePort();
}

JoebbiCPU::JoebbiCPUPort &
Pipeline::getDataPort()
{
    return execute.getDcachePort();
}

void
Pipeline::wakeupFetch(ThreadID tid)
{
    fetch1.wakeupFetch(tid);
}

bool
Pipeline::drain()
{
    DPRINTF(JoebbiCPU, "Draining pipeline by halting inst fetches. "
        " Execution should drain naturally\n");

    execute.drain();

    /* Make sure that needToSignalDrained isn't accidentally set if we
     * are 'pre-drained' */
    bool drained = isDrained();
    needToSignalDrained = !drained;

    return drained;
}

void
Pipeline::drainResume()
{
    DPRINTF(Drain, "Drain resume\n");

    for (ThreadID tid = 0; tid < cpu.numThreads; tid++) {
        fetch1.wakeupFetch(tid);
    }

    execute.drainResume();
}

bool
Pipeline::isDrained()
{
    bool fetch1_drained = fetch1.isDrained();
    bool fetch2_drained = fetch2.isDrained();
    bool decode_drained = decode.isDrained();
    bool execute1_drained = execute1.isDrained(); // new
    bool execute_drained = execute.isDrained();

    bool f1_to_f2_drained = f1ToF2.empty();
    bool f2_to_f1_drained = f2ToF1.empty();
    bool f2_to_d_drained = f2ToD.empty();
    bool d_to_e1_drained  = dToE1.empty(); // new
    bool e1_to_e_drained  = e1ToE.empty(); // new

    bool ret = fetch1_drained && fetch2_drained &&
        decode_drained && execute1_drained && execute_drained &&
        f1_to_f2_drained && f2_to_f1_drained &&
        f2_to_d_drained && d_to_e1_drained && e1_to_e_drained;

    DPRINTF(JoebbiCPU, "Pipeline undrained stages state:%s%s%s%s%s%s%s%s%s%s\n",
        (fetch1_drained ? "" : " Fetch1"),
        (fetch2_drained ? "" : " Fetch2"),
        (decode_drained ? "" : " Decode"),
        (execute1_drained ? "" : " Execute1"),
        (execute_drained ? "" : " Execute"),
        (f1_to_f2_drained ? "" : " F1->F2"),
        (f2_to_f1_drained ? "" : " F2->F1"),
        (f2_to_d_drained ? "" : " F2->D"),
        (d_to_e1_drained ? "" : " D->E1"),
        (e1_to_e_drained ? "" : " E1->E")
        );

    return ret;
}

} // namespace joebbi
} // namespace gem5
