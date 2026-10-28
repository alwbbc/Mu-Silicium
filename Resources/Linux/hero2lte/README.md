# Mainline Linux on hero2lte via Mu-Silicium

First bring-up kit: boots a mainline arm64 kernel (EFI stub) from the UEFI Shell.

- `exynos8890-hero2lte-uefi.dts`: minimal DT (A53 cluster, GIC-400, arch timer @ 26 MHz, PSCI, ramoops).
  CPU/GIC/timer nodes follow Ivaylo Ivanov's exynos8890.dtsi (v1, Sep 2025).
- `kernel.config`: allnoconfig + EFI stub, DTB loader, efifb/fbcon, efi earlycon, pstore/ramoops, initramfs.
- `init.c`: libc-free PID 1 that prints version/cpuinfo/meminfo and keeps the console alive.
- `startup.nsh`: run automatically by the Shell; starts `\IMAGE.EFI` with `dtb=\HERO2LTE.DTB`.
- `mkfat16.py`: builds the small FAT16 image written to the CACHE partition (sda15).

Build (from a Linux tree, LLVM=1 ARCH=arm64): copy kernel.config to .config, set INITRAMFS_SOURCE, `make Image`.
