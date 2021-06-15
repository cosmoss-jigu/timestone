CUR_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

include $(CUR_DIR)/../Makefile.inc

# configuration
ifeq ($(strip $(CONF)),gtest)
  LIB_SUFFIX = -gtest
  CXXFLAGS += -DTS_GTEST
endif

CXXFLAGS += -std=c++14 -I$(INC_DIR)
CXXFLAGS += -march=native -mtune=native -O3
CXXFLAGS += -g -ggdb3
CXXFLAGS += -Winline -Werror -Wall -Wextra -Wshadow
CXXFLAGS += -Wno-attributes -Wno-unused-parameter -Wno-unused-value
CXXFLAGS += -Wno-unused-function -Wno-packed-not-aligned
LDFLAGS += -lpthread $(MEMMGR) $(PMEM)

DEPS_DIR  := $(CUR_DIR)/.deps$(LIB_SUFFIX)
DEPCFLAGS = -MD -MF $(DEPS_DIR)/$*.d -MP

SRC_FILES = $(wildcard *.cpp)
OBJS_DIR  = $(CUR_DIR)/.objs$(LIB_SUFFIX)
OBJ_FILES = $(addprefix $(OBJS_DIR)/, $(SRC_FILES:.cpp=.o))

all: libts++$(LIB_SUFFIX).a

$(OBJS_DIR)/%.o: %.cpp $(DEPS_DIR) $(OBJS_DIR)
	$(Q)$(CXX) $(CXXFLAGS) $(DEPCFLAGS) -c -o $@ $<

$(OBJS_DIR):
	$(Q)mkdir -p $(OBJS_DIR)

$(DEPS_DIR):
	$(Q)mkdir -p $(DEPS_DIR)

libts++$(LIB_SUFFIX).a: $(OBJ_FILES)
	$(Q)/bin/rm -f $@
	$(Q)$(AR) cru $@ $^

clean:
	$(Q)rm -f libts++*.a
	$(Q)rm -rf $(CUR_DIR)/.deps*
	$(Q)rm -rf $(CUR_DIR)/.objs*

.PHONY: all clean
