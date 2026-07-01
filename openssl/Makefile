UK_ROOT ?= $(CURDIR)/../../unikraft
UK_BUILD ?= $(CURDIR)/build
UK_APP ?= $(CURDIR)
LIBS_BASE = $(CURDIR)/../../libs

# OpenSSL (libcrypto) needs lwip (BIO sockets) and a pthread surface. We supply
# the pthread surface from lib-freebsd-libc's single-threaded shim instead of
# lib-pthread-embedded (the unikernel is single-threaded), so that lib is
# intentionally NOT listed here.
# Old line (kept for reference):
# UK_LIBS ?= $(LIBS_BASE)/lib-freebsd-libc:$(LIBS_BASE)/lib-pthread-embedded:$(LIBS_BASE)/lib-lwip:$(LIBS_BASE)/lib-openssl
UK_LIBS ?= $(LIBS_BASE)/lib-freebsd-libc:$(LIBS_BASE)/lib-lwip:$(LIBS_BASE)/lib-openssl

.PHONY: all

all:
	@$(MAKE) -C $(UK_ROOT) L=$(UK_LIBS) A=$(UK_APP) O=$(UK_BUILD)

$(MAKECMDGOALS):
	@$(MAKE) -C $(UK_ROOT) L=$(UK_LIBS) A=$(UK_APP) O=$(UK_BUILD) $(MAKECMDGOALS)
