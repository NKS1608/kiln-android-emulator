#!/bin/bash
# Builds QEMU as it ships in Kiln's runtime, for the oldest macOS Kiln supports (MACOSX_DEPLOYMENT_TARGET, the app's
# LSMinimumSystemVersion). Homebrew's bottles and a plain build are made for the Mac they are built on, so a runtime bundled
# from them only starts on that macOS or later.
#
# 1. The libraries QEMU links, from source, into $PREFIX (default $DEV/kiln-deps; sources and build trees in $PREFIX/src):
#    glib (with pcre2 and proxy-libintl from its own subprojects), pixman, libslirp, zstd, SDL3 and sdl2-compat. One already
#    installed is skipped (delete $PREFIX to rebuild them).
# 2. QEMU in $DEV/qemu-kiln/build-14 (sources already patched by scripts/apply-qemu.sh), against those libraries only,
#    without the features Kiln does not use (VNC, TLS, SSH, host USB, capstone, rutabaga, Apple's PVG).
#
# The build trees keep no record of the target: always build them through this script. Needs meson, ninja, cmake and
# pkg-config. The paths at the top are the build machine's: change DEV for yours.
#
# Usage: scripts/build-qemu.sh [prefix]
set -euo pipefail

export MACOSX_DEPLOYMENT_TARGET=14.0
DEV=/Volumes/Expansion/Users/nks1608/Developer
PREFIX=${1:-$DEV/kiln-deps}
SRC=$PREFIX/src
mkdir -p "$SRC" "$PREFIX/lib"
# only this prefix and macOS: nothing from Homebrew may leak into a build
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig
export PKG_CONFIG_PATH=
export CMAKE_PREFIX_PATH=$PREFIX
JOBS=$(sysctl -n hw.ncpu)

# fetch <url> <dir name>: downloads and unpacks into $SRC/<dir name>
fetch() {
  local url=$1 dir=$SRC/$2
  [ -d "$dir" ] && return
  local tmp=$SRC/.unpack
  rm -rf "$tmp" && mkdir -p "$tmp"
  curl -fL --retry 3 -o "$tmp/archive" "$url"
  tar -xf "$tmp/archive" -C "$tmp"
  rm "$tmp/archive"
  mv "$tmp"/* "$dir"
  rm -rf "$tmp"
}

meson_build() {  # <dir> [options]
  local dir=$SRC/$1; shift
  rm -rf "$dir/build"
  meson setup "$dir/build" "$dir" --prefix="$PREFIX" --libdir=lib --buildtype=release -Ddefault_library=shared "$@"
  # meson finds pipe2 in the SDK (macOS 27 has it) although the deployment target is 14: the library would call a weak symbol
  # that is NULL there (it killed QEMU at start-up on macOS 14). Without HAVE_PIPE2 glib uses pipe() and fcntl().
  [ -f "$dir/build/config.h" ] && sed -i '' '/^#define HAVE_PIPE2 /d' "$dir/build/config.h"
  ninja -C "$dir/build" install
}

cmake_build() {  # <dir> [options]
  local dir=$SRC/$1; shift
  cmake -S "$dir" -B "$dir/build" -G Ninja -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET -DCMAKE_FIND_FRAMEWORK=LAST "$@"
  ninja -C "$dir/build" install
}

installed() { [ -e "$PREFIX/lib/$1" ] && echo "have $1"; }

installed libglib-2.0.dylib || {
  fetch https://download.gnome.org/sources/glib/2.90/glib-2.90.1.tar.xz glib
  # pcre2 and libintl from glib's own wraps; libffi and libiconv from macOS
  meson_build glib --force-fallback-for=pcre2,intl -Dtests=false -Dintrospection=disabled -Dman-pages=disabled \
    -Ddocumentation=false -Dnls=disabled
}

installed libpixman-1.dylib || {
  fetch https://cairographics.org/releases/pixman-0.46.4.tar.gz pixman
  meson_build pixman -Dtests=disabled -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled
}

installed libslirp.dylib || {
  fetch https://gitlab.freedesktop.org/slirp/libslirp/-/archive/v4.9.5/libslirp-v4.9.5.tar.gz libslirp
  meson_build libslirp
}

installed libzstd.dylib || {
  fetch https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz zstd
  cmake_build zstd/build/cmake -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_STATIC=OFF -DZSTD_BUILD_TESTS=OFF \
    -DZSTD_LZ4_SUPPORT=OFF -DZSTD_LZMA_SUPPORT=OFF -DZSTD_ZLIB_SUPPORT=OFF
}

installed libSDL3.dylib || {
  fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz SDL3
  cmake_build SDL3 -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_STATIC=OFF -DSDL_X11=OFF
}

installed libSDL2-2.0.dylib || installed libSDL2.dylib || {
  fetch https://github.com/libsdl-org/sdl2-compat/releases/download/release-2.32.74/sdl2-compat-2.32.74.tar.gz sdl2-compat
  cmake_build sdl2-compat -DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF
}

Q=$DEV/qemu-kiln
[ -f "$Q/build-14/build.ninja" ] || (mkdir -p "$Q/build-14" && cd "$Q/build-14" && ../configure \
  --python=/opt/homebrew/bin/python3.14 --target-list=aarch64-softmmu --enable-hvf --disable-docs --enable-sdl \
  --enable-slirp --enable-zstd --disable-pvg --disable-capstone --disable-gnutls --disable-nettle --disable-gcrypt \
  --disable-png --disable-vnc --disable-libssh --disable-libusb --disable-rutabaga-gfx --disable-gtk --disable-curl \
  --disable-curses --disable-libudev --disable-sdl-image --disable-virglrenderer --disable-opengl --disable-spice \
  --disable-lzo --disable-snappy --disable-bzip2 --disable-lzfse --disable-libnfs --disable-libiscsi --disable-guest-agent)
ninja -C "$Q/build-14"  # all: the binaries, the firmware and keymaps bundle-runtime.py copies

echo "QEMU built for macOS $MACOSX_DEPLOYMENT_TARGET"
