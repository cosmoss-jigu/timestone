#include "array.hpp"
#include "btree.hpp"
#include "ckptlog.h"
#include "list.hpp"
#include "nvlog.h"
#include "nvm.h"
#include "oplog.h"
#include "recovery.h"
#include "timestone.h"
#include "util.h"
#include <condition_variable>
#include <gtest/gtest.h>
#include <libpmemobj.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

#define MAX_LOOP 10000
#define MAX_OBJ_SIZE 512
#define KEYS 10000000
#define MISS_BUFF 10000
#define N_THREADS 64
//#define BENCH_INS
//#define PRINT
#define DISABLE

cpu_set_t cpu_set[N_THREADS];
std::mutex mtx;
std::condition_variable cv;
bool start_run = false;
int end_counter;

struct ts_op {
  unsigned long a;
  unsigned int b;
  char c;
};

TEST(nvlogtest, log_ops) {
  struct ts_op operation;
  ts_nvm_root_obj_t *nvm_root_obj;
  ts_nvlog_entry_hdr_t *nvl_entry_hdr;
  ts_nvlog_t nvlog;
  unsigned long enq_num, deq_num, i;
  const unsigned short type = TYPE_OPLOG;
  int need_recovery;

  ASSERT_NE((nvm_root_obj = nvm_init_heap(NVHEAP_POOL_PATH, NVHEAP_POOL_SIZE,
                                          &need_recovery)),
            nullptr)
      << "nvm_init_heap failed\n";
  ASSERT_NE(nvlog_init(nvm_root_obj), 0) << "init_nvlog_init failed\n";
  ASSERT_EQ(
      nvlog_create(NULL, &nvlog, TS_OPLOG_SIZE, type, STATUS_NVLOG_NORMAL), 0)
      << "nvlog_create failed\n";
  operation.a = 0;
  operation.b = 0;
  operation.c = 0;

  for (i = 0; i < MAX_LOOP; i++) {
    enq_num = deq_num = 0;
    while ((nvl_entry_hdr = nvlog_enq(&nvlog, sizeof(operation)))) {
      nvl_entry_hdr->wrt_clk = enq_num;
      memcpy((void *)&nvl_entry_hdr->obj, (void *)&operation,
             sizeof(operation));
      enq_num++;
    }
    nvlog_enq_persist(&nvlog);
    while ((nvl_entry_hdr = nvlog_deq(&nvlog))) {
      deq_num++;
    }
    ASSERT_EQ(enq_num, deq_num);
    nvlog_deq_persist(&nvlog);
  }

  nvlog_destroy(&nvlog);
  ASSERT_EQ(i, MAX_LOOP) << " nvlog variable size test failed\n";
  nvm_heap_destroy();
}

TEST(nvlogtest, variable_log_size) {
  unsigned long i;
  struct ts_op operation;
  ts_nvm_root_obj_t *nvm_root_obj;
  ts_nvlog_entry_hdr_t *nvl_entry_hdr;
  ts_nvlog_t nvlog;
  unsigned long enq_num, deq_num;
  const unsigned short type = TYPE_OPLOG;
  unsigned long nvlog_size = 0;
  int need_recovery;

  for (i = 3; i < 19; i++) {
    nvlog_size = 1 << i;
    ASSERT_NE((nvm_root_obj = nvm_init_heap(NVHEAP_POOL_PATH, NVHEAP_POOL_SIZE,
                                            &need_recovery)),
              nullptr)
        << "nvm_init_heap failed\n";
    ASSERT_NE(nvlog_init(nvm_root_obj), 0) << "init_nvlog_init failed\n";
    ASSERT_EQ(nvlog_create(NULL, &nvlog, nvlog_size, type, STATUS_NVLOG_NORMAL),
              0)
        << "nvlog_create failed\n";
    operation.a = 0;
    operation.b = 0;
    operation.c = 0;
    for (i = 0; i < MAX_LOOP; i++) {
      enq_num = deq_num = 0;
      while ((nvl_entry_hdr = nvlog_enq(&nvlog, sizeof(operation)))) {
        nvl_entry_hdr->wrt_clk = enq_num;
        memcpy((void *)&nvl_entry_hdr->obj, (void *)&operation,
               sizeof(operation));
        enq_num++;
      }
      nvlog_enq_persist(&nvlog);
      while ((nvl_entry_hdr = nvlog_deq(&nvlog))) {
        deq_num++;
      }
      ASSERT_EQ(enq_num, deq_num);
      nvlog_deq_persist(&nvlog);
    }

    nvlog_destroy(&nvlog);
    ASSERT_EQ(i, MAX_LOOP) << " nvlog variable size test failed\n";
    nvm_heap_destroy();
  }
}

TEST(nvlogtest, variable_obj_size) {
  char obj[MAX_OBJ_SIZE] = {
      0,
  };
  ts_nvm_root_obj_t *nvm_root_obj;
  ts_nvlog_entry_hdr_t *nvl_entry_hdr;
  ts_nvlog_t nvlog;
  unsigned long enq_num, deq_num, i;
  int obj_size, seed;
  const unsigned short type = TYPE_OPLOG;
  int need_recovery;

  seed = time(NULL);
  printf("time seed:%u \n", seed);
  srand(seed);
  ASSERT_NE((nvm_root_obj = nvm_init_heap(NVHEAP_POOL_PATH, NVHEAP_POOL_SIZE,
                                          &need_recovery)),
            nullptr)
      << "nvm_init_heap failed\n";
  ASSERT_NE(nvlog_init(nvm_root_obj), 0) << "init_nvlog_init failed\n";
  ASSERT_EQ(
      nvlog_create(NULL, &nvlog, TS_OPLOG_SIZE, type, STATUS_NVLOG_NORMAL), 0)
      << "nvlog_create failed\n";

  for (i = 0; i < MAX_LOOP; i++) {
    obj_size = rand() % MAX_OBJ_SIZE;
    enq_num = deq_num = 0;
    while ((nvl_entry_hdr = nvlog_enq(&nvlog, obj_size))) {
      nvl_entry_hdr->wrt_clk = enq_num;

      memcpy((void *)&nvl_entry_hdr->obj, (void *)obj, obj_size);
      enq_num++;
    }
    nvlog_enq_persist(&nvlog);

    while ((nvl_entry_hdr = nvlog_deq(&nvlog))) {
      deq_num++;
    }
    ASSERT_EQ(enq_num, deq_num);
    nvlog_deq_persist(&nvlog);
  }

  nvlog_destroy(&nvlog);
  ASSERT_EQ(i, MAX_LOOP) << " nvlog variable size test failed\n";
  nvm_heap_destroy();
}

TEST(oplog_test, test_enq_deq) {
  char op[MAX_OBJ_SIZE] = {
      0,
  };
  ts_nvm_root_obj_t *nvm_root_obj;
  ts_op_entry_t *op_entry;
  ts_oplog_t oplog;
  unsigned long enq_num, deq_num, i;
  int op_size, seed;
  int need_recovery;
  ts_op_info_t op_info = {.curr = 0,
                          .op_entry = {
                              .op_type = 0,
                          }};

  seed = time(NULL);
  printf("time seed:%u \n", seed);
  srand(seed);

  ASSERT_NE((nvm_root_obj = nvm_init_heap(NVHEAP_POOL_PATH, NVHEAP_POOL_SIZE,
                                          &need_recovery)),
            nullptr)
      << "nvm_init_heap failed\n";
  ASSERT_NE(nvlog_init(nvm_root_obj), 0) << "init_nvlog_init failed\n";
  ASSERT_EQ(oplog_create(NULL, &oplog, TS_OPLOG_SIZE, STATUS_NVLOG_NORMAL), 0)
      << "oplog_create failed\n";
  for (i = 0; i < MAX_LOOP; i++) {
    op_size = rand() % MAX_OBJ_SIZE;
    enq_num = deq_num = 0;
    while ((op_entry = oplog_enq(&oplog, enq_num, deq_num, &op_info))) {
      memcpy((void *)&op_entry->oplog_hdr.opd, (void *)op, op_size);
      enq_num++;
    }
    oplog_enq_persist(&oplog);
    while ((op_entry = oplog_deq(&oplog))) {
      deq_num++;
    }
    ASSERT_EQ(enq_num, deq_num);
    oplog_deq_persist(&oplog);
  }

  oplog_destroy(&oplog);
  ASSERT_EQ(i, MAX_LOOP) << " oplog test failed\n";
  nvm_heap_destroy();
}

TEST(ckptlog_test, test_enq_deq) {
  ts_nvm_root_obj_t *nvm_root_obj;
  ts_ckpt_entry_t *ckpt_entry;
  ts_cpy_hdr_struct_t *chs = NULL;
  ts_ckptlog_t ckptlog;
  ts_act_hdr_struct_t *ahs;
  unsigned long seq_num, i;
  int seed;
  int need_recovery;

  seed = time(NULL);
  srand(seed);

  ASSERT_NE((nvm_root_obj = nvm_init_heap(NVHEAP_POOL_PATH, NVHEAP_POOL_SIZE,
                                          &need_recovery)),
            nullptr)
      << "nvm_init_heap failed\n";
  ASSERT_NE(nvlog_init(nvm_root_obj), 0) << "init_nvlog_init failed\n";

  /* allocate master object*/
  ahs = (ts_act_hdr_struct_t *)nvm_alloc(sizeof(*ahs));
  ASSERT_NE(ahs, nullptr) << "NULL object header\n";

  /* allocate and initialize a cpy object*/
  chs = (ts_cpy_hdr_struct_t *)malloc(sizeof(ts_cpy_hdr_struct_t));
  ASSERT_NE(chs, nullptr) << "NULL copy header\n";
  chs->obj_hdr.type = TYPE_COPY;
  chs->obj_hdr.obj_size = MAX_OBJ_SIZE;
  chs->cpy_hdr.p_act_vhdr = (ts_act_vhdr_t *)malloc(sizeof(ts_act_vhdr_t));
  chs->cpy_hdr.p_act_vhdr->np_org_act = ahs->obj_hdr.obj;

  ASSERT_EQ(
      ckptlog_create(NULL, &ckptlog, TS_CKPTLOG_SIZE, STATUS_NVLOG_NORMAL), 0)
      << "ckptlog_create failed\n";
  ckptlog.free_set = NULL;

  for (i = 0; i < MAX_LOOP; i++) {
    seq_num = 0;
    while ((ckpt_entry = ckptlog_enq(&ckptlog, seq_num, seq_num + 1, chs))) {
      seq_num++;
    }
    ckptlog_enq_persist(&ckptlog);
    while ((ckpt_entry = ckptlog_deq(&ckptlog))) {
    }
    ckptlog_deq_persist(&ckptlog);
  }

  ckptlog_destroy(&ckptlog);
  ASSERT_EQ(i, MAX_LOOP) << "CKPTlog test failed\n";
  free(chs);
  nvm_free((void *)ahs);
  nvm_heap_destroy();
}

int test_op_exec(ts_thread_struct_t *self, unsigned long op_type,
                 unsigned char *opd) {
  return 0;
}

TEST(oplog_test, recovery) {
  // time:  1 2 3 4 5 6 7 8
  // Tx1:   <---------> | |
  // Tx2:   | <----------->
  // Tx3:   | | <---> | | |
  // Tx4:   | | | <-----> |
  ts_nvm_root_obj_t *nvm_root_obj;
  ts_recovery_t recovery;
  ts_oplog_t oplog1;
  ts_oplog_t oplog2;
  ts_oplog_t oplog3;
  ts_oplog_t oplog4;
  PMEMoid root_obj;
  PMEMobjpool *pop;
  void *addr;
  ts_nvm_root_obj_t *root;
  int need_recovery;
  ts_op_info_t op_info = {.curr = 0,
                          .op_entry = {
                              .op_type = 0,
                          }};

  ASSERT_NE((nvm_root_obj = nvm_init_heap(NVHEAP_POOL_PATH, NVHEAP_POOL_SIZE,
                                          &need_recovery)),
            nullptr)
      << "nvm_init_heap failed\n";
  ASSERT_NE(nvlog_init(nvm_root_obj), 0) << "init_nvlog_init failed\n";
  ASSERT_EQ(oplog_create(NULL, &oplog1, TS_OPLOG_SIZE, STATUS_NVLOG_NORMAL), 0)
      << "oplog1_create failed\n";
  ASSERT_EQ(oplog_create(NULL, &oplog2, TS_OPLOG_SIZE, STATUS_NVLOG_NORMAL), 0)
      << "oplog2_create failed\n";
  ASSERT_EQ(oplog_create(NULL, &oplog3, TS_OPLOG_SIZE, STATUS_NVLOG_NORMAL), 0)
      << "oplog3_create failed\n";
  ASSERT_EQ(oplog_create(NULL, &oplog4, TS_OPLOG_SIZE, STATUS_NVLOG_NORMAL), 0)
      << "oplog4_create failed\n";
  oplog_enq(&oplog1, 1, 6, &op_info);
  oplog_enq_persist(&oplog1);
  oplog_enq(&oplog2, 2, 8, &op_info);
  oplog_enq_persist(&oplog2);
  oplog_enq(&oplog3, 3, 5, &op_info);
  oplog_enq_persist(&oplog3);
  oplog_enq(&oplog4, 4, 7, &op_info);
  oplog_enq_persist(&oplog4);
  root = (ts_nvm_root_obj_t *)pmemobj_direct(root_obj);
  ASSERT_NE(root->next, nullptr) << "NVLOG pool empty \n";

  recovery.root = nvm_root_obj;
  recovery.op_exec = test_op_exec;
  ASSERT_EQ(oplog_recovery(&recovery), 0);
  nvm_heap_destroy();
}

TEST(ckptlog_test, recovery) {
  ts_nvm_root_obj_t *nvm_root_obj;
  ts_recovery_t recovery;
  PMEMoid root_obj;
  PMEMobjpool *pop;
  void *log_pool_addr;
  ts_nvm_root_obj_t *root;
  ts_ckptlog_t ckptlog;
  ts_ckptlog_t ckptlog2;
  ts_act_hdr_struct_t *ahs1;
  ts_act_hdr_struct_t *ahs2;
  ts_act_hdr_struct_t *ahs3;
  ts_cpy_hdr_struct_t *chs1;
  ts_cpy_hdr_struct_t *chs2;
  ts_cpy_hdr_struct_t *chs3;
  ts_cpy_hdr_struct_t *chs4;
  ts_cpy_hdr_struct_t *chs5;
  ts_cpy_hdr_struct_t *chs6;
  ts_ckpt_entry_t *ckpt_entry;
  int need_recovery;

  ASSERT_NE((nvm_root_obj = nvm_init_heap(NVHEAP_POOL_PATH, NVHEAP_POOL_SIZE,
                                          &need_recovery)),
            nullptr)
      << "nvm_init_heap failed\n";
  ASSERT_NE(nvlog_init(nvm_root_obj), 0) << "init_nvlog_init failed\n";

  /* allocate master object*/
  ahs1 = (ts_act_hdr_struct_t *)nvm_alloc(sizeof(*ahs1));
  ASSERT_NE(ahs1, nullptr) << "NULL object header\n";
  ahs1->act_nvhdr.p_act_vhdr = NULL;

  ahs2 = (ts_act_hdr_struct_t *)nvm_alloc(MAX_OBJ_SIZE + sizeof(*ahs2));
  ASSERT_NE(ahs2, nullptr) << "NULL object header\n";
  ahs2->act_nvhdr.p_act_vhdr = NULL;

  ahs3 = (ts_act_hdr_struct_t *)nvm_alloc(MAX_OBJ_SIZE + sizeof(*ahs3));
  ASSERT_NE(ahs3, nullptr) << "NULL object header\n";
  ahs3->act_nvhdr.p_act_vhdr = NULL;

  /* allocate 2 copy object for each master with different wrt_clk*/
  chs1 = (ts_cpy_hdr_struct_t *)malloc(sizeof(ts_cpy_hdr_struct_t));
  ASSERT_NE(chs1, nullptr) << "NULL copy header\n";
  chs1->obj_hdr.type = TYPE_COPY;
  chs1->obj_hdr.obj_size = MAX_OBJ_SIZE;
  chs1->cpy_hdr.p_act_vhdr = (ts_act_vhdr_t *)malloc(sizeof(ts_act_vhdr_t));
  chs1->cpy_hdr.p_act_vhdr->np_org_act = ahs1->obj_hdr.obj;

  chs2 = (ts_cpy_hdr_struct_t *)malloc(sizeof(ts_cpy_hdr_struct_t));
  ASSERT_NE(chs2, nullptr) << "NULL copy header\n";
  chs2->obj_hdr.type = TYPE_COPY;
  chs2->obj_hdr.obj_size = MAX_OBJ_SIZE;
  chs2->cpy_hdr.p_act_vhdr = (ts_act_vhdr_t *)malloc(sizeof(ts_act_vhdr_t));
  chs2->cpy_hdr.p_act_vhdr->np_org_act = ahs1->obj_hdr.obj;

  chs3 = (ts_cpy_hdr_struct_t *)malloc(sizeof(ts_cpy_hdr_struct_t));
  ASSERT_NE(chs3, nullptr) << "NULL copy header\n";
  chs3->obj_hdr.type = TYPE_COPY;
  chs3->obj_hdr.obj_size = MAX_OBJ_SIZE;
  chs3->cpy_hdr.p_act_vhdr = (ts_act_vhdr_t *)malloc(sizeof(ts_act_vhdr_t));
  chs3->cpy_hdr.p_act_vhdr->np_org_act = ahs2->obj_hdr.obj;

  chs4 = (ts_cpy_hdr_struct_t *)malloc(sizeof(ts_cpy_hdr_struct_t));
  ASSERT_NE(chs4, nullptr) << "NULL copy header\n";
  chs4->obj_hdr.type = TYPE_COPY;
  chs4->obj_hdr.obj_size = MAX_OBJ_SIZE;
  chs4->cpy_hdr.p_act_vhdr = (ts_act_vhdr_t *)malloc(sizeof(ts_act_vhdr_t));
  chs4->cpy_hdr.p_act_vhdr->np_org_act = ahs2->obj_hdr.obj;

  chs5 = (ts_cpy_hdr_struct_t *)malloc(sizeof(ts_cpy_hdr_struct_t));
  ASSERT_NE(chs5, nullptr) << "NULL copy header\n";
  chs5->obj_hdr.type = TYPE_COPY;
  chs5->obj_hdr.obj_size = MAX_OBJ_SIZE;
  chs5->cpy_hdr.p_act_vhdr = (ts_act_vhdr_t *)malloc(sizeof(ts_act_vhdr_t));
  chs5->cpy_hdr.p_act_vhdr->np_org_act = ahs3->obj_hdr.obj;

  chs6 = (ts_cpy_hdr_struct_t *)malloc(sizeof(ts_cpy_hdr_struct_t));
  ASSERT_NE(chs6, nullptr) << "NULL copy header\n";
  chs6->obj_hdr.type = TYPE_COPY;
  chs6->obj_hdr.obj_size = MAX_OBJ_SIZE;
  chs6->cpy_hdr.p_act_vhdr = (ts_act_vhdr_t *)malloc(sizeof(ts_act_vhdr_t));
  chs6->cpy_hdr.p_act_vhdr->np_org_act = ahs3->obj_hdr.obj;

  /* create ckptlog*/
  ASSERT_EQ(
      ckptlog_create(NULL, &ckptlog2, TS_CKPTLOG_SIZE, STATUS_NVLOG_NORMAL), 0)
      << "ckptlog_create failed\n";
  ASSERT_EQ(
      ckptlog_create(NULL, &ckptlog, TS_CKPTLOG_SIZE, STATUS_NVLOG_NORMAL), 0)
      << "ckptlog_create failed\n";
  root = (ts_nvm_root_obj_t *)pmemobj_direct(root_obj);
  ASSERT_NE(root->next, nullptr) << "NVLOG pool empty \n";

  /* ckptlog_enq*/
  ckpt_entry = ckptlog_enq(&ckptlog, 1, 0, chs1);
  ASSERT_NE(ckpt_entry, nullptr) << "ckpt_enq failed \n";
  ckptlog_enq_persist(&ckptlog);
  ckpt_entry = ckptlog_enq(&ckptlog, 2, 0, chs2);
  ASSERT_NE(ckpt_entry, nullptr) << "ckpt_enq failed \n";
  ckptlog_enq_persist(&ckptlog);
  ckpt_entry = ckptlog_enq(&ckptlog, 4, 0, chs3);
  ASSERT_NE(ckpt_entry, nullptr) << "ckpt_enq failed \n";
  ckptlog_enq_persist(&ckptlog);
  ckpt_entry = ckptlog_enq(&ckptlog, 6, 0, chs4);
  ASSERT_NE(ckpt_entry, nullptr) << "ckpt_enq failed \n";
  ckptlog_enq_persist(&ckptlog);
  ckpt_entry = ckptlog_enq(&ckptlog, 7, 0, chs5);
  ASSERT_NE(ckpt_entry, nullptr) << "ckpt_enq failed \n";
  ckptlog_enq_persist(&ckptlog);
  ckpt_entry = ckptlog_enq(&ckptlog, 9, 0, chs6);
  ASSERT_NE(ckpt_entry, nullptr) << "ckpt_enq failed \n";
  ckptlog_enq_persist(&ckptlog);
  ckpt_entry = ckptlog_enq(&ckptlog, 8, 0, chs5);
  ASSERT_NE(ckpt_entry, nullptr) << "ckpt_enq failed \n";
  ckptlog_enq_persist(&ckptlog);

  /* ckptlog_recovery*/
  recovery.root = nvm_root_obj;
  recovery.op_exec = test_op_exec;
  int ret = ckptlog_recovery(&recovery);
  ASSERT_EQ(ret, 0) << "ckptlog replay failed\n";
  ASSERT_NE(ahs1->act_nvhdr.p_act_vhdr, nullptr)
      << "ckptlog replay not properly done";
  ASSERT_NE(ahs2->act_nvhdr.p_act_vhdr, nullptr)
      << "ckptlog replay not properly done";
  ASSERT_NE(ahs3->act_nvhdr.p_act_vhdr, nullptr)
      << "ckptlog replay not properly done";

  free(chs1);
  free(chs2);
  free(chs3);
  free(chs4);
  free(chs5);
  free(chs6);
  nvm_free((void *)ahs1);
  nvm_free((void *)ahs2);
  nvm_free((void *)ahs3);
  nvm_heap_destroy();
}

void ut_list_add(ts::ts_persistent_ptr<list> &p_list, int64_t val) {
  p_list->add(val);
}

void ut_worker_main(ts::ts_persistent_ptr<list> &p_list, int cnt) {
  for (auto i = 0; i < cnt; ++i) {
    // RULE 16. In this example, each addition is
    // one ts::ts_txn. However, if you want or need,
    // you can make adding all nodes as a one transaction.
    ts::ts_txn::run_rw(ts::snapshot, [&]() { p_list->add(i * 100); });
  }
}

TEST(cpp, list) {
  ts::ts_persistent_ptr<list> p_list;

  // RULE 8. Initialize ts_system before use.
  ts::ts_system::init();

  // RULE 11. The main thread also can run
  // a transaction. You can use a lambda function
  // to embrace a timestone transaction.
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_list = new list; });

  // RULE 12. Or you can create a separate a function.
  ts::ts_txn::run_rw(ts::snapshot, ut_list_add, p_list, 1);

  // RULE 13. Make sure all your code should be in ts::ts_txn.
  // If you are sure that it is read-only, use run_ro().
  // run_ro() is slightly more efficient thatn run_rw()
  // because timestone does not call setjmp() to handle
  // abort(), which is hidden, and does not serialize operands
  // for oplogging. But if you are not sure whether
  // a transaction is read-only or not, just use run_rw().
  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    p_list->print();
    ASSERT_EQ(p_list->size(), 1);
  });

  // RULE 14. If you want to spawn a new thread,
  // use ts::ts_thread instead of std::thread. Basically,
  // the usage of ts::ts_thread is the same as std::thread
  // because ts::ts_thread inherits std::thread. In addition,
  // it initializes and deinitializes timestone thread
  // structure.
  ts::ts_thread worker(ut_worker_main, p_list, 9);

  // RULE 15. Of course, you should wait until all
  // threads terminate as usual C++ thread.
  worker.join();

  // RULE 17. Of course, the changes in a child thread
  // are visible.
  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    p_list->print();
    ASSERT_EQ(p_list->size(), 10);
  });

  // RULE 18. Removing a node is the same.
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_list->remove(100); });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    p_list->print();
    ASSERT_EQ(p_list->size(), 9);
    ASSERT_EQ(p_list->contains(100), 0);
  });

  // RULE 19. If you want, you can use higher level
  // of isolation models, serializability and linearizability.
  // But before using higher isolation level, make sure
  // you built a timestone with TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
  if (ts::ts_system::is_supported(ts::serializability)) {
    ts::ts_txn::run_ro(ts::serializability, [&]() {
      ASSERT_EQ(p_list->size(), 9);
      ASSERT_EQ(p_list->contains(200), 1);
    });
  }

  if (ts::ts_system::is_supported(ts::linearizability)) {
    ts::ts_txn::run_ro(ts::linearizability, [&]() {
      ASSERT_EQ(p_list->size(), 9);
      ASSERT_EQ(p_list->contains(200), 1);
    });
  }

  // RULE 9. Finish ts_system after use.
  ts::ts_system::finish();

  // RULE 10. You can still print out statistics
  // even after finishing ts_system.
  ts::ts_system::print_stats();
}

TEST(cpp_array, static_array) {
  int64_t val;

  // create an array object
  static_array array;
  ts::ts_system::init();

  // add elements to the array
  for (auto i = 0; i < 10; ++i) {
    ts::ts_txn::run_rw(ts::snapshot, [&]() { array.add_at(i, i + 1); });
  }

  // read an element from the array
  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.read_at(1);
    ASSERT_EQ(val, 2);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.read_at(9);
    ASSERT_EQ(val, 10);
  });
  // print array
  ts::ts_txn::run_ro(ts::snapshot, [&]() { array.print(); });

  for (auto i = 0; i < 10; ++i) {
    ts::ts_txn::run_rw(ts::snapshot,
                       [&]() { array.test_operator_plus(i, i + 100); });
  }

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.read_at(1);
    ASSERT_EQ(val, 101);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.read_at(9);
    ASSERT_EQ(val, 109);
  });

#ifndef DISABLE
  // test pointer arithmetic on ts_persistent_ptr
  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_plus(1, 2);
    ASSERT_EQ(val, 4);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_minus(5, 2);
    ASSERT_EQ(val, 4);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_peq(5, 2);
    ASSERT_EQ(val, 8);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_meq(5, 4);
    ASSERT_EQ(val, 2);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_incr(5);
    ASSERT_EQ(val, 7);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_decr(5);
    ASSERT_EQ(val, 5);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_pointer_arithmetic(5);
    ASSERT_EQ(val, 6);
  });
#endif
  // print array
  ts::ts_txn::run_ro(ts::snapshot, [&]() { array.print_use_plus(); });
}

#ifndef DISABLE
TEST(cpp_array, dynamic_array) {
  int64_t val;

  // create an array object
  dynamic_array array(10);
  ts::ts_system::init();

  // add elements to the array
  for (auto i = 0; i < 10; ++i) {
    ts::ts_txn::run_rw(ts::snapshot, [&]() { array.add_at(i, i + 1); });
  }

  // read an element from the array
  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.read_at(1);
    ASSERT_EQ(val, 2);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.read_at(9);
    ASSERT_EQ(val, 10);
  });

  // test pointer arithmetic on ts_persistent_ptr
  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_plus(1, 2);
    ASSERT_EQ(val, 4);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_minus(5, 2);
    ASSERT_EQ(val, 4);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_peq(5, 2);
    ASSERT_EQ(val, 8);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_meq(5, 4);
    ASSERT_EQ(val, 2);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_incr(5);
    ASSERT_EQ(val, 7);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_operator_decr(5);
    ASSERT_EQ(val, 5);
  });

  ts::ts_txn::run_ro(ts::snapshot, [&]() {
    val = array.test_pointer_arithmetic(5);
    ASSERT_EQ(val, 6);
  });

  // print array
  ts::ts_txn::run_ro(ts::snapshot, [&]() { array.print(); });
}
#endif

TEST(cpp_btree, leaf_insert) {
  ts::ts_persistent_ptr<btree> p_btree;
  char input_val[5] = "ABCD";
  char *val, *ret_val;
  int size, no_key = 9;

  size = strlen(input_val) + 1;
  val = new char[size];
  memcpy(val, input_val, size);
  ts::ts_system::init();
  std::cout << "init done" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree = new btree; });

  std::cout << "inserting keys" << std::endl;
  for (int i = 0; i < MAX_KEYS; ++i) {
    std::cout << "*** inserting key " << i << " ***" << std::endl;
    ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(i, val); });
  }

  std::cout << "reading keys" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(1);
    ASSERT_EQ(ret_val, val);
  });

  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(2);
    ASSERT_EQ(ret_val, val);
  });

  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(no_key);
    ASSERT_EQ(ret_val, nullptr);
    std::cout << "key " << no_key << " not found \n" << std::endl;
  });

  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(3);
    ASSERT_EQ(ret_val, val);
  });

  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(0);
    ASSERT_EQ(ret_val, val);
  });

  ts::ts_system::finish();
  ts::ts_system::print_stats();
}

TEST(cpp_btree, leaf_insert_rand) {
  ts::ts_persistent_ptr<btree> p_btree;
  char input_val[5] = "ABCD";
  char *val, *ret_val;
  int size, no_key = 9;
  int keys[4] = {3, 1, 0, 2};

  size = strlen(input_val) + 1;
  val = new char[size];
  memcpy(val, input_val, size);
  ts::ts_system::init();
  std::cout << "init done" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree = new btree; });

  for (int i = 0; i < MAX_KEYS; ++i) {
    std::cout << "inserting key " << keys[i] << std::endl;
    ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(keys[i], val); });
  }

  std::cout << "reading keys" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(1);
    ASSERT_EQ(ret_val, val);
  });

  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(2);
    ASSERT_EQ(ret_val, val);
  });

  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(no_key);
    ASSERT_EQ(ret_val, nullptr);
    std::cout << "key " << no_key << " not found \n" << std::endl;
  });

  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(3);
    ASSERT_EQ(ret_val, val);
  });

  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(0);
    ASSERT_EQ(ret_val, val);
  });
  ts::ts_system::finish();
  ts::ts_system::print_stats();
}

TEST(cpp_btree, leaf_split) {
  ts::ts_persistent_ptr<btree> p_btree;
  char input_val[5] = "ABCD";
  char *val, *ret_val;
  int size, no_key = 9;
  int txn_cnt = 1;

  size = strlen(input_val) + 1;
  val = new char[size];
  memcpy(val, input_val, size);
  ts::ts_system::init();
  std::cout << "*** init done ***" << txn_cnt << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree = new btree; });
  ++txn_cnt;

  std::cout << " *** Inserting Keys to the TS-B+Tree ***" << std::endl;
  for (int i = 1; i <= MAX_KEYS; ++i) {
    std::cout << "*** inserting key " << i << " ***" << std::endl;
    ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(i, val); });
    ++txn_cnt;
  }
  std::cout << "*** inserting key 0"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(0, val); });

  std::cout << "*** inserting key 5"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(5, val); });

  std::cout << "*** inserting key -1"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(-1, val); });

  std::cout << "*** inserting key 6"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(6, val); });

  std::cout << "*** Reading Keys from the TS-B+Tree" << std::endl;
  for (int i = 0; i <= MAX_KEYS + 2; ++i) {
    std::cout << "*** reading key " << i << " ***" << std::endl;
    ts::ts_txn::run_rw(ts::snapshot, [&] {
      ret_val = p_btree->lookup(i);
      ASSERT_EQ(ret_val, val);
    });
  }
  std::cout << "*** reading key -1"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(-1);
    ASSERT_EQ(ret_val, val);
  });
  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(no_key);
    ASSERT_EQ(ret_val, nullptr);
    std::cout << "key " << no_key << " not found \n" << std::endl;
  });

  ts::ts_system::finish();
  ts::ts_system::print_stats();
}

TEST(cpp_btree, node_split) {
  ts::ts_persistent_ptr<btree> p_btree;
  char input_val[5] = "ABCD";
  char *val, *ret_val;
  int size, no_key = 18;

  size = strlen(input_val) + 1;
  val = new char[size];
  memcpy(val, input_val, size);
  ts::ts_system::init();
  std::cout << "*** init done ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree = new btree; });

  std::cout << " *** Inserting Keys to the TS-B+Tree ***" << std::endl;
  for (int i = 0; i <= MAX_KEYS + 1; ++i) {
    std::cout << "*** inserting key " << i << " ***" << std::endl;
    ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(i, val); });
  }

  std::cout << "*** inserting key -1"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(-1, val); });

  std::cout << "*** inserting key -2"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(-2, val); });

  for (int i = -1; i <= MAX_KEYS + 1; ++i) {
    std::cout << "*** reading key " << i << " ***" << std::endl;
    ts::ts_txn::run_rw(ts::snapshot, [&] {
      ret_val = p_btree->lookup(i);
      ASSERT_EQ(ret_val, val);
    });
  }

  // trigger split on leaf node #1
  std::cout << "*** inserting key 6"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(6, val); });

  // trigger split on leaf node #0
  std::cout << "*** inserting key -3"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(-3, val); });

  std::cout << "*** inserting key 7"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(7, val); });

  // trigger split on leaf node #2
  std::cout << "*** inserting key 8"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(8, val); });

  std::cout << "*** inserting key 9"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(9, val); });

  std::cout << "*** inserting key 10"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(10, val); });

  std::cout << "*** inserting key 11"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(11, val); });

  std::cout << "*** inserting key 12"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(12, val); });

  std::cout << "*** inserting key 13"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(13, val); });

  std::cout << "*** inserting key 14"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(14, val); });

  std::cout << "*** inserting key 15"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree->insert(15, val); });

  std::cout << "*** Reading Keys from the TS-B+Tree" << std::endl;
  for (int i = -3; i <= MAX_KEYS + 11; ++i) {
    std::cout << "*** reading key " << i << " ***" << std::endl;
    ts::ts_txn::run_rw(ts::snapshot, [&] {
      ret_val = p_btree->lookup(i);
      ASSERT_EQ(ret_val, val);
    });
  }

  std::cout << "*** reading a non-existing key"
            << " ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&] {
    ret_val = p_btree->lookup(no_key);
    ASSERT_EQ(ret_val, nullptr);
    std::cout << "key " << no_key << " not found \n" << std::endl;
  });

  ts::ts_system::finish();
  ts::ts_system::print_stats();
}

void ut_worker_insert(ts::ts_persistent_ptr<btree> &p_btree, int start_key,
                      int end_key, int cpu, char *val) {
  bool ret;
  int count;

  sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set[cpu]);
  std::unique_lock<std::mutex> lck(mtx, std::defer_lock);
  lck.lock();
#ifdef PRINT
  std::cout << " Thread id= " << cpu << std::endl;
  std::cout << " start_key= " << start_key << std::endl;
  std::cout << " end_key= " << end_key << std::endl;
#endif
  while (!smp_atomic_load(&start_run))
    cv.wait(lck);
  lck.unlock();

  for (int i = start_key; i < end_key; ++i) {
#ifdef PRINT
    std::cout << "===============================================" << std::endl;
    std::cout << "*** inserting key " << i << " ***" << std::endl;
#endif
    ASSERT_TRUE(i < KEYS);
    ts::ts_txn::run_rw(ts::snapshot, [&]() {
      ret = p_btree->insert(i, val);
      ASSERT_EQ(ret, true);
    });
#ifdef PRINT
    std::cout << "===============================================" << std::endl;
#endif
  }
#ifdef BENCH_INS
  smp_faa(&end_counter, 1);
  count = smp_atomic_load(&end_counter);
  while (count != N_THREADS) {
    count = smp_atomic_load(&end_counter);
  }
#endif
#ifdef PRINT
  std::cout << "Thread " << cpu << " exiting" << std::endl;
#endif
}

void go() {
  std::unique_lock<std::mutex> lck(mtx);
  smp_cas(&start_run, false, true);
  cv.notify_all();
}

TEST(cpp_btree, concurrent_insert) {
  ts::ts_persistent_ptr<btree> p_btree;
  char input_val[10] = "ABCDEFGHI";
  char *val, *ret_val;
  int size, j, no_key = 1080;
  int read_miss = 0;
  int miss[MISS_BUFF];
  int read_miss_imm = 0;
  int read_miss_retry = 0;
  int miss_imm[MISS_BUFF];
  bool retry = false;
  int start_key[N_THREADS];
  int end_key[N_THREADS];
  std::vector<ts::ts_thread> threads;

  for (int i = 0; i < N_THREADS; ++i) {
    start_key[i] = (KEYS / N_THREADS) * i;
    end_key[i] = start_key[i] + (KEYS / N_THREADS);
    CPU_ZERO(&cpu_set[i]);
    CPU_SET(i, &cpu_set[i]);
  }
  size = strlen(input_val) + 1;
  val = new char[size];
  memcpy(val, input_val, size);
  ts::ts_system::init();
  std::cout << "*** init done ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree = new btree; });
  std::cout << " *** Inserting Keys to the TS-B+Tree ***" << std::endl;
  for (int i = 0; i < N_THREADS; i++) {
    std::cout << "creating thread " << i << std::endl;
    threads.push_back(ts::ts_thread(ut_worker_insert, p_btree, start_key[i],
                                    end_key[i], i, val));
  }
  go();
  for (auto &th : threads) {
    th.join();
  }
  std::cout << "*** Reading Keys from the TS-B+Tree" << std::endl;
  for (int i = 0; i < KEYS; ++i) {
#ifdef PRINT
    std::cout << "*** reading key " << i << " ***" << std::endl;
#endif
    ts::ts_txn::run_rw(ts::snapshot, [&] {
      ret_val = p_btree->lookup(i);
      if (ret_val != val) {
        miss[read_miss] = i;
        ++read_miss;
      }
      ASSERT_EQ(ret_val, val);
    });
  }
  std::cout << "trying read misses again" << std::endl;
  for (int i = 0; i < read_miss; ++i) {
    std::cout << "*** reading key " << miss[i] << " ***" << std::endl;
    ts::ts_txn::run_rw(ts::snapshot, [&] {
      ret_val = p_btree->lookup(miss[i]);
      if (ret_val != val) {
        ++read_miss_retry;
        retry = true;
      }
      ASSERT_EQ(ret_val, val);
    });
  }
  std::cout << "*** read misses= " << read_miss << " ***" << std::endl;
  std::cout << "*** read misses in retry= " << read_miss_retry << " ***"
            << std::endl;
  ts::ts_system::finish();
  ts::ts_system::print_stats();
}

void ut_worker_lookup(ts::ts_persistent_ptr<btree> &p_btree, int start_key,
                      int end_key, int cpu, char *val, int *read_miss) {
  char *ret_val;
  int id = cpu;

  sched_setaffinity(0, sizeof(cpu_set), &cpu_set[id]);
  std::unique_lock<std::mutex> lck(mtx, std::defer_lock);
  lck.lock();
#ifdef PRINT
  std::cout << " Thread id= " << cpu << std::endl;
  std::cout << " start_key= " << start_key << std::endl;
  std::cout << " end_key= " << end_key << std::endl;
#endif
  while (!smp_atomic_load(&start_run))
    cv.wait(lck);
  lck.unlock();

  for (int i = start_key; i < end_key; ++i) {
#ifdef PRINT
    std::cout << "*** reading key " << i << " ***" << std::endl;
#endif
    ts::ts_txn::run_rw(ts::snapshot, [&] {
      ret_val = p_btree->lookup(i);
      ASSERT_EQ(ret_val, val);
    });
  }
}

TEST(cpp_btree, concurrent_lookup) {
  ts::ts_persistent_ptr<btree> p_btree;
  char input_val[5] = "ABCD";
  char *val, *ret_val;
  int size, j, no_key = 1080;
  int start_key[N_THREADS];
  int end_key[N_THREADS];
  int n_threads_insert = 1;
  int *read_miss;
  int miss_cnt = 0;
  std::vector<ts::ts_thread> i_threads;
  std::vector<ts::ts_thread> l_threads;

  read_miss = new int[N_THREADS];
  for (int i = 0; i < N_THREADS; ++i) {
    start_key[i] = (KEYS / N_THREADS) * i;
    end_key[i] = start_key[i] + (KEYS / N_THREADS);
    CPU_ZERO(&cpu_set[i]);
    CPU_SET(i, &cpu_set[i]);
    read_miss[i] = 0;
  }
  size = strlen(input_val) + 1;
  val = new char[size];
  memcpy(val, input_val, size);
  ts::ts_system::init();
  std::cout << "*** init done ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree = new btree; });
  for (int i = 0; i < n_threads_insert; ++i) {
    std::cout << "creating thread " << i << std::endl;
    i_threads.push_back(
        ts::ts_thread(ut_worker_insert, p_btree, 0, KEYS, i, val));
  }
  go();
  for (auto &th : i_threads) {
    th.join();
  }
  std::cout << "*** Reading Keys from the TS-B+Tree ***" << std::endl;
  for (int i = 0; i < N_THREADS; ++i) {
    std::cout << "creating thread " << i << std::endl;
    l_threads.push_back(ts::ts_thread(ut_worker_lookup, p_btree, start_key[i],
                                      end_key[i], i, val, read_miss));
  }
  go();
  for (auto &th : l_threads) {
    th.join();
  }
  delete[] read_miss;
  ts::ts_system::finish();
  ts::ts_system::print_stats();
}

void ut_worker_init() {
  ts::ts_persistent_ptr<btree> p_btree;

  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree = new btree; });
}

TEST(cpp_btree, init) {
  ts::ts_persistent_ptr<btree> p_btree;

  ts::ts_system::init();
  std::cout << "*** ts init done ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree = new btree; });
  std::cout << "*** Btree root creation done ***" << std::endl;
  ts::ts_system::finish();
}

TEST(cpp_btree, concurrent_insert_lookup) {
  ts::ts_persistent_ptr<btree> p_btree;
  char input_val[10] = "ABCDEFGHI";
  char *val, *ret_val;
  int size, j, no_key = 1080;
  int start_key[N_THREADS];
  int end_key[N_THREADS];
  int n_threads_insert = 1;
  int *read_miss;
  int miss_cnt = 0;
  std::vector<ts::ts_thread> i_threads;
  std::vector<ts::ts_thread> l_threads;

  read_miss = new int[N_THREADS];
  for (int i = 0; i < N_THREADS; ++i) {
    start_key[i] = (KEYS / N_THREADS) * i;
    end_key[i] = start_key[i] + (KEYS / N_THREADS);
    CPU_ZERO(&cpu_set[i]);
    CPU_SET(i, &cpu_set[i]);
    read_miss[i] = 0;
  }
  size = strlen(input_val) + 1;
  val = new char[size];
  memcpy(val, input_val, size);
  ts::ts_system::init();
  std::cout << "*** init done ***" << std::endl;
  ts::ts_txn::run_rw(ts::snapshot, [&]() { p_btree = new btree; });
  std::cout << " *** Inserting Keys to the TS-B+Tree using " << N_THREADS
            << " threads ***" << std::endl;

  for (int i = 0; i < N_THREADS; ++i) {
    std::cout << "creating thread " << i << std::endl;
    i_threads.push_back(ts::ts_thread(ut_worker_insert, p_btree, start_key[i],
                                      end_key[i], i, val));
  }
  go();
  for (auto &th : i_threads) {
    th.join();
  }
  std::cout << " *** Reading Keys from TS-B+Tree using " << N_THREADS
            << " threads ***" << std::endl;
  for (int i = 0; i < N_THREADS; ++i) {
    std::cout << "creating thread " << i << std::endl;
    l_threads.push_back(ts::ts_thread(ut_worker_lookup, p_btree, start_key[i],
                                      end_key[i], i, val, read_miss));
  }
  go();
  for (auto &th : l_threads) {
    th.join();
  }
  for (int i = 0; i < N_THREADS; ++i) {
    if (read_miss[i] != 0) {
      std::cout << "Thread " << i << "\t"
                << "read_misses= " << read_miss[i] << std::endl;
      ++miss_cnt;
    }
  }
  if (!miss_cnt) {
    std::cout << "*** No read_miss occured ***" << std::endl;
  }
  delete[] read_miss;
  ts::ts_system::finish();
  ts::ts_system::print_stats();
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
