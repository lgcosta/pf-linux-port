LDFLAGS ?= -static
# PFLINUX — build OpenBSD 8.0 pfctl userland for Linux.
#
# Layout:
#   include/       compat headers (sys/ioctl.h with OpenBSD _IO encoding,
#                  net/{route,if,if_dl}.h, netinet/tcp_fsm.h, md5.h, ...)
#                  plus copies generated at build time:
#                    include/net/pfvar.h  <- ../module/include/net/pfvar.h
#                    include/sys/{queue,tree}.h, include/net/{radix,hfsc,
#                    fq_codel}.h          <- ../_vendor/upstream/sys/...
#   pfctl/         pristine OpenBSD 8.0 sbin/pfctl sources + marked edits
#   compat-user/   glibc missing pieces (sysctl, strtonum, md5, tcpstates)
#   abitest/       ABI struct layout table (built twice: tools/include and
#                  module/include, compared, MD5-verified)
#   out/           build products (make clean removes)
#
# Contract: `make -C tools` must work with plain make/gcc/bison.

TOP     := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(TOP)/..)
MODULE  := $(ROOT)/module
VENDOR  := $(ROOT)/_vendor/upstream
SRC     := $(TOP)/pfctl
COMPAT  := $(TOP)/compat-user
INC     := $(TOP)/include
OUT     := $(TOP)/out
OBJ     := $(OUT)/obj
GEN     := $(OUT)/gen

CC      ?= cc
BISON   ?= bison

# PFLINUX: include order is ABI-critical.  $(INC) first so sys/ioctl.h
# (OpenBSD PFKXIOC encoding, identical to module/include/pfk.h),
# net/pfvar.h, sys/{queue,tree}.h, net/{route,if,if_dl}.h,
# netinet/tcp_fsm.h and md5.h shadow the Linux system headers.
# pflinux_compat.h is force-included (attribute macros, strtonum,
# strings.h) before any source's own #includes.
CPPFLAGS += -I$(INC) -I$(GEN) -include $(INC)/pflinux_compat.h
CFLAGS   ?= -O2 -g
CFLAGS   += -std=gnu17 -Wall -D_GNU_SOURCE
LDLIBS   += -lm

# Headers copied verbatim at build time (never edited; single source of
# truth stays in module/ and _vendor/).
GENHDRS := $(INC)/net/pfvar.h $(INC)/net/radix.h $(INC)/net/hfsc.h \
	$(INC)/net/fq_codel.h $(INC)/sys/tree.h $(INC)/sys/queue.h

PF_SRCS := pfctl.c pfctl_parser.c pf_print_state.c pfctl_table.c \
	pfctl_queue.c pfctl_radix.c pfctl_osfp.c pfctl_optimize.c
CB_SRCS := sysctl.c strtonum.c bsd.c md5.c rbtree.c inet_net_pton.c

PF_OBJS  := $(addprefix $(OBJ)/,$(PF_SRCS:.c=.o)) $(OBJ)/parse.o
CB_OBJS  := $(addprefix $(OBJ)/,$(CB_SRCS:.c=.o))

GOODTESTS := tests/01_basic.conf tests/02_translations.conf \
	tests/03_queues_scrub.conf

.PHONY: all pfctl abitest test abi clean
all: pfctl abitest

pfctl: $(OUT)/pfctl

# ---------------------------------------------------------------------------
# generated (copied) headers
# ---------------------------------------------------------------------------

# Contract: userland MUST use the exact kernel-side ABI header.
$(INC)/net/pfvar.h: $(MODULE)/include/net/pfvar.h
	@mkdir -p $(@D)
	@cmp -s $< $@ || cp -f $< $@

$(INC)/net/radix.h: $(VENDOR)/sys/net/radix.h
	@mkdir -p $(@D)
	@cmp -s $< $@ || cp -f $< $@

$(INC)/net/hfsc.h: $(VENDOR)/sys/net/hfsc.h
	@mkdir -p $(@D)
	@cmp -s $< $@ || cp -f $< $@

$(INC)/net/fq_codel.h: $(VENDOR)/sys/net/fq_codel.h
	@mkdir -p $(@D)
	@cmp -s $< $@ || cp -f $< $@

$(INC)/sys/tree.h: $(VENDOR)/sys/sys/tree.h
	@mkdir -p $(@D)
	@cmp -s $< $@ || cp -f $< $@

$(INC)/sys/queue.h: $(VENDOR)/sys/sys/queue.h
	@mkdir -p $(@D)
	@cmp -s $< $@ || cp -f $< $@

# ---------------------------------------------------------------------------
# grammar: OpenBSD pfctl has a hand-written lexer inside parse.y; only
# bison is required (flex is NOT used).
# ---------------------------------------------------------------------------
$(GEN)/parse.c: $(SRC)/parse.y $(GENHDRS)
	@mkdir -p $(GEN)
	$(BISON) -d -o $@ $<

$(OBJ)/%.o: $(SRC)/%.c $(GENHDRS)
	@mkdir -p $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ)/parse.o: $(GEN)/parse.c $(SRC)/pfctl.h $(SRC)/pfctl_parser.h
	@mkdir -p $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(SRC) -c $< -o $@

$(OBJ)/%.o: $(COMPAT)/%.c $(GENHDRS)
	@mkdir -p $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# PFLINUX: OpenBSD 8.0 pfctl links the kernel's pf_ruleset.c userland
# branch ("Effort sponsored...  Userland equivalents so we can lend code
# to pfctl") for anchor/ruleset management (pf_anchor_setup,
# pf_find_or_create_ruleset, pf_main_anchor, ...).  Compiled straight
# from _vendor, unmodified.
$(OBJ)/pf_ruleset.o: $(VENDOR)/sys/net/pf_ruleset.c $(GENHDRS)
	@mkdir -p $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OUT)/pfctl: $(PF_OBJS) $(CB_OBJS) $(OBJ)/pf_ruleset.o
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(PF_OBJS) $(CB_OBJS) $(OBJ)/pf_ruleset.o $(LDLIBS)

# ---------------------------------------------------------------------------
# ABI table: same abitest.c compiled twice —
#   out/abitest        : headers from tools/include (copied pfvar.h)
#   out/abitest-module : tools/include shims but net/pfvar.h taken directly
#                        from ../module/include (kernel ABI header)
# The two tables must be byte-identical (proof: MD5 over the table).
# The kernel-module-side check (structs compiled with the real kernel
# compat headers under -D_KERNEL) is done separately by the integrator.
# ---------------------------------------------------------------------------
abitest: $(OUT)/abitest $(OUT)/abitest-module

$(OBJ)/abitest-user.o: $(TOP)/abitest/abitest.c $(GENHDRS)
	@mkdir -p $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ)/abitest-module.o: $(TOP)/abitest/abitest.c $(GENHDRS)
	@mkdir -p $(OBJ)
	$(CC) -I$(MODULE)/include $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OUT)/abitest: $(OBJ)/abitest-user.o $(OBJ)/md5.o
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OUT)/abitest-module: $(OBJ)/abitest-module.o $(OBJ)/md5.o
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

abi: abitest
	@$(OUT)/abitest > $(OUT)/abi-tools.tbl
	@$(OUT)/abitest-module > $(OUT)/abi-module.tbl
	@diff -u $(OUT)/abi-tools.tbl $(OUT)/abi-module.tbl && \
		echo "ABI OK: identical tables"
	@tail -n 1 $(OUT)/abi-tools.tbl

# ---------------------------------------------------------------------------
# offline (-n) parse validation, no /dev/pf required
# ---------------------------------------------------------------------------
test: $(OUT)/pfctl
	@ok=1; for c in $(GOODTESTS); do \
		if $(OUT)/pfctl -n -f $$c >/dev/null 2>&1; then \
			echo "PASS $$c (exit 0)"; \
		else \
			echo "FAIL $$c (exit $$?)"; ok=0; \
		fi; \
	done; \
	if $(OUT)/pfctl -n -f tests/broken.conf >/dev/null 2>&1; then \
		echo "FAIL tests/broken.conf accepted"; ok=0; \
	else \
		echo "PASS tests/broken.conf rejected (non-zero exit)"; \
	fi; \
	test $$ok -eq 1

clean:
	rm -rf $(OUT)
	rm -f $(GENHDRS)
