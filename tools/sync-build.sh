#!/usr/bin/env bash
# Sync sources to the VM and show compiler errors only.
set -uo pipefail
TC_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$TC_ROOT"
tar -czf - Makefile src tests tools resources vendor 2>/dev/null |
    bash tools/haiku.sh 'mkdir -p /boot/home/turbochook && tar xzf - --warning=no-timestamp -C /boot/home/turbochook 2>/dev/null; cd /boot/home/turbochook && make -j6 2>&1 | grep -E -A3 "error|Error|undefined" | head -${1:-80}'
