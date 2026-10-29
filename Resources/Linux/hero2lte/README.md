# Mainline Linux on hero2lte via Mu-Silicium

First bring-up kit: boots a mainline arm64 kernel (EFI stub) from the UEFI Shell.

- `exynos8890-hero2lte-uefi.dts`: minimal DT (A53 cluster, GIC-400, arch timer @ 26 MHz, PSCI, ramoops).
  CPU/GIC/timer nodes follow Ivaylo Ivanov's exynos8890.dtsi (v1, Sep 2025).
- `kernel.config`: allnoconfig + EFI stub, DTB loader, efifb/fbcon, efi earlycon, pstore/ramoops, initramfs.
- `init.c`: libc-free PID 1 that prints version/cpuinfo/meminfo and keeps the console alive.
- `startup.nsh`: run automatically by the Shell; starts `\IMAGE.EFI` with `dtb=\HERO2LTE.DTB`.
- `mkfat16.py`: builds the small FAT16 image written to the CACHE partition (sda15).

Build (from a Linux tree, LLVM=1 ARCH=arm64): copy kernel.config to .config, set INITRAMFS_SOURCE, `make Image`.

## Alpine + Weston desktop (display only)
`alpine/init` and `alpine/weston.ini` go into an Alpine v3.24 aarch64 rootfs (busybox, weston,
weston-backend-drm, weston-shell-desktop, weston-terminal, seatd-launch, eudev, font-dejavu; Mesa/LLVM
removed, pixman renderer). It is packed as a gzip cpio and loaded with `initrd=\ROOTFS.CPZ`.

## Full exynos8890 support (touch build)
`patches/` holds the kernel series applied on top of Linux 7.3 (torvalds 65ba8f34e): Ivaylo Ivanov's
exynos8890 CMU, S2MPS16 PMIC and S6SA552 touchscreen series (v1, Sep 2025; rebased with small fixups),
plus his exynos8890.dtsi and an `exynos8890-hero2lte.dts` derived from his herolte board file.
Boot with `clk_ignore_unused pd_ignore_unused regulator_ignore_unused` until display/UFS drivers exist.
