include test/rts/hsa/kernel/LocalMakefile.mk

src_test_rts_hsa:= \
	$(src_test_rts_hsa_kernel) \
	test/rts/hsa/TestHsa.cpp \
	test/rts/hsa/TestHsaContext.cpp \
	test/rts/hsa/TestHsaPerformance.cpp