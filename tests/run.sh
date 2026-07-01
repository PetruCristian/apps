#!/bin/bash
# Build and run the libc subsystem coverage tests. Run from apps/tests/.
set -e
make -j2
qemu-system-x86_64 -kernel build/tests_qemu-x86_64 -nographic -no-reboot -m 256M
