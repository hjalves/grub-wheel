#!/bin/bash
# Build usb_mouse.mod for i386-pc against GRUB 2.16 (same version and
# configure options as Arch's grub package) on asus-arch.
# Output: out/usb_mouse.mod (and out/grub-core/ for QEMU tests).
set -euo pipefail
cd "$(dirname "$0")"
HOST=${HOST:-asus-arch}
VER=2.16
TARBALL=upstream/grub-$VER.tar.xz
[ -f "$TARBALL" ] || curl -sSfL -o "$TARBALL" \
  "https://gitlab.freedesktop.org/api/v4/projects/26558/packages/generic/source-assets/grub-$VER/grub-$VER.tar.xz"

ssh "$HOST" mkdir -p /tmp/grub-wheel-build
rsync -a "$TARBALL" usb_mouse.c "$HOST":/tmp/grub-wheel-build/
ssh "$HOST" bash -s <<EOF
set -euo pipefail
cd /tmp/grub-wheel-build
if [ ! -f grub-$VER/.configured ]; then
  rm -rf grub-$VER && tar xf grub-$VER.tar.xz && cd grub-$VER
  cp ../usb_mouse.c grub-core/term/usb_mouse.c
  cat >> grub-core/Makefile.core.def <<'DEF'

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
  make -C grub-core -j"\$(nproc)" usb_mouse.mod moddep.lst >/dev/null
fi
ls -l grub-core/usb_mouse.mod
EOF
mkdir -p out
rsync -a "$HOST":/tmp/grub-wheel-build/grub-$VER/grub-core/usb_mouse.mod out/
echo "built out/usb_mouse.mod"
