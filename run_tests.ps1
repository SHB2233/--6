param([switch]$SkipBuild, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
if (!$SkipBuild) { & "$PSScriptRoot\run_build.ps1" }
$root = Split-Path $PSScriptRoot
$evidence = Join-Path $root 'delivery\evidence'
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$env:SOCKET_EVIDENCE_DIR = $evidence
$env:PYTHONUTF8 = '1'
foreach ($pair in @(@('server-tests','chatserver_test'), @('directcall-tests','directcall_test'), @('ui-tests','ui_test'))) {
    Push-Location (Join-Path $root ('build\' + $pair[0]))
    try {
        $output = Join-Path $evidence ($pair[1] + '.txt')
        & ('.\release\' + $pair[1] + '.exe') -o "$output,txt"
        if ($LASTEXITCODE -ne 0) { Get-Content $output; throw "Qt test failed: $($pair[1])" }
        Get-Content $output | Select-Object -Last 3
    } finally { Pop-Location }
}
foreach ($name in @('test_basic','test_chat')) {
    $output = Join-Path $evidence ($name + '.txt')
    & $Python -X utf8 (Join-Path $PSScriptRoot "tests\$name.py") -v > $output 2>&1
    if ($LASTEXITCODE -ne 0) { Get-Content $output; throw "Test failed: $name" }
    Get-Content $output | Select-Object -Last 4
}
