# grub-wheel: `usb_mouse.mod`

A GRUB 2.16 (i386-pc) module for asus-arch that lets a USB mouse drive the
boot menu: **wheel up/down = ↑/↓**, **left click = Enter**. Pointer motion is ignored.

- `usb_mouse.c`: the module. It is a HID input terminal modelled on
  `usb_keyboard`. It reads the HID report descriptor to find the wheel and
  button 1, because boot-protocol mouse reports carry no wheel. If that fails,
  it falls back to the `[buttons, x, y, wheel]` layout.
  It also adds the command `usb_mouse_probe [VAR]`, which reports the detected
  mice and can store the result in VAR.
- `build.sh`: builds on asus-arch from the grub-2.16 release tarball with Arch's
  configure flags. The resulting `usb.mod`, `ehci.mod` and `uhci.mod` are
  byte-identical to Arch's, so the ABI matches.
- `deploy/install.sh`: installs `/boot/grub/usb_mouse/{usb_mouse.mod,abi.sha256}`
  and `/etc/grub.d/05_usb_mouse`, then runs `grub-mkconfig`. It does not enable
  the module.
- `test/`: QEMU test. `mkimage.sh` builds an lnxboot+memdisk image, and
  `qemu-test.py` injects wheel and click events over QMP and takes screenshots.

## Enabling (on asus-arch)

    sudo grub-editenv - set usb_mouse_test=1   # next boot only; result → usb_mouse_result
    sudo grub-editenv - set usb_mouse=1        # every boot
    sudo grub-editenv - unset usb_mouse        # off

Hold **Shift** while GRUB starts to skip loading it.

## Notes

- On real hardware the mouse (Logitech 046d:c05a, low speed) sits behind the
  rate-matching hub of EHCI 00:1d.0. GRUB uses split transactions in the
  periodic schedule for that.
- Loading `ehci` takes the controllers away from the BIOS, and GRUB doesn't hand
  them back. `usb_keyboard` is loaded too, so USB keyboards keep working in GRUB.
  The Windows boot manager won't see a USB keyboard after that; the built-in
  PS/2 keyboard is unaffected.
- `abi.sha256` pins `usb.mod`/`ehci.mod`/`usb_keyboard.mod`. After a grub
  upgrade plus `grub-install`, the check fails and the snippet skips the module.
  Rebuild with `build.sh` and reinstall.
