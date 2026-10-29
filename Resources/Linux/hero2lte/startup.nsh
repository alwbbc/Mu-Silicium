@echo -off
echo "hero2lte: booting Alpine (touch build) from CACHE (FS0)"
fs0:
\IMAGE.EFI dtb=\HERO2LTE.DTB initrd=\ROOTFS.CPZ earlycon=efifb console=tty0 efi=noruntime clk_ignore_unused pd_ignore_unused regulator_ignore_unused loglevel=4 printk.time=1
echo "Linux returned, staying in shell"
