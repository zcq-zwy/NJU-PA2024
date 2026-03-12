#HAS_NAVY = 1
RAMDISK_FILE = build/ramdisk.img

NAME = nanos-lite
SRCS = $(shell find -L ./src/ -name "*.c" -o -name "*.cpp" -o -name "*.S")
include $(AM_HOME)/Makefile

ifeq ($(ARCH),native)
ISA = am_native
SRCS := $(filter-out ./src/ramdisk.c src/ramdisk.c,$(SRCS))
else ifneq ($(findstring -nemu,$(ARCH)),)
SRCS := $(filter-out ./src/ramdisk.c src/ramdisk.c,$(SRCS))
else
INC_PATH += include $(NAVY_HOME)/libs/libc/include
endif

ifeq ($(findstring src/resources.S,$(SRCS)),src/resources.S)
./src/resources.S: $(RAMDISK_FILE)
	@touch $@
endif

ifeq ($(HAS_NAVY),)
files = $(RAMDISK_FILE) src/files.h src/syscall.h
# create an empty file if it does not exist
$(foreach f,$(files),$(if $(wildcard $f),, $(shell touch $f)))
else

ifeq ($(wildcard $(NAVY_HOME)/libs/libos/src/syscall.h),)
  $(error $$NAVY_HOME must be a Navy-apps repo)
endif

update:
	$(MAKE) -s -C $(NAVY_HOME) ISA=$(ISA) VME=$(VME) ramdisk
	@ln -sf $(NAVY_HOME)/build/ramdisk.img $(RAMDISK_FILE)
	@ln -sf $(NAVY_HOME)/build/ramdisk.h src/files.h
	@ln -sf $(NAVY_HOME)/libs/libos/src/syscall.h src/syscall.h

.PHONY: update
endif
