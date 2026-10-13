#requires -Version 7.0
param(
    [string]$QtBin = (Split-Path (Get-Command qmake.exe -ErrorAction Stop).Source),
    [string]$VcVars = 'D:\visual_studio_install\VC\Auxiliary\Build\vcvars64.bat'
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$build = Join-Path (Split-Path $repo) 'build'
if (!(Test-Path -LiteralPath $VcVars)) { throw "MSVC environment not found: $VcVars; pass -VcVars." }
function Build-Project([string]$Name, [string]$Project) {
    $directory = Join-Path $build $Name
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    Push-Location $directory
    try {
        $relativeProject = [IO.Path]::GetRelativePath($directory, (Join-Path $repo $Project))
        $command = 'call "{0}" >nul && "{1}\qmake.exe" "{2}" CONFIG+=release && nmake /nologo' -f $VcVars, $QtBin, $relativeProject
        & cmd.exe /d /c $command > build.log 2>&1
        if ($LASTEXITCODE -ne 0) { Get-Content build.log -Tail 25; throw "Build failed: $Name" }
        Write-Output "Built: $Name"
    } finally { Pop-Location }
}
Build-Project 'qt' 'qt_chat.pro'
Build-Project 'server-tests' 'tests\chatserver_test.pro'
Build-Project 'directcall-tests' 'tests\directcall_test.pro'
Build-Project 'ui-tests' 'tests\ui_test.pro'
$basic = Join-Path $build 'basic'
New-Item -ItemType Directory -Path $basic -Force | Out-Null
Push-Location $repo
try {
    foreach ($role in @('server','client')) {
        $command = 'call "{0}" >nul && cl /nologo /std:c++17 /EHsc /utf-8 /MD tcp_basic_{1}.cpp /Fe"..\build\basic\tcp_basic_{1}.exe" /Fo"..\build\basic\tcp_basic_{1}.obj" /link Ws2_32.lib' -f $VcVars, $role
        & cmd.exe /d /c $command > (Join-Path $basic "$role-build.log") 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Basic TCP $role build failed" }
    }
} finally { Pop-Location }
Write-Output 'Built: basic TCP server/client'
