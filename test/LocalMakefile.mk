include test/rts/hsa/LocalMakefile.mk

src_test:=$(src_test_rts_hsa) $(src_test_hsa) $(src_test_platform) $(src_test_vectorized) test/main.cpp test/gtest/gtest-all.cpp
