#include "MurmurHash.h"
#include "Types.h"


__kernel void hash64(__global const uint64_t* in, __global uint64_t* out, const size_t n) {
   const size_t gid=get_global_id(0);
   const uint32_t lid = get_local_id(0);
   if (lid > 16) return;
   if (gid<n) {
      out[gid]=
      murmurHash64a(murmurHash64a(
                  murmurHash64a(murmurHash64a(
                              murmurHash64a(murmurHash64a(
                                          murmurHash64a(murmurHash64a(
                                                      murmurHash64a(murmurHash64a(in[gid]))))))))));
   }
}

__kernel void hash64Loop(__global const uint64_t* in, __global uint64_t* out, const size_t n) {
   const uint32_t numThreads = get_global_size(0);
   const uint32_t gid = get_global_id(0);
//   const uint32_t lid = get_local_id(0);

   uint32_t pos=gid;
   uint32_t limit=n;
   const uint32_t iterLimit = (n/numThreads);

   for (uint32_t i=0; i<iterLimit; i++) {
      if (pos%1024 <= 512) {
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
