#!/bin/sh

set -e

(
  cd "$(dirname "$0")"
  cmake -B build -S . \
                -DCMAKE_C_COMPILER=/usr/bin/gcc \
                -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
                -DCMAKE_MAKE_PROGRAM=/usr/bin/make
  cmake --build ./build
)

exec $(dirname "$0")/build/bittorrent "$@"
