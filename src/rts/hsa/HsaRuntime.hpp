#pragma once

#include <hsa.h>

namespace rts {
namespace hsa {

class HsaContext;

class HsaRuntime {
   friend HsaContext;

private:
   /// The dispatch agent (typically the CPU)
   hsa_agent_t dispatchAgent;
   /// The kernel dispatch agent (typically the GPU)
   hsa_agent_t kernelAgent;
   /// The instruction set architecture of the kernel agent
   hsa_isa_t kernelAgentIsa;

public:
   HsaRuntime();
   virtual ~HsaRuntime();

   void initialize();

   void shutDown();

};

}
}
