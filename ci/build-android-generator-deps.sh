#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=${ANDROID_GENERATOR_WORKDIR:-"$source_dir/.android-ci/generator-deps"}
vcpkg_root="$work_dir/vcpkg"
revision=319504a5326aa870edde46438c5455fa76305a56
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to the pinned Android NDK}"
mkdir -p "$work_dir"
if [[ ! -d "$vcpkg_root/.git" ]]; then
  git clone https://github.com/microsoft/vcpkg.git "$vcpkg_root"
  git -C "$vcpkg_root" checkout --detach "$revision"
fi
test "$(git -C "$vcpkg_root" rev-parse HEAD)" = "$revision"
if [[ ! -x "$vcpkg_root/vcpkg" ]]; then
  "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
fi
VCPKG_MAX_CONCURRENCY=${CMAKE_BUILD_PARALLEL_LEVEL:-4} \
  "$vcpkg_root/vcpkg" install \
    --x-manifest-root="$source_dir/ci/android-generator" \
    --x-install-root="$work_dir/installed" \
    --overlay-triplets="$source_dir/ci/android-generator" \
    --overlay-ports="$source_dir/ci/android-generator/ports" \
    --triplet=arm64-android-release --disable-metrics
