PROJ_DIR  := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
GIT_DIR   := $(PROJ_DIR)/.git
TOOLS_DIR := $(PROJ_DIR)/tools
INC_DIR   := $(PROJ_DIR)/include
LIB_DIR   := $(PROJ_DIR)/lib
LIB++_DIR := $(PROJ_DIR)/lib++
UT_DIR    := $(PROJ_DIR)/unittest
BIN_DIR   := $(PROJ_DIR)/bin

AR = ar
CC = gcc
CXX = g++
MAKE = make

# -jN
ifeq ($(strip $(NJOB)), )
  NJOB := ${shell nproc}
endif

# malloc
ifeq ($(strip $(MEMMGR)), )
  MEMMGR += -ljemalloc
endif

# pmem
PMEM := -lpmem -lpmemobj $(PROJ_DIR)/lib-nv-jemalloc/lib/libjemalloc.a

# $(PROJ_DIR)/lib-makalu/lib/libmakalu.a