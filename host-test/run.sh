#!/bin/sh
# Compile et lance la suite de tests hôte. À exécuter sous WSL.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -G Ninja >/dev/null
cmake --build build >/dev/null
./build/tests
