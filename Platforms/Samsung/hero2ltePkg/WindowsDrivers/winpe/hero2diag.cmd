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
echo Windows zaten hazirlanmis (W:\hero2_done.txt var). Bakim yapiliyor...
echo [1/3] suruculer guncelleniyor
copy /y X:\hero2drv\ufs\hero2ufs.sys W:\Windows\System32\drivers\hero2ufs.sys
copy /y X:\hero2drv\touch\hero2touch.sys W:\Windows\System32\drivers\hero2touch.sys
for /d %%d in (W:\Windows\System32\DriverStore\FileRepository\hero2ufs.inf_*) do copy /y X:\hero2drv\ufs\hero2ufs.sys "%%d\hero2ufs.sys"
for /d %%d in (W:\Windows\System32\DriverStore\FileRepository\hero2touch.inf_*) do copy /y X:\hero2drv\touch\hero2touch.sys "%%d\hero2touch.sys"
echo [2/3] mavi ekranda parametreleri goster, otomatik yeniden baslatma kapali
reg load HKLM\OFFSYS W:\Windows\System32\config\SYSTEM >nul
reg add HKLM\OFFSYS\ControlSet001\Control\CrashControl /v DisplayParameters /t REG_DWORD /d 1 /f >nul
reg add HKLM\OFFSYS\ControlSet001\Control\CrashControl /v AutoReboot /t REG_DWORD /d 0 /f >nul
echo [+] kurulum durumu (once):
reg query HKLM\OFFSYS\Setup /v SystemSetupInProgress 2>nul | find "REG_"
reg query HKLM\OFFSYS\Setup /v OOBEInProgress 2>nul | find "REG_"
reg query HKLM\OFFSYS\Setup\Status\ChildCompletion 2>nul | find "REG_"
rem "beklenmedik sekilde yeniden baslatildi" dongusunu kir: setup.exe asamasi tamam (3)
reg add HKLM\OFFSYS\Setup\Status\ChildCompletion /v setup.exe /t REG_DWORD /d 3 /f >nul && echo [+] ChildCompletion\setup.exe = 3 yapildi
rem kurulum ekranlarini atla: yerel hesap "S7" (sifresiz, otomatik giris)
if not exist W:\Windows\Panther mkdir W:\Windows\Panther
copy /y X:\hero2drv\unattend.xml W:\Windows\Panther\unattend.xml >nul && echo [+] unattend.xml kopyalandi (kurulum ekranlari atlanacak)
reg query HKLM\OFFSYS\Setup /v UnattendFile >nul 2>&1 && reg delete HKLM\OFFSYS\Setup /v UnattendFile /f >nul
reg unload HKLM\OFFSYS >nul
echo [3/3] son cokmeler (Kernel-Power 41 / BugCheck 1001):
wevtutil qe W:\Windows\System32\winevt\Logs\System.evtx /lf:true /q:"*[System[(EventID=41 or EventID=1001)]]" /c:1 /rd:true /f:xml
echo.
echo FOTOGRAF CEK. 30 saniye sonra telefon yeniden baslayacak.
ping -n 30 127.0.0.1 >nul
wpeutil reboot
goto end

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
