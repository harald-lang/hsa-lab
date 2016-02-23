#include "MurmurHash.h"
#include "Types.h"

__kernel void simtUtilization(__global const uint64_t* in, __global uint64_t* out, const uint32_t n, const uint32_t active) {
   const size_t gid = get_global_id(0);
   const uint32_t lid = get_local_id(0);
   if (lid > active) return;
   if (gid < n) {
      out[gid]=
      murmurHash64a(murmurHash64a(
                  murmurHash64a(murmurHash64a(
                              murmurHash64a(murmurHash64a(
                                          murmurHash64a(murmurHash64a(
                                                      murmurHash64a(murmurHash64a(in[gid]))))))))));
   }
}

__kernel void simtUtilizationLoop(__global const uint64_t* in, __global uint64_t* out, const uint32_t n, const uint32_t active) {
   const uint32_t numThreads = get_global_size(0);
   const uint32_t workgroupSize = get_local_size(0);
   
   const size_t gid = get_global_id(0);
   const uint32_t iterLimit = (n/numThreads);

   uint32_t pos = gid;
   for (uint32_t i=0; i<iterLimit; i++) {
      if (pos%workgroupSize <= active) {

         out[pos]=
         murmurHash64a(murmurHash64a(
                     murmurHash64a(murmurHash64a(
                                 murmurHash64a(murmurHash64a(
                                             murmurHash64a(murmurHash64a(
                                                         murmurHash64a(murmurHash64a(in[pos]))))))))));
         
         pos+=numThreads;
      }
      barrier(CLK_LOCAL_MEM_FENCE);
   }
}

