#!/bin/bash
# Build usb_mouse.mod for i386-pc against GRUB 2.16 (same version and
# configure options as Arch's grub package) on asus-arch.
# Output: out/usb_mouse.mod, out/ehci_keepbios.mod, out/abi.sha256.
#
# ehci_keepbios.mod is GRUB's own ehci driver minus the
# grub_stop_disk_firmware() call in its init: stock ehci unloads biosdisk
# (expecting native disk drivers), which leaves GRUB with no (hd0) and an
# unbootable menu. asus-arch boots from SATA, so keeping BIOS disk access
# while GRUB drives the USB controller is safe there.
set -euo pipefail
cd "$(dirname "$0")"
HOST=${HOST:-asus-arch}
VER=2.16
TARBALL=upstream/grub-$VER.tar.xz
[ -f "$TARBALL" ] || curl -sSfL -o "$TARBALL" \
  "https://gitlab.freedesktop.org/api/v4/projects/26558/packages/generic/source-assets/grub-$VER/grub-$VER.tar.xz"

ssh "$HOST" mkdir -p .cache/grub-wheel-build
rsync -a "$TARBALL" usb_mouse.c "$HOST":.cache/grub-wheel-build/
ssh "$HOST" bash -s <<EOF
set -euo pipefail
cd ~/.cache/grub-wheel-build
if [ ! -f grub-$VER/.configured ]; then
  rm -rf grub-$VER && tar xf grub-$VER.tar.xz && cd grub-$VER
  cp ../usb_mouse.c grub-core/term/usb_mouse.c
  grep -q '^  grub_stop_disk_firmware ();\$' grub-core/bus/usb/ehci.c
  ren='#define grub_ehci_init_device grub_ehci_keepbios_init_device\n#define grub_ehci_pci_scan grub_ehci_keepbios_pci_scan\n'
  { printf "\$ren"; sed '/^  grub_stop_disk_firmware ();\$/d; s/GRUB_MOD_INIT (ehci)/GRUB_MOD_INIT (ehci_keepbios)/; s/GRUB_MOD_FINI (ehci)/GRUB_MOD_FINI (ehci_keepbios)/' \
    grub-core/bus/usb/ehci.c; } > grub-core/bus/usb/ehci_keepbios.c
  { printf "\$ren"; cat grub-core/bus/usb/ehci-pci.c; } > grub-core/bus/usb/ehci_keepbios-pci.c
  ! grep -q grub_stop_disk_firmware grub-core/bus/usb/ehci_keepbios.c
  cat >> grub-core/Makefile.core.def <<'DEF'

module = {
  name = ehci_keepbios;
  common = bus/usb/ehci_keepbios.c;
  pci = bus/usb/ehci_keepbios-pci.c;
  enable = pci;
};

module = {
  name = usb_mouse;
  common = term/usb_mouse.c;
  enable = usb;
};
DEF
  ./autogen.sh >/dev/null
  unset CFLAGS
  ./configure --prefix=/usr --sbindir=/usr/bin --sysconfdir=/etc \
    --with-bootdir=boot --with-grubdir=grub --enable-boot-time \
    --enable-cache-stats --enable-efiemu --with-platform=pc --target=i386 \
    PACKAGE_VERSION="2:$VER-1" >/dev/null
  sed -i -e 's#po docs##' Makefile
  touch .configured
  make -j"\$(nproc)" >/dev/null
else
  cd grub-$VER
  cp ../usb_mouse.c grub-core/term/usb_mouse.c
  make -C grub-core -j"\$(nproc)" usb_mouse.mod ehci_keepbios.mod moddep.lst >/dev/null
fi
ls -l grub-core/usb_mouse.mod grub-core/ehci_keepbios.mod
grep -E '^(usb_mouse|ehci_keepbios):' grub-core/moddep.lst
(cd grub-core && sha256sum usb.mod pci.mod cs5536.mod boot.mod usb_keyboard.mod ehci.mod) > ../abi.sha256
EOF
mkdir -p out
rsync -a "$HOST":.cache/grub-wheel-build/grub-$VER/grub-core/{usb_mouse,ehci_keepbios}.mod "$HOST":.cache/grub-wheel-build/abi.sha256 out/
echo "built out/usb_mouse.mod out/ehci_keepbios.mod"
