#!/bin/bash
# Build test/grub.lnx (lnxboot + core with memdisk) on asus-arch, for qemu -kernel.
set -euo pipefail
cd "$(dirname "$0")"
HOST=${HOST:-asus-arch}
sed "/usb_mouse.mod ] || exit 0/d" ../deploy/05_usb_mouse > 05_usb_mouse_test; rsync -a grub.cfg 05_usb_mouse_test ../out/usb_mouse.mod "$HOST":/tmp/grub-wheel-build/
ssh "$HOST" bash -s <<'R'
set -euo pipefail
cd /tmp/grub-wheel-build && rm -rf md && mkdir -p md/boot/grub/i386-pc
L=/usr/lib/grub/i386-pc
want="normal gfxterm all_video uhci ehci usb usb_keyboard echo sleep gettext test font hashsum keystatus gcry_sha256 loadenv"
deps() { for m in "$@"; do echo $m; d=$(sed -n "s/^$m: *//p" $L/moddep.lst); [ -n "$d" ] && deps $d; done; }
for m in $(deps $want | sort -u); do cp $L/$m.mod md/boot/grub/i386-pc/; done
cp $L/*.lst md/boot/grub/i386-pc/
mkdir -p md/boot/grub/fonts && cp /boot/grub/fonts/terminus-32.pf2 md/boot/grub/fonts/ 2>/dev/null || true
mkdir -p md/boot/grub/usb_mouse && cp usb_mouse.mod md/boot/grub/usb_mouse/
(cd /usr/lib/grub/i386-pc && sha256sum usb.mod ehci.mod usb_keyboard.mod) > md/boot/grub/usb_mouse/abi.sha256
sed '/@SNIPPET@/,$d' grub.cfg > md/boot/grub/grub.cfg; sh 05_usb_mouse_test >> md/boot/grub/grub.cfg; sed '1,/@SNIPPET@/d' grub.cfg >> md/boot/grub/grub.cfg
tar -C md -cf memdisk.tar boot
grub-mkimage -O i386-pc -o core.img -p '(memdisk)/boot/grub' -m memdisk.tar memdisk tar biosdisk
cat /usr/lib/grub/i386-pc/lnxboot.img core.img > grub.lnx
R
rsync -a "$HOST":/tmp/grub-wheel-build/grub.lnx .
ls -l grub.lnx
