EE_BIN = ultima_aula.elf
EE_OBJS = main.o
EE_LIBS = -lgskit -ldmakit -lpad -lm
EE_CFLAGS += -std=gnu99 -O2

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
