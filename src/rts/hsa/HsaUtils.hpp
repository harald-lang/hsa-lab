#pragma once

#include <rts/hsa/HsaException.hpp>
#include <hsa.h>
#include <functional>

namespace rts {
namespace hsa {

class HsaUtils {
public:

   static bool isInitialized();

   static void checkStatus(hsa_status_t status);

   static hsa_agent_t determineDispatchAgent();

   static hsa_agent_t determineKernelAgent();

   static hsa_region_t determineKernelArgumentRegion(hsa_agent_t kernelAgent);

   static void apiCall(std::function<hsa_status_t()> hsaApiFunc) {
      const hsa_status_t status = hsaApiFunc();
      checkStatus(status);
   }

private:
   HsaUtils() {
   }
   ;
   ~HsaUtils() {
   }
   ;
};

}
}
