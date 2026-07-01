#!/bin/bash
# Build and run the OpenSSL demo. Run from apps/openssl/.
# -cpu max exposes RDRAND, which ukrandom needs to boot.
set -e
make -j2
qemu-system-x86_64 -cpu max -kernel build/openssl_qemu-x86_64 -nographic -no-reboot -m 256M
