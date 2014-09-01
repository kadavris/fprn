PATH1="."

LIBS_PATH=../../libs
LIBS_H=$(LIBS_PATH)/ap_log.h $(LIBS_PATH)/ap_str.h $(LIBS_PATH)/ap_tcp.h $(LIBS_PATH)/b64.h

cc=gcc
#OPTS ?= -Wall -mtune=pentium3 -m32

OUTFILE ?= shtrih_ltfrk.o
c_files=shtrih_ltfrk.c
deps=$(c_files) $(LIBS_H) shtrih_ltfrk.h shtrih_errors.h shtrih_answer_timeouts.h shtrih_flags.h shtrih_ltfrk_get_state.c shtrih_ltfrk_init.c

all: $(OUTFILE)

$(OUTFILE): $(deps)
	$(cc) -c $(OPTS) $(c_files) -o $(OUTFILE)

clean:
	rm -f *.o
