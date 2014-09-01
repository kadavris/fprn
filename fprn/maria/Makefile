PATH1="."

LIBS_PATH=../../libs
LIBS_H=$(LIBS_PATH)/ap_log.h $(LIBS_PATH)/ap_str.h $(LIBS_PATH)/ap_tcp.h $(LIBS_PATH)/b64.h

cc=gcc
OPTS ?= -Wall -mtune=pentium3 -m32

c_files=maria301.c
deps=$(c_files) $(LIBS_H) maria301.h maria301_errors.h
obj_files=$(c_files:.c=.o)
outfile=maria301.o

all: $(outfile)

$(outfile): $(deps)
	$(cc) -c $(OPTS) $(c_files) -o $(outfile)

clean:
	rm -f *.o
