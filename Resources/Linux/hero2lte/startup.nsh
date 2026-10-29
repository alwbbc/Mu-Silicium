@echo -off
for %a run (0 15)
  if exist fs%a:\BOOTSEL.EFI then
    fs%a:
    \BOOTSEL.EFI
    goto END
  endif
endfor
echo "BOOTSEL.EFI not found"
:END
