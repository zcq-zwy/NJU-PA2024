FCEUX_PATH = $(NAVY_HOME)/../fceux-am
ifeq ($(wildcard $(FCEUX_PATH)/src/x6502.cpp),)
  
else
  $(shell mkdir -p $(NAVY_HOME)/fsimg/share/games)
  $(shell ln -sf -T $(FCEUX_PATH)/nes/rom $(NAVY_HOME)/fsimg/share/games/nes)
endif

ifeq ($(findstring $(MAKECMDGOALS),clean|clean-all|html),)
libam:
	$(MAKE) -C $(NAVY_HOME)/libs/libam archive
else
libam: ;
endif

default $(MAKECMDGOALS): libam
	AM_HOME=$(NAVY_HOME) LIBS=libam $(MAKE) -C $(FCEUX_PATH) $(MAKECMDGOALS)

.PHONY: libam default $(MAKECMDGOALS)
