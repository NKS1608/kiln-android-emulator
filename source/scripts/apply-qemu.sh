#!/bin/sh
# Copy the kiln QEMU sources into a QEMU tree and hook them into the build.
# Idempotent. Usage: scripts/apply-qemu.sh <qemu_src_dir>
set -eu

Q="${1:?usage: $0 <qemu_src_dir>}"
O="$(cd "$(dirname "$0")/.." && pwd)/qemu"

# Same relative layout as the QEMU tree.
(cd "$O" && find . -type f -name '*.[ch]') | while read -r f; do
  mkdir -p "$Q/$(dirname "$f")"
  cp "$O/$f" "$Q/$f"
done

# Changes to QEMU's own files (qemu/patches/*.patch, -p1): applied once, a patch that is already in is skipped.
for p in "$O"/patches/*.patch; do
  [ -e "$p" ] || continue
  if (cd "$Q" && patch -p1 -R --dry-run -s -f < "$p" >/dev/null 2>&1); then continue; fi
  (cd "$Q" && patch -p1 -s < "$p")
done

grep -q "config VADDRESS" "$Q/hw/misc/Kconfig" || cat >> "$Q/hw/misc/Kconfig" <<'K'

config VADDRESS
    bool
    default y if PCI_DEVICES
    depends on PCI
K
grep -q "vaddress-gl.c" "$Q/hw/misc/meson.build" || {
  sed -i '' "/CONFIG_VADDRESS/d" "$Q/hw/misc/meson.build"
  echo "system_ss.add(when: 'CONFIG_VADDRESS', if_true: files('vaddress.c', 'vaddress-echo.c', 'vaddress-gl.c'))" >> "$Q/hw/misc/meson.build"
}
# The 0x9800 service sub-device was removed: drop it from an already patched tree.
rm -f "$Q/hw/misc/vaddress-service.c"
sed -i '' "s/ 'vaddress-service.c',//" "$Q/hw/misc/meson.build"
grep -q "vaddress-test" "$Q/tests/qtest/meson.build" ||
  sed -i '' "s/^qtests_aarch64 = \\\\\$/qtests_aarch64 = \\\\\n  (config_all_devices.has_key('CONFIG_VADDRESS') and config_all_devices.has_key('CONFIG_ARM_VIRT') ? ['vaddress-test'] : []) + \\\\/" "$Q/tests/qtest/meson.build"
grep -q "test-vaddress-ring" "$Q/tests/unit/meson.build" ||
  sed -i '' "s/^  'test-int128': \[\],\$/  'test-int128': [],\n  # all code tested by test-vaddress-ring is inside vaddress-ring.h\n  'test-vaddress-ring': [],/" "$Q/tests/unit/meson.build"
grep -q "vaddress-test" "$Q/tests/qtest/meson.build"
grep -q "test-vaddress-ring" "$Q/tests/unit/meson.build"
