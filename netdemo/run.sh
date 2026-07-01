#!/bin/bash
# Build and run the netdemo HTTP server. Run from apps/netdemo/.
# -cpu max exposes RDRAND (ukrandom). QEMU user-net gives the guest a static IP
# (10.0.2.15) and forwards host port 8080 -> guest 8080. This blocks in the
# server's accept() loop: test it from another terminal with ./test-connection.sh,
# then quit QEMU with Ctrl-a x.
set -e
make -j2
qemu-system-x86_64 -cpu max -kernel build/netdemo_qemu-x86_64 -nographic -no-reboot -m 256M \
  -append "netdemo netdev.ip=10.0.2.15/24:10.0.2.2::: --" \
  -netdev user,id=n0,hostfwd=tcp::8080-10.0.2.15:8080 \
  -device virtio-net-pci,netdev=n0
