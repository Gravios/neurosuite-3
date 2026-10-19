@echo off
:: install-deps-windows.bat -- runs install-deps-windows.ps1 from cmd, without
:: changing the PowerShell execution policy.  Options are passed through:
::   scripts\install-deps-windows.bat -WithHdf5 -WithPythonPackages
::   scripts\install-deps-windows.bat -DryRun
:: Run "powershell -File scripts\install-deps-windows.ps1 -?" for all options.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-deps-windows.ps1" %*
exit /b %ERRORLEVEL%
