
__kernel void storeArgs(__global size_t* output) {
   const size_t gid=get_global_id(0);
   if (gid == 0) {
      output[0]=gid;
   }
}
