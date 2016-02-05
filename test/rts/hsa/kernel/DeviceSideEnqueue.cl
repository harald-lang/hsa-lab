
__kernel void storeGlobalId(__global size_t* output, const size_t n) {
   const size_t gid=get_global_id(0);
   if (gid<n) output[gid]=gid;
}

__kernel void enqueueStoreGlobalId(__global size_t* output, const size_t n) {
   const size_t gid=get_global_id(0);
   if (gid == 0) {
      queue_t queue = get_default_queue();
      ndrange_t nd = ndrange_1D(n,128);
   
      void (^storeGlobalId_block)(void) = ^ {
         storeGlobalId(output,n);
      };

      enqueue_kernel(queue, CLK_ENQUEUE_FLAGS_NO_WAIT, nd, storeGlobalId_block);
   }
}
