#!/bin/bash
# Build and run the SQLite demo. Run from apps/sqlite/.
set -e
make -j2
qemu-system-x86_64 -kernel build/sqlite_qemu-x86_64 -nographic -no-reboot -m 256M
