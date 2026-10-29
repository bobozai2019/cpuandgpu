@echo off
setlocal
pushd "%~dp0"
:menu
cls
echo ===================================================
echo       DX11 CPU / GPU Parallel Classroom
echo ===================================================
echo.
echo   1  CPU / GPU pipeline
echo   2  GPU timing
echo   3  Dynamic uploads
echo   4  CPU jobs
echo   5  Compute shader particles
echo   6  GPU readback
echo   7  Deferred contexts
echo   8  Integrated experiment
echo.
echo   H  Open Chinese HTML handbook
echo   P  Open Visual Studio project
echo   Q  Quit
echo.
choice /c 12345678HPQ /n /m "Select chapter or action: "
set "selected=%errorlevel%"
if "%selected%"=="11" goto done
if "%selected%"=="10" goto project
if "%selected%"=="9" goto handbook
if %selected% LSS 1 goto done
if %selected% GTR 8 goto done
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run.ps1" -Chapter %selected%
if errorlevel 1 pause
goto menu
:handbook
start "" "%~dp0docs\handbook.html"
goto menu
:project
if exist "%~dp0build\DX11ParallelClassroom.slnx" (
    start "" "%~dp0build\DX11ParallelClassroom.slnx"
    goto menu
)
if exist "%~dp0build\DX11ParallelClassroom.sln" (
    start "" "%~dp0build\DX11ParallelClassroom.sln"
    goto menu
)
echo Run Build-All.cmd first to generate the Visual Studio project.
pause
goto menu
:done
popd
endlocal
