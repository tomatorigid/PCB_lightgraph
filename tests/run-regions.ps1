param(
    [string]$QtRoot = 'D:\Qt\6.11.1\mingw_64',
    [string]$CompilerRoot = 'D:\Qt\Tools\mingw1310_64',
    [string]$BuildDirectory = 'build-region-tests'
)
$ErrorActionPreference = 'Stop'
$regionRepo = Split-Path -Parent $PSScriptRoot
$regionBuild = Join-Path $regionRepo $BuildDirectory
New-Item -ItemType Directory -Path $regionBuild -Force | Out-Null
$env:PATH = "$QtRoot\bin;$CompilerRoot\bin;" + $env:PATH
# 固定原始实现作独立参考，避免测试重复新算法。
foreach ($regionExtension in @('h', 'cpp')) {
    $regionReference = (& git -C $regionRepo show "d1cac6f:imageprocessor.$regionExtension") -join "`n"
    if ($LASTEXITCODE -ne 0) { throw '无法读取 v1.5.1 参考源码' }
    $regionReference = $regionReference.Replace('ImageProcessor', 'BaselineImageProcessor').Replace('IMAGEPROCESSOR_H', 'BASELINE_IMAGEPROCESSOR_H').Replace('"imageprocessor.h"', '"baseline_imageprocessor.h"')
    [IO.File]::WriteAllText((Join-Path $regionBuild "baseline_imageprocessor.$regionExtension"), $regionReference, [Text.UTF8Encoding]::new($false))
}
Push-Location -LiteralPath $regionBuild
try {
    & "$QtRoot\bin\qmake.exe" "$PSScriptRoot\regions_tests.pro" 'CONFIG+=release'
    if ($LASTEXITCODE -ne 0) { throw '区域测试 qmake 失败' }
    & "$CompilerRoot\bin\mingw32-make.exe" -j4 *> build.log
    if ($LASTEXITCODE -ne 0) { Get-Content build.log -Tail 80; throw '区域测试构建失败' }
    $env:PCBLG_TEST_ARTIFACTS = $regionBuild
    $regionTestProcess = Start-Process -FilePath '.\release\regions_tests.exe' -ArgumentList @('-o', 'regions-results.txt,txt') -PassThru -WindowStyle Hidden
    $regionPeakBytes = 0L
    while (-not $regionTestProcess.HasExited) {
        $regionTestProcess.Refresh()
        if (-not $regionTestProcess.HasExited) { $regionPeakBytes = [Math]::Max($regionPeakBytes, $regionTestProcess.PeakWorkingSet64) }
        Start-Sleep -Milliseconds 100
    }
    $regionTestProcess.WaitForExit()
    $regionResult = $regionTestProcess.ExitCode
    $regionPeakReport = '测试进程峰值工作集：{0:N1} MiB' -f ($regionPeakBytes / 1MB)
    [IO.File]::WriteAllText((Join-Path $regionBuild 'memory-results.txt'), $regionPeakReport, [Text.UTF8Encoding]::new($false))
    Get-Content regions-results.txt
    Write-Output $regionPeakReport
    if ($regionResult -ne 0) { throw "区域测试失败：$regionResult" }
} finally { Pop-Location }
