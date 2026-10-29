param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$courseRoot = Split-Path $PSScriptRoot -Parent
if (!$SkipBuild) { & "$PSScriptRoot\build.ps1" -Configuration $Configuration }
$reportDirectory = Join-Path $courseRoot 'validation'
New-Item -ItemType Directory -Force -Path $reportDirectory | Out-Null
function Invoke-Checked([string]$Name, [string]$Program, [string[]]$Arguments) {
    $stdout = Join-Path $reportDirectory "$Configuration-$Name.log"
    $stderr = Join-Path $reportDirectory "$Configuration-$Name.err.log"
    $process = Start-Process -FilePath $Program -ArgumentList $Arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (!$process.WaitForExit(60000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "$Name timed out (60 seconds)."
    }
    $process.WaitForExit()
    Get-Content -LiteralPath $stdout
    if ($process.ExitCode -ne 0) { Get-Content -LiteralPath $stderr; throw "$Name failed: exit $($process.ExitCode)" }
}
$cppDirectory = Join-Path $courseRoot "cpp\build\$Configuration"
if (!(Test-Path -LiteralPath "$cppDirectory\thread_pool_tests.exe")) { $cppDirectory = Join-Path $courseRoot 'cpp\build' }
Invoke-Checked 'cpp-tests' "$cppDirectory\thread_pool_tests.exe" @('--test')
Invoke-Checked 'csharp-tests' (Get-Command dotnet -ErrorAction Stop).Source @(('"' + "$courseRoot\csharp\bin\$Configuration\net10.0\ThreadPoolCourse.dll" + '"'), '--test')
foreach ($workers in @(1, 4)) {
    Invoke-Checked "cpp-demo-$workers" "$cppDirectory\thread_pool_demo.exe" @('--workers', "$workers", '--tasks', '17', '--iterations', '10000')
    Invoke-Checked "csharp-demo-$workers" (Get-Command dotnet).Source @(('"' + "$courseRoot\csharp\bin\$Configuration\net10.0\ThreadPoolCourse.dll" + '"'), '--workers', "$workers", '--tasks', '17', '--iterations', '10000')
}
Write-Host "All checks passed. Logs: $reportDirectory"
