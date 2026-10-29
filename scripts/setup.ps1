param([switch]$CheckOnly,[switch]$InstallGraphicsTools)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
function Find-Compiler {
 if(Test-Path -LiteralPath $vswhere){
  $items=(& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
  return $items | Sort-Object installationVersion -Descending | Select-Object -First 1
 }
 return $null
}
function Find-Sdk {
 $sdkRoot=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
 foreach($v in (Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -ErrorAction SilentlyContinue | Sort-Object Name -Descending)){
  $required=@("Include\$($v.Name)\um\d3d11.h","Include\$($v.Name)\um\d3dcompiler.h","Lib\$($v.Name)\um\x64\d3d11.lib","Lib\$($v.Name)\um\x64\d3dcompiler.lib")
  $missing=@($required | Where-Object {!(Test-Path -LiteralPath (Join-Path $sdkRoot $_))})
  if($missing.Count -eq 0){return $v.Name}
 }
 return $null
}
$vs=Find-Compiler
$sdk=Find-Sdk
if(!$vs -or !$sdk){
 if($CheckOnly){throw 'MSVC or Windows SDK missing. Run setup.ps1 without -CheckOnly to install official prerequisites.'}
 if($vs){
  $installer=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
  $arguments=@('modify','--installPath',('"'+$vs.installationPath+'"'),'--add','Microsoft.VisualStudio.Component.Windows11SDK.26100','--passive','--norestart')
 }else{
  $downloads=Join-Path $root 'out\downloads';New-Item -ItemType Directory -Path $downloads -Force | Out-Null
  $installer=Join-Path $downloads 'vs_BuildTools.exe'
  Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile $installer
  $signature=Get-AuthenticodeSignature -LiteralPath $installer
  if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation'){throw 'Microsoft installer signature verification failed'}
  $arguments=@('--wait','--passive','--norestart','--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended')
 }
 $process=Start-Process -FilePath $installer -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden
 if($process.ExitCode -notin 0,3010){throw "Microsoft setup failed: $($process.ExitCode)"}
 if($process.ExitCode -eq 3010){Write-Warning 'Installer requested a restart. Restart Windows if subsequent detection fails.'}
 $vs=Find-Compiler;$sdk=Find-Sdk
 if(!$vs -or !$sdk){throw 'Compiler/SDK still unavailable after installation. Inspect Visual Studio Installer for required action.'}
}
$bundled=Join-Path $vs.installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if(!(Test-Path -LiteralPath $bundled) -and !(Get-Command cmake -ErrorAction SilentlyContinue)){
 if($CheckOnly){throw 'CMake missing'}
 & winget install --id Kitware.CMake --exact --source winget --accept-source-agreements --accept-package-agreements --silent
 if($LASTEXITCODE -ne 0){throw 'CMake installation failed'}
 $cmakeBin=Join-Path $env:ProgramFiles 'CMake\bin';$env:Path="$cmakeBin;$env:Path"
}
$hasDebug=@(Get-ChildItem -Path (Join-Path $env:WINDIR 'System32\d3d11*sdklayers.dll') -ErrorAction SilentlyContinue).Count -gt 0
if(!$hasDebug -and $InstallGraphicsTools){
 if($CheckOnly){throw 'Graphics Tools debug layer missing'}
 $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
 if(!$admin){throw 'Installing the Windows Graphics Tools capability requires an administrator PowerShell. Re-run setup.ps1 -InstallGraphicsTools there.'}
 Add-WindowsCapability -Online -Name 'Tools.Graphics.DirectX~~~~0.0.1.0' | Out-Host
 $hasDebug=@(Get-ChildItem -Path (Join-Path $env:WINDIR 'System32\d3d11*sdklayers.dll') -ErrorAction SilentlyContinue).Count -gt 0
}
Write-Host "MSVC installation: $($vs.installationPath)"
Write-Host "Windows SDK with DX11: $sdk"
Write-Host "D3DCompiler 47: $(Test-Path -LiteralPath (Join-Path $env:WINDIR 'System32\d3dcompiler_47.dll'))"
Write-Host "DX11 debug layer DLL found: $hasDebug (runtime validation also checks actual device creation)"
Write-Host 'Prerequisites ready. No legacy June 2010 DirectX SDK or D3DX is required.'
