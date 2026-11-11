# PF-on-Linux kernel module build (out-of-tree)
# Build:  make -C /path/to/linux-7.3.4 M=$PWD   (or scripts/build-module.sh)
#
# -D_KERNEL=1 selects the kernel-side branches of the staged OpenBSD sources
# (the same files also contain userland-only code guarded by #ifndef _KERNEL).
ccflags-y := -I$(src)/include -I$(src)/include/compat -DPFLINUX=1 \
             -D_KERNEL=1 \
             -Wno-declaration-after-statement \
             -Wno-implicit-fallthrough \
             -Wno-frame-larger-than

# our glue + compat layer
pf-objs := pfsrc/pfk_mod.o pfsrc/pfk_hooks.o \
           compat/pfk_mbuf.o compat/pfk_kern.o compat/pfk_if.o compat/pfk_xmit.o \
           compat/siphash.o compat/toeplitz.o compat/md5.o compat/sha2.o \
           compat/radix.o compat/pfk_rtable.o compat/pfk_ip6.o

# ported OpenBSD 8.0 engine
pf-objs += pfsrc/pf_ruleset.o pfsrc/pf_table.o pfsrc/pf_norm.o pfsrc/pf_if.o \
           pfsrc/pf_lb.o pfsrc/pf_osfp.o pfsrc/pf_syncookies.o \
           pfsrc/pf_ioctl.o pfsrc/pf.o pfsrc/divert_sock.o

# queueing framework (compat/pfk_ifq.c implements the ifq API with priq
# semantics; the OpenBSD hfsc/fq_codel back ends land in milestone M5)
pf-objs += compat/pfk_ifq.o

obj-m := pf.o
