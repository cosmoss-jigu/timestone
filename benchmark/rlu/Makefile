CUR_DIR   := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
include $(CUR_DIR)/../../Makefile.inc

# XXX: need to clean up following
URCUDIR ?= /usr/local

CC := gcc
LD := gcc

CFLAGS += -I$(URCUDIR)/include -I$(INC_DIR)
CFLAGS += -D_REENTRANT
CFLAGS += -Werror
#CFLAGS += --param inline-unit-growth=1000
CFLAGS += -mrtm
#CFLAGS += -DTHREAD_PINNING=1         # test param
#CFLAGS += -DSIZE_CHECK        	      # check for num. node

ifdef DEBUG
	CFLAGS += -O0 -g3
else
	CFLAGS += -DNDEBUG
	CFLAGS += -O1 -g
endif

IS_HAZARD_PTRS_HARRIS = -DIS_HAZARD_PTRS_HARRIS
IS_HARRIS = -DIS_HARRIS
IS_RCU = -DIS_RCU
IS_RLU = -DIS_RLU
IS_VERSION = -DIS_VERSION
IS_MVRLU = -DIS_MVRLU

LDFLAGS += -L$(URCUDIR)/lib
#LDFLAGS += -L$(PROJ_DIR)/lib-nv-jemalloc/lib
LDFLAGS += -lpthread $(MEMMGR) $(PMEM)
LDFLAGS += -lm

BINS = bench-harris bench-hp-harris bench-rcu bench-rlu bench-rlu bench-version bench-timestone

.PHONY:	all clean

all: $(BINS)
	@for d in $(BINS); \
                do ( cp $$d $(BIN_DIR); \
                ); \
        done

numa-config.h:
	$(TOOLS_DIR)/cpu-topology.py > $(CUR_DIR)/numa-config.h

rand.o: zipf/rand.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

zipf.o: zipf/zipf.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

rlu.o: rlu.c rlu.h
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

rlu.o: rlu.c rlu.h
	$(CC) $(CFLAGS) $(DEFINES) -DRLU_ORDO_TIMESTAMPING -c -o $@ $<

new-urcu.o: new-urcu.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

hazard_ptrs.o: hazard_ptrs.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

hash-list.o: hash-list.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

hash-list-version.o: hash-list.c
	$(CC) $(CFLAGS) $(IS_VERSION) $(DEFINES) -c -o $@ $<

hash-list-mvrlu.o: hash-list.c
	$(CC) $(CFLAGS) $(IS_MVRLU) $(DEFINES) -c -o $@ $<

version.o: list_vlist.c qsbr.c
	$(CC) $(CFLAGS) $(IS_VERSION) $(DEFINES) -c -o $@ $<

bench-harris.o: bench.c numa-config.h
	$(CC) $(CFLAGS) $(IS_HARRIS) $(DEFINES) -c -o $@ $<

bench-hp-harris.o: bench.c numa-config.h
	$(CC) $(CFLAGS) $(IS_HAZARD_PTRS_HARRIS) $(DEFINES) -c -o $@ $<

bench-rcu.o: bench.c numa-config.h
	$(CC) $(CFLAGS) $(IS_RCU) $(DEFINES) -c -o $@ $<

bench-mvrlu-gclk.o: bench.c numa-config.h
	$(CC) $(CFLAGS) $(IS_MVRLU) $(IS_RLU) $(DEFINES) -c -o $@ $<

bench-mvrlu.o: bench.c numa-config.h
	$(CC) $(CFLAGS) $(IS_MVRLU) $(IS_RLU) $(DEFINES) -c -o $@ $<

bench-rlu.o: bench.c numa-config.h
	$(CC) $(CFLAGS) $(IS_RLU) $(DEFINES) -c -o $@ $<

bench-rlu.o: bench.c numa-config.h
	$(CC) $(CFLAGS) $(IS_RLU) $(DEFINES) -c -o $@ $<

bench-version.o: bench.c numa-config.h
	$(CC) $(CFLAGS) $(IS_VERSION) $(DEFINES) -c -o $@ $<

bench-harris: rand.o zipf.o new-urcu.o hazard_ptrs.o rlu.o  hash-list.o bench-harris.o
	$(LD) -o $@ $^ $(LDFLAGS)

bench-hp-harris: rand.o zipf.o new-urcu.o hazard_ptrs.o rlu.o hash-list.o bench-hp-harris.o
	$(LD) -o $@ $^ $(LDFLAGS)

bench-rcu: rand.o zipf.o new-urcu.o hazard_ptrs.o rlu.o hash-list.o bench-rcu.o
	$(LD) -o $@ $^ $(LDFLAGS)

bench-mvrlu-gclk: rand.o zipf.o new-urcu.o hazard_ptrs.o hash-list-mvrlu.o bench-mvrlu-gclk.o $(LIB_DIR)/libts-gclk.a
	$(LD) -o $@ $^ $(LDFLAGS)

bench-timestone: rand.o zipf.o new-urcu.o hazard_ptrs.o hash-list-mvrlu.o bench-mvrlu.o $(LIB_DIR)/libts.a
	$(LD) -o $@ $^ $(LDFLAGS)

bench-rlu: rand.o zipf.o new-urcu.o hazard_ptrs.o rlu.o hash-list.o bench-rlu.o
	$(LD) -o $@ $^ $(LDFLAGS)

bench-rlu: rand.o zipf.o new-urcu.o hazard_ptrs.o rlu.o hash-list.o bench-rlu.o
	$(LD) -o $@ $^ $(LDFLAGS)

bench-version: rand.o zipf.o new-urcu.o hazard_ptrs.o qsbr.o rlu.o version.o hash-list-version.o bench-version.o
	$(LD) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(BINS) *.o
	rm -f $(CUR_DIR)/numa-config.h
	@for d in $(BINS); \
                do ( rm -f $(BIN_DIR)/$$d; \
                ); \
        done
