REPO_PATH = repo
ifeq ($(wildcard repo/include/BirdGame.h),)
  $(shell git clone --depth=1 git@github.com:NJU-ProjectN/sdlbird.git $(REPO_PATH))
endif

NAME = bird
SRCS = $(shell find $(REPO_PATH)/src/ -name "*.cpp")
INC_PATH = $(REPO_PATH)/include
LIBS += libminiSDL libndl libSDL_image libfixedptc
include $(NAVY_HOME)/Makefile

install: app install-file
	@echo + INSTALL "->" $(NAME)
	@mkdir -p $(NAVY_HOME)/fsimg/bin
	@cp $(APP) $(NAVY_HOME)/fsimg/bin/$(NAME)

init:;

install-file:
	@mkdir -p $(NAVY_HOME)/fsimg/share/games
	ln -sf -T $(abspath $(REPO_PATH)/res) $(NAVY_HOME)/fsimg/share/games/bird

.PHONY: install-file init
