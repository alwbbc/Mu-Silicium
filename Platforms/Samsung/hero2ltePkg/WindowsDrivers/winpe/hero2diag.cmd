@echo off
mode con: cols=100 lines=80
color 1F
echo ===== hero2lte WinPE diag (page 1/2): disks =====
echo running wpeinit (device install)...
wpeinit
echo list disk > X:\dp.txt
echo select disk 0 >> X:\dp.txt
echo list partition >> X:\dp.txt
echo list volume >> X:\dp.txt
diskpart /s X:\dp.txt
set K=HKLM\SYSTEM\CurrentControlSet\Services\hero2ufs\Parameters
for %%v in (GptFixups G_Overlay G_HiddenParts Commands Errors) do reg query %K% /v %%v 2>nul | find "REG_"
echo.
echo Page 2 in 60 s - take a photo of this screen
ping -n 60 127.0.0.1 >nul
cls
echo ===== hero2lte WinPE diag (page 2/2): hero2ufs =====
reg query %K%
echo.
echo Setup starts in 60 s - take a photo of this screen
ping -n 60 127.0.0.1 >nul
