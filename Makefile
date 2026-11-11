# PF-on-Linux top-level make
KERNELDIR ?= $(HOME)/lab/linux_kernel/linux-7.3.4

.PHONY: module tools kernel test clean vm vm-destroy docs demo

module:
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR)/module modules

tools:
	$(MAKE) -C tools KERNELDIR=$(KERNELDIR)

kernel:
	./scripts/build-kernel.sh

vm:
	./scripts/vm-create.sh

vm-destroy:
	./scripts/vm-destroy.sh

test:
	./scripts/vm-run-tests.sh

docs:
	@for f in docs/en/*.md; do echo "EN  $$f"; done
	@for f in docs/pt-BR/*.md; do echo "PT  $$f"; done

# Record the lab session as an asciinema cast (real output, scripted pacing).
demo:
	@mkdir -p out
	@command -v asciinema >/dev/null 2>&1 || { \
		echo "asciinema not installed: pip install asciinema"; exit 1; }
	asciinema rec out/demo-pf-linux.cast -c "bash scripts/demo-session.sh"
	@echo "-> out/demo-pf-linux.cast (play: asciinema play out/demo-pf-linux.cast)"

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR)/module clean || true
	$(MAKE) -C tools clean || true
	@find out -maxdepth 1 -type f ! -name '*.cast' -delete 2>/dev/null || true
