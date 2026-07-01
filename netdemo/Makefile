UK_ROOT ?= $(CURDIR)/../../unikraft
UK_BUILD ?= $(CURDIR)/build
UK_APP ?= $(CURDIR)
LIBS_BASE = $(CURDIR)/../../libs

# Networking demo on the FreeBSD-libc port: libc + lwip (registers as the
# posix-socket driver). No SQLite, no OpenSSL — just the socket stack.
UK_LIBS ?= $(LIBS_BASE)/lib-freebsd-libc:$(LIBS_BASE)/lib-lwip

.PHONY: all

all:
	@$(MAKE) -C $(UK_ROOT) L=$(UK_LIBS) A=$(UK_APP) O=$(UK_BUILD)

$(MAKECMDGOALS):
	@$(MAKE) -C $(UK_ROOT) L=$(UK_LIBS) A=$(UK_APP) O=$(UK_BUILD) $(MAKECMDGOALS)
