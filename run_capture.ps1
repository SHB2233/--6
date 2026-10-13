param([switch]$SkipBuild, [string]$WiresharkDir='F:\Wireshark')
$ErrorActionPreference='Stop'
if (!$SkipBuild) { & "$PSScriptRoot\run_build.ps1" }
$root=Split-Path $PSScriptRoot
$evidence=Join-Path $root 'delivery\evidence'
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$beforeEvidence=$env:SOCKET_EVIDENCE_DIR; $beforePort=$env:SOCKET_DEMO_PORT; $beforeCapture=$env:SOCKET_DUMPCAP
$capture=$null
try {
    $env:SOCKET_EVIDENCE_DIR=$evidence; $env:SOCKET_DEMO_PORT='18889'; $env:SOCKET_DUMPCAP=Join-Path $WiresharkDir 'dumpcap.exe'
    $capture=Start-Process -FilePath $env:SOCKET_DUMPCAP -ArgumentList @('-i','\Device\NPF_Loopback','-f','"tcp port 18889"','-a','duration:15','-w',('"'+(Join-Path $evidence 'chat_session.pcapng')+'"')) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $evidence 'chat_capture.log')
    Start-Sleep -Milliseconds 1200
    Push-Location (Join-Path $root 'build\ui-tests')
    try {
        & '.\release\ui_test.exe' completeGuiScenario -o "$evidence\gui_capture_test.txt,txt"
        if ($LASTEXITCODE -ne 0) { Get-Content "$evidence\gui_capture_test.txt"; throw 'Graphical capture scenario failed' }
    } finally { Pop-Location }
    if (!$capture.WaitForExit(25000)) { throw 'Timed out waiting for chat capture' }
    if ($capture.ExitCode -ne 0) { throw 'Chat capture failed' }
    & python -X utf8 "$PSScriptRoot\scripts\capture_basic.py" --wireshark-dir $WiresharkDir
    if ($LASTEXITCODE -ne 0) { throw 'Basic capture failed' }
    & python -X utf8 "$PSScriptRoot\scripts\analyze_captures.py" --wireshark-dir $WiresharkDir
    if ($LASTEXITCODE -ne 0) { throw 'Capture analysis failed' }
} finally {
    if ($capture -and !$capture.HasExited) { Stop-Process -Id $capture.Id }
    $env:SOCKET_EVIDENCE_DIR=$beforeEvidence; $env:SOCKET_DEMO_PORT=$beforePort; $env:SOCKET_DUMPCAP=$beforeCapture
}
