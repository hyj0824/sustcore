component-kind := module
component-name := test-linux
module-output := test-linux.mod
module-libc := placeholder-libc
module-libraries :=
module-linker-script := riscv64.ld
variant.loongarch64.script-ld := $(component-root)/loongarch64.ld

flags-ld := $(flags-module-ld) $(flags-common-ld) $(flags-mode-ld)

flags-c := $(flags-common-c) -nostdinc++ $(flags-mode-c)
include-c := -I$(path-include) -I$(path-include)/std \
	-I$(path-third_party)/include -I$(path-third_party)/include/libfdt \
	-I$(path-third_party)/include/std -I$(component-root) -I$(path-include)/arch
defs-c := -DASSERT_IMPLEMENTED=0 $(defs-mode-c)

flags-cpp := $(flags-common-cpp) -nostdinc $(flags-no-rtti-cpp) $(flags-no-exceptions-cpp) \
	$(flags-mode-cpp) -DUSE_SUSTCORE_FEATURES
include-cpp := -I$(path-include) -I$(path-include)/std -I$(path-include)/std/c++ \
	-I$(path-third_party)/include -I$(path-third_party)/include/libfdt \
	-I$(path-third_party)/include/std -I$(component-root) -I$(path-include)/arch
defs-cpp := -DASSERT_IMPLEMENTED=0 $(defs-mode-cpp)

# ifeq ($(architecture),loongarch64)
# flags-cpp += -mno-lsx
# endif
