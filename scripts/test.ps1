param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$WarpOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'out\validation'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$names=@('01_pipeline','02_timing','03_upload','04_cpu_jobs','05_compute','06_readback','07_deferred','08_integrated')
$cases=[Collections.Generic.List[object]]::new()
foreach($name in $names){foreach($mode in 0,1){
 $cases.Add(@{name=$name;mode=$mode;count=10003;workers=3;warp=[bool]$WarpOnly;tag="$name-mode$mode"})
}}
if(!$WarpOnly){foreach($name in $names){$cases.Add(@{name=$name;mode=1;count=1025;workers=3;warp=$true;tag="$name-warp"})}}
foreach($n in 1,255,256,257){$cases.Add(@{name='05_compute';mode=0;count=$n;workers=3;warp=[bool]$WarpOnly;tag="05_compute-edge$n"})}
$report=@()
Push-Location $root
try {
 foreach($case in $cases){
  $exe=Join-Path $root "build\$Configuration\$($case.name).exe"
  $csv=Join-Path $out "$($case.tag).csv"
  $png=Join-Path $out "$($case.tag).png"
  $args=@('--headless','--frames','64','--count',"$($case.count)",'--workers',"$($case.workers)",'--draws','257','--mode',"$($case.mode)",'--validate','--debug','--output',$csv,'--capture',$png)
  if($case.warp){$args+='--warp'}
  $log=& $exe @args 2>&1
  $exit=$LASTEXITCODE
  $log | Set-Content -LiteralPath (Join-Path $out "$($case.tag).log") -Encoding UTF8
  if($exit -ne 0){throw "FAILED $($case.tag): $log"}
  $summary=Get-Content -LiteralPath ([IO.Path]::ChangeExtension($csv,'.json')) -Raw | ConvertFrom-Json
  if(!$summary.validated -or !$summary.debug_layer -or $summary.debug_warnings_or_errors -ne 0 -or $summary.gpu_samples -lt 1){throw "Invalid validation evidence: $($case.tag)"}
  if($summary.frames -ne 64){throw "Incomplete run: $($case.tag)"}
  if($case.name -eq '07_deferred'){
   $imageMatch=[regex]::Match(($log -join "`n"),'lit pixels=(\d+)')
   if(!$imageMatch.Success -or [long]$imageMatch.Groups[1].Value -lt 10000){throw "Grid image coverage too small: $($case.tag)"}
  }
  $report+=[pscustomobject]@{case=$case.tag;adapter=$summary.adapter;passed=$true;gpuSamples=$summary.gpu_samples;debugErrors=$summary.debug_warnings_or_errors}
  Write-Host "PASS $($case.tag)"
 }
 # These paired paths must produce identical pixels, including upload offsets and command list replay.
 foreach($name in @('01_pipeline','02_timing','03_upload','04_cpu_jobs','07_deferred')){
  $a=(Get-FileHash -LiteralPath (Join-Path $out "$name-mode0.png") -Algorithm SHA256).Hash
  $b=(Get-FileHash -LiteralPath (Join-Path $out "$name-mode1.png") -Algorithm SHA256).Hash
  if($a -ne $b){throw "Image mismatch between modes: $name"}
  Write-Host "PASS pixel-equivalent modes: $name"
 }
 $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'report.json') -Encoding UTF8
 Write-Host "All $($cases.Count) runtime cases and 5 image comparisons passed. Reports: $out"
} finally {Pop-Location}
