#!/usr/bin/env bash
# Configures the native tree for the host toolchain and runs the tests through CTest, so the
# xatlas and Assimp pipelines can be verified without an Android device. Not part of the APK.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${G3D_HOST_BUILD_DIR:-$here/build-host}"

cmake -S "$here/.." -B "$out" -DCMAKE_BUILD_TYPE="${G3D_HOST_BUILD_TYPE:-Release}"
cmake --build "$out" -j"$(nproc)"
ctest --test-dir "$out" --output-on-failure
