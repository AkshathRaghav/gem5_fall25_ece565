#pragma once
#include "base/logging.hh"
#include "debug/WIB.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"

namespace gem5
{ 

namespace o3 
{

class WIB : public ClockedObject
{
  public:
    WIB(const ClockedObjectParams &p) : ClockedObject(p), _active(false) {}

    void tick() {
        DPRINTF(WIB, "WIB::tick()\n");
        if (!_active) return;
        // future: d    rain reinserts, etc.
        // For now, just keep scheduling to prove wiring.
        scheduleNext();
    }

    // Turn periodic ticking on/off (no functional effect yet)
    void setActive(bool on) {
        _active = on;
        if (on && !scheduled())
            schedule(event, clockEdge(Cycles(1)));
    }

    // Public hooks we’ll fill later (no-op now)
    void enqueue(const DynInstPtr&) {}
    void onMissComplete(unsigned /*tag*/) {}
    void onSquash(uint64_t /*youngestSeqNum*/) {}

    // Expose whether an event is scheduled (for sanity)
    bool scheduled() const { return event.scheduled(); }

    unsigned wibWidth;

  private:
    bool _active;

    // Event wrapper calling tick()
    EventFunctionWrapper event {
        [this]{ this->tick(); }, "WIBTick"
    };

    void scheduleNext() {
        schedule(event, clockEdge(Cycles(1))); // every cycle for now
    }
};

} // namespace o3
} // namespace gem5