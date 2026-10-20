#!/usr/bin/env bash
# Builds and runs the native core smoke test on the host toolchain, so the xatlas pipeline can be
# verified without an Android device. Nothing here is part of the APK.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="$here/build-host"
mkdir -p "$out"

: "${CXX:=g++}"
CXXFLAGS=(-std=c++17 -O2 -I "$root/native" -I "$root/third_party/xatlas" -I "$root/third_party/glm")

echo "==> compiling xatlas"
"$CXX" "${CXXFLAGS[@]}" -w -c "$root/third_party/xatlas/xatlas.cpp" -o "$out/xatlas.o"

echo "==> compiling g3d native core"
for src in core/mesh.cpp core/primitives.cpp uv/uv_service.cpp tests/host_smoke_test.cpp; do
  "$CXX" "${CXXFLAGS[@]}" -Wall -Wextra -Wno-unused-parameter -c "$root/native/$src" \
    -o "$out/$(basename "$src").o"
done

echo "==> linking and running"
"$CXX" -o "$out/host_smoke_test" \
  "$out/xatlas.o" "$out/mesh.cpp.o" "$out/primitives.cpp.o" "$out/uv_service.cpp.o" \
  "$out/host_smoke_test.cpp.o" -pthread

"$out/host_smoke_test"
