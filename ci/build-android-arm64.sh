#!/usr/bin/env bash
set -euo pipefail

source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=${ANDROID_BUILD_WORKDIR:-"$source_dir/.android-ci"}
sdk_root=${ANDROID_SDK_ROOT:-"$work_dir/android-sdk"}
core_source=${ANDROID_CORE_SOURCE:-"$work_dir/OpenCPN-5.14"}
core_build=${ANDROID_CORE_BUILD:-"$work_dir/core-build"}
plugin_build=${ANDROID_PLUGIN_BUILD:-"$work_dir/plugin-build"}
support_cache=${ANDROID_SUPPORT_CACHE:-"$work_dir/support-cache"}
artifacts="$source_dir/artifacts/android-arm64"
ndk_version=26.1.10909125
core_commit=91f3b674366068a6ecd61a5e9aba204bba85f57e
support_sha256=c4110c532e9a0bcf071bbd10fe6f7627d7e91380c803c52ac0e89ce5f993db9b

mkdir -p "$work_dir" "$support_cache" "$artifacts/package"

if [[ -z "${NDK_HOME:-}" ]]; then
  sdkmanager="$sdk_root/cmdline-tools/latest/bin/sdkmanager"
  if [[ ! -x "$sdkmanager" ]]; then
    tools_zip="$work_dir/commandlinetools-linux-11076708_latest.zip"
    curl --fail --location --retry 3 \
      https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip \
      --output "$tools_zip"
    mkdir -p "$sdk_root/cmdline-tools/latest"
    unzip -q "$tools_zip" -d "$work_dir/cmdline-tools-unpacked"
    cp -a "$work_dir/cmdline-tools-unpacked/cmdline-tools/." \
      "$sdk_root/cmdline-tools/latest/"
  fi
  set +o pipefail
  yes | "$sdkmanager" --sdk_root="$sdk_root" --licenses >/dev/null
  set -o pipefail
  "$sdkmanager" --sdk_root="$sdk_root" "ndk;$ndk_version"
  NDK_HOME="$sdk_root/ndk/$ndk_version"
fi
test -x "$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++"
export NDK_HOME OCPN_TARGET=android-arm64
tool_base="$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64"

if [[ ! -d "$core_source/.git" ]]; then
  git clone --depth 1 --branch Release_5.14.0 \
    https://github.com/OpenCPN/OpenCPN.git "$core_source"
fi
test "$(git -C "$core_source" rev-parse HEAD)" = "$core_commit"

support_zip="$support_cache/support.zip"
if [[ ! -s "$support_zip" ]]; then
  curl --fail --location --retry 3 \
    https://github.com/bdbcat/OCPNAndroidCoreBuildSupport/releases/download/v1.2/OCPNAndroidCoreBuildSupport.zip \
    --output "$support_zip"
fi
printf '%s  %s\n' "$support_sha256" "$support_zip" | sha256sum --check

cmake -S "$core_source" -B "$core_build" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_TOOLCHAIN_FILE="$core_source/buildandroid/build_android.cmake" \
  '-DOCPN_TARGET_TUPLE:STRING=Android-arm64;33;arm64' \
  -Dtool_base="$tool_base" \
  -DOCPN_ANDROID_CACHEDIR="$support_cache" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$core_build" --target gorp \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"

support_root="$support_cache/OCPNAndroidCoreBuildSupport"
test -f "$support_root/wxWidgets/libs/arm64/lib/wx/include/arm-linux-androideabi-qt-unicode-static-3.1/wx/setup.h"
git -C "$source_dir" submodule update --init opencpn-libs

cmake -S "$source_dir" -B "$plugin_build" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_TOOLCHAIN_FILE="$source_dir/cmake/android-aarch64-toolchain.cmake" \
  -D_wx_selected_config=androideabi-qt-arm64 \
  -DOCPN_Android_Common="$support_root" \
  -DOCPN_ANDROID_CORE_LIBRARY="$core_build/libgorp.so" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$plugin_build" \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
rm -f "$plugin_build"/weather_routing_pi-*-android-arm64.tar.gz \
      "$plugin_build"/weather_routing_pi-*-android-arm64.xml \
      "$artifacts/package"/weather_routing_pi-* "$artifacts/package/SHA256SUMS"
cmake --build "$plugin_build" --target package

shopt -s nullglob
packages=("$plugin_build"/weather_routing_pi-*-android-arm64.tar.gz)
metadata=("$plugin_build"/weather_routing_pi-*-android-arm64.xml)
test "${#packages[@]}" -eq 1
test "${#metadata[@]}" -eq 1
python3 "$source_dir/ci/package-android-import.py" \
  "${packages[0]}" "${metadata[0]}" "$artifacts/package"
python3 "$source_dir/ci/verify-shoreline-package.py" "${packages[0]}"
"$tool_base/bin/llvm-readelf" -h "$plugin_build/libweather_routing_pi.so" \
  | grep -E 'Machine:.*AArch64'
"$tool_base/bin/llvm-readelf" -d "$plugin_build/libweather_routing_pi.so" \
  | grep -E 'SONAME.*libweather_routing_pi.so|NEEDED.*libgorp.so'
cp "${packages[0]}" "${metadata[0]}" "$artifacts/package/"
(cd "$artifacts/package" && sha256sum ./*.tar.gz ./*.xml > SHA256SUMS)
