#ifndef __CPU_JOEBBI_EXECUTE1_HH__
#define __CPU_JOEBBI_EXECUTE1_HH__

#include <vector>

#include "base/named.hh"
#include "cpu/joebbi/buffers.hh"
#include "cpu/joebbi/cpu.hh"
#include "cpu/joebbi/dyn_inst.hh"
#include "cpu/joebbi/pipe_data.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Joebbi, joebbi);
namespace joebbi
{

/**
 * Execute1 is a pass-through stage inserted between Decode and Execute.
 * It does no macro->micro work and does not touch LSQ/scoreboards/etc.
 * It simply buffers and forwards bundles to Execute, adding one pipe stage.
 */
class Execute1 : public Named
{
  protected:
    /** Owning CPU */
    JoebbiCPU &cpu;

    /** Input (from Decode) and Output (to Execute) latches */
    Latch<ForwardInstData>::Output inp;
    Latch<ForwardInstData>::Input  out;

    /** Interface to reserve space in the next stage (Execute’s input buffers) */
    std::vector<InputBuffer<ForwardInstData>> &nextStageReserve;

    /** Output width in instructions (match Execute’s input width by default) */
    unsigned int outputWidth;

    /** If true, can consume more than one input bundle per cycle */
    bool processMoreThanOneInput;

  public:
    /** Per-thread input buffers (cycles-worth of inst bundles) */
    std::vector<InputBuffer<ForwardInstData>> inputBuffer;

  protected:
    struct Exec1ThreadInfo
    {
        Exec1ThreadInfo() = default;
        Exec1ThreadInfo(const Exec1ThreadInfo& o)
          : inputIndex(o.inputIndex), blocked(o.blocked) {}

        /** Index into head bundle marking first unconsumed slot */
        unsigned int inputIndex = 0;

        /** For reporting/trace */
        bool blocked = false;
    };

    std::vector<Exec1ThreadInfo> exec1Info;
    ThreadID threadPriority = 0;

    /** Helpers */
    const ForwardInstData *getInput(ThreadID tid);
    void popInput(ThreadID tid);
    ThreadID getScheduledThread();

  public:
    Execute1(const std::string &name,
        JoebbiCPU &cpu_,
        const JoebbiCPUParams &params,
        Latch<ForwardInstData>::Output inp_,
        Latch<ForwardInstData>::Input out_,
        std::vector<InputBuffer<ForwardInstData>> &next_stage_input_buffer);

    /** Forward input to output if possible */
    void evaluate();

    /** Is this stage drained? */
    bool isDrained();

    void joebbiTrace() const;
};

} // namespace joebbi
} // namespace gem5

#endif /* __CPU_JOEBBI_EXECUTE1_HH__ */
