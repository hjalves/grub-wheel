#!/bin/bash
# Install usb_mouse.mod + grub.d snippet on asus-arch and regenerate grub.cfg.
# Does NOT enable it: use `grub-editenv - set usb_mouse_test=1` (one boot)
# or `usb_mouse=1` (every boot) on the host.
set -euo pipefail
cd "$(dirname "$0")"
HOST=${HOST:-asus-arch}
rsync -a ../out/usb_mouse.mod 05_usb_mouse "$HOST":/tmp/grub-wheel-build/
ssh "$HOST" sudo bash -s <<'R'
set -euo pipefail
cd /tmp/grub-wheel-build
install -Dm644 usb_mouse.mod /boot/grub/usb_mouse/usb_mouse.mod
# ABI pin: the module was built from the same source as these.
for m in usb.mod ehci.mod usb_keyboard.mod; do
  cmp -s grub-2.16/grub-core/$m /usr/lib/grub/i386-pc/$m ||
    { echo "$m in the build tree differs from the installed grub package; rebuild"; exit 1; }
done
(cd /usr/lib/grub/i386-pc && sha256sum usb.mod ehci.mod usb_keyboard.mod) > abi.tmp
(cd /boot/grub/i386-pc && sha256sum --quiet -c /tmp/grub-wheel-build/abi.tmp) || {
  echo "/boot/grub/i386-pc differs from /usr/lib/grub/i386-pc (grub-install not run after an upgrade?)"; exit 1; }
install -m644 abi.tmp /boot/grub/usb_mouse/abi.sha256
install -m755 05_usb_mouse /etc/grub.d/05_usb_mouse
grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -3
grep -c "BEGIN usb_mouse" /boot/grub/grub.cfg
R
