@echo off
rem Runs once as SYSTEM before logon (HKLM\SYSTEM\Setup CmdLine), set up by WinPE.
rem Creates local admin "S7" (no password), marks OOBE done, enables auto logon.
set L=C:\hero2\skipoobe.log
echo hero2 skipoobe %DATE% %TIME% > %L%
net user S7 /add /passwordreq:no >> %L% 2>&1
powershell -NoProfile -Command "Add-LocalGroupMember -SID 'S-1-5-32-544' -Member 'S7'" >> %L% 2>&1
net localgroup Administrators S7 /add >> %L% 2>&1
wmic useraccount where "name='S7'" set PasswordExpires=false >> %L% 2>&1
set W=HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon
reg add "%W%" /v AutoAdminLogon /t REG_SZ /d 1 /f >> %L%
reg add "%W%" /v DefaultUserName /t REG_SZ /d S7 /f >> %L%
reg add "%W%" /v DefaultPassword /t REG_SZ /d "" /f >> %L%
reg add "%W%" /v DefaultDomainName /t REG_SZ /d . /f >> %L%
reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v EnableFirstLogonAnimation /t REG_DWORD /d 0 /f >> %L%
reg add HKLM\SOFTWARE\Policies\Microsoft\Windows\OOBE /v DisablePrivacyExperience /t REG_DWORD /d 1 /f >> %L%
reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Setup\State /v ImageState /t REG_SZ /d IMAGE_STATE_COMPLETE /f >> %L%
reg add HKLM\SYSTEM\Setup /v OOBEInProgress /t REG_DWORD /d 0 /f >> %L%
reg add HKLM\SYSTEM\Setup /v SystemSetupInProgress /t REG_DWORD /d 0 /f >> %L%
reg add HKLM\SYSTEM\Setup /v SetupType /t REG_DWORD /d 0 /f >> %L%
reg add HKLM\SYSTEM\Setup /v SetupPhase /t REG_DWORD /d 0 /f >> %L%
reg add HKLM\SYSTEM\Setup /v CmdLine /t REG_SZ /d "" /f >> %L%
net user >> %L% 2>&1
echo done >> %L%
shutdown /r /t 3
