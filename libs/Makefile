PATH1="."

cc=gcc
OPTS ?= -Wall
# -mtune=pentium3 -m32

OBJDIR ?= .
obj=$(OBJDIR)/b64.o $(OBJDIR)/ap_log.o $(OBJDIR)/ap_str.o $(OBJDIR)/ap_utils.o $(OBJDIR)/ap_tcp.o

all: $(obj)

$(OBJDIR)/b64.o: b64.c
	$(cc) -c $(OPTS) b64.c -o $(OBJDIR)/b64.o

$(OBJDIR)/ap_log.o: ap_log.c ap_tcp.o
	$(cc) -c $(OPTS) ap_log.c -o $(OBJDIR)/ap_log.o

$(OBJDIR)/ap_str.o: ap_str.c
	$(cc) -c $(OPTS) ap_str.c -o $(OBJDIR)/ap_str.o

$(OBJDIR)/ap_tcp.o: ap_tcp.c
	$(cc) -c $(OPTS) ap_tcp.c -o $(OBJDIR)/ap_tcp.o

$(OBJDIR)/ap_utils.o: ap_utils.c
	$(cc) -c $(OPTS) ap_utils.c -o $(OBJDIR)/ap_utils.o

clean:
	rm -f $(obj)
