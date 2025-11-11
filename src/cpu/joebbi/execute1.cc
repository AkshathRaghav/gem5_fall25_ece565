#include "cpu/joebbi/execute1.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/joebbi/pipeline.hh"
// Reuse Decode debug category for now to avoid adding a new flag file.
// You can switch to debug/Execute1.hh later if you add that category.
#include "debug/Decode.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Joebbi, joebbi);
namespace joebbi
{

Execute1::Execute1(const std::string &name,
    JoebbiCPU &cpu_,
    const JoebbiCPUParams &params,
    Latch<ForwardInstData>::Output inp_,
    Latch<ForwardInstData>::Input out_,
    std::vector<InputBuffer<ForwardInstData>> &next_stage_input_buffer)
  : Named(name),
    cpu(cpu_),
    inp(inp_),
    out(out_),
    nextStageReserve(next_stage_input_buffer),
    // Use Execute’s input width as our output width; you can add a dedicated
    // execute1InputWidth param later if desired.
    outputWidth(params.executeInputWidth),
    // Reuse Execute’s cycle-input packing behavior
    processMoreThanOneInput(false),
    exec1Info(params.numThreads)
{
    if (outputWidth < 1)
        fatal("%s: executeInputWidth must be >= 1 (%d)\n", name, outputWidth);

    // For EX1’s own input buffer size, reuse Execute’s input buffer size.
    if (params.executeInputBufferSize < 1) {
        fatal("%s: executeInputBufferSize must be >= 1 (%d)\n",
              name, params.executeInputBufferSize);
    }

    // Per-thread EX1 input buffers
    for (ThreadID tid = 0; tid < params.numThreads; tid++) {
        inputBuffer.emplace_back(
            InputBuffer<ForwardInstData>(
                name + ".inputBuffer" + std::to_string(tid), "insts",
                1));
    }
}

const ForwardInstData *
Execute1::getInput(ThreadID tid)
{
    if (!inputBuffer[tid].empty()) {
        const ForwardInstData &head = inputBuffer[tid].front();
        return (head.isBubble() ? nullptr : &inputBuffer[tid].front());
    }
    return nullptr;
}

void
Execute1::popInput(ThreadID tid)
{
    if (!inputBuffer[tid].empty())
        inputBuffer[tid].pop();
    exec1Info[tid].inputIndex = 0;
}

inline ThreadID
Execute1::getScheduledThread()
{
    // Same policy as other stages
    std::vector<ThreadID> priority_list;

    switch (cpu.joebbiThreadPolicy) {
      case enums::SingleThreaded:
        priority_list.push_back(0);
        break;
      case enums::RoundRobin:
        priority_list = cpu.roundRobinPriority(threadPriority);
        break;
      case enums::Random:
        priority_list = cpu.randomPriority();
        break;
      default:
        panic("Unknown fetch policy");
    }

    for (auto tid : priority_list) {
        if (getInput(tid) && !exec1Info[tid].blocked) {
            threadPriority = tid;
            return tid;
        }
    }
    return InvalidThreadID;
}

void Execute1::evaluate()
{
    // Ingest incoming bundle into per-thread buffer
    if (!inp.outputWire->isBubble())
        inputBuffer[inp.outputWire->threadId].setTail(*inp.outputWire);

    ForwardInstData &out_bundle = *out.inputWire;
    assert(out_bundle.isBubble());

    // Back-pressure: can the next stage (Execute) accept a bundle?
    for (ThreadID tid = 0; tid < cpu.numThreads; tid++)
        exec1Info[tid].blocked = !nextStageReserve[tid].canReserve();

    ThreadID tid = getScheduledThread();
    if (tid != InvalidThreadID) {
        const ForwardInstData *in_bundle = getInput(tid);

        if (in_bundle) {
            // ---- PURE PASS-THROUGH: copy the entire bundle, unchanged ----
            out_bundle = *in_bundle;        // copy all slots, including bubbles
            out_bundle.threadId = tid;

            // Reserve space in Execute and mark activity
            nextStageReserve[tid].reserve();
            cpu.activityRecorder->activity();

            // Consume exactly one input bundle
            popInput(tid);
        }
    }

    // Keep stage active if there is more input and room in next stage
    for (ThreadID i = 0; i < cpu.numThreads; i++) {
        if (getInput(i) && nextStageReserve[i].canReserve()) {
            cpu.activityRecorder->activateStage(Pipeline::Execute1StageId);
            break;
        }
    }

    // Push tails for any newly appended input
    if (!inp.outputWire->isBubble())
        inputBuffer[inp.outputWire->threadId].pushTail();
}


bool
Execute1::isDrained()
{
    for (const auto &buf : inputBuffer) {
        if (!buf.empty())
            return false;
    }
    return (*inp.outputWire).isBubble();
}

void
Execute1::joebbiTrace() const
{
    std::ostringstream data;

    if (exec1Info[0].blocked)
        data << 'B';
    else
        (*out.inputWire).reportData(data);

    joebbi::joebbiTrace("EX1 insts=%s\n", data.str());
    inputBuffer[0].joebbiTrace();
}

} // namespace joebbi
} // namespace gem5
