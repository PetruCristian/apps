UK_ROOT ?= $(CURDIR)/../../unikraft
UK_BUILD ?= $(CURDIR)/build
UK_APP ?= $(CURDIR)
LIBS_BASE = $(CURDIR)/../../libs
UK_LIBS ?= $(LIBS_BASE)/lib-freebsd-libc:$(LIBS_BASE)/lib-sqlite

.PHONY: all

all:
	@$(MAKE) -C $(UK_ROOT) L=$(UK_LIBS) A=$(UK_APP) O=$(UK_BUILD)

$(MAKECMDGOALS):
	@$(MAKE) -C $(UK_ROOT) L=$(UK_LIBS) A=$(UK_APP) O=$(UK_BUILD) $(MAKECMDGOALS)
