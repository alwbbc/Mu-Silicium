@echo -off
echo "hero2lte: searching for WinPE (\sources\boot.wim)..."
for %a run (0 15)
  if exist fs%a:\sources\boot.wim then
    echo "WinPE found on fs%a, starting bootaa64.efi"
    fs%a:
    \EFI\Boot\bootaa64.efi
    echo "bootaa64.efi returned"
  endif
endfor
echo "No WinPE, booting Linux"
for %b run (0 15)
  if exist fs%b:\HERO2LTE.DTB then
    fs%b:
    \IMAGE.EFI dtb=\HERO2LTE.DTB initrd=\ROOTFS.CPZ earlycon=efifb console=tty0 efi=noruntime clk_ignore_unused pd_ignore_unused regulator_ignore_unused loglevel=4 printk.time=1
  endif
endfor
echo "Linux returned, staying in shell"
