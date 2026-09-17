#!/usr/bin/env bash
# Copies the sources into the Turbo Chook VM, builds natively and runs the tests.
set -euo pipefail
TC_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$TC_ROOT"
tar -czf - Makefile README.md LICENSE src tests tools resources vendor docs |
    bash tools/haiku.sh 'mkdir -p /boot/home/turbochook && tar xzf - -C /boot/home/turbochook'
bash tools/haiku.sh 'cd /boot/home/turbochook && make -j6 2>&1 | grep -E "error|warning: unused|Error" ; make -j6 && make check-host'
mkdir -p artifacts
scp -O -i .vm/id_ed25519 -P 2226 -o IdentitiesOnly=yes -o BatchMode=yes -o UserKnownHostsFile=.vm/known_hosts \
    user@127.0.0.1:/boot/home/turbochook/build-haiku/TurboChook artifacts/TurboChook.new
mv -f artifacts/TurboChook.new artifacts/TurboChook
