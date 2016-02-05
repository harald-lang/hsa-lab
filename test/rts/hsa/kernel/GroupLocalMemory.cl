/*
 * This kernel uses the local memory segment.
 * The size of the local memory must be determined 
 * before the kernel is euqueued.
 */
__kernel void groupLocalMemory(
      __global uint* output) {
   
   __local uint localMem[128];
   
   const size_t gid = get_global_id(0);
   const size_t lid = get_local_id(0);

   localMem[lid] = gid;
   barrier(CLK_LOCAL_MEM_FENCE);
   output[gid] = localMem[lid];
   barrier(CLK_LOCAL_MEM_FENCE);
}
