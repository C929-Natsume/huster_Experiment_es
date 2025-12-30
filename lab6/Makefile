EXENAME := lab6
EXESRCS := main.c
EXESRCS += audio_util.c
EXESRCS += audio_record.c

include ../common/rules.mk

# Kitchen game target
KITCHEN_EXE := kitchen_game
KITCHEN_SRCS := kitchen_game.c
KITCHEN_SRCS += audio_util.c
KITCHEN_SRCS += audio_record.c

KITCHEN_EXESRCS := ../common/graphic.c ../common/touch.c ../common/image.c ../common/task.c $(KITCHEN_SRCS)
KITCHEN_EXEOBJS := $(patsubst %.c, %.o, $(KITCHEN_EXESRCS))

$(KITCHEN_EXE): $(KITCHEN_EXEOBJS)
	$(CC) $(LDFLAGS) -o $(KITCHEN_EXE) $(KITCHEN_EXEOBJS) $(LIB)
	mv $(KITCHEN_EXE) ../out/

.PHONY: kitchen_game
kitchen_game: $(KITCHEN_EXE)

# Gomoku game target
GOMOKU_EXE := gomoku_game
GOMOKU_SRCS := gomoku_game.c
GOMOKU_SRCS += audio_util.c
GOMOKU_SRCS += audio_record.c

GOMOKU_EXESRCS := ../common/graphic.c ../common/touch.c ../common/image.c ../common/task.c $(GOMOKU_SRCS)
GOMOKU_EXEOBJS := $(patsubst %.c, %.o, $(GOMOKU_EXESRCS))

$(GOMOKU_EXE): $(GOMOKU_EXEOBJS)
	$(CC) $(LDFLAGS) -o $(GOMOKU_EXE) $(GOMOKU_EXEOBJS) $(LIB)
	mv $(GOMOKU_EXE) ../out/

.PHONY: gomoku_game
gomoku_game: $(GOMOKU_EXE)
