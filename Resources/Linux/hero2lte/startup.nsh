@echo -off
echo "hero2lte: booting Alpine Linux desktop from CACHE (FS0)"
fs0:
\IMAGE.EFI dtb=\HERO2LTE.DTB initrd=\ROOTFS.CPZ earlycon=efifb console=tty0 efi=noruntime loglevel=4 printk.time=1
echo "Linux returned, staying in shell"
