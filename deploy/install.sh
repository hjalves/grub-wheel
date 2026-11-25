#!/bin/bash
# Install usb_mouse.mod + grub.d snippet on asus-arch and regenerate grub.cfg.
# Does NOT enable it: use `grub-editenv - set usb_mouse_test=1` (one boot)
# or `usb_mouse=1` (every boot) on the host.
set -euo pipefail
cd "$(dirname "$0")"
HOST=${HOST:-asus-arch}
rsync -a ../out/usb_mouse.mod ../out/ehci_keepbios.mod ../out/abi.sha256 05_usb_mouse "$HOST":/tmp/
ssh "$HOST" sudo bash -s <<'R'
set -euo pipefail
cd /tmp
# ABI pin: the stock modules from the build must match what
# GRUB actually loads.
(cd /boot/grub/i386-pc && sha256sum --quiet -c /tmp/abi.sha256) || {
  echo "/boot/grub/i386-pc modules differ from the build; rebuild (build.sh)"; exit 1; }
install -Dm644 usb_mouse.mod /boot/grub/usb_mouse/usb_mouse.mod
install -m644 ehci_keepbios.mod /boot/grub/usb_mouse/ehci_keepbios.mod
install -m644 abi.sha256 /boot/grub/usb_mouse/abi.sha256
install -m755 05_usb_mouse /etc/grub.d/05_usb_mouse
grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -3
grep -c "BEGIN usb_mouse" /boot/grub/grub.cfg
rm -f usb_mouse.mod ehci_keepbios.mod abi.sha256 05_usb_mouse
R
