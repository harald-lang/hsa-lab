__kernel void add(__global size_t* a, const size_t b, const size_t n) {
   	const size_t gid=get_global_id(0);
   	if (gid<n) {
   		a[gid]=a[gid]+b;
	}
}
