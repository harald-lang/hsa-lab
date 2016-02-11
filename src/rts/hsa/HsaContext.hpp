#pragma once
//---------------------------------------------------------------------------
// SIM[DT] Lab
// (c) Harald Lang 2016
//---------------------------------------------------------------------------
#include <algorithm>
#include <cstring> // memset
#include <rts/hsa/HsaException.hpp>
#include <rts/hsa/HsaRuntime.hpp>
#include <rts/hsa/HsaUtils.hpp>
#include <functional>
#include <hsa.h>
#include <hsa_ext_finalize.h>
#include <iostream> // TODO remove
#include <memory>

namespace rts {
namespace hsa {

class HsaContext {
public:
   struct KernelDescriptor {
      uint64_t kernelObject;
      uint32_t argumentSegmentSize;
      uint32_t groupSegmentSize;
      uint32_t privateSegmentSize;
   };

   struct Future {
      hsa_signal_t completionSignal;

      Future()
      :
            completionSignal({0}) {
      }

      Future(hsa_signal_t completionSignal)
      :
            completionSignal(completionSignal) {
      }

      void wait() {
         // Wait for the task to finish, which is the same as waiting for the value
         // of the completion signal to become zero
         while (hsa_signal_wait_acquire(completionSignal, HSA_SIGNAL_CONDITION_EQ,
               0, UINT64_MAX,
               HSA_WAIT_STATE_BLOCKED) != 0)
            ;
         // Done! The kernel has completed. Time to cleanup resources and leave
         HsaUtils::apiCall([&] {return hsa_signal_destroy(completionSignal);});
      }
   };

   /// C'tor, requires an initialized HSA runtime object.
   explicit HsaContext(HsaRuntime &rt);

   /// D'tor
   ~HsaContext();

   ///
   void addModule(const char *brigModulePtr); // TODO use uint8_t instead

   void finalize();

   void createQueue();

   KernelDescriptor getKernelObject(const std::string &kernelSymbolName);

   template<typename ... Args>
   inline void dispatch(const std::string &kernelSymbolName, const size_t n, const Args &... args) {
      const KernelDescriptor kernel = getKernelObject(kernelSymbolName);
      dispatch<Args...>(kernel, n, args...);
   }

   template<typename ... Args>
   inline void dispatch(const KernelDescriptor &kernel, const size_t n, const Args &... args) {
      Future task = dispatchAsync<Args...>(kernel, n, args...);
      task.wait();
   }

   template<typename ... Args>
   inline Future dispatchAsync(const KernelDescriptor &kernel, const size_t n, const Args &... args) {
      // Request and populate an AQL packet.
      const uint64_t packetId = queueRequestPacketId();
      hsa_kernel_dispatch_packet_t *packetPtr = queueGetKernelDispatchPacketPtr(packetId);
      void *argPtr = getArgBufferPtr(packetId);

      // Reserved fields, private and group memory, and completion signal are all set to 0.
      // Note: The header (first 4 bytes) will be written atomically later on.
      std::memset(((uint8_t *) packetPtr) + 4, 0, sizeof(hsa_kernel_dispatch_packet_t) - 4);
      const size_t defaultWorkgroupSize = 1024; // TODO: make hardware dependent parameter configurable
      packetPtr->workgroup_size_x = std::min(n, defaultWorkgroupSize);
      packetPtr->workgroup_size_y = 1;
      packetPtr->workgroup_size_z = 1;
      packetPtr->grid_size_x = n;
      packetPtr->grid_size_y = 1;
      packetPtr->grid_size_z = 1;

      // Indicate which executable code to run. (= pointer to the finalized kernel)
      packetPtr->kernel_object = kernel.kernelObject;
      packetPtr->kernarg_address = argPtr;
      packetPtr->private_segment_size = kernel.privateSegmentSize;
      packetPtr->group_segment_size = kernel.groupSegmentSize;

      // Copy arguments.
      //   Note: OpenCL kernels compiled with CLOC have 6 additional leading
      //   parameters which can all be set to NULL.
      constexpr size_t numLeadingParameters = 6;
      void** writer = reinterpret_cast<void**>(argPtr);
      for (size_t i = 0; i < numLeadingParameters; i++) {
         //*writer = queue;
         writer++;
      }
//      void **fooo = reinterpret_cast<void **>(argPtr);
//      fooo[4] = queue;
//      fooo[5] = packetPtr;
      writeArgs(writer, args...);

      // Create a signal with an initial value of one to monitor the task
      // completion
      HsaUtils::apiCall([&] {
         return hsa_signal_create(1, 0, NULL, &packetPtr->completion_signal);
      });

      // Atomically set header and setup fields (as described in the specs)
      __atomic_store_n(reinterpret_cast<uint32_t *>(packetPtr), dispatchPacketHeader, __ATOMIC_RELEASE);

      // Notify the runtime that a new packet is enqueued
      hsa_signal_store_release(queue->doorbell_signal, packetId);

      Future task{packetPtr->completion_signal};
      return task;
   }

   template<typename ... Args>
   inline void dispatchBatch(const KernelDescriptor &kernelObject,
         const size_t n, const Args&... args) {
      // Enqueue AQL packet.
      const uint64_t packetId = enqueueForBatchProcessing<Args...>(kernelObject, n, args...);

      // Notify the runtime that a new packet is enqueued.
      ringDoorbell(packetId);
   }

   template<typename ... Args>
   inline uint64_t enqueueForBatchProcessing(const KernelDescriptor &kernel, const size_t n, const Args &... args) {
      // Request and populate an AQL packet.
      const uint64_t packetId = queueRequestPacketId();
      hsa_kernel_dispatch_packet_t *packetPtr = queueGetKernelDispatchPacketPtr(packetId);
      void *argPtr = getArgBufferPtr(packetId);

      packetPtr->grid_size_x = n;

      // Indicate which executable code to run. (= pointer to the finalized kernel)
      packetPtr->kernel_object = kernel.kernelObject;
      packetPtr->private_segment_size = kernel.privateSegmentSize;
      packetPtr->group_segment_size = kernel.groupSegmentSize;

      // Use the batch completion signal. All packets that belong to a batch share the same signal.
      packetPtr->completion_signal = batchCompletionSignal;

      // Copy arguments.
      //   Note: OpenCL kernels compiled with CLOC have 6 additional leading
      //   parameters which can all be set to NULL.
      constexpr size_t numLeadingParameters = 6;
      uintptr_t *writer = reinterpret_cast<uintptr_t *>(argPtr);
      writeArgs(&writer[numLeadingParameters], args...);

      // Atomically increment the completion signal value
      hsa_signal_add_relaxed(batchCompletionSignal, 1);

      // Atomically set header and setup fields (as described in the specs)
      __atomic_store_n(
            reinterpret_cast<uint32_t *>(packetPtr),
            dispatchPacketHeader, __ATOMIC_RELEASE);

      return packetId;
   }

   inline void ringDoorbell(const uint64_t packetId) {
      // Notify the runtime that a new packet is enqueued.
      hsa_signal_store_release(queue->doorbell_signal, packetId);
   }

   inline void waitForBatchCompletion() {
      while (hsa_signal_wait_acquire(batchCompletionSignal,
            HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX,
            HSA_WAIT_STATE_BLOCKED) != 0) {
      };
   }

protected:
   void iterateKernelCodeSymbols(std::function<void(std::string &)> callback);

   hsa_executable_symbol_t
   getExecutableSymbol(const std::string &kernelSymbolName);

   inline uint64_t queueRequestPacketId() {
      return hsa_queue_add_write_index_relaxed(queue, 1);
   }

   inline hsa_kernel_dispatch_packet_t *
   queueGetKernelDispatchPacketPtr(const uint64_t packetId) {
      const uint32_t queueMask = queueSize - 1;
      hsa_kernel_dispatch_packet_t *packetPtr =
            reinterpret_cast<hsa_kernel_dispatch_packet_t *>(queue->base_address) + (packetId & queueMask);
      return packetPtr;
   }

   void *getArgBufferPtr(const uint64_t packetId);

   static void writeArgs(void * /* writer */) {
   }

   template<typename T, typename ... Ts>
   static void writeArgs(void *writer, const T &arg, const Ts &... args) {
      uintptr_t pointer = reinterpret_cast<uintptr_t>(writer);
      uint8_t *writePosition = reinterpret_cast<uint8_t *>((pointer + alignof(T) - 1) & -alignof(T));
      *reinterpret_cast<T *>(writePosition) = arg;
      writeArgs(writePosition + sizeof(T), args...);
   }

   // We support only 1-dimensional kernels.
   static constexpr uint32_t numDimensions = 1;
   static constexpr uint32_t dispatchPacketHeader =
         0 | (HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE) |
               (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE) |
               (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE) |
               ((numDimensions << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS) << 16);

private:
   HsaRuntime &rt;
   hsa_ext_program_t program;
   hsa_code_object_t codeObject;
   hsa_executable_t executable;
   hsa_queue_t *queue;
   uint32_t queueSize;

   /// Points to the pre-allocated kernel argument memory-segment. It contains
   /// ``queueSize'' entries, each of size ``argumentSize''.
   void *argumentMemoryPtr;

   /// The the number of bytes required for kernel argument passing (including padding).
   uint32_t argumentSize;

   /// Completion signal for batch-dispatching.
   hsa_signal_t batchCompletionSignal;
};
}
}
