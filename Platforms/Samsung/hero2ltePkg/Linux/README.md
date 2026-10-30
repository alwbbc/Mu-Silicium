# hero2lte Linux kernel

Patch series on top of mainline Linux 7.3 (branch `hero2lte`, 18 commits):
Exynos 8890 / hero2lte device tree, S2MPS16 regulators, S6SA552 touchscreen,
gpio keys. `config-7.3-hero2lte` is the kernel config used for the Alpine
initramfs image (no UFS driver yet: rootfs lives in `ROOTFS.CPZ` on the ESP).

    git am patches/*.patch
    cp config-7.3-hero2lte .config && make olddefconfig && make Image dtbs
