#!/bin/bash
# Boot disk.img in QEMU with an EHCI controller + USB mouse. The HMP monitor
# is at ./mon.sock (sendkey, mouse_button, screendump ...).
cd "$(dirname "$0")"
exec qemu-system-x86_64 -M pc -m 256 -display none -vga std -kernel grub.lnx \
  -drive file=disk.img,format=raw,if=ide \
  -device ich9-usb-ehci1,id=ehci -device ich9-usb-uhci1,masterbus=ehci.0,firstport=0 \
  -device usb-mouse,bus=ehci.0 -monitor unix:mon.sock,server=on,wait=off "$@"
