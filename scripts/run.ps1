[CmdletBinding(PositionalBinding=$false)]
param(
 [ValidateRange(1,8)][int]$Chapter=1,
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [Parameter(ValueFromRemainingArguments=$true)][string[]]$Options
)
$ErrorActionPreference='Stop'
# PowerShell treats --debug as its own common -Debug switch. Forward the
# explicitly requested switch to the DX11 executable as documented.
if($PSBoundParameters.ContainsKey('Debug') -and $PSBoundParameters['Debug']){
 $Options += '--debug'
}
$root=Split-Path $PSScriptRoot -Parent
$names=@('01_pipeline','02_timing','03_upload','04_cpu_jobs','05_compute','06_readback','07_deferred','08_integrated')
$exe=Join-Path $root "build\$Configuration\$($names[$Chapter-1]).exe"
if(!(Test-Path -LiteralPath $exe)){& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration}
Push-Location $root
try {& $exe @Options; if($LASTEXITCODE -ne 0){throw "Chapter exited with code $LASTEXITCODE"}}
finally {Pop-Location}
