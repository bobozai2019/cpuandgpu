@echo off
setlocal
pushd "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup.ps1"
if errorlevel 1 goto failed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" -Configuration Release
if errorlevel 1 goto failed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" -Configuration Debug
if errorlevel 1 goto failed
echo.
echo Ready. Open Start-Learning.cmd or build\DX11ParallelClassroom.slnx
pause
popd
endlocal
exit /b 0
:failed
echo Build failed. See the error above.
pause
popd
endlocal
exit /b 1
