#!/usr/bin/env python3
"""
Build a Galaxy S7 Edge (hero2lte, SM-G935F) boot image in the stock Samsung layout.

The stock BOOT image is an Android v0 header where the 'header_version' slot holds
the size of a Samsung DTBH dt.img, appended after kernel/ramdisk/second, followed by
the SEANDROIDENFORCE footer. The generic mkbootimg.py cannot produce this, so the
S7 bootloader would find no DT. Values below are read from the device's stock BOOT.

Usage:
  hero2lte_mkboot.py <kernel/UEFI payload> <dt.img> <output.img> [ramdisk]
"""
import struct, sys, hashlib

BASE        = 0x10000000
KERNEL_OFF  = 0x00008000
RAMDISK_OFF = 0x01000000
SECOND_OFF  = 0x00F00000
TAGS_OFF    = 0x00000100
PAGE        = 2048
OS_VERSION  = 0x1000012C          # copied from stock header
BOARD       = b"SRPOI30A006KU"     # copied from stock header
CMDLINE     = b""
FOOTER      = b"SEANDROIDENFORCE"

def pad(b):
    return b + b"\0" * ((PAGE - len(b) % PAGE) % PAGE)

def build(kernel, dt, ramdisk, second=b""):
    sha = hashlib.sha1()
    for blob in (kernel, ramdisk, second):
        sha.update(blob); sha.update(struct.pack("<I", len(blob)))
    sha.update(dt); sha.update(struct.pack("<I", len(dt)))
    hdr  = b"ANDROID!"
    hdr += struct.pack("<10I", len(kernel), BASE + KERNEL_OFF, len(ramdisk), BASE + RAMDISK_OFF,
                       len(second), BASE + SECOND_OFF, BASE + TAGS_OFF, PAGE, len(dt), OS_VERSION)
    hdr += BOARD.ljust(16, b"\0") + CMDLINE.ljust(512, b"\0")
    hdr += sha.digest().ljust(32, b"\0") + b"\0" * 1024
    return pad(hdr) + pad(kernel) + pad(ramdisk) + pad(second) + pad(dt) + FOOTER

if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    kernel  = open(sys.argv[1], "rb").read()
    dt      = open(sys.argv[2], "rb").read()
    ramdisk = open(sys.argv[4], "rb").read() if len(sys.argv) > 4 else b""
    if dt[:4] != b"DTBH":
        sys.exit("dt.img is not a Samsung DTBH image")
    img = build(kernel, dt, ramdisk)
    open(sys.argv[3], "wb").write(img)
    print(f"{sys.argv[3]}: {len(img)} bytes (kernel {len(kernel)}, ramdisk {len(ramdisk)}, dt {len(dt)})")
