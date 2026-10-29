@echo off
mode con: cols=100 lines=80
color 1F
echo ===== hero2lte WinPE =====
echo running wpeinit (device install)...
wpeinit
set K=HKLM\SYSTEM\CurrentControlSet\Services\hero2ufs\Parameters
rem ---- give the Windows partitions fixed letters (ESP = 18, WINDOWS = 19) ----
(echo select disk 0
 echo select partition 19
 echo assign letter=W noerr
 echo select partition 18
 echo assign letter=S noerr) > X:\dpa.txt
diskpart /s X:\dpa.txt >nul 2>&1
if not exist W:\Windows\System32\ntoskrnl.exe goto diag
if exist W:\hero2_done.txt goto already

echo.
echo ===== Windows bulundu: kurulum tamamlaniyor =====
echo [1/5] bcdboot
bcdboot W:\Windows /s S: /f UEFI
echo [2/5] test modu
bcdedit /store S:\EFI\Microsoft\Boot\BCD /set {default} testsigning on
bcdedit /store S:\EFI\Microsoft\Boot\BCD /set {default} nointegritychecks on
bcdedit /store S:\EFI\Microsoft\Boot\BCD /set {default} recoveryenabled no
bcdedit /store S:\EFI\Microsoft\Boot\BCD /set {bootmgr} displaybootmenu no
echo [3/5] suruculer (UFS + dokunmatik)
Dism /Image:W:\ /Add-Driver /Driver:X:\hero2drv /Recurse /ForceUnsigned
echo [4/5] internet olmadan kurulum (BypassNRO)
reg load HKLM\OFFSOFT W:\Windows\System32\config\SOFTWARE
reg add HKLM\OFFSOFT\Microsoft\Windows\CurrentVersion\OOBE /v BypassNRO /t REG_DWORD /d 1 /f
reg unload HKLM\OFFSOFT
echo [5/5] bitti
echo done> W:\hero2_done.txt
dir S:\EFI\Microsoft\Boot\bootmgfw.efi
echo.
echo TAMAM. 90 saniye sonra telefon yeniden baslayacak.
echo Acilis menusunde SES ACMA = yuklu Windows.
ping -n 90 127.0.0.1 >nul
wpeutil reboot
goto end

:already
echo Windows zaten hazirlanmis (W:\hero2_done.txt var).
echo.

:diag
echo list disk > X:\dp.txt
echo select disk 0 >> X:\dp.txt
echo list partition >> X:\dp.txt
echo list volume >> X:\dp.txt
diskpart /s X:\dp.txt
for %%v in (GptFixups G_Overlay G_HiddenParts Commands Errors) do reg query %K% /v %%v 2>nul | find "REG_"
echo.
echo Setup 60 s sonra baslar - fotograf cek
ping -n 60 127.0.0.1 >nul
:end
