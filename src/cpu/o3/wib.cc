#include "cpu/o3/wib.hh"

namespace gem5
{ 

namespace o3 
{

WIB::WIB(CPU *_cpu, const BaseO3CPUParams &params)
    : wibWidth(params.wibWidth),
      cpu(_cpu)
{

}

};

} // namespace gem5::o3
