param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$labRoot=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if(!(Test-Path -LiteralPath $vswhere)){throw 'Install Visual Studio C++ tools first.'}
$vs=(& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json) | Sort-Object installationVersion -Descending | Select-Object -First 1
if(!$vs){throw 'MSVC C++ tools are missing.'}
$cmake=Join-Path $vs.installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$generator=switch(([version]$vs.installationVersion).Major){18{'Visual Studio 18 2026'}17{'Visual Studio 17 2022'}default{throw 'Visual Studio 2022 or newer required.'}}
& $cmake -S $labRoot -B "$labRoot\build" -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($vs.installationPath)"
if($LASTEXITCODE -ne 0){throw 'Configure failed'}
& $cmake --build "$labRoot\build" --config $Configuration --parallel
if($LASTEXITCODE -ne 0){throw 'Build failed'}
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
& $ctest --test-dir "$labRoot\build" -C $Configuration --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Tests failed'}
