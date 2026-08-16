#!/usr/bin/env bash
# Compile et lance le simulateur. Le répertoire de compilation vit dans le
# dépôt et non dans /tmp : sous WSL, /tmp est effacé entre deux invocations
# lancées depuis Windows.
set -euo pipefail
ici="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$ici" -B "$ici/build" -G Ninja >/dev/null
cmake --build "$ici/build"
exec "$ici/build/ktouch-sim" "$@"
