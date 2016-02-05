/*
 * A simple kernel which is used for functionality tests only.
 */
__kernel void storeGlobalId(__global size_t* output, const size_t n) {
   const size_t gid=get_global_id(0);
   if (gid<n) output[gid]=gid;
}

size_t test() {
	return get_global_id(0);
}

__kernel void storeGlobalId2(__global size_t* output, const size_t n) {
   const size_t gid=test();
   if (gid<n) output[gid]=gid;
}
