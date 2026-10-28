# Galaxy S7 Edge (SM-G935F, hero2lte, Exynos 8890) port

Started from dreamlte (S8 / Exynos 8895). Values below were filled in from the device's own TWRP dump.

## Done (from dump)
| Item | Value | Source |
|---|---|---|
| DRAM | 0x80000000-0xFE3FFFFF, 0x880000000-0x8FFFFFFFF (4GB) | DT memory@ nodes, iomem |
| GIC | GICD 0x11001000, GICC 0x11002000, GICH 0x11004000, GICV 0x11006000 | DT interrupt-controller@11001000 |
| Timer | 26 MHz, virtual timer | kernel log |
| Framebuffer | 0xE2A00000, 1440x2560, 32bpp, stride 5760 | cmdline s3cfb.bootloaderfb, fb0 |
| Decon | decon_f @ 0x13960000 | iomem / DT |
| Reserved | ess, sec logs, tima, ION+camera, modem, secure_camera | DT reserved-memory, cmdline |
| GPIO controllers | 11 pinctrl nodes (alive = 0x10580000) | DT |
| Keys | VolUp gpa0-3, VolDown gpa0-4, Home gpa0-5, Power gpa2-4 | DT gpio_keys + pinctrl |
| UEFI | FD 0xB2000000, Stack 0xB2200000, DXE heap 0xB2240000-0xC0000000 | chosen in free RAM |

| Boot image | v0, base 0x10000000, page 2048, Samsung DTBH dt.img (2 DTBs: hw_rev 4-7, 8-10) + SEANDROIDENFORCE | stock BOOT (sda5) |
| DT | Resources/DTBs/hero2lte-dt.img (stock, used for boot), hero2lte.dtb (hw_rev 8-10, reference) | stock BOOT |

hero2lte_mkboot.py rebuilds the stock BOOT byte-for-byte from its parts (verified), minus the 256-byte Samsung signature.
Without that signature the bootloader shows a warning but boots, since this phone is already unlocked.

## Still open
1. Decon HW_SW_TRIG_CONTROL offset: 0x6B0 (7870-style) set; fall back to 0x70 if the screen stays black.
2. Other GPIO banks, UFS, USB: not ported.

## Build
    git clone https://github.com/Project-Silicium/Mu-Silicium.git --recursive
    # unzip this package over it
    python3 build_uefi.py -d hero2lte -r DEBUG
    python3 Resources/Scripts/hero2lte_mkboot.py Build/hero2ltePkg/hero2lte-kernel Resources/DTBs/hero2lte-dt.img Mu-hero2lte-boot.img Resources/ramdisk

## Test (no flashing)
Keep the stock boot.img. In TWRP: flash Mu-hero2lte-boot.img to BOOT, reboot.
To go back: flash the stock boot.img to BOOT again (TWRP or Odin).
