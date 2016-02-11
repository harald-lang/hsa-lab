#include <rts/hsa/HsaException.hpp>
#include <rts/hsa/HsaUtils.hpp>
#include <hsa.h>
#include <iostream>
#include <limits>

namespace rts {
namespace hsa {

using namespace std;

bool HsaUtils::isInitialized() {
   hsa_machine_model_t machine_model;
   hsa_status_t status;
   status = hsa_system_get_info(HSA_SYSTEM_INFO_MACHINE_MODEL, &machine_model);
   return status == HSA_STATUS_SUCCESS;
}

void HsaUtils::checkStatus(hsa_status_t status) {
   if (status != HSA_STATUS_SUCCESS) {
      if (isInitialized()) {
         const char* errorMessage;
         hsa_status_string(status, &errorMessage);
         if (errorMessage) {
            cout << errorMessage << endl;
            throw HsaException(errorMessage);
         }
         else {
            cout << "HSA_STATUS_CODE: " << status << endl;
            throw HsaException("HSA_STATUS_CODE: " + status);
         }
      }
      else {
         cout << "HSA_STATUS_CODE: " << status << endl;
         throw HsaException("HSA_STATUS_CODE: " + status);
      }
   }
}

hsa_agent_t HsaUtils::determineDispatchAgent() {
   auto determineDispatchAgentCallback = [](hsa_agent_t agent, void* data) -> hsa_status_t {
      hsa_agent_t* foundDispatchAgent = static_cast<hsa_agent_t*>(data);
      hsa_device_type_t hsa_device_type;
      hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &hsa_device_type);
      if (hsa_device_type == HSA_DEVICE_TYPE_CPU) {
         *foundDispatchAgent = agent;
      }
      return HSA_STATUS_SUCCESS;
   };

   hsa_agent_t dispatchAgent{0};
   hsa_status_t status;
   status = hsa_iterate_agents(determineDispatchAgentCallback, &dispatchAgent);
   checkStatus(status);
   return dispatchAgent;
}

hsa_agent_t HsaUtils::determineKernelAgent() {
   auto determineKernelAgentCallback = [](hsa_agent_t agent, void* data) -> hsa_status_t {
      hsa_agent_t* foundDispatchAgent = static_cast<hsa_agent_t*>(data);

      hsa_agent_feature_t hsa_agent_feature;
      hsa_agent_get_info(agent, HSA_AGENT_INFO_FEATURE, &hsa_agent_feature);
      if (hsa_agent_feature == HSA_AGENT_FEATURE_KERNEL_DISPATCH) {
         *foundDispatchAgent = agent;
      }
      return HSA_STATUS_SUCCESS;
   };

   hsa_agent_t kernelAgent{0};
   hsa_status_t status;
   status = hsa_iterate_agents(determineKernelAgentCallback, &kernelAgent);
   checkStatus(status);
   return kernelAgent;
}

hsa_region_t HsaUtils::determineKernelArgumentRegion(hsa_agent_t kernelAgent) {
   auto getKernelArgumentRegionCallback = [](hsa_region_t region, void* data) -> hsa_status_t {
      hsa_region_segment_t segment;
      hsa_region_get_info(region, HSA_REGION_INFO_SEGMENT, &segment);
      if (segment != HSA_REGION_SEGMENT_GLOBAL) {
         return HSA_STATUS_SUCCESS;
      }
      hsa_region_global_flag_t flags;
      hsa_region_get_info(region, HSA_REGION_INFO_GLOBAL_FLAGS, &flags);
      if (flags & HSA_REGION_GLOBAL_FLAG_KERNARG) {
         hsa_region_t* ret = (hsa_region_t*) data;
         *ret = region;
         return HSA_STATUS_INFO_BREAK;
      }
      return HSA_STATUS_SUCCESS;
   };

   hsa_status_t status;
   const auto notFound = numeric_limits<uint64_t>::max();
   hsa_region_t region{notFound};
   status = hsa_agent_iterate_regions(kernelAgent, getKernelArgumentRegionCallback, &region);
   if ((status != HSA_STATUS_SUCCESS && status != HSA_STATUS_INFO_BREAK) || region.handle == notFound) {
      checkStatus(status);
   }
   return region;
}

}
}
