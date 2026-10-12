# Student-written HW2 tests. Run from the repository root with, e.g.:
#   just test hw2-student ioctl-checks
#   just test-all hw2-student
MINEMU ?= minemu

.PHONY: all prepare-ioctl-checks prepare-user-fault clean

all: prepare-ioctl-checks prepare-user-fault

prepare-ioctl-checks:
	$(MAKE) -C fixtures/ioctl-checks MINEMU="$(MINEMU)" all

prepare-user-fault:
	$(MAKE) -C fixtures/user-fault MINEMU="$(MINEMU)" all

prepare-%:
	@:

clean:
	$(MAKE) -C fixtures/ioctl-checks clean
	$(MAKE) -C fixtures/user-fault clean
