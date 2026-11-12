# PF-on-Linux top-level make
KERNELDIR ?= $(HOME)/lab/linux_kernel/linux-7.3.4

.PHONY: module tools kernel test stress clean vm vm-destroy docs demo

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

# Stress: throughput/state/rule/table churn against the lab, with a report in
# out/stress/<timestamp>/report.md.  Pass options through, e.g.
#   make stress ARGS="--quick"
stress:
	./scripts/stress/run-stress.sh $(ARGS)

docs:
	@for f in docs/en/*.md; do echo "EN  $$f"; done
	@for f in docs/pt-BR/*.md; do echo "PT  $$f"; done

# Record the narrated screencasts (real shell sessions, MiniMax voice).
# Needs MINIMAX_API_KEY and the VM lab up; see docs/*/screencast.md.
screencast:
	./scripts/screencast/build-all.sh

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR)/module clean || true
	$(MAKE) -C tools clean || true
	@find out -maxdepth 1 -type f ! -name '*.cast' -delete 2>/dev/null || true
