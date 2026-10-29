@echo off
cd /d "%~dp0"
:menu
cls
echo Thread Pool Course - C++17 and C# / .NET 10
echo.
echo 1. Open C++ lesson (offline HTML)
echo 2. Open C# lesson  (offline HTML)
echo 3. Run C++ demos
echo 4. Run C# demos
echo 5. Build both projects
echo 6. Build and test both projects
echo Q. Exit
choice /c 123456Q /n /m "Select: "
if errorlevel 7 exit /b 0
if errorlevel 6 (call Test-All.cmd & goto menu)
if errorlevel 5 (call Build-All.cmd & goto menu)
if errorlevel 4 goto csharp
if errorlevel 3 goto cpp
if errorlevel 2 (start "" "%~dp0csharp\lesson.html" & goto menu)
if errorlevel 1 (start "" "%~dp0cpp\lesson.html" & goto menu)
:cpp
if not exist "%~dp0cpp\build\Release\thread_pool_demo.exe" call Build-All.cmd
if exist "%~dp0cpp\build\Release\thread_pool_demo.exe" "%~dp0cpp\build\Release\thread_pool_demo.exe"
pause
goto menu
:csharp
dotnet run --project "%~dp0csharp\ThreadPoolCourse.csproj" -c Release -- --demo all
pause
goto menu
