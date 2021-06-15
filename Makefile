CUR_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

include $(CUR_DIR)/Makefile.inc

PHONY := all
all: lib-nv-jemalloc lib lib++ unittest benchmark

PHONY += clean
clean:
	(cd $(PROJ_DIR)/lib && make -s clean)
	(cd $(PROJ_DIR)/lib++ && make -s clean)
	(cd $(PROJ_DIR)/lib-nv-jemalloc && make -s clean)
	(cd $(PROJ_DIR)/benchmark/rlu && make -s clean)

PHONY += distclean
distclean: clean
	@echo -e "\033[0;32m# Clean everything completely...\033[0m"
	(cd $(PROJ_DIR)/benchmark && make distclean)
	rm -f $(BIN_DIR)/*-rlu
	rm -f $(BIN_DIR)/*-timestone*
	rm -f $(BIN_DIR)/*-mvrlu
	rm -f $(BIN_DIR)/*-mvrlu-*
	rm -f $(BIN_DIR)/*-vanilla

PHONY += format
format: git-hooks
	@echo -e "\033[0;32m# Running clang-format...\033[0m"
	@clang-format -i $(INC_DIR)/*.[ch] $(LIB_DIR)/*.[ch] $(UT_DIR)/*.cc
	@clang-format -style=Google -i $(INC_DIR)/*.hpp $(LIB++_DIR)/*.cpp

git-hooks: $(GIT_DIR)/hooks/pre-commit

$(GIT_DIR)/hooks/pre-commit:
	@echo -e "\033[0;32m# Installing git pre-commit hook for formatting\033[0m"
	@ln -s $(TOOLS_DIR)/pre-commit $(GIT_DIR)/hooks/pre-commit

PHONY += benchmark
benchmark: lib
	(cd $(PROJ_DIR)/benchmark/rlu && make)

PHONY += benchmark-clean
benchmark-clean:
	(cd $(PROJ_DIR)/benchmark/rlu && make clean)

PHONY += lib-nv-jemalloc
lib-nv-jemalloc: git-hooks
	make -C $(PROJ_DIR)/lib-nv-jemalloc -j$(NJOB)

PHONY += lib-nv-jemalloc-clean
lib-nv-jemalloc-clean: git-hooks
	make -C $(PROJ_DIR)/lib-nv-jemalloc clean

PHONY += lib
lib: git-hooks lib-nv-jemalloc
	(cd $(PROJ_DIR)/lib && \
	 CONF= make -j$(NJOB) && \
	 CONF=gtest make -j$(NJOB))

PHONY += lib-clean
lib-clean: git-hooks
	(cd $(PROJ_DIR)/lib && \
	 CONF= make -j$(NJOB) clean && \
	 CONF=gtest make -j$(NJOB) clean)

PHONY += lib++
lib++: git-hooks
	(cd $(PROJ_DIR)/lib++ && \
	 CONF= make -j$(NJOB) && \
	 CONF=gtest make -j$(NJOB))

PHONY += lib++-clean
lib++-clean: git-hooks
	(cd $(PROJ_DIR)/lib++ && \
	 CONF= make -j$(NJOB) clean && \
	 CONF=gtest make -j$(NJOB) clean)

PHONY += unittest
unittest: lib lib++
	make -C $(PROJ_DIR)/unittest -j$(NJOB)

PHONY += unittest-clean
unittest-clean:
	make -C $(PROJ_DIR)/unittest -j$(NJOB) clean

PHONY += ordo
ordo: git-hooks
	make -C $(TOOLS_DIR)/ordo/
	(cd $(TOOLS_DIR)/ordo && sudo ./gen_table.py)

PHONY += ordo-clean
ordo-clean:
	make -C $(TOOLS_DIR)/ordo/ clean

PHONY += help
help: git-hooks
	@echo '## Generic targets:'
	@echo '  all                    - Configure and build all source code'
	@echo '  clean                  - Remove most generated files.'
	@echo '  distclean              - Remove all generated files and config files'
	@echo '  format                 - Apply clang-format. Follow LLVM style for C++ code'
	@echo '                           and Linux kernel style for C code'
	@echo ''
	@echo '## Library targets:'
	@echo '  lib                    - Build TimeStone C library'
	@echo '  lib-clean              - Clean TimeStone C library'
	@echo '  lib++                  - Build TimeStone C++ library'
	@echo '  lib++-clean            - Clean TimeStone C++ library'
	@echo '  lib-nv-jemalloc        - Build NV Jemalloc allocator'
	@echo '  lib-nv-jemalloc-clean  - Clean NV Jemalloc allocator'
	@echo '  lib-makalu             - Build Makalu NVM allocator'
	@echo '  lib-makalu-clean       - Clean Makalu NVM allocator'
	@echo '  ordo        	        - Get ordo value of the server'
	@echo ''
	@echo '## Unittest targets:'
	@echo '  unittest               - Build unittest'
	@echo '                           Install google test lib: `sudo dnf install gtest-devel`'
	@echo '  unittest-clean         - Clean unittest'
	@echo ''
	@echo '## Benchmark targets:'
	@echo '  benchmark              - Build all benchmarks'
	@echo '  benchmark-clean        - Clean all benchmarks'
	@echo ''
	@echo '## ORDO targets:'
	@echo '  ordo                   - Build and measure ORDO boundary'
	@echo '  ordo-clean             - Clean ORDO binaries'
	@echo ''

.PHONY: $(PHONY)

