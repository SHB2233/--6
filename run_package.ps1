param(
    [string]$QtBin = (Split-Path (Get-Command qmake.exe -ErrorAction Stop).Source),
    [string]$Dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source,
    [string]$Redist = 'D:\visual_studio_install\VC\Redist\MSVC',
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
& $Python -X utf8 "$PSScriptRoot\scripts\package_runtime.py" --qt-bin $QtBin --dumpbin $Dumpbin --redist $Redist
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
