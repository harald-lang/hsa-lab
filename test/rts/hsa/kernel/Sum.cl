#include "Types.h"

__kernel void sumLoop(__global const uint32_t* in, __global uint64_t* out, const size_t n) {
   const uint32_t numThreads = get_global_size(0);
   const uint32_t gid = get_global_id(0);

   uint32_t pos = gid;
   uint64_t localSum = 0;

   const uint32_t iterLimit = (n / numThreads);
   for (uint32_t i = 0; i < iterLimit; i++) {
      localSum += in[pos];
      pos += numThreads;
   }
   out[gid] = localSum;
}

//#define GROUP_SIZE 128

__kernel void sumLoopReduction(__global const uint32_t* in, __global uint64_t* out, const size_t n) {
   const uint32_t globalSize = get_global_size(0);
   const uint32_t groupId = get_group_id(0);
   const uint32_t groupSize = get_local_size(0);
   const uint32_t localId = get_local_id(0);

   uint32_t pos = groupSize * groupId + localId; // = global_id
   uint64_t sum = 0;

   const uint32_t iterLimit = (n / globalSize);
   for (uint32_t i = 0; i < iterLimit; i++) {
      sum += work_group_reduce_add(in[pos]);
//      sum += in[pos];
      pos += globalSize;
   }
//   if (localId == 0) {
   out[get_global_id(0)] = sum;
//   }
}

__kernel void sumGroupReduction(__global const uint32_t* in, __global uint64_t* out) {
   const uint32_t groupId = get_group_id(0);
   const uint32_t groupSize = get_local_size(0);
   const uint32_t localId = get_local_id(0);

   uint32_t pos = groupSize * groupId + localId; // = global_id
   uint64_t value = in[pos];
   uint64_t sum = work_group_reduce_add(value);
   if (localId == 0) {
      out[groupId] = sum;
   }
}

__kernel void sumGroupReductionHand(__global const uint32_t* in, __global uint64_t* out) {
   const uint32_t groupId   = get_group_id(0);
   const uint32_t groupSize = get_local_size(0);
   const uint32_t localId   = get_local_id(0);
   const uint32_t globalId  = get_global_id(0);

   // use static local group memory
   __local uint64_t buf[128];// max group size

   // load
   buf[localId] = in[globalId];

   // reduce locally
   barrier(CLK_LOCAL_MEM_FENCE);
   for(uint32_t stride = groupSize/2; stride > 1; stride >>= 1) {
      if (localId < stride) {
         buf[localId] += buf[localId + stride];
      }
      barrier(CLK_LOCAL_MEM_FENCE);
   }
   // store
   if (localId == 0) {
      out[groupId] = buf[0] + buf[1];
   }
}

__kernel void sumGroupReductionHandLoop(__global const uint32_t* in, __global uint64_t* out, const uint32_t n) {
   const uint32_t groupId   = get_group_id(0);
   const uint32_t groupSize = get_local_size(0);
   const uint32_t localId   = get_local_id(0);
   const uint32_t globalId  = get_global_id(0);
   const uint32_t globalSize  = get_global_size(0);

   // use static local group memory
   __local uint64_t buf[128];// max group size

   uint64_t sum = 0;
   const uint32_t iterLimit = (n / globalSize); // n is a multiple of globalSize
   for (uint32_t i = 0; i < iterLimit; i++) {
      
      // load
      buf[localId] = in[globalId*i];

      // reduce locally
      barrier(CLK_LOCAL_MEM_FENCE);
      for(uint32_t stride = groupSize/2; stride > 1; stride >>= 1) {
         if (localId < stride) {
            buf[localId] += buf[localId + stride];
         }
         barrier(CLK_LOCAL_MEM_FENCE);
      }
      if (localId == 0) {
         sum += buf[0] + buf[1];
      }
   }
   // store
   if (localId == 0) {
      out[groupId] = sum;
   }
}
