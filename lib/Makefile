CUR_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

include $(CUR_DIR)/../Makefile.inc

# configuration
ifeq ($(strip $(CONF)),gtest)
  LIB_SUFFIX = -gtest
  CFLAGS += -DTS_GTEST
endif

# - nvm configuration
#CFLAGS += -DTS_NVM_IS_DRAM
#CFLAGS += -DTS_NVM_IS_PMDK
# CFLAGS += -DTS_NVM_IS_MAKALU
CFLAGS += -DTS_NVM_IS_NV_JEMALLOC

# - debug configuration
#CFLAGS += -DTS_ENABLE_ASSERT ## XXX enable for debugging only
#CFLAGS += -DTS_ENABLE_FREE_POISIONING ## XXX should be removed

CFLAGS += -I$(INC_DIR)
CFLAGS += -I$(PROJ_DIR)/lib-makalu/include -I$(PROJ_DIR)/lib-nv-jemalloc/include
CFLAGS += -march=native -mtune=native -O3 
CFLAGS += -g -ggdb3
CFLAGS += -Winline -Wall -Wextra -Wshadow -Wdeclaration-after-statement
CFLAGS += -Wno-attributes -Wno-unused-parameter -Wno-unused-value
CFLAGS += -Wno-unused-function -Wno-packed-not-aligned
LDFLAGS += -lpthread $(MEMMGR) $(PMEM)

DEPS_DIR  := $(CUR_DIR)/.deps$(LIB_SUFFIX)
DEPCFLAGS = -MD -MF $(DEPS_DIR)/$*.d -MP

SRC_FILES = $(wildcard *.c)
OBJS_DIR  = $(CUR_DIR)/.objs$(LIB_SUFFIX)
OBJ_FILES = $(addprefix $(OBJS_DIR)/, $(SRC_FILES:.c=.o))

all: $(INC_DIR)/numa-config.h libts$(LIB_SUFFIX).a

$(OBJS_DIR)/%.o: %.c $(DEPS_DIR) $(OBJS_DIR)
	$(Q)$(CC) $(CFLAGS) $(DEPCFLAGS) -c -o $@ $<

$(OBJS_DIR):
	$(Q)mkdir -p $(OBJS_DIR)

$(DEPS_DIR):
	$(Q)mkdir -p $(DEPS_DIR)

$(INC_DIR)/numa-config.h:
	$(Q)$(TOOLS_DIR)/cpu-topology.py > $@

libts$(LIB_SUFFIX).a: $(OBJ_FILES)
	$(Q)/bin/rm -f $@
	$(Q)$(AR) cru $@ $^


clean:
	$(Q)rm -f libts*.a
	$(Q)rm -rf $(CUR_DIR)/.deps*
	$(Q)rm -rf $(CUR_DIR)/.objs*
	$(Q)rm -f $(INC_DIR)/numa-config.h

.PHONY: all clean
