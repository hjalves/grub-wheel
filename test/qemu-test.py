#!/usr/bin/env python3
"""Boot test/grub.lnx in QEMU with a USB mouse, roll the wheel / click via
QMP and take screenshots.  Usage: qemu-test.py [uhci|ehci-companion] """
import json, os, socket, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "shots")
os.makedirs(OUT, exist_ok=True)
mode = sys.argv[1] if len(sys.argv) > 1 else "uhci"
sock = os.path.join(HERE, "qmp.sock")
if os.path.exists(sock):
    os.unlink(sock)

usb = {
    "uhci": ["-device", "piix3-usb-uhci,id=uhci", "-device", "usb-mouse,bus=uhci.0,id=m"],
    "ehci-companion": [
        "-device", "ich9-usb-ehci1,id=ehci",
        "-device", "ich9-usb-uhci1,masterbus=ehci.0,firstport=0",
        "-device", "ich9-usb-uhci2,masterbus=ehci.0,firstport=2",
        "-device", "ich9-usb-uhci3,masterbus=ehci.0,firstport=4",
        "-device", "usb-mouse,bus=ehci.0,id=m"],
}[mode]

q = subprocess.Popen(["qemu-system-x86_64", "-M", "pc", "-m", "256", "-display", "none",
                      "-vga", "std", "-kernel", os.path.join(HERE, "grub.lnx"),
                      "-qmp", f"unix:{sock},server=on,wait=off", *usb])
for _ in range(100):
    if os.path.exists(sock):
        break
    time.sleep(0.1)
s = socket.socket(socket.AF_UNIX)
s.connect(sock)
f = s.makefile("rw")
f.readline()

def cmd(name, **args):
    f.write(json.dumps({"execute": name, "arguments": args}) + "\n")
    f.flush()
    while True:
        r = json.loads(f.readline())
        if "return" in r or "error" in r:
            return r

def shot(name):
    p = os.path.join(OUT, f"{mode}-{name}.ppm")
    cmd("screendump", filename=p)
    time.sleep(0.3)
    subprocess.run(["magick", p, p[:-4] + ".png"], check=False)
    os.unlink(p)
    print("shot", p[:-4] + ".png")

def btn(button, n=1):
    for _ in range(n):
        for down in (True, False):
            cmd("input-send-event",
                events=[{"type": "btn", "data": {"down": down, "button": button}}])
            time.sleep(0.15)

cmd("qmp_capabilities")
time.sleep(float(os.environ.get("BOOT_WAIT", "25")))
shot("0-menu")
btn("wheel-down", 2)
time.sleep(1)
shot("1-down2")
btn("wheel-up", 1)
time.sleep(1)
shot("2-up1")
btn("wheel-down", 3)
time.sleep(1)
shot("3-down3-last")
btn("left")
time.sleep(1.5)
shot("4-click")
cmd("quit")
q.wait()
