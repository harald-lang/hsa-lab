/*
 * HsaRuntime.cpp
 *
 *  Created on: Nov 18, 2015
 *      Author: hl
 */

#include <rts/hsa/HsaException.hpp>
#include <rts/hsa/HsaRuntime.hpp>
#include <rts/hsa/HsaUtils.hpp>
#include <hsa.h>

namespace rts {
namespace hsa {

HsaRuntime::HsaRuntime() :
      dispatchAgent({0}),
            kernelAgent({0}),
            kernelAgentIsa({0}) {
}

HsaRuntime::~HsaRuntime() {
   // TODO Auto-generated destructor stub
}

void HsaRuntime::initialize() {
   hsa_status_t status;
   status = hsa_init();
   HsaUtils::checkStatus(status);

   dispatchAgent = HsaUtils::determineDispatchAgent();
   if (!dispatchAgent.handle) {
      throw HsaException("Failed to determine dispatch agent (CPU).");
   }

   kernelAgent = HsaUtils::determineKernelAgent();
   if (!kernelAgent.handle) {
      throw HsaException("Failed to determine kernel agent (GPU).");
   }
   status = hsa_agent_get_info(kernelAgent, HSA_AGENT_INFO_ISA, &kernelAgentIsa);
   HsaUtils::checkStatus(status);

   // TODO check for machine model LARGE
   // TODO check for profile FULL

}

void HsaRuntime::shutDown() {
   hsa_status_t status;
   status = hsa_shut_down();
   HsaUtils::checkStatus(status);
}

}
}
