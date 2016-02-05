
# Include config.local
include ReadConfig.mk

ifeq ($(PREFIX),)
 ifeq ($(DEBUG),)
  PREFIX:=bin/
 else
  PREFIX:=bin/debug/
 endif
else
 FORCEPREFIX:=$(PREFIX)
endif

MARCH?=-mtune=corei7 -march=corei7

#CXX=clang++-3.5
CXX=g++
CXXFLAGSOPTIMIZE?=-O3 $(MARCH) -DNDEBUG
CXXFLAGSDEBUG?=-O0 $(MARCH) -DDEBUG
ifeq ($(DEBUG),)
 CXXFLAGS?=$(CXXFLAGSOPTIMIZE)
else
 CXXFLAGS?=$(CXXFLAGSDEBUG)
endif
ifeq ($(ALLOWWARNINGS),)
 WERROR:=-Werror
endif
#WARNFLAGS?=-Wall -Wextra -Woverloaded-virtual $(WERROR)

# Where to find the user code.
SRC_DIR=src

CXXDEBUGFLAGS:=-g
CXXFLAGS:=-std=c++11 $(CXXDEBUGFLAGS) $(CXXFLAGS) $(WARNFLAGS) -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS
IFLAGS:=-I. -I$(SRC_DIR) -I$(PREFIX) -I$(GTEST_INCLUDE_PATH)

# Where to put the binaries.
BIN_DIR=$(PREFIX)

defaulttargets: executables $(PREFIX)tester

#############################################################################
# Heterogeneous System Architecture (HSA)
ifneq "$(MAKECMDGOALS)" "clean"

$(info [HSA] Checking for hardware support ...)
HSA_DIR:=scripts/hsa
HSA_CHECK_INSTALLATION_SCRIPT:=$(HSA_DIR)/kfd_check_installation.sh
$(eval HSA_CHECK_OUTPUT="$(shell $(HSA_CHECK_INSTALLATION_SCRIPT))")
$(eval CAN_RUN_HSA="$(shell echo $(HSA_CHECK_OUTPUT) | tail -1 | sed -e 's/^.*\.//')")
ifeq ($(CAN_RUN_HSA),"YES")
 $(info [HSA] System can run HSA kernels.)
 HAVE_HSA_METAL?=1
else
 $(info [HSA] FAILED: HSA not supported. If you think this is an error, please run '$(HSA_DIR)/kfd_check_installation.sh' for diagnosis.)
 HAVE_HSA_METAL?=0
endif

INCLUDE-hsa:=-I$(HSA_RUNTIME_DIR)/include -I$(HSA_RUNTIME_SRC_DIR) -I$(HSA_KMT_INC_DIR)
LIBFILE-hsa:=-Lhsakmt -L"$(HSA_RUNTIME_DIR)/lib" -lhsa-runtime64
IFLAGS+=$(INCLUDE-hsa)

endif
#############################################################################


LIBFILE-pthread:=-lpthread

LIBS:=pthread hsa

checkdir=@mkdir -p $(dir $@)
#buildexe=$(BUILDEXEPREFIX)$(CXX) -o $@ $(filter-out -gsplit-dwarf,$(CXXFLAGS)) $(filter %.o,$^) $(foreach file,$(LIBS-$(patsubst $(PREFIX)%,%,$@)),$(LIBFILE-$(file))) $(BUILDEXE-symbols-$(words $(findstring $(VERSIONSCRIPT),$^))) $(LDFLAGS)
buildexe=$(BUILDEXEPREFIX)$(CXX) -o $@ $(filter-out -gsplit-dwarf,$(CXXFLAGS)) $(filter %.o,$^) $(foreach file,$(LIBS),$(LIBFILE-$(file))) $(BUILDEXE-symbols-$(words $(findstring $(VERSIONSCRIPT),$^))) $(LDFLAGS)
createrunscript=@echo "\#!/bin/bash\nenv $(HSA_ENV) $@ \"\$$@\"" > $(PREFIX)$(notdir $@).sh; chmod +x $(PREFIX)$(notdir $@).sh

all: defaulttargets

.PHONY: clear executables run_tests run_simd_tests bin/experiments/memlatency

clean:
	rm -fr $(BIN_DIR)*
	rm -f brig_tmp_*
	rm -f hsail_tmp_*

include src/LocalMakefile.mk
include test/LocalMakefile.mk
#DISABLED#include 3rdparty/LocalMakefile.mk

src_hsa=$(src_3rdparty) $(src_hsa_kmt) $(src_hsa_runtime) $(src)
#src_hsa_test=$(src_hsa_kmt) $(src_hsa_runtime) $(src) $(src_test)
src_hsa_test=$(src_3rdparty) $(src_hsa_runtime) $(src) $(src_test)

substext_hsa=$(patsubst %.hsail,%.brig, \
			 $(patsubst %.cl,%.brig, \
			 $(patsubst %.c,%.o, \
             $(patsubst %.cpp,%.o, $(src_hsa)))))

substext_hsa_test=$(patsubst %.hsail,%.brig, \
				  $(patsubst %.cl,%.brig, \
				  $(patsubst %.c,%.o, \
                  $(patsubst %.cpp,%.o, $(src_hsa_test)))))

#exe_src=src/experiments/q1/q1.cpp
exe_obj= $(patsubst %.cpp,%.o, $(exe_src))
exe_bin= $(patsubst %.cpp,%, $(exe_src))

executables: $(addprefix $(PREFIX),$(exe_bin))

#############################################################################
# tester: Unit tests
$(PREFIX)tester: $(addprefix $(PREFIX),$(substext_hsa_test))
	@echo $+
	$(buildexe)
	$(createrunscript)
#############################################################################

compile=$(CXX) -o $@ -c $(strip $(CXXFLAGS) $(CXXFLAGS-$(dir $<)) $(CXXFLAGS-$<) $(IFLAGS)) $<

$(PREFIX)%: $(PREFIX)%.o $(addprefix $(PREFIX),$(substext_hsa)) #$(addprefix $(PREFIX),$(exe_obj))
	$(buildexe)
	$(createrunscript)

$(PREFIX)%.o: %.cpp
	$(checkdir)
	$(compile)

$(PREFIX)%.o: $(PREFIX)%.cpp
	$(checkdir)
	$(compile)

$(PREFIX)%.o: %.c
	$(checkdir)
	$(compile-c)

# OpenCL Offline Compiler (CLOC)
#CLOCFLAGS?=-opt 0

compile_kernel_hsail=cloc $(CLOCFLAGS) -q -o $@ -hsail $<
compile_kernel_brig=cloc $(CLOCFLAGS) -q -o $@ $<
compile_hsail_to_brig=HSAILasm -o $@ $<

$(PREFIX)%.hsail: %.cl
	$(checkdir)
	$(compile_kernel_hsail)

$(PREFIX)%.hsail: $(PREFIX)%.cl
	$(checkdir)
	$(compile_kernel_hsail)

$(PREFIX)%.brig: %.cl
	$(checkdir)
	$(compile_kernel_brig)

$(PREFIX)%.brig: $(PREFIX)%.cl
	$(checkdir)
	$(compile_kernel_brig)

$(PREFIX)%.brig: %.hsail
	$(checkdir)
	$(compile_hsail_to_brig)

$(PREFIX)%.brig: $(PREFIX)%.hsail
	$(checkdir)
	$(compile_hsail_to_brig)

#$(PREFIX)%.hsail:
#	cp $(@:$(PREFIX)%=%) $@

bin/experiments/memlatency:
	@mkdir -p bin/experiments
	@rm -f bin/experiments/memlatency
	g++ -std=c++11 -O3 -g -o bin/experiments/memlatency src/experiments/memlatency.cpp -lpthread
