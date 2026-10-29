@echo off
cd /d "%~dp0"
if not exist build\Release\cpu_benchmark.exe (
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build.ps1
  if errorlevel 1 (pause & exit /b 1)
)
build\Release\cpu_benchmark.exe --samples 64 --repeats 3 --output out\benchmark
pause
