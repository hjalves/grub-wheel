#!/bin/bash
# Build test/disk/{disk.img,grub.lnx}: an MBR disk with an ext2 partition holding
# /boot/grub (stock i386-pc modules + ours, grub.cfg from CFG), booted through
# lnxboot via qemu -kernel so GRUB reads it with the BIOS disk driver (biosdisk).
# Usage: mkdisk.sh [CFG]   (default: e2e.cfg)
set -euo pipefail
cd "$(dirname "$0")"
HOST=${HOST:-asus-arch}
CFG=${1:-e2e.cfg}
rsync -a "$CFG" ../../out/usb_mouse.mod ../../out/ehci_keepbios.mod ../../out/abi.sha256 "$HOST":/tmp/gwd-in/ 2>/dev/null ||
  { ssh "$HOST" mkdir -p /tmp/gwd-in; rsync -a "$CFG" ../../out/usb_mouse.mod ../../out/ehci_keepbios.mod ../../out/abi.sha256 "$HOST":/tmp/gwd-in/; }
ssh "$HOST" CFG="$(basename "$CFG")" bash -s <<'R'
set -euo pipefail
rm -rf /tmp/gwd && mkdir -p /tmp/gwd/root/boot/grub/i386-pc /tmp/gwd/root/boot/grub/usb_mouse && cd /tmp/gwd
cp /usr/lib/grub/i386-pc/*.mod /usr/lib/grub/i386-pc/*.lst root/boot/grub/i386-pc/
cp /tmp/gwd-in/{usb_mouse.mod,ehci_keepbios.mod,abi.sha256} root/boot/grub/usb_mouse/
cp /tmp/gwd-in/$CFG root/boot/grub/grub.cfg
grub-editenv root/boot/grub/grubenv create
grub-editenv root/boot/grub/grubenv set usb_mouse_test=1
truncate -s 64M disk.img && echo "2048,,83" | sfdisk -q disk.img
truncate -s 63M fs.img && mkfs.ext2 -q -d root fs.img
dd if=fs.img of=disk.img bs=1M seek=1 conv=notrunc status=none
grub-mkimage -O i386-pc -o core.img -p "(hd0,msdos1)/boot/grub" biosdisk part_msdos ext2
cat /usr/lib/grub/i386-pc/lnxboot.img core.img > grub.lnx
R
rsync -a "$HOST":/tmp/gwd/disk.img "$HOST":/tmp/gwd/grub.lnx .
echo "built disk.img grub.lnx; run: ./run.sh"
