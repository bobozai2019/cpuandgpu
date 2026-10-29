param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$courseRoot = Split-Path $PSScriptRoot -Parent
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = $null
if (Test-Path -LiteralPath $vswhere) {
    $vs = (& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json) |
        Sort-Object installationVersion -Descending | Select-Object -First 1
}
if ($vs) {
    $bundled = Join-Path $vs.installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $cmakePath = if (Test-Path -LiteralPath $bundled) { $bundled } elseif ($cmakeCommand) { $cmakeCommand.Source } else { throw 'CMake 3.24+ is required.' }
    $generator = switch (([version]$vs.installationVersion).Major) {
        18 { 'Visual Studio 18 2026' }
        17 { 'Visual Studio 17 2022' }
        default { throw 'Use Visual Studio 2022/2026 or build cpp/CMakeLists.txt manually.' }
    }
    & $cmakePath -S "$courseRoot\cpp" -B "$courseRoot\cpp\build" -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($vs.installationPath)"
} elseif ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
    & $cmakePath -S "$courseRoot\cpp" -B "$courseRoot\cpp\build" "-DCMAKE_BUILD_TYPE=$Configuration"
} else { throw 'Install CMake and a C++17 compiler, or Visual Studio C++ tools.' }
if ($LASTEXITCODE -ne 0) { throw 'C++ configure failed.' }
& $cmakePath --build "$courseRoot\cpp\build" --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
if (!(Get-Command dotnet -ErrorAction SilentlyContinue)) { throw '.NET 10 SDK is required.' }
& dotnet build "$courseRoot\csharp\ThreadPoolCourse.csproj" -c $Configuration --nologo
if ($LASTEXITCODE -ne 0) { throw 'C# build failed (.NET 10 SDK required).' }
Write-Host "Both projects built under $courseRoot"
