CUR_DIR   := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
include $(CUR_DIR)/../../Makefile.inc

CC := gcc
LD := gcc

MAKE := make

#debug flag (g)
CFLAGS += -Wall -Winline
CFLAGS += -O3 -g
CFLAGS += -I$(INC_DIR)
#CFLAGS += -DTHREAD_PINNING=1

LDFLAGS += -lpthread $(MEMMGR) $(PMEM)
LDFLAGS += -lm

.PHONY: all clean

BINS = benchmark_list_spinlock      \
       benchmark_list_rcu           \
       benchmark_list_timestone	    \
       benchmark_list_harris        \
       benchmark_list_vlist         \
       benchmark_list_swisstm       \
       benchmark_list_move_spinlock \
       benchmark_list_move_vlist    \
       benchmark_list_move_swisstm  \
       benchmark_tree_prcu_eer      \
       benchmark_tree_prcu_d        \
       benchmark_tree_prcu_deer     \
       benchmark_tree_rcu           \
       benchmark_tree_mvrlu         \
       benchmark_tree_citrus_mvrlu  \
       benchmark_tree_vtree         \
       benchmark_tree_bonsai        \
       benchmark_tree_vrbtree       \
       benchmark_tree_swisstm

all: $(BINS)
	@for d in $(BINS); \
                do ( cp $$d $(BIN_DIR); \
                ); \
        done

numa-config.h:
	$(TOOLS_DIR)/cpu-topology.py > $(CUR_DIR)/numa-config.h

benchmark_list.o: benchmark_list.c benchmark_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

benchmark_list_ts.o: benchmark_list.c benchmark_list.h
	$(CC) $(CFLAGS) -DTIMESTAMP -c -o $@ $<

rand.o: zipf/rand.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

zipf.o: zipf/zipf.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

# LIST

list_spinlock.o: list_spinlock.c benchmark_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

list_rcu.o: list_rcu.c benchmark_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

rlu.o: rlu.c rlu.h
	$(CC) $(CFLAGS) -c -o $@ $<

rlu_ordo.o: rlu.c rlu.h
	$(CC) $(CFLAGS) -DRLU_ORDO_TIMESTAMPING -c -o $@ $<

list_rlu.o: list_rlu.c benchmark_list.h rlu.h
	$(CC) $(CFLAGS) -c -o $@ $<

list_mvrlu.o: list_rlu.c benchmark_list.h rlu.h
	$(CC) $(CFLAGS) -DMVRLU -c -o $@ $<

qsbr.o: qsbr.c qsbr.h util.h
	$(CC) $(CFLAGS) -c -o $@ $<

list_harris.o: list_harris.c benchmark_list.h util.h
	$(CC) $(CFLAGS) -c -o $@ $<

list_vlist.o: list_vlist.c benchmark_list.h util.h
	$(CC) $(CFLAGS) -c -o $@ $<

benchmark_list_spinlock: rand.o zipf.o benchmark_list.o list_spinlock.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_rcu: rand.o zipf.o benchmark_list.o list_rcu.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_rlu: rand.o zipf.o benchmark_list.o list_rlu.o rlu.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_rlu_ordo: rand.o zipf.o benchmark_list.o list_rlu.o rlu_ordo.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_timestone: rand.o zipf.o benchmark_list.o list_rlu.o rlu.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_mvrlu_ordo: rand.o zipf.o benchmark_list.o list_mvrlu.o $(LIB_DIR)/libts.a
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_harris: rand.o zipf.o benchmark_list.o list_harris.o qsbr.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_vlist: rand.o zipf.o benchmark_list.o list_vlist.o qsbr.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_swisstm:
	cd swisstm && $(MAKE) target/obj/intset-list
	cp swisstm/target/obj/intset-list $@

# LIST MOVE

benchmark_list_move.o: benchmark_list_move.c benchmark_list_move.h
	$(CC) $(CFLAGS) -c -o $@ $<

list_move_spinlock.o: list_move_spinlock.c benchmark_list_move.h
	$(CC) $(CFLAGS) -c -o $@ $<

list_move_rlu.o: list_move_rlu.c benchmark_list_move.h
	$(CC) $(CFLAGS) -c -o $@ $<

list_move_vlist.o: list_move_vlist.c benchmark_list_move.h
	$(CC) $(CFLAGS) -c -o $@ $<

benchmark_list_move_spinlock: rand.o zipf.o benchmark_list_move.o list_move_spinlock.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_move_rlu: rand.o zipf.o benchmark_list_move.o list_move_rlu.o rlu.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_move_vlist: rand.o zipf.o benchmark_list_move.o list_move_vlist.o qsbr.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_list_move_swisstm: benchmark_list_swisstm
	cd swisstm && $(MAKE) target/obj/intset-list-mv
	cp swisstm/target/obj/intset-list-mv $@

# NON-BALANCED TREE

tree_prcu_eer.o: tree_prcu.c benchmark_list.h
	$(CC) $(CFLAGS) -DPRCU_EER -c -o $@ $<

tree_prcu_d.o: tree_prcu.c benchmark_list.h
	$(CC) $(CFLAGS) -DPRCU_D -c -o $@ $<

tree_prcu_deer.o: tree_prcu.c benchmark_list.h
	$(CC) $(CFLAGS) -DPRCU_DEER -c -o $@ $<

tree_rcu.o: tree_rcu.c benchmark_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

tree_rlu.o: tree_rlu.c benchmark_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

tree_mvrlu.o: tree_rlu.c benchmark_list.h
	$(CC) $(CFLAGS) -DMVRLU -c -o $@ $<

tree_citrus_rlu.o: tree_citrus_rlu.c benchmark_list.h
	$(CC) $(CFLAGS) -DCITRUS -c -o $@ $<

tree_citrus_mvrlu.o: tree_citrus_rlu.c benchmark_list.h
	$(CC) $(CFLAGS) -DMVRLU -DCITRUS -c -o $@ $<

tree_vtree.o: tree_vtree.c benchmark_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

benchmark_tree_prcu_eer: rand.o zipf.o benchmark_list.o tree_prcu_eer.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_prcu_d: rand.o zipf.o benchmark_list.o tree_prcu_d.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_prcu_deer: rand.o zipf.o benchmark_list.o tree_prcu_deer.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_rcu: rand.o zipf.o benchmark_list.o tree_rcu.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_rlu: rand.o zipf.o benchmark_list.o tree_rlu.o rlu.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_rlu_ordo: rand.o zipf.o benchmark_list.o tree_rlu.o rlu_ordo.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_citrus_rlu: rand.o zipf.o benchmark_list.o tree_citrus_rlu.o rlu.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_mvrlu: rand.o zipf.o benchmark_list_ts.o tree_mvrlu.o $(LIB_DIR)/libts.a
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_citrus_mvrlu: rand.o zipf.o benchmark_list.o tree_citrus_mvrlu.o $(LIB_DIR)/libts.a
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_vtree: rand.o zipf.o benchmark_list.o tree_vtree.o qsbr.o
	$(LD) -o $@ $^ $(LDFLAGS)

# BALANCED TREE

tree_bonsai.o: tree_bonsai.c benchmark_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

tree_vrbtree.o: tree_vrbtree.c benchmark_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

benchmark_tree_bonsai: rand.o zipf.o benchmark_list.o tree_bonsai.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_vrbtree: rand.o zipf.o benchmark_list.o tree_vrbtree.o qsbr.o
	$(LD) -o $@ $^ $(LDFLAGS)

benchmark_tree_swisstm: benchmark_list_swisstm
	cd swisstm && $(MAKE) target/obj/intset-tree
	cp swisstm/target/obj/intset-tree $@

clean:
	cd swisstm && $(MAKE) clean
	rm -f $(BINS) *.o
	for d in $(BINS); \
                do ( rm -f $(BIN_DIR)/$$d; \
                ); \
        done
