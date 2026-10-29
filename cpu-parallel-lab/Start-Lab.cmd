@echo off
cd /d "%~dp0"
if not exist build\Release\cpu_parallel_lab.exe (
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build.ps1
  if errorlevel 1 (pause & exit /b 1)
)
start "" build\Release\cpu_parallel_lab.exe
