#pragma once

#include <hsa.h>
#include <iostream>

namespace rts {
namespace hsa {

class KernelArgs {

private:
   /// Points to the HSA kernel argument buffer.
   void* ptr;

   KernelArgs(void* ptr) :
         ptr(ptr) {
   }
   ;

public:
   ~KernelArgs() {
      hsa_status_t status;
      status = hsa_memory_free(ptr);
      if (status != HSA_STATUS_SUCCESS) {
         std::cerr << "Failed to free kernel argument memory." << std::endl;
      }
   }

   void* operator*() {
      return ptr;
   }

   template<typename ... Args>
   static KernelArgs build(const hsa_region_t kernelArgumentRegion, const Args&... args) {
      // Determine the size of the kernel arguments
      constexpr size_t argsSize = sizeOfArgs(args...);

      // Allocate memory (Note: we have to use HSA functions to allocate memory in a region that is associated with the kernel agent)
      void* ptr = nullptr;
      hsa_status_t status;
      status = hsa_memory_allocate(kernelArgumentRegion, argsSize, &ptr);
      if (status != HSA_STATUS_SUCCESS) {
         // TODO throw exception
      }

      // Copy the actual parameters to the newly allocated memory
      writeArgs(ptr, args...);
      return KernelArgs{ptr};
   }

   template<typename ... Args>
   static KernelArgs buildOpenCL(const hsa_region_t kernelArgumentRegion, const Args&... args) {
      // OpenCL kernels compiled with CLOC have 6 additional leading parameters which can all be set to NULL.
      constexpr size_t numLeadingParameters = 6;

      // Determine the size of the kernel arguments
      constexpr size_t argsSize = sizeOfArgs(args...) + numLeadingParameters * sizeof(uintptr_t);

      // Allocate memory (Note: we have to use HSA functions to allocate memory in a region that is associated with the kernel agent)
      void* ptr = nullptr;
      hsa_status_t status;
      status = hsa_memory_allocate(kernelArgumentRegion, argsSize, &ptr);
      if (status != HSA_STATUS_SUCCESS) {
         // TODO throw exception
      }

      // Copy the actual parameters to the newly allocated memory
      uintptr_t* writer = reinterpret_cast<uintptr_t*>(ptr);
      for (size_t i = 0; i < numLeadingParameters; i++) {
         *writer = 0;
         writer++;
      }
      writeArgs(writer, args...);
      return KernelArgs{ptr};
   }

private:
   static constexpr size_t sizeOfArgsRec(const size_t size) {
      return size;
   }

   template<typename T, typename ... Ts>
   static constexpr size_t sizeOfArgsRec(const size_t size, const T& /* arg */, const Ts&... args) {
      return sizeOfArgsRec(((size + alignof(T) - 1) & -alignof(T)) + sizeof(T), args...);
   }

   template<typename ... Args>
   static constexpr size_t sizeOfArgs(const Args&... args) {
      // Calculates how much space the arguments will take as kernel arguments.
      // Basically it returns sizeof(args_struct) padded to multiple of 16 with
      //
      // struct args_struct {
      //     Arg1 arg1;
      //     Arg2 arg2;
      //     ...
      // }
      //
      // while taking into account all needed padding between the arguments.
      return (sizeOfArgsRec(0, args...) + 15) / 16 * 16;
   }

   static void writeArgs(void* /* writer */) {
   }

   template<typename T, typename ... Ts>
   static void writeArgs(void* writer, const T& arg, const Ts&... args) {
      uintptr_t pointer = reinterpret_cast<uintptr_t>(writer);
      uint8_t* writePosition = reinterpret_cast<uint8_t*>((pointer + alignof(T) - 1) & -alignof(T));
      *reinterpret_cast<T*>(writePosition) = arg;
      writeArgs(writePosition + sizeof(T), args...);
   }

};

}
}
