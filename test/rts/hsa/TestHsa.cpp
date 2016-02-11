#include "gtest/gtest.h"
#include "hsa.h"
#include "hsa_ext_finalize.h"
#include <sstream>
#include <rts/hsa/HsaException.hpp>
#include <rts/hsa/HsaRuntime.hpp>
#include <rts/hsa/HsaUtils.hpp>
#include <rts/hsa/KernelArgs.hpp>
#include <fstream>
#include <memory>
#include <atomic>
#include <limits>
#include <thread>
#include <chrono>
//---------------------------------------------------------------------------
// SIM[DT] Lab
// (c) Harald Lang 2015
//---------------------------------------------------------------------------
namespace {

using namespace std;
using namespace rts::hsa;

/// Helper function to assert a HSA status.
/// Note: This functions must not be called after `hsa_shut_down()` was called.
static void ASSERT_HSA_STATUS(hsa_status_t expected, hsa_status_t actual) {
   if (expected != actual) {
      const char* status_string_expected;
      hsa_status_string(expected, &status_string_expected);
      const char* status_string_actual;
      hsa_status_string(actual, &status_string_actual);
      ASSERT_STREQ(status_string_expected, status_string_actual);
   }
}

/// Round up to the next power of two.
/// Source: Bit Twiddling Hacks (http://graphics.stanford.edu/~seander/bithacks.html)
static uint32_t roundUpToNextPowerOfTwo(uint32_t value) {
   value--;
   value |= value >> 1;
   value |= value >> 2;
   value |= value >> 4;
   value |= value >> 8;
   value |= value >> 16;
   value++;
   return value;
}

TEST(HSA, InitAndShutdown) {
   hsa_status_t status;
   status = hsa_init();
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);
   status = hsa_shut_down();
   ASSERT_EQ(HSA_STATUS_SUCCESS, status);
}

TEST(HSA, DetermineHsaState) {
   ASSERT_FALSE(HsaUtils::isInitialized());
   hsa_status_t status;
   status = hsa_init();
   ASSERT_TRUE(HsaUtils::isInitialized());
   status = hsa_shut_down();
   ASSERT_FALSE(HsaUtils::isInitialized());
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);
}

TEST(HSA, SystemGetInfo) {
   hsa_status_t status;
   status = hsa_init();
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

   hsa_machine_model_t machine_model;
   status = hsa_system_get_info(HSA_SYSTEM_INFO_MACHINE_MODEL, &machine_model);
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);
   ASSERT_EQ(HSA_MACHINE_MODEL_LARGE, machine_model);

   status = hsa_shut_down();
   ASSERT_EQ(HSA_STATUS_SUCCESS, status);
}

TEST(HSA, AgentDiscovery) {
   hsa_status_t status;
   status = hsa_init();
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

   auto iterateAgentsCallback = [](hsa_agent_t agent, void* data) -> hsa_status_t {
      uint32_t* deviceId = static_cast<uint32_t*>(data);
      stringstream info;
      info << "id=" << *deviceId << ", ";
      (*deviceId)++;

      hsa_device_type_t hsa_device_type;
      hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &hsa_device_type);
      switch (hsa_device_type) {
         case HSA_DEVICE_TYPE_CPU: info << "type=CPU"; break;
         case HSA_DEVICE_TYPE_GPU: info << "type=GPU"; break;
         case HSA_DEVICE_TYPE_DSP: info << "type=DSP"; break;
         default: info << "type=unknown"; break;
      }

      info << ", ";

      hsa_agent_feature_t hsa_agent_feature;
      hsa_agent_get_info(agent, HSA_AGENT_INFO_FEATURE, &hsa_agent_feature);
      switch (hsa_agent_feature) {
         case HSA_AGENT_FEATURE_KERNEL_DISPATCH: info << "dispatch=kernel"; break;
         case HSA_AGENT_FEATURE_AGENT_DISPATCH: info << "dispatch=agent"; break;
         default: info << "dispatch=unknown"; break;
      }

      cout << info.str() << endl;
      return HSA_STATUS_SUCCESS;
   };

   uint32_t deviceId = 0;
   status = hsa_iterate_agents(iterateAgentsCallback, &deviceId);

   status = hsa_shut_down();
   ASSERT_EQ(HSA_STATUS_SUCCESS, status);
}

static hsa_agent_t determineDispatchAgent() {
   auto determineDispatchAgentCallback = [](hsa_agent_t agent, void* data) -> hsa_status_t {
      hsa_agent_t* foundDispatchAgent = static_cast<hsa_agent_t*>(data);
      hsa_device_type_t hsa_device_type;
      hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &hsa_device_type);
      if (hsa_device_type == HSA_DEVICE_TYPE_CPU) {
         *foundDispatchAgent = agent;
      }
      return HSA_STATUS_SUCCESS;
   };

   hsa_status_t status;
   hsa_agent_t dispatchAgent{0};
   status = hsa_iterate_agents(determineDispatchAgentCallback, &dispatchAgent);
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);
   return dispatchAgent;
}

TEST(HSA, DetermineDispatchAgent) {
   hsa_status_t status;
   status = hsa_init();
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

   hsa_agent_t dispatchAgent = determineDispatchAgent();

   hsa_device_type_t hsa_device_type;
   hsa_agent_get_info(dispatchAgent, HSA_AGENT_INFO_DEVICE, &hsa_device_type);
   ASSERT_EQ(HSA_DEVICE_TYPE_CPU, hsa_device_type);

   status = hsa_shut_down();
   ASSERT_EQ(HSA_STATUS_SUCCESS, status);
}

static hsa_agent_t determineKernelAgent() {
   auto determineKernelAgentCallback = [](hsa_agent_t agent, void* data) -> hsa_status_t {
      hsa_agent_t* foundDispatchAgent = static_cast<hsa_agent_t*>(data);

      hsa_agent_feature_t hsa_agent_feature;
      hsa_agent_get_info(agent, HSA_AGENT_INFO_FEATURE, &hsa_agent_feature);
      if (hsa_agent_feature == HSA_AGENT_FEATURE_KERNEL_DISPATCH) {
         *foundDispatchAgent = agent;
      }
      return HSA_STATUS_SUCCESS;
   };

   hsa_status_t status;
   hsa_agent_t kernelAgent{0};
   status = hsa_iterate_agents(determineKernelAgentCallback, &kernelAgent);
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);
   return kernelAgent;
}

TEST(HSA, DetermineKernelAgent) {
   hsa_status_t status;
   status = hsa_init();
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

   hsa_agent_t kernelAgent = determineKernelAgent();

   hsa_device_type_t hsa_device_type;
   hsa_agent_get_info(kernelAgent, HSA_AGENT_INFO_DEVICE, &hsa_device_type);
   ASSERT_EQ(HSA_DEVICE_TYPE_GPU, hsa_device_type);

   hsa_agent_feature_t hsa_agent_feature;
   hsa_agent_get_info(kernelAgent, HSA_AGENT_INFO_FEATURE, &hsa_agent_feature);
   ASSERT_EQ(HSA_AGENT_FEATURE_KERNEL_DISPATCH, hsa_agent_feature);

   status = hsa_shut_down();
   ASSERT_EQ(HSA_STATUS_SUCCESS, status);
}

TEST(HSA, Signals) {
   // TODO
}

static hsa_queue_t* createQueue(hsa_agent_t kernelAgent, const uint32_t minQueueSize) {
   const uint32_t queueSize = roundUpToNextPowerOfTwo(minQueueSize);

   hsa_status_t status;
   hsa_queue_t* queue = nullptr;
   status = hsa_queue_create(kernelAgent, queueSize, HSA_QUEUE_TYPE_SINGLE, nullptr, nullptr, 0, 0, &queue);
   if (status != HSA_STATUS_SUCCESS) {
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);
   }
   return queue;
}

/*
 TEST(HSA, Queue) {
 hsa_status_t status;
 status = hsa_init();
 ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);
 const uint32_t minQueueSize = 4;

 hsa_agent_t kernelAgent = determineKernelAgent();

 // create a queue associated with the kernel agent
 hsa_queue_t* queue = nullptr;
 const uint32_t queueSize = roundUpToNextPowerOfTwo(minQueueSize);
 const uint32_t queueMask = queueSize - 1;
 hsa_queue_create(kernelAgent, queueSize, HSA_QUEUE_TYPE_SINGLE, nullptr, nullptr, 0, 0, &queue);
 ASSERT_TRUE(queue);

 // request a packet id
 uint64_t packetId = hsa_queue_add_write_index_relaxed(queue, 1);
 ASSERT_EQ(0, packetId);

 // calculate the virtual address of the packet. Note: All packet types are of the same size (64 bytes)
 hsa_kernel_dispatch_packet_t* packet =
 static_cast<hsa_kernel_dispatch_packet_t*>(queue->base_address) + (packetId & queueMask);

 // populate fields in dispatch packet

 status = hsa_shut_down();
 ASSERT_EQ(HSA_STATUS_SUCCESS, status);
 }
 */

static void printCodeSymbols(hsa_code_object_t codeObject) {
   hsa_code_object_iterate_symbols(codeObject,
         [] (hsa_code_object_t /* code */, hsa_code_symbol_t symbol, void* /* data */) -> hsa_status_t {
            hsa_symbol_kind_t kind;
            hsa_code_symbol_get_info(symbol, HSA_CODE_SYMBOL_INFO_TYPE, &kind);
            if (kind != HSA_SYMBOL_KIND_KERNEL) {
               // continue iteration
         return HSA_STATUS_SUCCESS;
      }
      uint32_t length;
      hsa_code_symbol_get_info(symbol, HSA_CODE_SYMBOL_INFO_NAME_LENGTH, &length);
      std::string buffer(length, '_');
      hsa_code_symbol_get_info(symbol, HSA_CODE_SYMBOL_INFO_NAME, &buffer[0]);
      cout << buffer << endl;
      return HSA_STATUS_SUCCESS;
   }, nullptr);
}

static hsa_region_t determineKernelArgumentRegion(hsa_agent_t kernelAgent) {
   auto getKernelArgumentRegionCallback =
         [](hsa_region_t region, void* data) -> hsa_status_t {
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
      cerr << "Failed to determine the kernel argument region." << endl;
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);
   }
   return region;
}

static void finalize() {
   hsa_status_t status;
   status = hsa_init();
   ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

   {

      // Create a HSA program
      hsa_ext_program_t program;
      status = hsa_ext_program_create(
            HSA_MACHINE_MODEL_LARGE,
            HSA_PROFILE_FULL,
            HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT,
            nullptr, // no options
            &program);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      // Add the BRIG to the program (add module)
//		auto module = reinterpret_cast<hsa_ext_module_t>(rts::hsa::kernel::StoreGlobalIdKernelBrig);
//		status = hsa_ext_program_add_module(program, (hsa_ext_module_t)(rts::hsa::kernel::StoreGlobalIdKernelBrig));
//		ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      // TODO fix loading from file
      const string filename = "bin/test/rts/hsa/kernel/StoreGlobalId.brig";
      ifstream file(filename, ifstream::binary);
      if (file.fail()) {
         throw "couldn't open file: " + filename;
      }
      string contents((istreambuf_iterator<char>(file)), (istreambuf_iterator<char>()));
      if (contents.substr(0, 8) != "HSA BRIG") {
         throw "invalid magic number";
      }
      status = hsa_ext_program_add_module(program, (hsa_ext_module_t) contents.c_str());
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      hsa_agent_t kernelAgent = determineKernelAgent();
      hsa_isa_t kernelAgentIsa;
      status = hsa_agent_get_info(kernelAgent, HSA_AGENT_INFO_ISA, &kernelAgentIsa);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      // Finalize
      hsa_code_object_t codeObject;
      {
         hsa_ext_control_directives_t finalizerControlDirectives;
         finalizerControlDirectives.control_directives_mask = 0;
         status = hsa_ext_program_finalize(program, kernelAgentIsa,
               HSA_EXT_FINALIZER_CALL_CONVENTION_AUTO, finalizerControlDirectives,
               nullptr, HSA_CODE_OBJECT_TYPE_PROGRAM, &codeObject);
         ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);
         printCodeSymbols(codeObject);
      }
      hsa_ext_program_destroy(program);

      hsa_executable_t executable;
      status = hsa_executable_create(HSA_PROFILE_FULL, HSA_EXECUTABLE_STATE_UNFROZEN, nullptr, &executable);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      status = hsa_executable_load_code_object(executable, kernelAgent, codeObject, nullptr);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      hsa_code_object_destroy(codeObject);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      status = hsa_executable_freeze(executable, nullptr);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      hsa_executable_symbol_t executableSymbol;
      const char* moduleName = nullptr;
      status = hsa_executable_get_symbol(executable, moduleName, "&__OpenCL_storeGlobalId2_kernel",
            kernelAgent, HSA_EXT_FINALIZER_CALL_CONVENTION_AUTO, &executableSymbol);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      uint64_t kernelObject;
      status = hsa_executable_symbol_get_info(executableSymbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &kernelObject);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      uint32_t kernelArgSegmentSize;
      status = hsa_executable_symbol_get_info(executableSymbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE, &kernelArgSegmentSize);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      uint32_t kernelGroupSegmentSize;
      status = hsa_executable_symbol_get_info(executableSymbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE, &kernelGroupSegmentSize);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      uint32_t kernelPrivateSegmentSize;
      status = hsa_executable_symbol_get_info(executableSymbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE, &kernelPrivateSegmentSize);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      hsa_region_t kernelArgRegion = determineKernelArgumentRegion(kernelAgent);

      const size_t n = 1024 * 1024;
      size_t* output = new size_t[n];
      memset(output, 0, n * sizeof(size_t));

      KernelArgs kernelArgs = KernelArgs::buildOpenCL<size_t*, size_t>(kernelArgRegion, output, n);

      hsa_queue_t* queue = createQueue(kernelAgent, 4);

      // request a packet id
      uint64_t packetId = hsa_queue_add_write_index_relaxed(queue, 1);
      ASSERT_EQ(0u, packetId);

      // Calculate the virtual address of the packet. Note: All packet types are of the same size (64 bytes)
      const uint32_t queueSize = roundUpToNextPowerOfTwo(4);
      const uint32_t queueMask = queueSize - 1;
      hsa_kernel_dispatch_packet_t* packet =
            reinterpret_cast<hsa_kernel_dispatch_packet_t*>(queue->base_address) + (packetId & queueMask);

      // Reserved fields, private and group memory, and completion signal are all set to 0.
      memset(((uint8_t*) packet) + 4, 0, sizeof(hsa_kernel_dispatch_packet_t) - 4);
      packet->workgroup_size_x = 128;
      packet->workgroup_size_y = 1;
      packet->workgroup_size_z = 1;
      packet->grid_size_x = n;
      packet->grid_size_y = 1;
      packet->grid_size_z = 1;

      // Indicate which executable code to run.
      // The application is expected to have a finalized kernel (for example, using the finalization API).
      packet->kernel_object = kernelObject;
      packet->kernarg_address = *kernelArgs;

      // Create a signal with an initial value of one to monitor the task completion
      hsa_signal_create(1, 0, NULL, &packet->completion_signal);

      uint16_t header = 0;
      header |= HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE;
      header |= HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE;
      header |= HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE;

      // We assume that we only dispatch 1-dimensional kernels.
      constexpr uint16_t numDimensions = 1;
      uint16_t setup = numDimensions << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;

      // Atomically set header and setup fields (as described in the specs)
      __atomic_store_n(reinterpret_cast<uint32_t*>(packet), header | (setup << 16), __ATOMIC_RELEASE);

      // Notify the runtime that a new packet is enqueued
      hsa_signal_store_release(queue->doorbell_signal, packetId);

      // Wait for the task to finish, which is the same as waiting for the value of the completion signal to be zero
      while (hsa_signal_wait_acquire(packet->completion_signal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_ACTIVE) != 0)
         ;

      // Done! The kernel has completed. Time to cleanup resources and leave

      for (size_t i = 0; i < n; i++) {
         ASSERT_EQ(i, output[i]);
      }

      hsa_signal_destroy(packet->completion_signal);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      hsa_queue_destroy(queue);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      hsa_executable_destroy(executable);
      ASSERT_HSA_STATUS(HSA_STATUS_SUCCESS, status);

      delete[] output;

   }
   status = hsa_shut_down();
   ASSERT_EQ(HSA_STATUS_SUCCESS, status);
}

TEST(HSA, Finalize) {
   finalize();
}

} // namespace
