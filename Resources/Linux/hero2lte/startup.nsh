@echo -off
echo "hero2lte: booting Linux from CACHE (FS0)"
fs0:
\IMAGE.EFI dtb=\HERO2LTE.DTB earlycon=efifb keep_bootcon console=tty0 efi=noruntime ignore_loglevel printk.time=1
echo "Linux returned, staying in shell"
