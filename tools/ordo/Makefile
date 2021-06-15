# Quiet compile, unset for verbose output
Q	= @

O	= o
CC	= gcc

CWARNS	= -Wformat=2 -Wextra -Wmissing-noreturn -Wwrite-strings -Wshadow \
	  -Wno-unused-parameter -Wmissing-format-attribute -fno-builtin \
	  -Wswitch-default -Wmissing-prototypes \
	  -Wmissing-declarations
CFLAGS  = $(DEFS) -Wall -Werror -fno-strict-aliasing $(CWARNS) \
	  -g -O0 -D_GNU_SOURCE

LDFLAGS = -lrt -lm

LIBS	= bench.c
DEPS	= bench.h config.h

BINS	= tttable reftable membandwidthtable cc
BINS	:= $(addprefix $(O)/, $(BINS))


all: CPUSEQ $(BINS)

CPUSEQ:
	@echo "generating sequential cpu"
	$(Q)./gen_cpuseq.py

$(O)/%: %.c $(DEPS) $(CPUSEQ)
	@mkdir -p $(@D)
	@echo "CC	$@"
	$(Q)$(CC) $(CFLAGS) -o $@ $< $(LIBS) -lpthread $(LDFLAGS)

clean:
	@echo "CLEAN"
	$(Q)rm -rf $(O) && rm -rf cpuseq.h
