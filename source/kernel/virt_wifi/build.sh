#!/bin/bash
# Builds virt_wifi.ko (with the ssid=/bssid= parameters) for the guest kernel: LineageOS android_kernel_virt_virtio at
# 4f6bf47200d9, the guest's own config, release 6.12.81-4k-g4f6bf47200d9. Runs in the kiln-kernel-build image of the
# colima profile "openmumu"; the kernel tree lives in the Docker volume kiln-kernel-src (a case-sensitive file system:
# on the Mac's disk, files like xt_CONNMARK.h / xt_connmark.h overwrite each other).
# Needs, in $KILN_KERNEL (default /Volumes/Expansion/Users/nks1608/Developer/kiln-kernel): config.gz and vmlinux.symvers
# from the guest kernel, and refmods/modules/virt_wifi.ko (the image's own) to check the symbol versions against.
# Output: $KILN_KERNEL/virt_wifi.ko
set -euo pipefail
S="$(cd "$(dirname "$0")" && pwd)"
KK="${KILN_KERNEL:-/Volumes/Expansion/Users/nks1608/Developer/kiln-kernel}"
export DOCKER_HOST="${DOCKER_HOST:-unix:///Volumes/Expansion/Users/nks1608/.colima/openmumu/docker.sock}"
mkdir -p "$KK/virt_wifi-src"
cp "$S/virt_wifi.c" "$S/Kbuild" "$KK/virt_wifi-src/"
docker volume create kiln-kernel-src >/dev/null
docker run --rm -v kiln-kernel-src:/k -v "$KK":/kk kiln-kernel-build bash -c '
set -euo pipefail
T=/k/tree M="make -s ARCH=arm64 LLVM=1"
if [ ! -d $T/.git ]; then
  mkdir -p $T && cd $T && git init -q
  git fetch -q --depth 1 https://github.com/LineageOS/android_kernel_virt_virtio 4f6bf47200d9cf735e65cad80823295d2141982c
  git checkout -q FETCH_HEAD
fi
cd $T
if [ ! -f Module.symvers ] || ! cmp -s <(gzip -dc /kk/config.gz) .config.kiln; then
  gzip -dc /kk/config.gz > .config; cp .config .config.kiln
  $M olddefconfig; $M -j"$(nproc)" modules_prepare
  cp /kk/vmlinux.symvers Module.symvers
fi
rel=$($M kernelrelease); [ "$rel" = 6.12.81-4k-g4f6bf47200d9 ] || { echo "kernel release is $rel" >&2; exit 1; }
# cfg80211 for the versions of the symbols virt_wifi uses (it needs rfkill, another module: its own imports only warn)
[ -s net/wireless/Module.symvers ] || $M -j"$(nproc)" M=net/wireless KBUILD_MODPOST_WARN=1 modules
rm -rf /k/virt_wifi && cp -r /kk/virt_wifi-src /k/virt_wifi
$M -C $T M=/k/virt_wifi KBUILD_EXTRA_SYMBOLS=$T/net/wireless/Module.symvers modules
llvm-strip --strip-debug /k/virt_wifi/virt_wifi.ko
# the symbol versions must be the ones the image'"'"'s cfg80211 and kernel export, or the module is refused
python3 - /kk/refmods/modules/virt_wifi.ko /k/virt_wifi/virt_wifi.ko <<PY
import sys, struct
def versions(path):
    data = open(path, "rb").read()
    import subprocess
    subprocess.run(["llvm-objcopy", "-O", "binary", "--only-section=__versions", path, "/tmp/versions.bin"], check=True)
    out = open("/tmp/versions.bin", "rb").read()
    v = {}
    for i in range(0, len(out), 64):
        crc = struct.unpack_from("<Q", out, i)[0]; name = out[i+8:i+64].split(b"\0")[0].decode()  # u64 crc, char[56]
        if name: v[name] = crc
    return v
ref, ours = versions(sys.argv[1]), versions(sys.argv[2])
bad = [n for n in ours if n in ref and ours[n] != ref[n]]
missing = [n for n in ours if n not in ref]
print(f"symbol versions: {len(ours)} used, {len(bad)} differ from the image, {len(missing)} not in the original module")
if bad or not ours: print("differ:", bad[:10] or "no __versions read"); sys.exit(1)
PY
modinfo /k/virt_wifi/virt_wifi.ko | grep -E "^(vermagic|parm):"
cp /k/virt_wifi/virt_wifi.ko /kk/virt_wifi.ko
'
echo "built: $KK/virt_wifi.ko"
