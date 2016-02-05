__kernel void nothing(__global size_t* dst, const size_t value) {
   	const size_t gid = get_global_id(0);
   	if (gid == -1) { // trick the compiler
   		dst[0]=value;
	}
}
