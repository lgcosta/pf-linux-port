savedcmd_pf.o := ld -m elf_x86_64 --fatal-warnings -z noexecstack --no-warn-rwx-segments   -r -o pf.o @pf.mod  ; /home/lgcosta/lab/linux_kernel/linux-7.3.4/tools/objtool/objtool --hacks=jump_label --hacks=noinstr --hacks=skylake --ibt --prefix=16 --orc --retpoline --rethunk --static-call --uaccess  --link  --module pf.o

pf.o: $(wildcard /home/lgcosta/lab/linux_kernel/linux-7.3.4/tools/objtool/objtool)
