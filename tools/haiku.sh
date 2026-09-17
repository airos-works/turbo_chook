#!/usr/bin/env bash
set -euo pipefail
TC_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec ssh -i "$TC_ROOT/.vm/id_ed25519" -p 2226 \
    -o IdentitiesOnly=yes -o BatchMode=yes \
    -o UserKnownHostsFile="$TC_ROOT/.vm/known_hosts" \
    -o ConnectTimeout=10 user@127.0.0.1 "$@"
