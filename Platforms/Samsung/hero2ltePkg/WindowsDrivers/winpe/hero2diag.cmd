@echo off
mode con: cols=100 lines=75
color 1F
echo ===== hero2lte WinPE diag (page 1/2) =====
echo running wpeinit (device install)...
wpeinit
echo.
echo --- hero2touch driver status (Services\hero2touch\Parameters) ---
reg query HKLM\SYSTEM\CurrentControlSet\Services\hero2touch\Parameters
echo --- device ACPI\HERO0552 ---
pnputil /enum-devices /deviceid "ACPI\HERO0552" /drivers
reg query HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\HERO0552\0 /v Service
reg query HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\HERO0552\0 /v LowerFilters
reg query HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\HERO0552\0 /v ConfigFlags
echo --- devices with problems ---
pnputil /enum-devices /problem
echo.
echo Page 2 in 60 s - take a photo of this screen
ping -n 60 127.0.0.1 >nul
cls
echo ===== hero2lte WinPE diag (page 2/2): setupapi.dev.log =====
find /i "hero2" X:\Windows\INF\setupapi.dev.log
find "!!!" X:\Windows\INF\setupapi.dev.log
echo --- driver status again ---
reg query HKLM\SYSTEM\CurrentControlSet\Services\hero2touch\Parameters
echo.
echo Setup starts in 60 s - take a photo of this screen
ping -n 60 127.0.0.1 >nul
