CROSS_COMPILE = riscv64-linux-gnu-
LNK_ADDR = $(if $(VME), 0x40000000, 0x83000000)
ifeq ($(PIE),1)
CFLAGS  += -fpie -march=rv64g -mcmodel=medany
LDFLAGS += --no-relax -pie --no-dynamic-linker
else
CFLAGS  += -fno-pic -march=rv64g -mcmodel=medany
LDFLAGS += --no-relax -Ttext-segment $(LNK_ADDR)
endif
