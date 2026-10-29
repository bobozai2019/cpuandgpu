param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if(!(Test-Path -LiteralPath $vswhere)){throw 'Visual Studio C++ tools missing. Run scripts\setup.ps1 first.'}
$installations=(& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
$vs=$installations | Sort-Object installationVersion -Descending | Select-Object -First 1
if(!$vs){throw 'MSVC x64 compiler missing. Run scripts\setup.ps1 first.'}
$bundled=Join-Path $vs.installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$cmake=if(Test-Path -LiteralPath $bundled){$bundled}else{(Get-Command cmake -ErrorAction Stop).Source}
$major=([version]$vs.installationVersion).Major
$generator=switch($major){18{'Visual Studio 18 2026'}17{'Visual Studio 17 2022'}default{throw "Unsupported Visual Studio version $major (requires 2022 or newer)."}}
& $cmake -S $root -B (Join-Path $root 'build') -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($vs.installationPath)"
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
& $cmake --build (Join-Path $root 'build') --config $Configuration --parallel
if($LASTEXITCODE -ne 0){throw 'Build failed'}
Write-Host "Built: $root\build\$Configuration"
