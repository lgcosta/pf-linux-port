# PF-on-Linux kernel module build (out-of-tree)
# Build:  make -C /path/to/linux-7.3.4 M=$PWD   (or scripts/build-module.sh)
ccflags-y := -I$(src)/include -I$(src)/include/compat -DPFLINUX=1 \
             -Wno-declaration-after-statement

# M1 skeleton set — staged engine files (pf.c, pf_ioctl.c, ...) are added
# per plan tasks T7b/T10/T11/T12; keep list explicit (no wildcards).
pf-objs := pfsrc/pfk_mod.o pfsrc/pfk_hooks.o \
           compat/pfk_mbuf.o compat/pfk_kern.o compat/pfk_if.o compat/pfk_xmit.o \
           compat/siphash.o compat/toeplitz.o compat/md5.o compat/sha2.o
# staged BSD sources (uncomment as ported):
# pf-objs += compat/sha2.o compat/sha1.o compat/siphash.o compat/toeplitz.o
# pf-objs += compat/radix.o compat/rtable.o
# pf-objs += pfsrc/pf.o pfsrc/pf_ioctl.o pfsrc/pf_norm.o pfsrc/pf_if.o pfsrc/pf_lb.o
# pf-objs += pfsrc/pf_ruleset.o pfsrc/pf_table.o pfsrc/pf_osfp.o pfsrc/pf_syncookies.o

obj-m := pf.o
