PATH1="."
LIBS_PATH=../libs

LIBS_H=$(LIBS_PATH)/ap_log.h $(LIBS_PATH)/ap_str.h $(LIBS_PATH)/ap_tcp.h $(LIBS_PATH)/b64.h
LIBS_O=$(LIBS_H:.h=.o)

CC=gcc
OPTSCOMMON=-Wall
#OPTSCOMMON=-Wall -mtune=pentium3 -m32
OPTSRELEASE=-ggdb

obj_for_driver_SHTRIH_LTFRK=shtrih/shtrih_ltfrk.o
obj_for_driver_MARIA301=maria/maria301.o

ifndef DRIVERS
#$(patsubst A,B,$(VAR))
#DRIVERS=MARIA301 SHTRIH_LTFRK
DRIVERS=SHTRIH_LTFRK
endif

DRIVERS_O=$(foreach dr,$(DRIVERS),$(obj_for_driver_$(dr)))
DRIVERS_DEF=$(foreach dr,$(DRIVERS),-DDRIVER_$(dr))

DEPLIST=fprn.o fprnconfig.o tcpanswer.o printers_common.o versioning.o $(DRIVERS_O)

all:  release

libs:
	make -C$(LIBS_PATH) -e OPTS="$(OPTS)"

#$(DRIVERS_O): make -C

release: OPTS=$(OPTSCOMMON) $(OPTSRELEASE) $(CLIENTDEFS)
release: libs versioning $(DEPLIST) $(LIBS_O)
	$(CC) $(OPTS) $(DRIVERS_DEF) -lm -o fprn $(DEPLIST) $(LIBS_O)
	strip fprn

fprn.o: fprn.c fprnconfig.h $(LIBS_H)
	$(CC) -c $(OPTS) $(DRIVERS_DEF) fprn.c

fprnconfig.o: fprnconfig.c fprnconfig.h $(LIBS_H)
	$(CC) -c $(OPTS) $(DRIVERS_DEF) fprnconfig.c

tcpanswer.o: tcpanswer.c fprnconfig.h $(LIBS_H)
	$(CC) -c $(OPTS) tcpanswer.c

printers_common.o: printers_common.c fprnconfig.h
	$(CC) -c $(OPTS) printers_common.c

shtrih_ltfrk.o: shtrih_ltfrk.c fprnconfig.h printers_common.c
	$(CC) -c $(OPTS) shtrih_ltfrk.c

maria301.o: maria301.c fprnconfig.h printers_common.c
	$(CC) -c $(OPTS) maria301.c

versioning:
	/bin/sh ./versioning.sh "$(OPTS)"
	$(CC) -c $(OPTS) versioning.c

clean:
	rm -f fprn
	rm -f *.o
