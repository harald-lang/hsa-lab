__kernel void storeValue(__global size_t* dst, const size_t value) {
   	const size_t gid = get_global_id(0);
   	if (gid == 0) {
   		dst[0]=value;
	}
}
