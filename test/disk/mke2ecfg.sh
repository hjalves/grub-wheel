#!/bin/sh
# Regenerate e2e.cfg: the real grub.d snippet between a minimal header and a
# menu whose first entry prints grubenv (expect usb_mouse_result=... ok).
cd "$(dirname "$0")"
{ printf 'insmod part_msdos\ninsmod ext2\nload_env\nterminal_input console\n'
  sed '/usb_mouse.mod ] || exit 0/d; /ehci_keepbios.mod ] || exit 0/d' ../../deploy/05_usb_mouse | sh
  printf 'set timeout=-1\nmenuentry "Read disk" { cat /boot/grub/grubenv; ls /boot/grub/usb_mouse; sleep 1000 }\nmenuentry "Two" { true }\n'
} > e2e.cfg
