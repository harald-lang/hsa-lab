#include <rts/hsa/HsaContext.hpp>
#include <rts/hsa/HsaException.hpp>
#include <rts/hsa/HsaRuntime.hpp>
#include <rts/hsa/HsaUtils.hpp>
#include <hsa.h>
#include <hsa_ext_finalize.h>
#include <functional>
#include <memory>
#include <iostream> // TODO remove

namespace rts {
namespace hsa {

using namespace std;

HsaContext::HsaContext(HsaRuntime& rt) :
      rt(rt), program({0}), codeObject({0}), executable({0}),
            queue(nullptr), argumentMemoryPtr(nullptr), argumentSize(0) {

   if (HsaUtils::isInitialized() == false) {
      throw HsaException("HSA runtime not initialized.");
   }

   HsaUtils::apiCall([&] {
      return hsa_ext_program_create(
            HSA_MACHINE_MODEL_LARGE,
            HSA_PROFILE_FULL,
            HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT,
            nullptr, /* no options */
            &program);
   });

   // Create batch-completion signal.
   HsaUtils::apiCall([&] {
      return hsa_signal_create(0, 0, NULL, &batchCompletionSignal);
   });
}

HsaContext::~HsaContext() {
   if (HsaUtils::isInitialized() == false) return;

   // Free argument memory-segment.
   if (argumentMemoryPtr != nullptr) {
      HsaUtils::apiCall([&] {
         return hsa_memory_free(argumentMemoryPtr);
      });
   }

   // Destroy queue. // TODO explicitly destroy signals
   if (queue != nullptr) {
      HsaUtils::apiCall([&] {
         return hsa_queue_destroy(queue);
      });
   }

   // Destroy executable.
   if (executable.handle != 0) {
      HsaUtils::apiCall([&] {
         return hsa_executable_destroy(executable);
      });
   }

   // Destroy code object.
   if (codeObject.handle != 0) {
      HsaUtils::apiCall([&] {
         return hsa_code_object_destroy(codeObject);
      });
   }

   // Destroy program.
   if (program.handle != 0) {
      HsaUtils::apiCall([&] {
         return hsa_ext_program_destroy(program);
      });
   }
   cout << "HSA context destructed." << endl;
}

void HsaContext::addModule(const char* brigModulePtr) {
   HsaUtils::apiCall([&] {
      return hsa_ext_program_add_module(program, (hsa_ext_module_t)brigModulePtr);
   });
}

void HsaContext::finalize() {
   // Finalize HSA modules (results in a ``code object'')
   HsaUtils::apiCall([&] {
      hsa_ext_control_directives_t finalizerControlDirectives;
      finalizerControlDirectives.control_directives_mask = 0;
      return hsa_ext_program_finalize(
            program,
            rt.kernelAgentIsa,
            HSA_EXT_FINALIZER_CALL_CONVENTION_AUTO,
            finalizerControlDirectives,
            nullptr, /* no options */
            HSA_CODE_OBJECT_TYPE_PROGRAM,
            &codeObject);
   });

   // Destroy the HSA program as it is no longer needed.
   HsaUtils::apiCall([&] {
      return hsa_ext_program_destroy(program);
   });
   program = {0};

   // Create an executable.
   // Note, that the lifetime of the code object must exceed that of the executable.
   HsaUtils::apiCall([&] {
      return hsa_executable_create(
            HSA_PROFILE_FULL,
            HSA_EXECUTABLE_STATE_UNFROZEN,
            nullptr, /* no options */
            &executable);
   });

   HsaUtils::apiCall([&] {
      return hsa_executable_load_code_object(
            executable,
            rt.kernelAgent,
            codeObject,
            nullptr);
   });

   HsaUtils::apiCall([&] {
      return hsa_executable_freeze(
            executable,
            nullptr);
   });
}

void HsaContext::createQueue() {
   // Determine the sizes of memory segments.
   uint32_t maxKernelArgSegmentSize = 0;
   uint32_t maxKernelGroupSegmentSize = 0;
   uint32_t maxKernelPrivateSegmentSize = 0;

   iterateKernelCodeSymbols([&](std::string& kernelSymbolName) {
      std::cout << kernelSymbolName << std::endl;
      hsa_executable_symbol_t executableSymbol;
      const char* moduleName = nullptr; // not yet supported
         HsaUtils::apiCall([&] {
                  return hsa_executable_get_symbol(
                        executable,
                        moduleName,
                        kernelSymbolName.c_str(),
                        rt.kernelAgent,
                        HSA_EXT_FINALIZER_CALL_CONVENTION_AUTO,
                        &executableSymbol);
               });
         uint32_t kernelArgSegmentSize;
         HsaUtils::apiCall([&] {
                  return hsa_executable_symbol_get_info(
                        executableSymbol,
                        HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE,
                        &kernelArgSegmentSize);
               });
         maxKernelArgSegmentSize = std::max(maxKernelArgSegmentSize, kernelArgSegmentSize);

         uint32_t kernelGroupSegmentSize;
         HsaUtils::apiCall([&] {
                  return hsa_executable_symbol_get_info(
                        executableSymbol,
                        HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE,
                        &kernelGroupSegmentSize);
               });
         maxKernelGroupSegmentSize = std::max(maxKernelGroupSegmentSize, kernelGroupSegmentSize);

         uint32_t kernelPrivateSegmentSize;
         HsaUtils::apiCall([&] {
                  return hsa_executable_symbol_get_info(
                        executableSymbol,
                        HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE,
                        &kernelPrivateSegmentSize);
               });
         maxKernelPrivateSegmentSize = std::max(maxKernelPrivateSegmentSize, kernelPrivateSegmentSize);
      });

   // Determine the queue size.
   uint32_t minQueueSize;
   HsaUtils::apiCall([&] {
      return hsa_agent_get_info(
            rt.kernelAgent,
            HSA_AGENT_INFO_QUEUE_MIN_SIZE,
            &minQueueSize);
   });
   uint32_t maxQueueSize;
   HsaUtils::apiCall([&] {
      return hsa_agent_get_info(
            rt.kernelAgent,
            HSA_AGENT_INFO_QUEUE_MAX_SIZE,
            &maxQueueSize);
   });
   const uint32_t preferredQueueSize = 1 << 4; // must be a power of two
   const uint32_t queueSize = std::min(std::max(preferredQueueSize, minQueueSize), maxQueueSize);
   std::cout << "queueSize=" << queueSize << " (min=" << minQueueSize << ", max=" << maxQueueSize << ")" << std::endl;

   // Create the actual queue.
   HsaUtils::apiCall([&] {
      return hsa_queue_create(
            rt.kernelAgent,
            queueSize,
            HSA_QUEUE_TYPE_SINGLE, /* not thread-safe! */
            nullptr,
            nullptr,
            maxKernelPrivateSegmentSize,
            maxKernelGroupSegmentSize,
            &queue);
   });

   // (Pre-)Allocate memory for kernel arguments.
   const hsa_region_t kernelArgumentRegion = HsaUtils::determineKernelArgumentRegion(rt.kernelAgent);
   constexpr uint32_t argAlign = 8;
   argumentSize = ((maxKernelArgSegmentSize / argAlign) * argAlign) + argAlign * (maxKernelArgSegmentSize % argAlign);
   std::cout << "argSize=" << maxKernelArgSegmentSize << ", paddedSize=" << argumentSize << std::endl;
   const uint32_t totalArgSegmentSize = argumentSize * queueSize;
   HsaUtils::apiCall([&] {
      return hsa_memory_allocate(kernelArgumentRegion, totalArgSegmentSize, &argumentMemoryPtr);
   });

   // Initialize argument buffer
   std::memset(argumentMemoryPtr, 0, queueSize * argumentSize);

   // Initialize packets
   for (size_t i = 0; i < queueSize; i++) {
      hsa_kernel_dispatch_packet_t* packetPtr = queueGetKernelDispatchPacketPtr(i);
      std::memset(((uint8_t*) packetPtr) + 4, 0, sizeof(hsa_kernel_dispatch_packet_t) - 4);
      packetPtr->workgroup_size_x = 128; // TODO: make hardware dependent parameter configurable
      packetPtr->workgroup_size_y = 1;
      packetPtr->workgroup_size_z = 1;
      packetPtr->grid_size_x = 1;
      packetPtr->grid_size_y = 1;
      packetPtr->grid_size_z = 1;
      void* argPtr = getArgBufferPtr(i);
      packetPtr->kernarg_address = argPtr;
   }

   // TODO bind arg buffers to queue entries
   // TODO init queue entries (signals etc.)
}

void HsaContext::iterateKernelCodeSymbols(std::function<void(std::string&)> callback) {
   hsa_code_object_iterate_symbols(codeObject,
         [] (hsa_code_object_t /* code */, hsa_code_symbol_t symbol, void* data) -> hsa_status_t {
            hsa_symbol_kind_t kind;
            hsa_code_symbol_get_info(symbol, HSA_CODE_SYMBOL_INFO_TYPE, &kind);
            if (kind != HSA_SYMBOL_KIND_KERNEL) {
               /* continue iteration */
               return HSA_STATUS_SUCCESS;
            }
            uint32_t length;
            hsa_code_symbol_get_info(symbol, HSA_CODE_SYMBOL_INFO_NAME_LENGTH, &length);
            std::string symbolName(length, '_');
            hsa_code_symbol_get_info(symbol, HSA_CODE_SYMBOL_INFO_NAME, &symbolName[0]);
            std::function<void(std::string&)> callback = *reinterpret_cast<std::function<void(std::string&)>*>(data);
            callback(symbolName);
            return HSA_STATUS_SUCCESS;
         }, &callback);
}

void* HsaContext::getArgBufferPtr(const uint64_t packetId) {
   const uint32_t queueMask = queue->size - 1;
   const uint64_t pos = packetId & queueMask;
   void* argPtr = reinterpret_cast<uint8_t*>(argumentMemoryPtr) + (pos * argumentSize);
   return argPtr;
}

// TODO remove from public API
hsa_executable_symbol_t HsaContext::getExecutableSymbol(const std::string& kernelSymbolName) {
   hsa_executable_symbol_t executableSymbol;
   const char* moduleName = nullptr; // not yet supported
   HsaUtils::apiCall([&] {
      return hsa_executable_get_symbol(
            executable,
            moduleName,
            kernelSymbolName.c_str(),
            rt.kernelAgent,
            HSA_EXT_FINALIZER_CALL_CONVENTION_AUTO,
            &executableSymbol);
   });
   return executableSymbol;
}

HsaContext::KernelDescriptor HsaContext::getKernelObject(const std::string& kernelSymbolName) {
   const hsa_executable_symbol_t executableSymbol = getExecutableSymbol(kernelSymbolName);

   KernelDescriptor kernel;

   HsaUtils::apiCall([&] {
      return hsa_executable_symbol_get_info(
            executableSymbol,
            HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT,
            &kernel.kernelObject);
   });

   // Extract dispatch information such as group segment size etc.
   HsaUtils::apiCall([&] {
      return hsa_executable_symbol_get_info(
            executableSymbol,
            HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE,
            &kernel.argumentSegmentSize);
   });
   HsaUtils::apiCall([&] {
      return hsa_executable_symbol_get_info(
            executableSymbol,
            HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE,
            &kernel.groupSegmentSize);
   });
   HsaUtils::apiCall([&] {
      return hsa_executable_symbol_get_info(
            executableSymbol,
            HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE,
            &kernel.privateSegmentSize);
   });

   return kernel;
}

} // namespace hsa
} // namespace rts
